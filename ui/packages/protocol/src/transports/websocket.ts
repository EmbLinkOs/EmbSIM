// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// WebSocket transport (SPEC-008 §1): one JSON object per text frame; binary
// frames carry waveform samples with a 16-byte header. This is the only file
// in `ui/` allowed to touch `WebSocket` (eslint rule embsim/io-only-in-protocol).

import { TransportBase, type Transport } from '../transport';

export interface WebSocketTransportOptions {
  /** Reconnect automatically after an unexpected close. Default false. */
  reconnect?: boolean;
  /** Delay before reconnecting, ms. Default 1000. */
  reconnectDelayMs?: number;
}

export class WebSocketTransport extends TransportBase implements Transport {
  readonly url: string;
  readonly label: string;
  private socket: WebSocket | null = null;
  private readonly options: WebSocketTransportOptions;
  private closedByUser = false;
  private queue: string[] = [];

  constructor(url: string, options: WebSocketTransportOptions = {}) {
    super();
    this.url = url;
    this.label = url;
    this.options = options;
  }

  connect(): Promise<void> {
    this.closedByUser = false;
    return new Promise((resolve, reject) => {
      this.setState('connecting');
      let settled = false;
      const socket = new WebSocket(this.url);
      socket.binaryType = 'arraybuffer';
      this.socket = socket;
      socket.onopen = () => {
        settled = true;
        this.setState('open');
        for (const frame of this.queue.splice(0)) socket.send(frame);
        resolve();
      };
      socket.onmessage = (ev: MessageEvent<string | ArrayBuffer>) => {
        if (typeof ev.data === 'string') this.emitText(ev.data);
        else this.emitBinary(ev.data);
      };
      socket.onerror = () => {
        if (!settled) {
          settled = true;
          this.setState('closed', 'connection failed');
          reject(new Error(`WebSocket connection to ${this.url} failed`));
        }
      };
      socket.onclose = (ev: CloseEvent) => {
        this.socket = null;
        this.setState('closed', ev.reason || `closed (${ev.code})`);
        if (!settled) {
          settled = true;
          reject(new Error(`WebSocket to ${this.url} closed before opening`));
        }
        if (!this.closedByUser && this.options.reconnect) {
          setTimeout(() => {
            void this.connect().catch(() => undefined);
          }, this.options.reconnectDelayMs ?? 1000);
        }
      };
    });
  }

  close(): void {
    this.closedByUser = true;
    this.socket?.close(1000, 'client closed');
    this.socket = null;
    this.setState('closed', 'client closed');
  }

  send(frame: string): void {
    if (this.socket && this.socket.readyState === WebSocket.OPEN) this.socket.send(frame);
    else if (this.state === 'connecting') this.queue.push(frame);
    else throw new Error('WebSocket transport is not open');
  }
}

/**
 * Chooses the WebSocket URL for a page served by `embsim serve`: the
 * `VITE_EMBSIM_WS` build variable, then `?ws=` in the query, then the same
 * origin's `/ws`. Returns null when nothing indicates a server (dev builds
 * on a plain file server use the mock).
 */
export function resolveServerUrl(location: Pick<Location, 'protocol' | 'host' | 'search'>, env: Record<string, string | undefined>): string | null {
  const params = new URLSearchParams(location.search);
  const explicit = params.get('ws') ?? env['VITE_EMBSIM_WS'];
  if (explicit) return explicit;
  if (params.get('mock') === '1') return null;
  if (env['VITE_EMBSIM_SERVED'] === '1' || params.has('served')) {
    const scheme = location.protocol === 'https:' ? 'wss' : 'ws';
    return `${scheme}://${location.host}/ws`;
  }
  return null;
}
