// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe, expect, it } from 'vitest';
import { findJunctions, findAirwires, magneticDrag, pinPosition, routeOrthogonal, snap, symbolBounds, symbolTransform } from './geometry';

const resistor = { svg: '', width: 60, height: 20, pins: { '1': { x: 0, y: 10, side: 'left' as const }, '2': { x: 60, y: 10, side: 'right' as const } } };

describe('geometry', () => {
  it('snaps to the 10-unit grid', () => {
    expect(snap(14)).toBe(10);
    expect(snap(15)).toBe(20);
    expect(snap(-4)).toBe(-0);
  });

  it('places pins with rotation about the symbol centre', () => {
    expect(pinPosition({ x: 100, y: 100 }, resistor, '1')).toEqual({ x: 100, y: 110 });
    expect(pinPosition({ x: 100, y: 100, rotation: 90 }, resistor, '1')).toEqual({ x: 130, y: 80 });
    expect(pinPosition({ x: 100, y: 100, rotation: 90 }, resistor, '2')).toEqual({ x: 130, y: 140 });
    expect(pinPosition({ x: 100, y: 100, rotation: 180 }, resistor, '1')).toEqual({ x: 160, y: 110 });
    expect(pinPosition({ x: 100, y: 100, mirror: true }, resistor, '1')).toEqual({ x: 160, y: 110 });
    expect(pinPosition({ x: 0, y: 0 }, resistor, 'nope')).toBeNull();
  });

  it('keeps rotated pins on the grid for the demo project symbols', () => {
    for (const r of [0, 90, 180, 270]) {
      for (const pin of ['1', '2']) {
        const p = pinPosition({ x: 270, y: 60, rotation: r }, resistor, pin)!;
        expect(p.x % 10).toBe(0);
        expect(p.y % 10).toBe(0);
      }
    }
  });

  it('computes rotated bounds and transforms', () => {
    const b = symbolBounds({ x: 100, y: 100, rotation: 90 }, resistor);
    expect(b).toEqual({ x: 120, y: 80, width: 20, height: 60 });
    expect(symbolTransform({ x: 1, y: 2, rotation: 90 }, resistor)).toBe('translate(1 2) rotate(90 30 10)');
  });

  it('routes orthogonally', () => {
    expect(routeOrthogonal({ x: 0, y: 0 }, { x: 50, y: 0 })).toEqual([[0, 0], [50, 0]]);
    expect(routeOrthogonal({ x: 0, y: 0 }, { x: 50, y: 30 })).toEqual([[0, 0], [50, 0], [50, 30]]);
    expect(routeOrthogonal({ x: 0, y: 0 }, { x: 50, y: 30 }, false)).toEqual([[0, 0], [0, 30], [50, 30]]);
    expect(routeOrthogonal({ x: 3, y: 4 }, { x: 47, y: 4 })).toEqual([[0, 0], [50, 0]]);
  });

  it('finds T junctions and three-way meets', () => {
    const wires = [
      { net: 'A', points: [[0, 0], [100, 0]] as [number, number][] },
      { net: 'A', points: [[50, 0], [50, 40]] as [number, number][] },
      { net: 'B', points: [[0, 100], [100, 100]] as [number, number][] },
      { net: 'B', points: [[100, 100], [100, 150]] as [number, number][] },
      { net: 'B', points: [[100, 100], [150, 100]] as [number, number][] },
    ];
    const j = findJunctions(wires);
    expect(j).toEqual([
      { net: 'A', point: { x: 50, y: 0 } },
      { net: 'B', point: { x: 100, y: 100 } },
    ]);
    // A pin touching the interior of a wire is a junction too.
    expect(findJunctions([wires[0]!], { A: [{ x: 20, y: 0 }] })).toEqual([{ net: 'A', point: { x: 20, y: 0 } }]);
    // A plain corner is not.
    expect(findJunctions([{ net: 'C', points: [[0, 0], [10, 0], [10, 10]] }])).toEqual([]);
  });
});


describe('magnetic drag alignment', () => {
  it('snaps edges and centers and reports visible guides', () => {
    const result = magneticDrag([{ x: 0, y: 0, width: 60, height: 40 }], [{ x: 100, y: 100, width: 60, height: 40 }], { x: 90, y: 90 }, 12);
    expect(result.delta).toEqual({ x: 100, y: 100 });
    expect(result.x).toBe(100); expect(result.y).toBe(100);
  });
  it('does not pull distant parts or break grid snapping', () => {
    const result = magneticDrag([{ x: 0, y: 0, width: 60, height: 40 }], [{ x: 1000, y: 1000, width: 60, height: 40 }], { x: 13, y: 24 });
    expect(result).toEqual({ delta: { x: 10, y: 20 } });
  });
});


describe('unrouted visual connectivity', () => {
  it('finds a missing connection even when every pin touches a wire', () => {
    const pins = [{ x: 0, y: 0 }, { x: 100, y: 0 }, { x: 300, y: 0 }, { x: 400, y: 0 }];
    expect(findAirwires(pins, [{ net: 'N', points: [[0, 0], [100, 0]] }, { net: 'N', points: [[300, 0], [400, 0]] }])).toEqual([{ a: pins[1], b: pins[2] }]);
  });
  it('shows a minimal tree for undrawn pins and no line for connected branches', () => {
    const pins = [{ x: 0, y: 0 }, { x: 100, y: 0 }, { x: 100, y: 100 }];
    expect(findAirwires(pins, [])).toHaveLength(2);
    expect(findAirwires(pins, [{ net: 'N', points: [[0, 0], [100, 0]] }, { net: 'N', points: [[100, 0], [100, 100]] }])).toEqual([]);
  });
});
