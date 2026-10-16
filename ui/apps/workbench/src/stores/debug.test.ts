// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe, expect, it } from 'vitest';
import { hex32, parseAddress } from './debug';

describe('debug addresses', () => {
  it('reads hexadecimal and decimal addresses, with digit grouping', () => {
    expect(parseAddress('0x080002f4')).toEqual({ address: 0x080002f4 });
    expect(parseAddress(' 0x0800_02F4 ')).toEqual({ address: 0x080002f4 });
    expect(parseAddress('134218484')).toEqual({ address: 134218484 });
  });

  it('keeps a symbol as written, underscores included', () => {
    expect(parseAddress('EXTI0_IRQHandler')).toEqual({ symbol: 'EXTI0_IRQHandler' });
    expect(parseAddress('main')).toEqual({ symbol: 'main' });
    expect(parseAddress('   ')).toEqual({});
  });

  it('formats 32-bit values', () => {
    expect(hex32(0x414)).toBe('0x00000414');
    expect(hex32(-1)).toBe('0xffffffff');
  });
});
