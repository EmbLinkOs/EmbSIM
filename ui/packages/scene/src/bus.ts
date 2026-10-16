// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// A bus's value from its nets' states: the first net is bit 0. An analog
// net counts as 1 above half the range; a net in Z, X or unknown makes the
// value unknown.

import type { NetState } from '@embsim/protocol';

export function busValue(nets: string[], states: Record<string, NetState> | undefined, range: [number, number] = [0, 3.3]): number | null {
  let v = 0;
  for (let i = nets.length - 1; i >= 0; i--) {
    const s = states?.[nets[i]!];
    let bit: number;
    if (s?.logic === '1') bit = 1;
    else if (s?.logic === '0') bit = 0;
    else if (s?.logic === undefined && typeof s?.voltage === 'number') bit = s.voltage > (range[0] + range[1]) / 2 ? 1 : 0;
    else return null;
    v = v * 2 + bit;
  }
  return v;
}

/** "0x0A5" with as many hex digits as the bus has nibbles. */
export function busText(value: number | null, width: number): string {
  if (value === null) return '?';
  return `0x${value.toString(16).toUpperCase().padStart(Math.max(1, Math.ceil(width / 4)), '0')}`;
}
