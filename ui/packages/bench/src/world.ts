// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The world's bodies on the bench and the beams distance sensors cast at
// them (ADR-026), computed as the engine computes them (core/engine/world.c):
// a beam leaves the part's bench.sense.at, turned with the part, its height
// measured from the desk, and stops at the nearest body's face.

import type { WorldBody } from '@embsim/protocol';
import type { PlacedPart } from './layout';
import type { Point3 } from './Bench3D';

/**
 * Where a body is at virtual time `nowPs` (picoseconds) when it was set at
 * `sincePs`, as the engine moves it (core/engine/world.c): along its velocity,
 * and back and forth over `travel_mm` when it has one.
 */
export function bodyAt(b: WorldBody, nowPs: bigint, sincePs = 0n): [number, number, number] {
  const dtPath = nowPs > sincePs ? Number(nowPs - sincePs) / 1e12 : 0;
  if (b.path_mm && b.path_mm.length >= 2 && (b.speed_mm_s ?? 0) > 0) return walk(b, (b.speed_mm_s ?? 0) * dtPath).at;
  const v = b.velocity_mm_s;
  const speed = v ? Math.hypot(v[0], v[1], v[2]) : 0;
  const dt = nowPs > sincePs ? Number(nowPs - sincePs) / 1e12 : 0;
  if (!v || speed <= 0 || dt <= 0) return [b.at[0], b.at[1], b.at[2]];
  let s = speed * dt;
  const travel = b.travel_mm ?? 0;
  if (travel > 0) {
    s %= 2 * travel;
    if (s > travel) s = 2 * travel - s;
  }
  return [b.at[0] + (v[0] / speed) * s, b.at[1] + (v[1] / speed) * s, b.at[2] + (v[2] / speed) * s];
}

/**
 * Where a body walking its path is after `s` mm, and which way it is going
 * (as core/engine/world.c walks it): looped, round again; else there and back.
 */
export function walk(b: WorldBody, s: number): { at: [number, number, number]; dir: [number, number, number] } {
  const p = b.path_mm!;
  const loop = b.loop === true;
  const segs = loop ? p.length : p.length - 1;
  const len = (i: number) => Math.hypot(...([0, 1, 2].map((k) => p[(i + 1) % p.length]![k]! - p[i]![k]!) as [number, number, number]));
  let total = 0;
  for (let i = 0; i < segs; i++) total += len(i);
  if (total <= 0) return { at: [...p[0]!] as [number, number, number], dir: [0, 1, 0] };
  let back = false;
  if (loop) s %= total;
  else {
    s %= 2 * total;
    if (s > total) {
      s = 2 * total - s;
      back = true;
    }
  }
  for (let i = 0; i < segs; i++) {
    const l = len(i);
    if (s <= l || i === segs - 1) {
      const a = p[i]!;
      const c = p[(i + 1) % p.length]!;
      const t = l > 0 ? Math.min(1, s / l) : 0;
      const d = l > 0 ? ([0, 1, 2].map((k) => ((c[k]! - a[k]!) / l) * (back ? -1 : 1)) as [number, number, number]) : ([0, 1, 0] as [number, number, number]);
      return { at: [0, 1, 2].map((k) => a[k]! + (c[k]! - a[k]!) * t) as [number, number, number], dir: d };
    }
    s -= l;
  }
  return { at: [...p[0]!] as [number, number, number], dir: [0, 1, 0] };
}

/** whether a body moves (a velocity, or a path walked at a speed) */
export function moves(b: WorldBody): boolean {
  return Boolean(b.velocity_mm_s) || Boolean(b.path_mm && b.path_mm.length >= 2 && (b.speed_mm_s ?? 0) > 0);
}

/** the bodies where they are at `nowPs` (the project's are set at time 0); a walker carries its heading and distance */
export function bodiesAt(bodies: WorldBody[], nowPs: bigint): WorldBody[] {
  return bodies.map((b) => {
    if (b.path_mm && moves(b)) {
      const s = (b.speed_mm_s ?? 0) * (Number(nowPs) / 1e12);
      const w = walk(b, s);
      // its heading as a velocity (for facing and the gait); the distance walked drives the stride
      return { ...b, at: w.at, velocity_mm_s: w.dir.map((x) => x * (b.speed_mm_s ?? 0)) as [number, number, number], walked_mm: s } as WorldBody;
    }
    return b.velocity_mm_s ? { ...b, at: bodyAt(b, nowPs) } : b;
  });
}

/** the distance (mm) along the unit vector d from o to the nearest body, or -1 */
export function rayDistance(bodies: WorldBody[], o: Point3, d: Point3): number {
  let best = -1;
  for (const b of bodies) {
    const t = b.shape === 'sphere' ? raySphere(b, o, d) : rayBox(b, o, d);
    if (t >= 0 && (best < 0 || t < best)) best = t;
  }
  return best;
}

function rayBox(b: WorldBody, o: Point3, d: Point3): number {
  let t0 = 0;
  let t1 = Infinity;
  for (let k = 0; k < 3; k++) {
    const lo = b.at[k]! - b.size[k]! / 2;
    const hi = b.at[k]! + b.size[k]! / 2;
    if (Math.abs(d[k]!) < 1e-12) {
      if (o[k]! < lo || o[k]! > hi) return -1;
    } else {
      let a = (lo - o[k]!) / d[k]!;
      let c = (hi - o[k]!) / d[k]!;
      if (a > c) [a, c] = [c, a];
      t0 = Math.max(t0, a);
      t1 = Math.min(t1, c);
      if (t0 > t1) return -1;
    }
  }
  return t0;
}

function raySphere(b: WorldBody, o: Point3, d: Point3): number {
  const r = b.size[0] / 2;
  const oc = [o[0] - b.at[0], o[1] - b.at[1], o[2] - b.at[2]];
  const bq = oc[0]! * d[0] + oc[1]! * d[1] + oc[2]! * d[2];
  const c = oc[0]! ** 2 + oc[1]! ** 2 + oc[2]! ** 2 - r * r;
  const disc = bq * bq - c;
  if (disc < 0) return -1;
  let t = -bq - Math.sqrt(disc);
  if (t < 0) t = -bq + Math.sqrt(disc);
  return t < 0 ? -1 : c <= 0 ? 0 : t;
}

export interface Beam {
  part: string;
  /** bench coordinates, mm (x, y, z up) */
  from: Point3;
  to: Point3;
  /** the distance to what it meets, mm; -1 when it meets nothing within `range` */
  distance: number;
}

/** the beam of each sensor on the bench that has one */
export function beams(parts: PlacedPart[], bodies: WorldBody[], range = 4000): Beam[] {
  const out: Beam[] = [];
  for (const p of parts) {
    const sense = p.shape.sense;
    if (!sense) continue;
    const at = sense.at ?? [0, 0, 10];
    const dir = sense.dir ?? [0, -1, 0];
    const r = (p.place.rotation * Math.PI) / 180;
    const c = Math.cos(r);
    const s = Math.sin(r);
    const o: Point3 = [p.place.x + at[0] * c - at[1] * s, p.place.y + at[0] * s + at[1] * c, at[2]];
    let d: Point3 = [dir[0] * c - dir[1] * s, dir[0] * s + dir[1] * c, dir[2]];
    const n = Math.hypot(...d) || 1;
    d = [d[0] / n, d[1] / n, d[2] / n];
    const t = rayDistance(bodies, o, d);
    const len = t >= 0 && t <= range ? t : range;
    out.push({ part: p.id, from: o, to: [o[0] + d[0] * len, o[1] + d[1] * len, o[2] + d[2] * len], distance: t >= 0 && t <= range ? t : -1 });
  }
  return out;
}
