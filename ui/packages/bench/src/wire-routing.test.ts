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
