// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// Where the sun is, for the room's light (docs/engineering/bench-3d.md,
// stage 6). The position is the usual solar-geometry approximation (Cooper's
// declination, the hour angle from local solar time), good to about a degree,
// which is all a shadow on a bench shows. Whether the sun lights the bench
// directly is geometry: the ray from the table's centre towards the sun must
// leave the room through the window, not a wall. Presentation only; the
// world's illuminance stays what the light sensors read.

/** the sun in the sky: elevation above the horizon and azimuth clockwise from north, degrees */
export interface SolarPosition {
  elevation: number;
  azimuth: number;
}

const RAD = Math.PI / 180;

/** the day of the year (1 to 365) of a "MM-DD" date; 172 (21 June) when it does not parse */
export function dayOfYear(date: string | undefined): number {
  const m = /^(\d{2})-(\d{2})$/.exec(date ?? '');
  if (!m) return 172;
  const month = Number(m[1]);
  const day = Number(m[2]);
  if (month < 1 || month > 12 || day < 1 || day > 31) return 172;
  const starts = [0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334];
  return starts[month - 1]! + day;
}

/** the sun's position at a latitude, on a day of the year, at a local solar time (hours, 12 = noon) */
export function solarPosition(latitudeDeg: number, day: number, solarHour: number): SolarPosition {
  const decl = 23.44 * Math.sin((360 / 365) * (284 + day) * RAD) * RAD;
  const hour = 15 * (solarHour - 12) * RAD;
  const lat = latitudeDeg * RAD;
  const sinEl = Math.sin(lat) * Math.sin(decl) + Math.cos(lat) * Math.cos(decl) * Math.cos(hour);
  const elevation = Math.asin(Math.max(-1, Math.min(1, sinEl))) / RAD;
  // clockwise from north: east in the morning, west in the afternoon
  const az = Math.atan2(-Math.sin(hour), Math.tan(decl) * Math.cos(lat) - Math.sin(lat) * Math.cos(hour)) / RAD;
  return { elevation, azimuth: (az + 360) % 360 };
}

/**
 * The direction towards the sun in the bench's frame (x right, y up, z
 * towards the person), for a room whose window wall (the left, -x) looks
 * out towards `windowAzimuth` (degrees clockwise from north).
 */
export function sunDirection(sun: SolarPosition, windowAzimuth: number): [number, number, number] {
  const b = (sun.azimuth - windowAzimuth) * RAD;
  const e = sun.elevation * RAD;
  // seen from above, clockwise from -x: towards -z (the back wall), then +x
  return [-Math.cos(b) * Math.cos(e), Math.sin(e), -Math.sin(b) * Math.cos(e)];
}

/** a window in the left wall: the wall's x, the opening's z centre and width, its bottom and top above the bench's surface (mm) */
export interface WindowOpening {
  wallX: number;
  z: number;
  width: number;
  bottom: number;
  top: number;
}

/** whether sunlight reaches the point `at` (bench mm, y up) straight through the window */
export function sunlit(dir: [number, number, number], at: [number, number, number], w: WindowOpening): boolean {
  if (dir[1] <= 0 || dir[0] >= 0) return false; // below the horizon, or on the far side of the window wall
  const t = (w.wallX - at[0]) / dir[0];
  const y = at[1] + dir[1] * t;
  const z = at[2] + dir[2] * t;
  return y >= w.bottom && y <= w.top && Math.abs(z - w.z) <= w.width / 2;
}

/** daylight's colour by the sun's height: warm near the horizon, near white high up (linear RGB) */
export function sunColor(elevation: number): [number, number, number] {
  const t = Math.max(0, Math.min(1, elevation / 30));
  return [1, 0.72 + 0.24 * t, 0.5 + 0.4 * t];
}
