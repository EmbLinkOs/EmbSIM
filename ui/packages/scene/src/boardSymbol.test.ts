// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe, expect, it } from 'vitest';
import { boardRuntimeHeader, boardSymbol, portGroups } from './boardSymbol';
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


describe('board live headers', () => {
  it('preserves every connection anchor and binds controls to native member names', () => {
    const original=boardSymbol('Board',ports);
    const view=boardRuntimeHeader(original,[{component:'SW1',input:'pressed',selector:'#btn-boot'},{component:'D1',output:'color',selector:'#led'}]);
    expect(view.symbol.pins).toEqual(original.symbol.pins);
    expect(view.symbol.width).toBe(original.symbol.width);
    expect(view.symbol.hotspots?.[0]).toMatchObject({target:'SW1',input:'pressed',gesture:'hold'});
    expect(view.symbol.states?.map((s) => s.output)).toEqual(['D1.color','D1.luminance']);
    expect(view.svg).toContain('BOOT');
  });
});
