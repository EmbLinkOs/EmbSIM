// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe, expect, it } from 'vitest';
import { busText, busValue } from './bus';

describe('bus value', () => {
  it('reads the first net as bit 0', () => {
    const states = { D0: { logic: '1' as const }, D1: { logic: '0' as const }, D2: { logic: '1' as const }, D3: { voltage: 3.1 } };
    expect(busValue(['D0', 'D1', 'D2', 'D3'], states)).toBe(0b1101);
    expect(busText(0b1101, 4)).toBe('0xD');
    expect(busText(5, 9)).toBe('0x005');
  });

  it('is unknown when a net is Z or missing', () => {
    expect(busValue(['A', 'B'], { A: { logic: '1' as const }, B: { logic: 'Z' as const } })).toBeNull();
    expect(busValue(['A'], {})).toBeNull();
    expect(busText(null, 8)).toBe('?');
  });
});
