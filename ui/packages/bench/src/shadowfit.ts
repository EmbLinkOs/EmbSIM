// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// Where the key light's shadow map looks. One map serves the bench and the
// room: framed on what the camera shows, it is tight on the bench when the
// camera is close (crisp shadows of a chip's legs) and takes in the floor
// when the camera stands back, so a person walking round the table casts
// their shadow on the floor as they would under the lamp or in the sun. The
// map's texels are then about as fine on the screen either way.

import type { RoomSize } from './room';

export interface ShadowFit {
  /** the frustum's half width and height, mm */
  half: number;
  /** where it is centred (the light's target), scene mm (y up, the table's top at 0) */
  centre: [number, number, number];
  /** how far the light stands back from its target along its direction, mm */
  reach: number;
  near: number;
  far: number;
}

/** the sizes the frame steps through, so it does not shimmer as the camera moves */
const STEP = 1.25;

/**
 * Frame the shadow map on `seen` (the radius in mm of what the camera shows
 * about `target`). From outside (the sun through the window) the light starts
 * beyond the room so the walls cast their shadows and the frame always covers
 * the table; from inside, the lamp or the window's daylight, it starts at the
 * room's height.
 */
export function fitShadow(seen: number, target: [number, number, number], outside: boolean, room: RoomSize): ShadowFit {
  const least = outside ? 1000 : 400;
  const most = Math.hypot(room.width, room.depth) / 2 + 200;
  let half = least;
  while (half < seen && half < most) half *= STEP;
  half = Math.min(half, Math.max(least, most));
  // close in, the frame is on the bench's centre (as it always was); standing back, on what is seen,
  // kept within the room (the floor is the scene's x-z plane)
  const wide = half > least;
  const centre: [number, number, number] = wide
    ? [clamp(target[0], room.width / 2), 0, clamp(target[2], room.depth / 2)]
    : [0, 0, 0];
  const diag = Math.hypot(room.width, room.depth, room.height);
  // inside, the light stays under the ceiling (which casts shadows) but above a standing person's head
  const reach = outside ? diag : wide ? 1500 : 520;
  return { half, centre, reach, near: outside ? 50 : 0.5, far: outside ? reach * 1.6 : Math.max(1500, reach + half * 2) };
}

function clamp(v: number, lim: number): number {
  return Math.max(-lim, Math.min(lim, v));
}
