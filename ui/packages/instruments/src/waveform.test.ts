// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { bytesToBase64 } from '@embsim/protocol';
import { describe, expect, it } from 'vitest';
import { WaveformRecord } from './waveform';

describe('waveform record', () => {
  it('merges contiguous frames and answers range queries', () => {
    const r = new WaveformRecord('a', 'analog');
    r.append(0n, 10n, Float32Array.from([1, 2, 3]));
    r.append(30n, 10n, Float32Array.from([4, 5]));
    expect(r.length).toBe(5);
    expect(r.start).toBe(0n);
    expect(r.end).toBe(40n);
    expect([...r.samples(10n, 30n)].map((s) => s.v)).toEqual([2, 3, 4]);
    expect(r.valueAt(25n)).toBe(3);
    expect(r.valueAt(100n)).toBe(5);
    expect(Number.isNaN(r.valueAt(-1n))).toBe(true);
  });

  it('keeps gaps as separate segments and trims to capacity', () => {
    const r = new WaveformRecord('a', 'analog', 'a', 'V', 4);
    r.append(0n, 1n, Float32Array.from([1, 2, 3]));
    r.append(100n, 1n, Float32Array.from([4, 5, 6]), true);
    expect(r.length).toBe(4);
    expect(r.start).toBe(2n);
    expect([...r.samples(0n, 200n)].map((s) => s.v)).toEqual([3, 4, 5, 6]);
  });

  it('decimates with min/max per bucket', () => {
    const r = new WaveformRecord('a', 'analog');
    const values = Float32Array.from({ length: 100 }, (_, i) => (i % 10) - 5);
    r.append(0n, 1n, values);
    const d = r.decimate(0n, 100n, 10);
    expect(Array.from(d.min)).toEqual(new Array(10).fill(-5));
    expect(Array.from(d.max)).toEqual(new Array(10).fill(4));
    expect(Array.from(d.count)).toEqual(new Array(10).fill(10));
    const empty = r.decimate(1000n, 2000n, 4);
    expect(Number.isNaN(empty.min[0])).toBe(true);
  });

  it('measures period, frequency and statistics', () => {
    const r = new WaveformRecord('d', 'digital');
    const values = new Uint8Array(1000);
    for (let i = 0; i < 1000; i++) values[i] = Math.floor(i / 50) % 2; // period 100 samples
    r.append(0n, 1_000_000n, values); // 1 µs per sample → period 100 µs, 10 kHz
    const m = r.periodAndFrequency(0n, 999_000_000n);
    expect(m.period).toBe(100_000_000n);
    expect(m.frequencyHz).toBeCloseTo(10_000, 3);
    const s = r.stats(0n, 999_000_000n);
    expect(s.min).toBe(0);
    expect(s.max).toBe(1);
    expect(s.mean).toBeCloseTo(0.5, 2);
  });

  it('decodes json pair frames with hold semantics', () => {
    const d = new WaveformRecord('btn', 'digital');
    d.appendFrame({ stream: 1, seq: 0, encoding: 'json', samples: [['0', 0], ['0', 1], ['2000000000', 0], ['4000000000', 1], ['6000000000', 0]] });
    expect(d.valueAt(1_000_000_000n)).toBe(1);
    expect(d.valueAt(3_000_000_000n)).toBe(0);
    expect(d.valueAt(9_000_000_000n)).toBe(0);
    expect(d.risingEdges(0n, 10_000_000_000n).length).toBe(2);
    const dec = d.decimate(0n, 8_000_000_000n, 8);
    expect(dec.count[2]).toBe(1);
    expect(dec.last[4]).toBe(1);
  });

  it('decodes protocol frames', () => {
    const r = new WaveformRecord('coll', 'analog');
    const f = Float32Array.from([0.5, 1.5]);
    r.appendFrame({ stream: 's1', seq: 0, first_time_ps: '5', encoding: 'f32', data: bytesToBase64(new Uint8Array(f.buffer)), period_ps: '2' });
    expect(r.valueAt(7n)).toBe(1.5);
    const d = new WaveformRecord('btn', 'digital');
    d.appendFrame({ stream: 's1', seq: 0, first_time_ps: '0', encoding: 'u8', data: bytesToBase64(Uint8Array.from([1, 0, 2])), period_ps: '1' });
    expect(d.valueAt(2n)).toBe(2);
  });
});
