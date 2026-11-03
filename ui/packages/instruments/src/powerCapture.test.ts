// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import type { PowerCapture } from '@embsim/protocol';
import { describe, expect, it } from 'vitest';
import { PowerCaptureRecords } from './powerCapture';

const read = (over: Partial<PowerCapture>): PowerCapture => ({
  kind: 'power', state: 'sampling', mode: 'source', sample_rate: 100000, start_ps: '0', period_ps: '10000000',
  first: 0, since: 0, next: 4, samples: [0.001, 0.001, 0.101, 0.101], ...over,
});

describe('a power profiler followed live', () => {
  it('puts each sample on the 10 us grid as one analog record', () => {
    const rec = new PowerCaptureRecords('PPK.METER');
    rec.ingest(read({}));
    expect(rec.length).toBe(4);
    expect(rec.record.valueAt(5_000_000n)).toBeCloseTo(0.001, 6);
    expect(rec.record.valueAt(25_000_000n)).toBeCloseTo(0.101, 6);
    expect(rec.timeOf(2)).toBe(20_000_000n);
  });
  it("gives a window's average, peak and charge as the Power Profiler app does", () => {
    const rec = new PowerCaptureRecords('PPK.METER');
    rec.ingest(read({}));
    const w = rec.window(0n, 40_000_000n);
    expect(w.samples).toBe(4);
    expect(w.average).toBeCloseTo(0.051, 6);
    expect(w.max).toBeCloseTo(0.101, 6);
    expect(w.charge).toBeCloseTo(0.204 * 10e-6, 12); // 204 mA x 10 us
    expect(rec.window(20_000_000n, 40_000_000n).average).toBeCloseTo(0.101, 6);
  });
  it('appends reads and starts over for a new capture', () => {
    const rec = new PowerCaptureRecords('PPK.METER');
    rec.ingest(read({}));
    rec.ingest(read({ since: 4, next: 6, samples: [0.001, 0.001] }));
    expect(rec.length).toBe(6);
    rec.ingest(read({ start_ps: '90000000', since: 0, next: 1, samples: [0.002] }));
    expect(rec.length).toBe(1);
    expect(rec.toCsv()).toBe('Time [s],Current [A]\n0.000000,0.00200000\n');
  });
});
