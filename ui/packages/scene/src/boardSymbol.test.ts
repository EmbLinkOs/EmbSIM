// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe, expect, it } from 'vitest';
import { boardSymbol, portGroups } from './boardSymbol';
import { GRID } from './geometry';

const ports = {
  B2: { connector: 'J1', pin: 2 },
  A1: { connector: 'J1', pin: 1 },
  X1: { connector: 'J2', pin: 1 },
  SWD: { connector: 'J3', pin: 1 },
  GND: { connector: 'J3', pin: 2 },
};

describe('boardSymbol', () => {
  it('groups ports by connector in pin order', () => {
    expect(portGroups(ports)).toEqual([['A1', 'B2'], ['X1'], ['SWD', 'GND']]);
  });

  it('puts the first connector left, the second right, the rest along the bottom, on the grid', () => {
    const { symbol, svg } = boardSymbol('Board <1>', ports);
    expect(symbol.pins.A1).toMatchObject({ x: 0, side: 'left' });
    expect(symbol.pins.B2!.y - symbol.pins.A1!.y).toBe(2 * GRID);
    expect(symbol.pins.X1).toMatchObject({ x: symbol.width, side: 'right' });
    expect(symbol.pins.SWD).toMatchObject({ y: symbol.height, side: 'bottom' });
    for (const p of Object.values(symbol.pins)) {
      expect(p.x % GRID).toBe(0);
      expect(p.y % GRID).toBe(0);
    }
    expect(svg).toContain('Board &lt;1&gt;');
  });
});
