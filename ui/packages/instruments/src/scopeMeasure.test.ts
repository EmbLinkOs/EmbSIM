// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe, expect, it } from 'vitest';
import { measure } from './scopeMeasure';

describe("an oscilloscope's measurements", () => {
  it('reads a square wave: levels, period, frequency, duty', () => {
    // 0/3.3 V, 25 % duty, a period of 100 samples 1 us apart: 10 kHz
    const pts = Array.from({ length: 1000 }, (_, i) => (i % 100 < 25 ? 3.3 : 0));
    const m = measure(pts, 1e-6);
    expect(m.max).toBe(3.3);
    expect(m.min).toBe(0);
    expect(m.vpp).toBeCloseTo(3.3, 9);
    expect(m.avg).toBeCloseTo(0.825, 9);
    expect(m.rms).toBeCloseTo(3.3 * Math.sqrt(0.25), 9);
    expect(m.period).toBeCloseTo(100e-6, 9);
    expect(m.freq).toBeCloseTo(10e3, 3);
    expect(m.duty).toBeCloseTo(0.25, 2);
  });
  it('reads an RC rise: 10 % to 90 % is 2.2 time constants', () => {
    const tau = 100; // samples
    const pts = Array.from({ length: 1200 }, (_, i) => 3.3 * (1 - Math.exp(-i / tau)));
    expect(measure(pts, 1e-6).rise).toBeCloseTo(2.197e-4, 5);
  });
  it('a flat line has no period', () => {
    const m = measure([1, 1, 1, 1], 1e-6);
    expect(m.vpp).toBe(0);
    expect(Number.isNaN(m.freq)).toBe(true);
  });
});
