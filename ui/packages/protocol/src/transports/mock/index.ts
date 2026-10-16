// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// MockTransport: an in-memory server speaking the same JSON-RPC frames as
// `embsim serve`, so the client code path is identical. The workbench shows
// a "Demo session" badge when it is in use.

import { JsonRpcCode, decodeMessage, encodeError, encodeNotification, encodeResult } from '../../jsonrpc';
import type { MethodName, Methods, Params } from '../../methods';
import { TransportBase, type Transport } from '../../transport';
import type { Project } from '../../types';
import { MOCK_CATALOG, catalogItems } from './catalog';
import { EngineError, MockSession, type Notify } from './session';

export { demoProject } from './project';
export { MOCK_CATALOG } from './catalog';

const ENGINE = '0.1.0-mock';
const PROTOCOL = '1.0';
const CAPABILITIES = ['analog'];

export interface MockDevHooks {
  /** Emits one `net.contention` warning diagnostic at the current time. */
  raiseContention(): void;
  /** Direct access to the fake session, for tests. */
  readonly session: MockSession;
}

type Handler<M extends MethodName> = (params: Params<M>) => Methods[M]['result'] | Promise<Methods[M]['result']>;

export class MockTransport extends TransportBase implements Transport {
  readonly label = 'Demo session';
  readonly dev: MockDevHooks;
  private readonly session: MockSession;
  private readonly handlers: { [M in MethodName]: Handler<M> };
  private opened = false;
  /** Simulated one-way latency in ms, so the status bar shows something realistic. */
  latencyMs = 1;

  constructor(options: { project?: Project; latencyMs?: number } = {}) {
    super();
    const notify: Notify = (method: string, params: unknown) => {
      if (this.state === 'open') this.emitText(encodeNotification(method, params));
    };
    this.session = new MockSession(notify);
    if (options.project) this.session.loadProject(options.project);
    if (options.latencyMs !== undefined) this.latencyMs = options.latencyMs;
    const s = this.session;
    this.dev = { raiseContention: () => s.raiseContention(), session: s };
    this.handlers = {
      'session.open': (p) => {
        if (p.protocol.split('.')[0] !== PROTOCOL.split('.')[0]) {
          throw new EngineError(JsonRpcCode.PROTOCOL_VERSION, `protocol ${p.protocol} is not ${PROTOCOL}`);
        }
        this.opened = true;
        return { protocol: PROTOCOL, engine: ENGINE, abi: 1, capabilities: CAPABILITIES };
      },
      'session.close': () => {
        this.opened = false;
        s.pause('stop');
        return {};
      },
      'session.info': () => ({
        project: s.project.name,
        policy: s.policy,
        now_ps: s.now.toString(),
        running: s.running,
        speed: s.speed,
        boards: [],
        islands: 1,
        limits: { components: 10_000, nets: 10_000 },
        timing_tiers: {},
      }),
      'session.policy': (p) => ({ policy: s.setPolicy(p.policy) }),

      'project.load': (p) => s.loadProject('json' in p ? p.json : undefined),
      'project.get': () => ({ project: structuredClone(s.project) }),
      'project.save': (p) => ({ path: p.path ?? 'memory://demo/project.json' }),
      'project.validate': () => ({ ok: true, diagnostics: [] }),
      'project.transaction': (p) => {
        switch (p.op) {
          case 'begin':
            return { tx: s.beginTx() };
          case 'edit':
            s.editTx(p.tx, p.edits);
            return {};
          case 'commit':
            return s.commitTx(p.tx);
          case 'abort':
            s.abortTx(p.tx);
            return {};
        }
      },
      'project.topology': () => s.topology(),

      'control.run': (p) => {
        s.run(p.speed, p.until);
        return { running: true };
      },
      'control.pause': () => ({ now_ps: s.pause().toString() }),
      'control.step': (p) => ({ now_ps: s.step(p.mode).toString() }),
      'control.advance': (p) => ({ now_ps: s.advance(p).toString() }),
      'control.reset': () => {
        s.reset();
        return {};
      },
      'control.speed': (p) => ({ speed: s.setSpeed(p.speed) }),

      'input.set': (p) => s.setInput(p.target, p.input, p.value, p.at),
      'input.press': (p) => s.setInput(p.target, 'pressed', true),
      'input.release': (p) => s.setInput(p.target, 'pressed', false),
      'input.serial': () => {
        throw new EngineError(-32038, 'no serial peripheral in this session');
      },
      'input.list': () => ({ inputs: s.listInputs() }),

      'probe.add': (p) => {
        const info = s.addProbe(p.target, p.kind, p.rate_hz);
        return { probe: info.probe };
      },
      'probe.remove': (p) => {
        s.removeProbe(p.probe);
        return {};
      },
      'probe.list': () => ({ probes: s.listProbes() }),
      'probe.read': (p) => s.readProbe(p.probe, p.from_ps, p.to_ps, p.max),
      'waveform.subscribe': (p) => ({ stream: s.subscribe(p.probes, p.max_rate_hz) }),
      'waveform.unsubscribe': (p) => {
        s.unsubscribe(String(p.stream));
        return {};
      },

      'observe.snapshot': (p) => s.snapshot(p.include),
      'observe.subscribe': (p) => {
        s.subscribeObserve(p.what, p.max_rate_hz);
        return {};
      },

      'debug.breakpoint': () => noBoard(),
      'debug.breakpoints': () => noBoard(),
      'debug.halt': () => noBoard(),
      'debug.continue': () => noBoard(),
      'debug.step': () => noBoard(),
      'debug.symbol': () => noBoard(),
      'debug.source': () => noBoard(),
      'debug.registers': () => noBoard(),
      'debug.set_register': () => noBoard(),
      'debug.memory': () => noBoard(),
      'debug.write_memory': () => noBoard(),
      'debug.where': () => noBoard(),
      'debug.variables': () => noBoard(),
      'debug.disassemble': () => noBoard(),
      'debug.peripherals': () => noBoard(),
      'debug.gdb': () => noBoard(),

      'catalog.list': (p) => {
        const q = p.query?.toLowerCase();
        return {
          items: catalogItems().filter((item) => {
            if (p.category && item.category !== p.category) return false;
            if (!q) return true;
            const d = MOCK_CATALOG[item.id]!.description;
            return [item.id, item.name, d.description ?? '', ...(d.keywords ?? [])].some((t) => t.toLowerCase().includes(q));
          }),
        };
      },
      'catalog.get': (p) => {
        const entry = MOCK_CATALOG[p.id];
        if (!entry) throw new EngineError(-32002, `no component ${p.id} in the catalog`);
        return structuredClone(entry);
      },
      'catalog.validate': () => ({ ok: true, diagnostics: [] }),
      'catalog.libraries': () => ({ libraries: [{ path: '<mock>', name: 'emblink', version: '0.1.0', items: catalogItems().length }] }),

      'analysis.start': () => notSupported('analysis'),
      'analysis.stop': () => ({}),
      'analysis.coverage': () => notSupported('analysis'),
      'analysis.profile': () => notSupported('analysis'),
      'record.start': () => notSupported('record'),
      'record.stop': () => notSupported('record'),
      'record.replay': () => notSupported('record'),
      'record.to_scenario': () => notSupported('record'),
      'toolchain.detect': () => ({ profiles: [] }),
      'toolchain.build': () => notSupported('toolchain'),
      'scenario.run': () => notSupported('scenario'),
      'scenario.get': () => notSupported('scenario'),
      'scenario.save': () => notSupported('scenario'),
    };
  }

  async connect(): Promise<void> {
    this.setState('connecting');
    await delay(this.latencyMs);
    this.setState('open');
  }

  close(): void {
    this.session.dispose();
    this.setState('closed', 'client closed');
  }

  send(frame: string): void {
    if (this.state !== 'open') throw new Error('mock transport is not open');
    const decoded = decodeMessage(frame);
    if (decoded.kind === 'invalid') {
      this.reply(encodeError(null, JsonRpcCode.INVALID_REQUEST, decoded.reason));
      return;
    }
    if (decoded.kind !== 'request') return; // notifications to the server are ignored
    const { id, method, params } = decoded.message;
    void this.dispatch(method, params)
      .then((result) => this.reply(encodeResult(id, result)))
      .catch((e: unknown) => {
        if (e instanceof EngineError) this.reply(encodeError(id, e.code, e.message, e.data));
        else if (e instanceof RangeError) this.reply(encodeError(id, JsonRpcCode.INVALID_PARAMS, e.message));
        else this.reply(encodeError(id, JsonRpcCode.INTERNAL, e instanceof Error ? e.message : String(e)));
      });
  }

  private async dispatch(method: string, params: unknown): Promise<unknown> {
    await delay(this.latencyMs);
    const handler = (this.handlers as Record<string, (p: unknown) => unknown>)[method];
    if (!handler) throw new EngineError(JsonRpcCode.METHOD_NOT_FOUND, `unknown method ${method}`);
    if (!this.opened && method !== 'session.open') throw new EngineError(-32016, 'session.open first');
    if (params !== undefined && (typeof params !== 'object' || params === null || Array.isArray(params))) {
      throw new EngineError(JsonRpcCode.INVALID_PARAMS, 'params must be an object');
    }
    return handler(params ?? {});
  }

  private reply(frame: string): void {
    if (this.state === 'open') this.emitText(frame);
  }
}

function noBoard(): never {
  throw new EngineError(-32002, 'no MCU board in this session');
}

function notSupported(what: string): never {
  throw new EngineError(-32038, `${what} is not supported by the demo session`);
}

function delay(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}
