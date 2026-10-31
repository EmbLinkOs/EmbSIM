// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import type { WorldBody } from '@embsim/protocol';
import { describe, expect, it } from 'vitest';
import { bodiesAt, bodyAt, rayDistance } from './world';

const cart: WorldBody = { id: 'cart', shape: 'box', at: [0, -300, 50], size: [100, 100, 100], velocity_mm_s: [0, 100, 0], travel_mm: 200 };
const s = (seconds: number) => BigInt(Math.round(seconds * 1e12));

describe('moving bodies (as core/engine/world.c moves them)', () => {
  it('goes along its velocity, then back and forth over its travel', () => {
    expect(bodyAt(cart, 0n)).toEqual([0, -300, 50]);
    expect(bodyAt(cart, s(1))[1]).toBeCloseTo(-200); // as tests/protocol/test_world.py checks the engine
    expect(bodyAt(cart, s(2))[1]).toBeCloseTo(-100); // the far end
    expect(bodyAt(cart, s(3))[1]).toBeCloseTo(-200); // on its way back
    expect(bodyAt(cart, s(4))[1]).toBeCloseTo(-300); // home again
    expect(bodyAt({ ...cart, travel_mm: 0 }, s(5))[1]).toBeCloseTo(200); // straight on
  });
  it('a still body stays put, and a sensor\'s ray sees a moving one where it is now', () => {
    const wall: WorldBody = { id: 'w', at: [0, -500, 50], size: [500, 10, 200] };
    expect(bodiesAt([wall], s(9))[0]).toBe(wall);
    const moved = bodiesAt([{ ...wall, velocity_mm_s: [0, 200, 0] }], s(1));
    expect(rayDistance(moved, [0, 0, 50], [0, -1, 0])).toBeCloseTo(295); // the face, 300 mm minus half its depth
  });
});
