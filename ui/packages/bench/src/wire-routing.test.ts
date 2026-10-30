// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { Vector3 } from 'three';
import { describe, expect, it } from 'vitest';
import { jumperCurve } from './Bench3D';

describe('anchored wire paths', () => {
  it('retains endpoint contacts and passes through explicit intermediate points', () => {
    const curve = jumperCurve([0, 8, 0], [50, 12, 20], [[10, 30, 0], [40, 30, 20]]);
    expect(curve.getPoint(0).toArray()).toEqual([0, 8, 0]);
    expect(curve.getPoint(1).toArray()).toEqual([50, 12, 20]);
    expect(curve.getLength()).toBeCloseTo(new Vector3(0, 8, 0).distanceTo(new Vector3(10, 30, 0)) + Math.hypot(30, 20) + Math.hypot(10, 18));
  });
  it('honors the requested rise without overshoot and tolerates repeated points', () => {
    const curve = jumperCurve([0, 8, 0], [50, 12, 20], undefined, 3);
    const points = curve.getPoints(100);
    expect(Math.max(...points.map((p) => p.y))).toBeCloseTo(15);
    const repeated = jumperCurve([0, 8, 0], [50, 12, 20], [[0, 8, 0], [0, 8, 0]]);
    expect(repeated.getLength()).toBeCloseTo(Math.hypot(50, 4, 20));
  });
});

describe('a long Dupont wire drapes', () => {
  it('falls from its ends and lies on the desk between them', () => {
    const curve = jumperCurve([0, 34, 0], [150, 10, 40]);
    curve.getPoint(0).toArray().forEach((v, i) => expect(v).toBeCloseTo([0, 34, 0][i]!, 6));
    curve.getPoint(1).toArray().forEach((v, i) => expect(v).toBeCloseTo([150, 10, 40][i]!, 6));
    const ys = curve.getPoints(200).map((p) => p.y);
    expect(Math.min(...ys)).toBeGreaterThan(0.5);
    expect(curve.getPoint(0.5).y).toBeLessThan(2);
    // never higher than its ends' risers
    expect(Math.max(...ys)).toBeLessThan(34 + 7);
  });
  it('rests on what is under it, never through it', () => {
    // a 12 mm board across the middle of the path
    const floor = (x: number) => (x > 50 && x < 100 ? 12 : 0);
    const curve = jumperCurve([0, 34, 0], [150, 10, 0], undefined, undefined, floor);
    expect(curve.getPoint(0.5).y).toBeGreaterThan(12.5);
  });
  it('a short span still arcs', () => {
    const curve = jumperCurve([0, 10, 0], [40, 10, 10]);
    expect(Math.min(...curve.getPoints(50).map((p) => p.y))).toBeGreaterThanOrEqual(10 - 1e-9);
  });
});
