// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// What the bench's inspection tools compute, as EDA 3D viewers have them
// (docs/engineering/bench-3d.md, stage 1): the standard views (KiCad's top,
// bottom, front, back, left, right, and an isometric), a measurement between
// two points (Altium's), a section plane across an axis (Altium's), and the
// parts rising through a height plane (Proteus's height check). Pure
// functions on plain numbers, in the scene's frame: millimetres, +y up, the
// desk's x to the right and its z towards the person at it. The bench's own
// coordinates name the desk's depth y and the height z; measurements are
// reported in those.

export type Vec3 = [number, number, number];

export type StandardView = 'top' | 'bottom' | 'front' | 'back' | 'left' | 'right' | 'iso' | 'perspective';

export interface Bounds {
  min: Vec3;
  max: Vec3;
}

export interface ViewPose {
  position: Vec3;
  target: Vec3;
  up: Vec3;
  /** the distance from the target, framing the bounds */
  distance: number;
  /** for an orthographic camera: half the frustum's height at zoom 1 */
  halfHeight: number;
}

/** each view's direction from the target towards the camera, and its up */
const DIRECTIONS: Record<StandardView, { dir: Vec3; up: Vec3 }> = {
  top: { dir: [0, 1, 0], up: [0, 0, -1] },
  bottom: { dir: [0, -1, 0], up: [0, 0, 1] },
  front: { dir: [0, 0, 1], up: [0, 1, 0] },
  back: { dir: [0, 0, -1], up: [0, 1, 0] },
  left: { dir: [-1, 0, 0], up: [0, 1, 0] },
  right: { dir: [1, 0, 0], up: [0, 1, 0] },
  // from the front right, 35.26 degrees above the desk: the cube's diagonal
  iso: { dir: [1 / Math.sqrt(3), 1 / Math.sqrt(3), 1 / Math.sqrt(3)], up: [0, 1, 0] },
  // as a person sees the bench from a chair at the desk
  perspective: { dir: [0, 0.754, 0.657], up: [0, 1, 0] },
};

function sub(a: Vec3, b: Vec3): Vec3 { return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]; }
function dot(a: Vec3, b: Vec3): number { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
function cross(a: Vec3, b: Vec3): Vec3 { return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]; }
function norm(a: Vec3): Vec3 { const l = Math.hypot(...a) || 1; return [a[0] / l, a[1] / l, a[2] / l]; }

/** the eight corners of a box */
function corners(b: Bounds): Vec3[] {
  const out: Vec3[] = [];
  for (const x of [b.min[0], b.max[0]]) for (const y of [b.min[1], b.max[1]]) for (const z of [b.min[2], b.max[2]]) out.push([x, y, z]);
  return out;
}

/**
 * Where the camera stands to see `bounds` from `view`, filling the frame
 * with a margin: the box's extent across the view (its corners projected on
 * the view's right and up) decides the distance for a perspective camera of
 * `fovDeg` and the frustum for an orthographic one.
 */
export function viewPose(bounds: Bounds, view: StandardView, aspect: number, fovDeg: number, margin = 1.08): ViewPose {
  const { dir, up } = DIRECTIONS[view];
  const d = norm(dir);
  const right = norm(cross(up, d));
  const trueUp = cross(d, right);
  const target: Vec3 = [(bounds.min[0] + bounds.max[0]) / 2, (bounds.min[1] + bounds.max[1]) / 2, (bounds.min[2] + bounds.max[2]) / 2];
  let halfW = 0;
  let halfH = 0;
  let depth = 0;
  for (const c of corners(bounds)) {
    const r = sub(c, target);
    halfW = Math.max(halfW, Math.abs(dot(r, right)));
    halfH = Math.max(halfH, Math.abs(dot(r, trueUp)));
    depth = Math.max(depth, dot(r, d));
  }
  const a = Math.max(0.1, aspect);
  const halfHeight = Math.max(halfH, halfW / a, 1) * margin;
  const fov = (fovDeg * Math.PI) / 180;
  // the nearest face of the box stays in front of the camera
  const distance = halfHeight / Math.tan(fov / 2) + depth;
  const position: Vec3 = [target[0] + d[0] * distance, target[1] + d[1] * distance, target[2] + d[2] * distance];
  return { position, target, up: trueUp, distance, halfHeight };
}

/** the view a camera's direction is closest to (for keeping the view's name when it is orbited a little) */
export function nearestView(position: Vec3, target: Vec3): StandardView {
  const d = norm(sub(position, target));
  let best: StandardView = 'perspective';
  let score = -2;
  for (const [name, v] of Object.entries(DIRECTIONS) as [StandardView, { dir: Vec3 }][]) {
    const s = dot(d, norm(v.dir));
    if (s > score) { score = s; best = name; }
  }
  return score > 0.999 ? best : 'perspective';
}

export interface Measurement {
  a: Vec3;
  b: Vec3;
  /** straight-line distance, mm */
  distance: number;
  /** along the bench's x (right), y (depth, towards the person) and z (height) */
  dx: number;
  dy: number;
  dz: number;
}

/** the distance between two points of the scene, with its components in the bench's axes */
export function measure(a: Vec3, b: Vec3): Measurement {
  const dx = b[0] - a[0];
  const dy = b[2] - a[2];
  const dz = b[1] - a[1];
  return { a, b, distance: Math.hypot(dx, dy, dz), dx, dy, dz };
}

/** a millimetre value as a person reads it off a caliper: 0.01 mm, no trailing noise */
export function mm(v: number): string {
  const r = Math.round(v * 100) / 100;
  return `${Object.is(r, -0) ? 0 : r} mm`;
}

/** the measurement as a line of text, with the distance in tenths of an inch when it is on the 2.54 mm grid */
export function describeMeasurement(m: Measurement): string {
  const pitches = m.distance / 2.54;
  const onGrid = m.distance > 0.01 && Math.abs(pitches - Math.round(pitches)) < 0.02;
  const grid = onGrid ? ` (${Math.round(pitches)} × 2.54 mm)` : '';
  return `${mm(m.distance)}${grid} · Δx ${mm(m.dx)} · Δy ${mm(m.dy)} · Δz ${mm(m.dz)}`;
}

/** the nearest of `points` to `p` within `radius`, else `p` itself (a click snaps to a pin or a hole) */
export function snapPoint(p: Vec3, points: Vec3[], radius: number): { at: Vec3; snapped: boolean } {
  let best: Vec3 | undefined;
  let bestD = radius;
  for (const q of points) {
    const d = Math.hypot(q[0] - p[0], q[1] - p[1], q[2] - p[2]);
    if (d <= bestD) { bestD = d; best = q; }
  }
  return best ? { at: best, snapped: true } : { at: p, snapped: false };
}

export type SectionAxis = 'x' | 'y' | 'z';

export interface Section {
  axis: SectionAxis;
  /** where the plane cuts, mm along the bench's axis */
  at: number;
  /** keep the side beyond the plane instead of the near one */
  flip?: boolean;
}

/**
 * The clipping plane of a section, as Three.js takes it: points p with
 * normal·p + constant >= 0 are kept. The bench's x, y (depth) and z
 * (height) are the scene's x, z and y.
 */
export function sectionPlane(s: Section): { normal: Vec3; constant: number } {
  const axis: Vec3 = s.axis === 'x' ? [1, 0, 0] : s.axis === 'y' ? [0, 0, 1] : [0, 1, 0];
  // unflipped, the part below the plane's position is kept (x < at), as a cut seen from the front
  const sign = s.flip ? 1 : -1;
  return { normal: [axis[0] * sign, axis[1] * sign, axis[2] * sign], constant: -sign * s.at };
}

export interface PartBounds {
  id: string;
  bounds: Bounds;
}

/** the parts whose bodies rise above `height` mm over the desk, tallest first */
export function aboveHeight(parts: PartBounds[], height: number): { id: string; top: number }[] {
  return parts
    .map((p) => ({ id: p.id, top: Math.round(p.bounds.max[1] * 100) / 100 }))
    .filter((p) => p.top > height + 1e-6)
    .sort((a, b) => b.top - a.top || a.id.localeCompare(b.id));
}

export interface BenchLayers {
  boards: boolean;
  parts: boolean;
  breadboards: boolean;
  jumpers: boolean;
  world: boolean;
  labels: boolean;
}

export const ALL_LAYERS: BenchLayers = { boards: true, parts: true, breadboards: true, jumpers: true, world: true, labels: true };

/** which layer a part's package belongs to */
export function layerOf(pkg: string): 'boards' | 'breadboards' | 'parts' {
  return pkg === 'board' ? 'boards' : pkg === 'breadboard' ? 'breadboards' : 'parts';
}
