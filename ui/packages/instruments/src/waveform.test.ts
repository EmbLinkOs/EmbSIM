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

  it('integrates uneven PWM edges and clipped endpoints by time', () => {
    const r = new WaveformRecord('pwm', 'digital');
    r.appendFrame({ stream: 1, seq: 0, encoding: 'json', samples: [['0', 1], ['250', 0], ['1000', 1], ['1250', 0], ['2000', 1]] });
    expect(r.measurements(0n, 2000n).mean).toBe(.25);
    expect(r.measurements(0n, 2000n).rms).toBeCloseTo(.5);
    expect(r.measurements(0n, 2000n).dutyCycle).toBe(.25);
    expect(r.measurements(0n, 2000n).coverage).toBe(1);
    expect(r.measurements(125n, 1125n).dutyCycle).toBeCloseTo(.25);
    expect(r.measurements(500n, 900n).dutyCycle).toBe(0);
    expect(r.measurements(0n, 250n).dutyCycle).toBe(1);
  });

  it('measures average and RMS current without interpreting analog values as duty', () => {
    const r = new WaveformRecord('current', 'analog', 'red current', 'A');
    r.append(0n, 250n, Float64Array.from([.02, 0, 0, 0, .02]));
    const m = r.measurements(0n, 1000n);
    expect(m.mean).toBeCloseTo(.005);
    expect(m.rms).toBeCloseTo(.01);
    expect(Number.isNaN(m.dutyCycle)).toBe(true);
  });

  it('reports incomplete coverage for gaps, evicted anchors and future windows', () => {
    const r = new WaveformRecord('pwm', 'digital', 'pwm', '', 4);
    r.append(0n, 10n, Uint8Array.from([1, 0]));
    r.append(100n, 10n, Uint8Array.from([1, 0]), true);
    expect(Number.isNaN(r.measurements(0n, 110n).mean)).toBe(true);
    expect(r.measurements(0n, 110n).coverage).toBeCloseTo(20 / 110);
    expect(r.measurements(100n, 110n).dutyCycle).toBe(1);
    expect(Number.isNaN(r.measurements(100n, 120n).mean)).toBe(true);
    expect(r.measurements(100n, 120n, 120n).coverage).toBe(1);
    r.append(120n, 10n, Uint8Array.from([0, 1]));
    expect(Number.isNaN(r.measurements(0n, 130n).mean)).toBe(true);
  });

  it('rejects X/Z and nonfinite intervals but excludes zero-duration and right-edge changes', () => {
    const r = new WaveformRecord('pwm', 'digital');
    const base = 10_000_000_000_000_000n;
    r.appendFrame({ stream: 1, seq: 0, encoding: 'json', samples: [[String(base), 3], [String(base), 1], [String(base + 250n), 0], [String(base + 1000n), 2]] });
    expect(r.measurements(base, base + 1000n).dutyCycle).toBeCloseTo(.25);
    expect(Number.isNaN(r.measurements(base, base + 1100n, base + 1100n).mean)).toBe(true);
    const a = new WaveformRecord('nan', 'analog');
    a.append(0n, 10n, Float64Array.from([1, NaN, 1]));
    expect(Number.isNaN(a.measurements(0n, 20n).rms)).toBe(true);
  });

  it('does not extrapolate a partial stream to the engine clock, and advances constant captures with empty watermarks', () => {
    const r = new WaveformRecord('current', 'analog');
    r.appendFrame({ stream: 1, seq: 0, encoding: 'json', restart: true, through_time_ps: '10', samples: [['0', 1], ['10', 0]] });
    expect(r.measurements(0n, 20n, 20n).coverage).toBe(.5);
    expect(Number.isNaN(r.measurements(0n, 20n, 20n).mean)).toBe(true);
    r.appendFrame({ stream: 1, seq: 1, encoding: 'json', through_time_ps: '20', samples: [] });
    expect(r.measurements(0n, 20n).mean).toBe(.5);
    r.appendFrame({ stream: 1, seq: 2, encoding: 'json', restart: true, through_time_ps: '5', samples: [['0', 2], ['5', 2]] });
    expect(r.length).toBe(2);
    expect(r.measurements(0n, 5n, 5n).mean).toBe(2);
    expect(Number.isNaN(r.measurements(0n, 20n, 20n).mean)).toBe(true);
  });
});


describe('capture integrity', () => {
  it('does not include a sample before the requested left endpoint', () => {
    const r = new WaveformRecord('x','analog'); r.append(0n,10n,Float32Array.of(1,2,3));
    expect([...r.samples(5n,15n)]).toEqual([{t:10n,v:2}]);
  });
  it('keeps unknown JSON readings unknown rather than converting null to zero', () => {
    for (const kind of ['analog','digital'] as const) {
      const r = new WaveformRecord('x',kind);
      r.appendFrame({stream:'s',seq:0,encoding:'json',samples:[['0',null]],through_time_ps:'10'});
      expect(kind === 'analog' ? Number.isNaN(r.valueAt(0n)) : r.valueAt(0n) === 3).toBe(true);
      expect(r.measurements(0n,10n).coverage).toBe(0);
    }
  });
  it('rejects obsolete epochs without erasing an already received new capture', () => {
    const r = new WaveformRecord('x','analog');
    r.appendFrame({stream:'s',seq:0,encoding:'json',capture_epoch:2,samples:[['10',5]],through_time_ps:'20'});
    expect(r.setCaptureEpoch(2)).toBe(true); expect(r.length).toBe(1);
    r.appendFrame({stream:'s',seq:1,encoding:'json',capture_epoch:1,restart:true,samples:[['0',0]],through_time_ps:'5'});
    expect(r.start).toBe(10n); expect(r.captureEnd).toBe(20n);
  });
  it('does not draw, read or infer frequency across loss', () => {
    const r = new WaveformRecord('x','digital'); r.append(0n,10n,Uint8Array.of(0,1,0)); r.append(100n,10n,Uint8Array.of(1,0,1),true);
    expect(Number.isNaN(r.observedValueAt(50n))).toBe(true);
    expect(Number.isNaN(r.observedValueAt(121n))).toBe(true);
    expect(r.decimate(0n,120n,12).missing[5]).toBe(1);
    expect(r.periodAndFrequency(0n,120n).frequencyHz).toBeNull();
  });
  it('tracks absolute event positions through eviction and snapshot isolation', () => {
    const r = new WaveformRecord('x','analog','x','V',3); r.append(0n,10n,Float32Array.of(0,1,2));
    const copy = r.snapshot(5n,20n); r.append(30n,10n,Float32Array.of(3,4));
    expect(r.firstOrdinal).toBe(2); expect([...r.events(3)].map((e) => e.v)).toEqual([3,4]);
    r.clear(); expect(copy.observedValueAt(10n)).toBe(1); expect(copy.observedValueAt(20n)).toBe(2);
  });
});
