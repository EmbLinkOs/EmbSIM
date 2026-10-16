// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe, expect, it } from 'vitest';
import { formatSi, parseSi } from './units';

describe('SI numbers', () => {
  it('parses suffixes', () => {
    expect(parseSi('4.7k')).toBeCloseTo(4700);
    expect(parseSi('100n')).toBeCloseTo(1e-7);
    expect(parseSi('1meg')).toBe(1e6);
    expect(parseSi('1G')).toBe(1e9);
    expect(parseSi('2.2u')).toBeCloseTo(2.2e-6);
    expect(parseSi(220)).toBe(220);
    expect(parseSi('abc')).toBeUndefined();
    expect(parseSi(undefined)).toBeUndefined();
  });
  it('formats with prefixes', () => {
    expect(formatSi(4700, 'Ω')).toBe('4.7 kΩ');
    expect(formatSi(0.0057, 'A')).toBe('5.7 mA');
    expect(formatSi(0)).toBe('0');
    expect(formatSi(1e-7, 'F')).toBe('100 nF');
  });
});
