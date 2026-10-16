// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// Sheet geometry: the 10-unit grid, symbol placement (rotation about the
// symbol centre, optional mirror), pin anchors, orthogonal routing and
// junction detection. Pure functions; unit tested.

import type { Layout, SymbolDescription, Wire } from '@embsim/protocol';

export const GRID = 10;

export interface Point {
  x: number;
  y: number;
}

export interface Rect {
  x: number;
  y: number;
  width: number;
  height: number;
}

export function snap(v: number, grid = GRID): number {
  return Math.round(v / grid) * grid;
}

export function snapPoint(p: Point, grid = GRID): Point {
  return { x: snap(p.x, grid), y: snap(p.y, grid) };
}

export function normalizeRotation(r: number | undefined): 0 | 90 | 180 | 270 {
  const n = (((r ?? 0) % 360) + 360) % 360;
  return (Math.round(n / 90) * 90) % 360 as 0 | 90 | 180 | 270;
}

/** The SVG transform placing a symbol: translate to its origin, then rotate (and mirror) about its centre. */
export function symbolTransform(layout: Layout, symbol: Pick<SymbolDescription, 'width' | 'height'>): string {
  const cx = symbol.width / 2;
  const cy = symbol.height / 2;
  const r = normalizeRotation(layout.rotation);
  const parts = [`translate(${layout.x} ${layout.y})`];
  if (layout.mirror) parts.push(`translate(${cx} ${cy}) scale(-1 1) translate(${-cx} ${-cy})`);
  if (r !== 0) parts.push(`rotate(${r} ${cx} ${cy})`);
  return parts.join(' ');
}

/** Maps a point in symbol-local coordinates to the sheet. */
export function localToSheet(p: Point, layout: Layout, symbol: Pick<SymbolDescription, 'width' | 'height'>): Point {
  const cx = symbol.width / 2;
  const cy = symbol.height / 2;
  let dx = p.x - cx;
  const dy = p.y - cy;
  if (layout.mirror) dx = -dx;
  const r = normalizeRotation(layout.rotation);
  let rx = dx;
  let ry = dy;
  if (r === 90) {
    rx = -dy;
    ry = dx;
  } else if (r === 180) {
    rx = -dx;
    ry = -dy;
  } else if (r === 270) {
    rx = dy;
    ry = -dx;
  }
  return { x: layout.x + cx + rx, y: layout.y + cy + ry };
}

/** The sheet position of a named pin anchor. */
export function pinPosition(layout: Layout, symbol: SymbolDescription, pin: string): Point | null {
  const anchor = symbol.pins[pin];
  if (!anchor) return null;
  return localToSheet(anchor, layout, symbol);
}

/** Axis-aligned bounds of a placed symbol on the sheet. */
export function symbolBounds(layout: Layout, symbol: Pick<SymbolDescription, 'width' | 'height'>, margin = 0): Rect {
  const corners = [
    { x: 0, y: 0 },
    { x: symbol.width, y: 0 },
    { x: 0, y: symbol.height },
    { x: symbol.width, y: symbol.height },
  ].map((c) => localToSheet(c, layout, symbol));
  const xs = corners.map((c) => c.x);
  const ys = corners.map((c) => c.y);
  const minX = Math.min(...xs) - margin;
  const minY = Math.min(...ys) - margin;
  return { x: minX, y: minY, width: Math.max(...xs) + margin - minX, height: Math.max(...ys) + margin - minY };
}

export function rectsIntersect(a: Rect, b: Rect): boolean {
  return a.x < b.x + b.width && a.x + a.width > b.x && a.y < b.y + b.height && a.y + a.height > b.y;
}

export function rectContains(r: Rect, p: Point): boolean {
  return p.x >= r.x && p.x <= r.x + r.width && p.y >= r.y && p.y <= r.y + r.height;
}

export function rectContainsRect(outer: Rect, inner: Rect): boolean {
  return inner.x >= outer.x && inner.y >= outer.y && inner.x + inner.width <= outer.x + outer.width && inner.y + inner.height <= outer.y + outer.height;
}

export function pointsEqual(a: Point, b: Point): boolean {
  return a.x === b.x && a.y === b.y;
}

/**
 * Routes an orthogonal polyline from a to b: straight when aligned, else an
 * L through the corner chosen by `horizontalFirst`. Points are snapped.
 */
export function routeOrthogonal(a: Point, b: Point, horizontalFirst = true): [number, number][] {
  const p = snapPoint(a);
  const q = snapPoint(b);
  if (p.x === q.x || p.y === q.y) return [[p.x, p.y], [q.x, q.y]];
  const corner: Point = horizontalFirst ? { x: q.x, y: p.y } : { x: p.x, y: q.y };
  return [[p.x, p.y], [corner.x, corner.y], [q.x, q.y]];
}

/** Distance from p to the segment ab. */
export function distanceToSegment(p: Point, a: Point, b: Point): number {
  const dx = b.x - a.x;
  const dy = b.y - a.y;
  const len2 = dx * dx + dy * dy;
  let t = len2 === 0 ? 0 : ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2;
  t = Math.max(0, Math.min(1, t));
  const cx = a.x + t * dx;
  const cy = a.y + t * dy;
  return Math.hypot(p.x - cx, p.y - cy);
}

/** Whether p lies strictly inside segment ab (not at an end). */
export function pointInsideSegment(p: Point, a: Point, b: Point): boolean {
  if (pointsEqual(p, a) || pointsEqual(p, b)) return false;
  return distanceToSegment(p, a, b) < 0.01;
}

export function wireSegments(wire: Wire): [Point, Point][] {
  const out: [Point, Point][] = [];
  for (let i = 0; i + 1 < wire.points.length; i++) {
    const a = wire.points[i]!;
    const b = wire.points[i + 1]!;
    out.push([{ x: a[0], y: a[1] }, { x: b[0], y: b[1] }]);
  }
  return out;
}

/**
 * Junction dots: a point where a wire end (or a pin) touches the interior of
 * a segment of the same net, or where three or more wire ends meet.
 */
export function findJunctions(wires: Wire[], pinsByNet: Record<string, Point[]> = {}): { net: string; point: Point }[] {
  const byNet = new Map<string, Wire[]>();
  for (const w of wires) {
    const list = byNet.get(w.net) ?? [];
    list.push(w);
    byNet.set(w.net, list);
  }
  const result: { net: string; point: Point }[] = [];
  for (const [net, list] of byNet) {
    const segments = list.flatMap(wireSegments);
    const ends: Point[] = [];
    for (const w of list) {
      const first = w.points[0];
      const last = w.points[w.points.length - 1];
      if (first) ends.push({ x: first[0], y: first[1] });
      if (last && w.points.length > 1) ends.push({ x: last[0], y: last[1] });
    }
    const candidates = [...ends, ...(pinsByNet[net] ?? [])];
    const seen = new Set<string>();
    for (const c of candidates) {
      const key = `${c.x},${c.y}`;
      if (seen.has(key)) continue;
      const touchesInterior = segments.some(([a, b]) => pointInsideSegment(c, a, b));
      const meeting = ends.filter((e) => pointsEqual(e, c)).length;
      if (touchesInterior || meeting >= 3) {
        seen.add(key);
        result.push({ net, point: c });
      }
    }
  }
  return result;
}

/** Nearest point of a net's wiring to p, for ratsnest lines of unwired pins. */
export function nearestWirePoint(p: Point, wires: Wire[]): Point | null {
  let best: Point | null = null;
  let bestD = Infinity;
  for (const w of wires) {
    for (const [a, b] of wireSegments(w)) {
      const dx = b.x - a.x;
      const dy = b.y - a.y;
      const len2 = dx * dx + dy * dy;
      let t = len2 === 0 ? 0 : ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2;
      t = Math.max(0, Math.min(1, t));
      const c = { x: a.x + t * dx, y: a.y + t * dy };
      const d = Math.hypot(p.x - c.x, p.y - c.y);
      if (d < bestD) {
        bestD = d;
        best = c;
      }
    }
  }
  return best;
}

/** Whether a sheet point touches any point of the wire (endpoints or interior). */
export function wireTouches(wire: Wire, p: Point): boolean {
  return wireSegments(wire).some(([a, b]) => distanceToSegment(p, a, b) < 0.01);
}

/** Rotates a set of layouts by 90° about their common centre snapped to the grid. */
export function rotateLayouts(layouts: { layout: Layout; symbol: Pick<SymbolDescription, 'width' | 'height'> }[]): Layout[] {
  if (layouts.length === 0) return [];
  if (layouts.length === 1) {
    const l = layouts[0]!.layout;
    return [{ ...l, rotation: (normalizeRotation(l.rotation) + 90) % 360 }];
  }
  const bounds = layouts.map((l) => symbolBounds(l.layout, l.symbol));
  const minX = Math.min(...bounds.map((b) => b.x));
  const minY = Math.min(...bounds.map((b) => b.y));
  const maxX = Math.max(...bounds.map((b) => b.x + b.width));
  const maxY = Math.max(...bounds.map((b) => b.y + b.height));
  const cx = snap((minX + maxX) / 2);
  const cy = snap((minY + maxY) / 2);
  return layouts.map(({ layout, symbol }) => {
    const c = { x: layout.x + symbol.width / 2, y: layout.y + symbol.height / 2 };
    const nx = cx - (c.y - cy);
    const ny = cy + (c.x - cx);
    return { ...layout, x: snap(nx - symbol.width / 2), y: snap(ny - symbol.height / 2), rotation: (normalizeRotation(layout.rotation) + 90) % 360 };
  });
}
