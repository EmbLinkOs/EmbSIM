// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import type { WorldBody } from '@embsim/protocol';
import { describe, expect, it } from 'vitest';
import { bodiesAt, bodyAt, fall, rayDistance, walk } from './world';

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
  it('walks its path at its speed: round the loop, or there and back', () => {
    const square: [number, number, number][] = [[0, 0, 0], [1000, 0, 0], [1000, 1000, 0], [0, 1000, 0]];
    const walker: WorldBody = { id: 'walker', at: [0, 0, 0], size: [100, 100, 100], path_mm: square, speed_mm_s: 1000, loop: true };
    // the numbers tests/protocol/test_world.py checks the engine with
    const a = bodyAt(walker, s(1.5));
    expect(a[0]).toBeCloseTo(1000);
    expect(a[1]).toBeCloseTo(500);
    const home = bodyAt(walker, s(4));
    expect(home[0]).toBeCloseTo(0);
    expect(home[1]).toBeCloseTo(0);
    const there = { ...walker, path_mm: square.slice(0, 3), loop: false };
    const b = bodyAt(there, s(2.5));
    expect(b[0]).toBeCloseTo(1000);
    expect(b[1]).toBeCloseTo(500);
    expect(walk(there, 2500).dir[1]).toBeCloseTo(-1); // on its way back
    // drawn, it carries its heading and the distance walked
    const drawn = bodiesAt([walker], s(1.5))[0] as WorldBody & { walked_mm?: number };
    expect(drawn.velocity_mm_s![1]).toBeCloseTo(1000);
    expect(drawn.walked_mm).toBeCloseTo(1500);
  });
  it('a body let go falls, bounces and rests, as the engine has it', () => {
    // the numbers tests/protocol/test_world.py checks the engine with
    const ball: WorldBody = { id: 'ball', shape: 'sphere', at: [0, 0, 1000], size: [60, 60, 60], falls: true, restitution: 0.5 };
    expect(bodyAt(ball, s(0.3))[2]).toBeCloseTo(1000 - (9806.65 * 0.09) / 2, 6);
    const t1 = Math.sqrt((2 * 970) / 9806.65);
    expect(fall(ball, t1 + t1 / 2).at[2]).toBeCloseTo(30 + 970 / 4, 3); // up a quarter of the way
    expect(fall(ball, 3)).toEqual({ at: [0, 0, 30], resting: true }); // on the table
    expect(fall({ ...ball, at: [1200, 0, 0] }, 3).at[2]).toBeCloseTo(-720, 9); // beside it, the floor
    const thrown = fall({ ...ball, at: [800, 0, 200], velocity_mm_s: [1000, 0, 0] }, 3);
    expect(thrown.resting).toBe(true);
    expect(thrown.at[2]).toBeCloseTo(-720, 9); // off the table's edge
    expect(thrown.at[0]).toBeGreaterThan(900);
    expect(fall({ ...ball, restitution: 0 }, t1 + 0.01)).toEqual({ at: [0, 0, 30], resting: true }); // a dead drop
    expect(bodiesAt([ball], s(3))[0]).toMatchObject({ at: [0, 0, 30], resting: true });
  });
});
