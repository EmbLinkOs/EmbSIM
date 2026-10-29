// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe, expect, it } from 'vitest';
import { netReading } from './SchematicCanvas';

describe('the reading a net label carries', () => {
  it('is the voltage when the net has one, else its level', () => {
    expect(netReading({ logic: '1', voltage: 3.3 })).toBe('3.300 V');
    expect(netReading({ logic: '0' })).toBe('0');
    expect(netReading({ logic: 'Z' })).toBe('Z');
    expect(netReading({ contention: true, logic: 'X' })).toBe('X');
    expect(netReading(undefined)).toBe('');
  });
});
