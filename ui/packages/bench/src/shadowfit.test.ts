// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe, expect, it } from 'vitest';
import { ROOM } from './room';
import { fitShadow } from './shadowfit';

describe('the shadow map framed on what is seen', () => {
  it('stays tight on the bench close in, as it was', () => {
    expect(fitShadow(150, [20, 0, 10], false, ROOM)).toMatchObject({ half: 400, centre: [0, 0, 0], reach: 520 });
    expect(fitShadow(150, [20, 0, 10], true, ROOM).half).toBe(1000); // the sun's frame covers the table
  });
  it('takes in the floor round the table standing back, centred on what is seen', () => {
    const f = fitShadow(2400, [300, 0, -200], false, ROOM);
    expect(f.half).toBeGreaterThanOrEqual(2400);
    expect(f.centre).toEqual([300, 0, -200]);
    // a person walking round the table, half a metre clear of it, is inside the frame
    expect(f.half).toBeGreaterThan(1400 + 300);
    // the lamp's light stays under the ceiling (the room is 2.7 m, the table 0.75 m)
    expect(f.reach).toBeLessThan(ROOM.height - 750);
  });
  it('steps rather than slides, and never beyond the room', () => {
    const a = fitShadow(900, [0, 0, 0], false, ROOM).half;
    expect(fitShadow(910, [0, 0, 0], false, ROOM).half).toBe(a);
    const most = fitShadow(1e6, [9000, 0, 9000], false, ROOM);
    expect(most.half).toBeLessThanOrEqual(Math.hypot(ROOM.width, ROOM.depth) / 2 + 200);
    expect(most.centre).toEqual([ROOM.width / 2, 0, ROOM.depth / 2]);
  });
});
