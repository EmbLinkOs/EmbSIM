// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The typed session client, generated at runtime from METHOD_NAMES and typed
// from the `Methods` table. `client.call('control.run', {speed: 1})` and the
// namespaced form `client.control.run({speed: 1})` are the same thing.

import {
  JsonRpcError,
  JsonRpcCode,
  createIdGenerator,
  decodeBinaryFrame,
  decodeMessage,
  encodeRequest,
  type JsonRpcId,
} from './jsonrpc';
import { METHOD_NAMES, type MethodName, type NotificationName, type NotificationParams, type Params, type Result } from './methods';
import type { Transport, TransportState } from './transport';

type Split<S extends string> = S extends `${infer A}.${infer B}` ? [A, B] : never;
type NamespaceOf<M extends MethodName> = Split<M>[0];
type ShortOf<M extends MethodName> = Split<M>[1];

/** `client.control.run(...)` style accessors, one per namespace of the method table. */
export type NamespacedMethods = {
  [NS in NamespaceOf<MethodName>]: {
    [M in MethodName as NamespaceOf<M> extends NS ? ShortOf<M> : never]: (params: Params<M>) => Promise<Result<M>>;
  };
};

export type NotificationListener<N extends NotificationName> = (params: NotificationParams<N>) => void;
export type AnyNotificationListener = (method: string, params: unknown) => void;
export type BinaryFrameListener = (frame: { streamId: number; seq: number; firstTimePs: bigint; samples: ArrayBuffer }) => void;

export interface ClientOptions {
  /** Request timeout in ms; 0 disables. Default 30 000. */
  timeoutMs?: number;
  /** Receives every unknown or malformed frame, for diagnostics. */
  onProtocolFault?: (reason: string, raw: string) => void;
}

interface Pending {
  method: string;
  resolve: (value: unknown) => void;
  reject: (error: Error) => void;
  timer: ReturnType<typeof setTimeout> | null;
  startedAt: number;
}

export class SessionClient {
  readonly transport: Transport;
  private readonly nextId = createIdGenerator();
  private readonly pending = new Map<JsonRpcId, Pending>();
  private readonly listeners = new Map<string, Set<(params: unknown) => void>>();
  private readonly anyListeners = new Set<AnyNotificationListener>();
  private readonly binaryListeners = new Set<BinaryFrameListener>();
  private readonly latencyListeners = new Set<(ms: number, method: string) => void>();
  private readonly options: ClientOptions;
  private lastLatencyMs = 0;

  constructor(transport: Transport, options: ClientOptions = {}) {
    this.transport = transport;
    this.options = options;
    transport.onText((frame) => this.receive(frame));
    transport.onBinary((frame) => {
      try {
        const decoded = decodeBinaryFrame(frame);
        for (const l of this.binaryListeners) l(decoded);
      } catch (e) {
        options.onProtocolFault?.((e as Error).message, '<binary>');
      }
    });
    transport.onState((state) => {
      if (state === 'closed') this.failAll(new JsonRpcError(JsonRpcCode.INTERNAL, 'transport closed'));
    });
    for (const name of METHOD_NAMES) {
      const [ns, short] = name.split('.') as [string, string];
      const target = ((this as unknown as Record<string, unknown>)[ns] ??= {}) as Record<string, unknown>;
      target[short] = (params: unknown) => this.call(name, params as never);
    }
  }

  get state(): TransportState {
    return this.transport.state;
  }

  /** Round-trip time of the most recent completed request, ms. */
  get latencyMs(): number {
    return this.lastLatencyMs;
  }

  connect(): Promise<void> {
    return this.transport.connect();
  }

  close(): void {
    this.transport.close();
  }

  call<M extends MethodName>(method: M, params: Params<M>): Promise<Result<M>> {
    const id = this.nextId();
    const frame = encodeRequest(id, method, params);
    return new Promise<Result<M>>((resolve, reject) => {
      const timeoutMs = this.options.timeoutMs ?? 30_000;
      const entry: Pending = {
        method,
        resolve: resolve as (v: unknown) => void,
        reject,
        startedAt: now(),
        timer:
          timeoutMs > 0
            ? setTimeout(() => {
                this.pending.delete(id);
                reject(new JsonRpcError(JsonRpcCode.INTERNAL, `${method} timed out after ${timeoutMs} ms`, undefined, method));
              }, timeoutMs)
            : null,
      };
      this.pending.set(id, entry);
      try {
        this.transport.send(frame);
      } catch (e) {
        this.pending.delete(id);
        if (entry.timer) clearTimeout(entry.timer);
        reject(e instanceof Error ? e : new Error(String(e)));
      }
    });
  }

  on<N extends NotificationName>(name: N, listener: NotificationListener<N>): () => void {
    let set = this.listeners.get(name);
    if (!set) {
      set = new Set();
      this.listeners.set(name, set);
    }
    set.add(listener as (p: unknown) => void);
    return () => set?.delete(listener as (p: unknown) => void);
  }

  onAny(listener: AnyNotificationListener): () => void {
    this.anyListeners.add(listener);
    return () => this.anyListeners.delete(listener);
  }

  onBinaryFrame(listener: BinaryFrameListener): () => void {
    this.binaryListeners.add(listener);
    return () => this.binaryListeners.delete(listener);
  }

  onLatency(listener: (ms: number, method: string) => void): () => void {
    this.latencyListeners.add(listener);
    return () => this.latencyListeners.delete(listener);
  }

  private receive(frame: string): void {
    const decoded = decodeMessage(frame);
    switch (decoded.kind) {
      case 'result': {
        const entry = this.takePending(decoded.message.id);
        if (!entry) return this.options.onProtocolFault?.(`response to unknown id ${String(decoded.message.id)}`, frame);
        this.recordLatency(entry);
        entry.resolve(decoded.message.result);
        return;
      }
      case 'error': {
        const { id, error } = decoded.message;
        const entry = id === null ? undefined : this.takePending(id);
        const err = new JsonRpcError(error.code, error.message, error.data, entry?.method);
        if (!entry) return this.options.onProtocolFault?.(`error without matching request: ${error.message}`, frame);
        this.recordLatency(entry);
        entry.reject(err);
        return;
      }
      case 'notification': {
        const { method, params } = decoded.message;
        const set = this.listeners.get(method);
        if (set) for (const l of set) l(params);
        for (const l of this.anyListeners) l(method, params);
        return;
      }
      case 'request':
        // Servers do not call clients in protocol 1.0.
        this.options.onProtocolFault?.('unexpected request from server', frame);
        return;
      case 'invalid':
        this.options.onProtocolFault?.(decoded.reason, frame);
        return;
    }
  }

  private takePending(id: JsonRpcId): Pending | undefined {
    const entry = this.pending.get(id);
    if (!entry) return undefined;
    this.pending.delete(id);
    if (entry.timer) clearTimeout(entry.timer);
    return entry;
  }

  private recordLatency(entry: Pending): void {
    this.lastLatencyMs = now() - entry.startedAt;
    for (const l of this.latencyListeners) l(this.lastLatencyMs, entry.method);
  }

  private failAll(error: Error): void {
    for (const [, entry] of this.pending) {
      if (entry.timer) clearTimeout(entry.timer);
      entry.reject(error);
    }
    this.pending.clear();
  }
}

/** The client with its generated namespaces. */
export type Client = SessionClient & NamespacedMethods;

export function createClient(transport: Transport, options?: ClientOptions): Client {
  return new SessionClient(transport, options) as Client;
}

function now(): number {
  return typeof performance !== 'undefined' ? performance.now() : Date.now();
}
