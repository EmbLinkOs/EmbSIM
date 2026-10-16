// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// Control, input and probe actions: thin wrappers over the client that keep
// the stores in step with the responses. Errors are returned as messages so
// the caller can toast them.

import { JsonRpcError, type Policy, type ProbeKind } from '@embsim/protocol';
import { getClient, resubscribeWaveforms } from './connection';
import { useInstruments } from './stores/instruments';
import { useSession } from './stores/session';

export function errorMessage(e: unknown): string {
  if (e instanceof JsonRpcError) return `${e.message} (${e.code})`;
  return e instanceof Error ? e.message : String(e);
}

export const controls = {
  async run(): Promise<void> {
    const c = getClient();
    await c.control.run({ speed: useSession.getState().speed });
    useSession.getState().setInfo({ running: true });
  },
  async pause(): Promise<void> {
    const r = await getClient().control.pause({});
    useSession.getState().setInfo({ running: false, now: BigInt(r.now_ps) });
  },
  async toggle(): Promise<void> {
    if (useSession.getState().running) await controls.pause();
    else await controls.run();
  },
  async step(mode: 'event' | 'instruction' | 'delta' = 'event'): Promise<void> {
    const r = await getClient().control.step({ mode });
    useSession.getState().setInfo({ now: BigInt(r.now_ps) });
  },
  async advance(by: string): Promise<void> {
    const r = await getClient().control.advance({ by });
    useSession.getState().setInfo({ now: BigInt(r.now_ps) });
  },
  async reset(): Promise<void> {
    await getClient().control.reset({});
    useSession.getState().setInfo({ now: 0n, running: false, nets: {}, outputs: {} });
    useInstruments.getState().clearRecords();
    const snap = await getClient().observe.snapshot({ include: ['nets', 'outputs'] });
    useSession.getState().applyObserve(snap);
    await resubscribeWaveforms(getClient());
  },
  async setSpeed(speed: number): Promise<void> {
    const r = await getClient().control.speed({ speed });
    useSession.getState().setInfo({ speed: r.speed });
  },
  async setPolicy(policy: Policy): Promise<void> {
    const r = await getClient().session.policy({ policy });
    useSession.getState().setInfo({ policy: r.policy });
  },
};

export const inputs = {
  async set(target: string, input: string, value: unknown): Promise<void> {
    useSession.getState().setInputValue(target, input, value);
    await getClient().input.set({ target, input, value });
  },
  async press(target: string): Promise<void> {
    useSession.getState().setInputValue(target, 'pressed', true);
    await getClient().input.press({ target });
  },
  async release(target: string): Promise<void> {
    useSession.getState().setInputValue(target, 'pressed', false);
    await getClient().input.release({ target });
  },
  async serial(target: string, text: string): Promise<void> {
    await getClient().input.serial({ target, text });
    useInstruments.getState().addSerialLine({ at: useSession.getState().now, direction: 'tx', text: text.replace(/\n$/, '') });
  },
};

export const probes = {
  async add(target: string, kind: ProbeKind, rateHz?: number): Promise<string> {
    const c = getClient();
    const r = await c.probe.add({ target, kind, rate_hz: rateHz });
    const list = await c.probe.list({});
    useInstruments.getState().setProbes(list.probes);
    await resubscribeWaveforms(c);
    return r.id ?? String(r.probe);
  },
  async remove(probe: string): Promise<void> {
    const c = getClient();
    await c.probe.remove({ probe });
    const list = await c.probe.list({});
    useInstruments.getState().setProbes(list.probes);
    await resubscribeWaveforms(c);
  },
};

/** Exports the records of the given probes over a window as CSV text. */
export function exportCsv(probeIds: string[], from: bigint, to: bigint, max = 200_000): string {
  const inst = useInstruments.getState();
  const records = probeIds.map((id) => inst.records.get(id)).filter((r): r is NonNullable<typeof r> => Boolean(r));
  const lines = [`time_ps,${records.map((r) => r.id).join(',')}`];
  if (records.length === 0) return lines.join('\n');
  const span = to - from;
  const step = span / BigInt(max) > 0n ? span / BigInt(max) : 1n;
  for (let t = from; t <= to; t += step) {
    lines.push(`${t},${records.map((r) => r.valueAt(t)).join(',')}`);
    if (lines.length > max + 1) break;
  }
  return lines.join('\n');
}

/** Exports digital records as a minimal VCD. */
export function exportVcd(probeIds: string[], from: bigint, to: bigint): string {
  const inst = useInstruments.getState();
  const records = probeIds.map((id) => inst.records.get(id)).filter((r): r is NonNullable<typeof r> => Boolean(r));
  const ids = records.map((_, i) => String.fromCharCode(33 + i));
  const out = ['$timescale 1ps $end', '$scope module embsim $end'];
  records.forEach((r, i) => out.push(`$var ${r.kind === 'digital' ? 'wire 1' : 'real 64'} ${ids[i]} ${r.id} $end`));
  out.push('$upscope $end', '$enddefinitions $end');
  const last: (number | null)[] = records.map(() => null);
  const changes = new Map<bigint, string[]>();
  records.forEach((r, i) => {
    for (const { t, v } of r.samples(from, to)) {
      if (last[i] === v) continue;
      last[i] = v;
      const code = r.kind === 'digital' ? (v === 2 ? 'z' : v === 3 ? 'x' : String(v)) + ids[i] : `r${v} ${ids[i]}`;
      (changes.get(t) ?? changes.set(t, []).get(t)!).push(code);
    }
  });
  for (const t of [...changes.keys()].sort((a, b) => (a < b ? -1 : a > b ? 1 : 0))) {
    out.push(`#${t}`);
    out.push(...changes.get(t)!);
  }
  return out.join('\n');
}
