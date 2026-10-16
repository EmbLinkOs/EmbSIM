// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe, expect, it } from 'vitest';
import { findAirwires } from './geometry';
import { cleanWirePoints, shiftWireSegment } from './wireEditing';

const track = { net: 'N', points: [[0, 0], [100, 0]] as [number, number][] };
describe('wire segment reshaping', () => {
  it('keeps both pin endpoints and snaps the perpendicular offset', () => {
    const wires = [structuredClone(track)];
    expect(shiftWireSegment(wires, 0, 0, 27)![0]!.points).toEqual([[0, 0], [0, 30], [100, 30], [100, 0]]);
    expect(wires).toEqual([track]);
  });
  it('retains branch, crossing and pin contacts of the same net', () => {
    const pins = [{ x: 0, y: 0 }, { x: 100, y: 0 }, { x: 20, y: 0 }, { x: 50, y: -50 }, { x: 70, y: 50 }];
    const branch = { net: 'N', points: [[50, 0], [50, -50]] as [number, number][] };
    const crossing = { net: 'N', points: [[70, -50], [70, 50]] as [number, number][] };
    const foreign = { net: 'OTHER', points: [[80, 0], [80, -50]] as [number, number][] };
    const result = shiftWireSegment([track, branch, crossing, foreign], 0, 0, 30, { N: pins })!;
    expect(result.slice(1, 4)).toEqual([branch, crossing, foreign]);
    expect(result.slice(4).map((w) => w.points[0])).toEqual(expect.arrayContaining([[20, 0], [50, 0], [70, 0]]));
    expect(result.slice(4).some((w) => w.points[0]![0] === 80)).toBe(false);
    expect(findAirwires(pins, result.filter((w) => w.net === 'N'))).toEqual([]);
  });
  it('supports vertical and interior segments, no-op and invalid requests', () => {
    const w = { net: 'N', points: [[0, 0], [0, 50], [50, 50], [50, 100]] as [number, number][] };
    const changed = shiftWireSegment([w], 0, 1, -20)![0]!;
    expect(changed.points[0]).toEqual([0, 0]);
    expect(changed.points.at(-1)).toEqual([50, 100]);
    expect(shiftWireSegment([track], 0, 0, 2)).toBeNull();
    expect(shiftWireSegment([track], 3, 0, 10)).toBeNull();
    expect(shiftWireSegment([{ net: 'N', points: [[0, 0], [10, 10]] }], 0, 0, 20)).toBeNull();
    expect(shiftWireSegment([{ net: 'N', points: [[0, 0], [0, 50]] }], 0, 0, -20)![0]!.points).toEqual([[0, 0], [-20, 0], [-20, 50], [0, 50]]);
  });
  it('cleans redundant vertices without erasing intentional backtracking', () => {
    expect(cleanWirePoints([[0, 0], [0, 0], [10, 0], [20, 0], [10, 0]])).toEqual([[0, 0], [20, 0], [10, 0]]);
  });
});
