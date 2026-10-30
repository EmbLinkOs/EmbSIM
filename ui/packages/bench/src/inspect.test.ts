// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe, expect, it } from 'vitest';
import { aboveHeight, describeMeasurement, layerOf, measure, mm, nearestView, sectionPlane, snapPoint, viewPose, type Bounds, type Vec3 } from './inspect';

const box: Bounds = { min: [-50, 0, -30], max: [50, 20, 30] };

describe('standard views', () => {
  it('the top view looks straight down at the centre, the desk\'s far side up', () => {
    const v = viewPose(box, 'top', 1.5, 38);
    expect(v.target).toEqual([0, 10, 0]);
    expect(v.position[0]).toBeCloseTo(0);
    expect(v.position[2]).toBeCloseTo(0);
    expect(v.position[1]).toBeGreaterThan(20);
    expect(v.up).toEqual([0, 0, -1].map((x) => x + 0) as Vec3);
  });

  it('every view frames the box: its corners fit in the frustum', () => {
    for (const view of ['top', 'bottom', 'front', 'back', 'left', 'right', 'iso', 'perspective'] as const) {
      const aspect = 1.6;
      const v = viewPose(box, view, aspect, 38);
      const half = Math.tan((38 * Math.PI) / 360);
      const d = v.position.map((p, i) => (p - v.target[i]!) / v.distance) as Vec3;
      const up = v.up;
      const right: Vec3 = [up[1] * d[2] - up[2] * d[1], up[2] * d[0] - up[0] * d[2], up[0] * d[1] - up[1] * d[0]];
      for (const x of [box.min[0], box.max[0]]) for (const y of [box.min[1], box.max[1]]) for (const z of [box.min[2], box.max[2]]) {
        const r: Vec3 = [x - v.position[0], y - v.position[1], z - v.position[2]];
        const depth = -(r[0] * d[0] + r[1] * d[1] + r[2] * d[2]);
        expect(depth).toBeGreaterThan(0);
        const sx = Math.abs(r[0] * right[0] + r[1] * right[1] + r[2] * right[2]) / depth;
        const sy = Math.abs(r[0] * up[0] + r[1] * up[1] + r[2] * up[2]) / depth;
        expect(sy).toBeLessThanOrEqual(half + 1e-9);
        expect(sx).toBeLessThanOrEqual(half * aspect + 1e-9);
      }
      expect(v.halfHeight).toBeGreaterThan(0);
    }
  });

  it('the front view is level, from the person\'s side of the desk', () => {
    const v = viewPose(box, 'front', 1, 38);
    expect(v.position[1]).toBeCloseTo(10);
    expect(v.position[2]).toBeGreaterThan(30);
    expect(nearestView(v.position, v.target)).toBe('front');
  });

  it('an orbited camera is no named view', () => {
    expect(nearestView([10, 100, 40], [0, 0, 0])).toBe('perspective');
    expect(nearestView([0, 100, 0.0001], [0, 0, 0])).toBe('top');
  });
});

describe('measurement', () => {
  it('reports the bench\'s axes: x right, y depth, z height', () => {
    const m = measure([0, 5, 0], [3, 9, -4]);
    expect(m.dx).toBe(3);
    expect(m.dy).toBe(-4);
    expect(m.dz).toBe(4);
    expect(m.distance).toBeCloseTo(Math.sqrt(41));
  });

  it('names the pitch when the distance is on the 0.1 in grid', () => {
    expect(describeMeasurement(measure([0, 0, 0], [10.16, 0, 0]))).toBe('10.16 mm (4 × 2.54 mm) · Δx 10.16 mm · Δy 0 mm · Δz 0 mm');
    expect(describeMeasurement(measure([0, 0, 0], [3, 0, 4]))).toBe('5 mm · Δx 3 mm · Δy 4 mm · Δz 0 mm');
  });

  it('rounds to a caliper\'s 0.01 mm', () => {
    expect(mm(1.23456)).toBe('1.23 mm');
    expect(mm(-0.001)).toBe('0 mm');
  });

  it('snaps a click to the nearest pin within the radius', () => {
    const pins: Vec3[] = [[0, 0, 0], [2.54, 0, 0]];
    expect(snapPoint([2.2, 0.3, 0], pins, 1.5)).toEqual({ at: [2.54, 0, 0], snapped: true });
    expect(snapPoint([10, 0, 0], pins, 1.5)).toEqual({ at: [10, 0, 0], snapped: false });
  });
});

describe('section', () => {
  const keeps = (s: Parameters<typeof sectionPlane>[0], p: Vec3) => {
    const { normal, constant } = sectionPlane(s);
    return normal[0] * p[0] + normal[1] * p[1] + normal[2] * p[2] + constant >= 0;
  };
  it('keeps the near side of the cut, or the far side when flipped', () => {
    expect(keeps({ axis: 'x', at: 10 }, [5, 0, 0])).toBe(true);
    expect(keeps({ axis: 'x', at: 10 }, [15, 0, 0])).toBe(false);
    expect(keeps({ axis: 'x', at: 10, flip: true }, [15, 0, 0])).toBe(true);
  });
  it('cuts the bench\'s y (depth) along the scene\'s z and its z (height) along the scene\'s y', () => {
    expect(keeps({ axis: 'y', at: 0 }, [0, 100, -5])).toBe(true);
    expect(keeps({ axis: 'y', at: 0 }, [0, 100, 5])).toBe(false);
    expect(keeps({ axis: 'z', at: 8 }, [0, 5, 0])).toBe(true);
    expect(keeps({ axis: 'z', at: 8 }, [0, 9, 0])).toBe(false);
  });
});

describe('height plane', () => {
  it('lists the parts through the plane, tallest first', () => {
    const parts = [
      { id: 'R1', bounds: { min: [0, 0, 0], max: [1, 3, 1] } as Bounds },
      { id: 'C1', bounds: { min: [0, 0, 0], max: [1, 12.5, 1] } as Bounds },
      { id: 'U1', bounds: { min: [0, 0, 0], max: [1, 9, 1] } as Bounds },
    ];
    expect(aboveHeight(parts, 8)).toEqual([{ id: 'C1', top: 12.5 }, { id: 'U1', top: 9 }]);
    expect(aboveHeight(parts, 20)).toEqual([]);
  });
  it('sorts packages into the layers a user hides', () => {
    expect(layerOf('board')).toBe('boards');
    expect(layerOf('breadboard')).toBe('breadboards');
    expect(layerOf('dip')).toBe('parts');
  });
});
