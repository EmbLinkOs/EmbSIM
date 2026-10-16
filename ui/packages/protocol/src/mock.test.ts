// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe, expect, it } from 'vitest';
import { createClient } from './client';
import { base64ToBytes } from './jsonrpc';
import { MockTransport } from './transports/mock';
import type { ObserveUpdate, WaveformSamples } from './types';

async function open() {
  const transport = new MockTransport({ latencyMs: 0 });
  const client = createClient(transport, { timeoutMs: 2000 });
  await client.connect();
  await client.session.open({ client: 'test', protocol: '1.0', streams: 'base64' });
  return { client, transport };
}

describe('mock transport', () => {
  it('refuses calls before session.open', async () => {
    const transport = new MockTransport({ latencyMs: 0 });
    const client = createClient(transport, { timeoutMs: 2000 });
    await client.connect();
    await expect(client.session.info({})).rejects.toThrow(/session.open/);
  });

  it('loads the demo project and reports topology', async () => {
    const { client } = await open();
    const { project } = await client.project.get({});
    expect(project.components.map((c) => c.id)).toContain('SW1');
    const topo = await client.project.topology({});
    expect(topo.nets.find((n) => n.name === 'BTN')?.pins).toContain('SW1.1');
  });

  it('advances the virtual clock and reflects the button in observations', async () => {
    const { client } = await open();
    const updates: ObserveUpdate[] = [];
    client.on('observe.update', (u) => updates.push(u));
    await client.observe.subscribe({ what: ['nets', 'outputs', 'clock'], max_rate_hz: 30 });
    const snap = await client.observe.snapshot({ include: ['nets', 'outputs'] });
    expect(snap.nets?.['BTN']?.logic).toBe('1');
    expect(snap.outputs?.['D1.lit']).toBe(true);

    await client.input.press({ target: 'SW1' });
    const { now_ps } = await client.control.advance({ by: '1ms' });
    expect(BigInt(now_ps)).toBe(1_000_000_000n);
    const after = await client.observe.snapshot({ include: ['nets', 'outputs'] });
    expect(after.nets?.['BTN']?.logic).toBe('0');
    expect(after.outputs?.['D1.lit']).toBe(false);
    expect(updates.length).toBeGreaterThan(0);
    expect(updates.at(-1)?.now_ps).toBe('1000000000');
  });

  it('streams synthetic waveform samples', async () => {
    const { client } = await open();
    const frames: WaveformSamples[] = [];
    client.on('waveform.samples', (f) => frames.push(f));
    const { stream } = await client.waveform.subscribe({ probes: ['btn', 'coll'] });
    await client.control.advance({ by: '100us' });
    await client.input.press({ target: 'SW1' });
    await client.control.advance({ by: '100us' });
    const btn = frames.filter((f) => f.stream === stream && f.probe === 'btn');
    const coll = frames.filter((f) => f.stream === stream && f.probe === 'coll');
    expect(btn.length).toBeGreaterThan(0);
    const bytes = base64ToBytes(btn[0]!.data!);
    expect(bytes.length).toBe(20); // 100 µs at 200 kHz
    expect(Array.from(bytes).every((b) => b === 1)).toBe(true);
    const second = base64ToBytes(btn[1]!.data!);
    expect(Array.from(second.slice(1)).every((b) => b === 0)).toBe(true); // the sample at the edge instant still reads the old level
    const f32 = new Float32Array(base64ToBytes(coll[1]!.data!).buffer);
    expect(f32[0]).toBeLessThan(1);
    expect(f32[f32.length - 1]).toBeGreaterThan(3);
  });

  it('applies project transactions', async () => {
    const { client } = await open();
    const { tx } = await client.project.transaction({ op: 'begin' });
    await client.project.transaction({
      op: 'edit',
      tx: tx!,
      edits: [
        { add_component: { id: 'C1', component: 'emblink/capacitor', layout: { x: 100, y: 100 } } },
        { connect: { net: 'COLL', pin: 'C1.1' } },
        { connect: { net: 'GND', pin: 'C1.2' } },
        { rename_net: { from: 'Q1B', to: 'BASE' } },
      ],
    });
    const r = await client.project.transaction({ op: 'commit', tx: tx! });
    expect(r.applied).toBe(1);
    const { project } = await client.project.get({});
    expect(project.nets['COLL']).toContain('C1.1');
    expect(project.nets['BASE']).toBeDefined();
    expect(project.nets['Q1B']).toBeUndefined();
  });

  it('reports engine errors as JSON-RPC errors', async () => {
    const { client } = await open();
    await expect(client.debug.registers({ board: 'B1' })).rejects.toMatchObject({ code: -32002 });
    const raw = client.call.bind(client) as unknown as (method: string, params: unknown) => Promise<unknown>;
    await expect(raw('nope.method', {})).rejects.toMatchObject({ code: -32601 });
  });

  it('emits a contention diagnostic from the dev hook', async () => {
    const { client, transport } = await open();
    const records: string[] = [];
    client.on('diagnostics', (d) => records.push(...d.records.map((r) => r.code)));
    transport.dev.raiseContention();
    expect(records).toEqual(['net.contention']);
  });
});
