// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe, expect, it } from 'vitest';
import { sweepCsv, sweepValues } from './sweeps';
import type { ParameterSweep } from './types';

describe('native sweep preparation and exports', () => {
  it('includes exact endpoints for ascending/descending linear and logarithmic runs', () => {
    expect(sweepValues('1k', '3k', 3, false)).toEqual([1000, 2000, 3000]);
    const descending = sweepValues('1meg', '1k', 4, true);
    expect(descending[0]).toBe(1e6); expect(descending[3]).toBe(1000);
    expect(descending[1]).toBeCloseTo(100000); expect(descending[2]).toBeCloseTo(10000);
    expect(sweepValues('-1', '1', 3, false)).toEqual([-1, 0, 1]);
  });
  it('refuses invalid bounds, oversized requests and logarithms through zero', () => {
    for (const count of [0, 1, 65, 3.5, NaN]) expect(() => sweepValues('1', '2', count, false)).toThrow();
    expect(() => sweepValues('0', '10', 5, true)).toThrow(/positive/);
    expect(() => sweepValues('1e999', '10', 5, false)).toThrow();
    expect(() => sweepValues('1e308T', '10', 5, false)).toThrow();
  });
  it('keeps absent voltages blank and retains failure and logic evidence in CSV', () => {
    const result = {request: {probes: ['net:OUT']}, points: [
      {requested: '1k', resolved: 1000, at_ps: '1000000', ok: true, diagnostics: [], measurements: [{voltage: 0, logic: 'Z'}]},
      {requested: -1, resolved: null, at_ps: '0', ok: false, diagnostics: [{code: 'range', message: 'bad, "value"'}], measurements: [{voltage: null, logic: 'X'}]},
    ]} as ParameterSweep;
    const csv = sweepCsv(result);
    expect(csv).toContain('"0","Z"'); expect(csv).toContain('"failed","","X"'); expect(csv).toContain('bad, ""value""');
  });
});
