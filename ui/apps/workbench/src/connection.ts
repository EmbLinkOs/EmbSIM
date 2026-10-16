// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// Connection bootstrap: chooses the transport, opens the session and wires
// notifications into the stores. The client is a module singleton so stores
// and actions share one request queue.

import {
  createClient,
  JsonRpcError,
  MockTransport,
  WebSocketTransport,
  type Client,
  type MockDevHooks,
  type Transport,
} from '@embsim/protocol';
import { isTauri, platform } from './platform/platform';
import { useInstruments } from './stores/instruments';
import { useProject } from './stores/project';
import { useSession } from './stores/session';

let client: Client | null = null;
let transport: Transport | null = null;

export function getClient(): Client {
  if (!client) throw new Error('not connected');
  return client;
}

export function tryClient(): Client | null {
  return client;
}

export function mockDev(): MockDevHooks | null {
  return transport instanceof MockTransport ? transport.dev : null;
}

/**
 * Where the server is. In order: `?ws=` in the query, `VITE_EMBSIM_WS`,
 * `?mock=1` forces the demo, the Tauri sidecar, and for a production build
 * served over http(s) the same origin's `/ws` (that is `embsim serve`). The
 * Vite dev server has no engine behind it, so it uses the demo session.
 */
export async function resolveTransport(): Promise<{ transport: Transport; isDemo: boolean; fallback?: string }> {
  const params = new URLSearchParams(window.location.search);
  const env = import.meta.env as Record<string, string | undefined>;
  const explicit = params.get('ws') ?? env['VITE_EMBSIM_WS'];
  if (explicit) return { transport: new WebSocketTransport(explicit), isDemo: false };
  if (params.get('mock') === '1') return { transport: new MockTransport(), isDemo: true };
  if (isTauri()) {
    try {
      const url = await (await platform()).startSidecar();
      if (url) return { transport: new WebSocketTransport(url), isDemo: false };
    } catch (e) {
      return { transport: new MockTransport(), isDemo: true, fallback: `sidecar failed: ${(e as Error).message}` };
    }
  }
  if (!import.meta.env.DEV && /^https?:$/.test(window.location.protocol)) {
    const scheme = window.location.protocol === 'https:' ? 'wss' : 'ws';
    const url = `${scheme}://${window.location.host}/ws`;
    const ws = new WebSocketTransport(url);
    try {
      await ws.connect();
      return { transport: ws, isDemo: false };
    } catch {
      return { transport: new MockTransport(), isDemo: true, fallback: `no server at ${url}` };
    }
  }
  return { transport: new MockTransport(), isDemo: true };
}

export interface ConnectOptions {
  /** Supply a transport (tests). */
  transport?: Transport;
  isDemo?: boolean;
}

let connecting: Promise<Client> | null = null;

/**
 * Connects, opens the session and primes every store. A second call while
 * one is under way (React's development double effects) shares it: the
 * engine serves one client at a time, so a second socket would wait forever.
 */
export function connectSession(options: ConnectOptions = {}): Promise<Client> {
  connecting ??= openSession(options).finally(() => {
    connecting = null;
  });
  return connecting;
}

async function openSession(options: ConnectOptions): Promise<Client> {
  const session = useSession.getState();
  session.setConnection({ status: 'connecting' });
  let isDemo = options.isDemo ?? false;
  let fallback: string | undefined;
  if (options.transport) transport = options.transport;
  else {
    const r = await resolveTransport();
    transport = r.transport;
    isDemo = r.isDemo;
    fallback = r.fallback;
  }
  const c = createClient(transport, {
    onProtocolFault: (reason) => console.warn('[protocol]', reason),
  });
  client = c;
  session.setConnection({ transportLabel: transport.label, isDemo, error: fallback ?? null });

  transport.onState((state, detail) => {
    useSession.getState().setConnection({ status: state === 'open' ? 'open' : state === 'closed' ? 'closed' : 'connecting', error: detail && state === 'closed' ? detail : null });
  });
  c.onLatency((ms) => useSession.getState().setLatency(ms));
  c.on('observe.update', (u) => useSession.getState().applyObserve(u));
  c.on('control.state', (s) => useSession.getState().applyControlState(s));
  c.on('diagnostics', (d) => useSession.getState().addDiagnostics(d.records));
  c.on('waveform.samples', (f) => useInstruments.getState().appendFrame(f));
  c.on('serial.data', (d) => {
    const text = (() => {
      try {
        return new TextDecoder().decode(Uint8Array.from(atob(d.base64), (ch) => ch.charCodeAt(0)));
      } catch {
        return d.base64;
      }
    })();
    useInstruments.getState().addSerialText(d.target, useSession.getState().now, text);
  });
  let refreshTimer: ReturnType<typeof setTimeout> | null = null;
  c.on('project.changed', () => {
    if (refreshTimer) clearTimeout(refreshTimer);
    refreshTimer = setTimeout(() => void useProject.getState().refresh(), 50);
  });

  if (transport.state !== 'open') await c.connect();
  const opened = await c.session.open({ client: 'workbench/0.1', protocol: '1.0', streams: 'base64' });
  session.setInfo({ engine: opened.engine, protocol: opened.protocol, capabilities: opened.capabilities });
  await primeStores(c);
  session.setConnection({ status: 'open' });
  return c;
}

/** Loads session facts, project, catalog, inputs, probes and subscriptions. */
export async function primeStores(c: Client): Promise<void> {
  const info = await c.session.info({});
  useSession.getState().setInfo({
    projectName: info.project,
    now: BigInt(info.now_ps),
    running: info.running,
    speed: info.speed,
    policy: info.policy,
    boards: info.boards,
    timingTiers: info.timing_tiers ?? {},
  });
  await useProject.getState().loadAll();
  const [inputs, probes] = await Promise.all([c.input.list({}), c.probe.list({})]);
  useSession.getState().setInputs(inputs.inputs);
  useInstruments.getState().setProbes(probes.probes);
  await c.observe.subscribe({ what: ['nets', 'outputs', 'diagnostics', 'clock'], max_rate_hz: 30 });
  const snap = await c.observe.snapshot({ include: ['nets', 'outputs'] });
  useSession.getState().applyObserve(snap);
  await resubscribeWaveforms(c);
}

/** (Re)subscribes the waveform stream to every known probe. */
export async function resubscribeWaveforms(c: Client): Promise<void> {
  const inst = useInstruments.getState();
  const ids = inst.probes.filter((p) => p.kind === 'analog' || p.kind === 'digital' || p.kind === 'value').map((p) => p.probe as string);
  for (const stream of inst.streams) {
    try {
      await c.waveform.unsubscribe({ stream });
    } catch {
      // the stream may be gone after a reset
    }
  }
  inst.setStreams([], []);
  if (ids.length === 0) return;
  try {
    const r = await c.waveform.subscribe({ probes: ids, max_rate_hz: 200_000 });
    const streams = r.streams ?? (r.stream !== undefined ? [r.stream] : []);
    useInstruments.getState().setStreams(streams, ids);
  } catch (e) {
    if (!(e instanceof JsonRpcError)) throw e;
    console.warn('waveform.subscribe failed:', e.message);
  }
}

export function disconnectSession(): void {
  client?.close();
  client = null;
  transport = null;
}
