// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe, expect, it } from 'vitest';
import { formatPsRaw, formatTime, parseDuration, parsePs, pickUnit, scaleTime, toPs } from './time';

describe('time helpers', () => {
  it('parses picosecond strings beyond 2^53 without loss', () => {
    const big = '123456789012345678901';
    expect(toPs(parsePs(big))).toBe(big);
    expect(parsePs(big) > BigInt(Number.MAX_SAFE_INTEGER)).toBe(true);
  });

  it('refuses non-integer picosecond strings', () => {
    expect(() => parsePs('12.5')).toThrow();
    expect(() => parsePs('10ms')).toThrow();
    expect(() => parsePs(1.5)).toThrow();
  });

  it('parses unit durations exactly', () => {
    expect(parseDuration('10ms')).toBe(10_000_000_000n);
    expect(parseDuration('1.5us')).toBe(1_500_000n);
    expect(parseDuration('250')).toBe(250n);
    expect(parseDuration('0.000000000001s')).toBe(1n);
    expect(parseDuration('2 ns')).toBe(2000n);
    expect(parseDuration('3µs')).toBe(3_000_000n);
  });

  it('refuses sub-picosecond fractions', () => {
    expect(() => parseDuration('0.5ps')).toThrow();
    expect(() => parseDuration('1.0000000000001s')).toThrow();
    expect(parseDuration('1.0000ns')).toBe(1000n);
  });

  it('formats with the largest fitting unit', () => {
    expect(formatTime(12_345_000_000n)).toBe('12.345 ms');
    expect(formatTime(0n)).toBe('0.000 ps');
    expect(formatTime(999n)).toBe('999.000 ps');
    expect(formatTime(1_000n)).toBe('1.000 ns');
    expect(formatTime(1_500_000n)).toBe('1.500 µs');
    expect(formatTime(2_000_000_000_000n)).toBe('2.000 s');
    expect(formatTime(123_456_789_012_345_678_901n)).toBe('123456789.012 s');
    expect(pickUnit(999_999_999n)).toBe('us');
  });

  it('truncates rather than rounds', () => {
    expect(formatTime(1_999_999n, { unit: 'ms' })).toBe('0.001 ms');
    expect(formatTime(1_999_999n, { unit: 'us', decimals: 6 })).toBe('1.999999 µs');
    expect(formatTime(1_999_999n, { unit: 'us', padZeros: false })).toBe('1.999 µs');
  });

  it('formats raw picoseconds with groups', () => {
    expect(formatPsRaw(12_345_000_000n)).toBe('12 345 000 000 ps');
    expect(formatPsRaw(-5n)).toBe('-5 ps');
  });

  it('scales by float factors', () => {
    expect(scaleTime(1_000_000n, 0.5)).toBe(500_000n);
    expect(scaleTime(10n ** 20n, 2)).toBe(2n * 10n ** 20n);
  });
});
