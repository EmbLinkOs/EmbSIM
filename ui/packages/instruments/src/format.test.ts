// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe, expect, it } from 'vitest';
import { axisLabels, formatMeterValue } from './Meter';

describe('formatMeterValue', () => {
  it('picks the prefix and keeps four significant digits', () => {
    expect(formatMeterValue(-0.0005, 'A')).toBe('-500 µA');
    expect(formatMeterValue(0.002488, 'W')).toBe('2.488 mW');
    expect(formatMeterValue(5.263e-3, 'W')).toBe('5.263 mW');
    expect(formatMeterValue(4.474e-12, 'A')).toBe('4.474 pA');
    expect(formatMeterValue(1.2e-15, 'W')).toBe('1.2 fW');
    expect(formatMeterValue(12_000_000, 'Hz')).toBe('12 MHz');
  });

  it('shows residue below a femto-unit as about zero, and carries rounding', () => {
    expect(formatMeterValue(1.175e-19, 'W')).toBe('≈ 0 W');
    expect(formatMeterValue(0, 'V')).toBe('0 V');
    expect(formatMeterValue(0.99996, 'V')).toBe('1 V');
    expect(formatMeterValue(true)).toBe('ON');
  });
});

describe('axisLabels', () => {
  it('shares one prefix and takes its decimals from the step', () => {
    expect(axisLabels([0, 0.2, 0.4, 0.6, 0.8, 1], 0.2, 'V')).toEqual(['0.0 V', '0.2 V', '0.4 V', '0.6 V', '0.8 V', '1.0 V']);
    expect(axisLabels([0, 0.2, 0.4, 0.6, 0.8], 0.2, 'V')).toEqual(['0 mV', '200 mV', '400 mV', '600 mV', '800 mV']);
    expect(axisLabels([-0.005, 0, 0.005], 0.005, 'A')).toEqual(['-5 mA', '0 mA', '5 mA']);
    expect(axisLabels([0, 2.5, 5], 2.5, 'V')).toEqual(['0.0 V', '2.5 V', '5.0 V']);
  });
});
