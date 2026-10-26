// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe, expect, it } from 'vitest';
import { emptyEmission, measureEmission } from './schematicEmission';

describe('schematic exposure from native accepted-time integrals', () => {
  it('uses elapsed virtual time and preserves an average through a same-time pause', () => {
    const start = measureEmission(emptyEmission(), 1000000000n, { 'D.emission_integral': .00025 });
    const frame = measureEmission(start, 5000000000n, { 'D.emission_integral': .00125 });
    expect(frame.means['D.luminance']).toBeCloseTo(.25);
    expect(measureEmission(frame, frame.at, { 'D.emission_integral': .00125 }).means).toEqual(frame.means);
    expect(measureEmission(frame, 0n, { 'D.emission_integral': 0 }).means).toEqual({});
  });
  it('does not turn cached/partial integral notifications into zero exposure', () => {
    const start = measureEmission(emptyEmission(), 0n, { 'A.emission_integral': 0, 'B.emission_integral': 0 });
    const frame = measureEmission(start, 1000000000n, { 'A.emission_integral': .0005, 'B.emission_integral': .00025 });
    expect(measureEmission(frame, 2000000000n, {})).toBe(frame);
    expect(measureEmission(frame, 2000000000n, { 'A.emission_integral': .001 })).toBe(frame);
    const next = measureEmission(frame, 3000000000n, { 'A.emission_integral': .0015, 'B.emission_integral': .00075 });
    expect(next.means['A.luminance']).toBeCloseTo(.5);
    expect(next.means['B.luminance']).toBeCloseTo(.25);
    expect(measureEmission(next, next.at, { 'A.emission_integral': 0, 'B.emission_integral': 0 }).means).toEqual({});
  });
  it('mixes independent RGB channel exposures without changing electrical outputs', () => {
    const frame = measureEmission({ ...emptyEmission(), totals: { 'D.red_emission_integral': 0, 'D.green_emission_integral': 0, 'D.blue_emission_integral': 0 } }, 1000000000n, { 'D.red_emission_integral': .00025, 'D.green_emission_integral': .0005, 'D.blue_emission_integral': .001 });
    expect(frame.means['D.color']).toBe('#4080ff');
    expect(frame.means['D.luminance']).toBe(1);
  });
  it('does not invent means when an integral is missing, reset or nonfinite', () => {
    const start = { ...emptyEmission(), at: 1n, totals: { 'D.emission_integral': 1 } };
    expect(measureEmission(start, 20n, { 'D.emission_integral': .5 }).means).toEqual({});
    expect(measureEmission(start, 20n, { 'D.emission_integral': NaN }).means).toEqual({});
  });
});
