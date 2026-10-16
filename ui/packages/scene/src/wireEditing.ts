// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import type { Project, Wire } from '@embsim/protocol';
import { distanceToSegment, pinPosition, snap, wireSegments, wireTouches, type Point } from './geometry';
import { layoutParts } from './autoLayout';
import type { SceneCatalog } from './SchematicCanvas';

/** Real symbol anchors, including rotated/mirrored boards and parts. */
export function projectPinPoints(project: Project, catalog: SceneCatalog): Record<string, Point[]> {
  const parts = new Map(layoutParts(project, catalog).map((p) => [p.id, p]));
  const result: Record<string, Point[]> = {};
  for (const [net, pins] of Object.entries(project.nets)) {
    result[net] = pins.flatMap((ref) => {
      const dot = ref.indexOf('.');
      const part = parts.get(ref.slice(0, dot));
      const point = part && pinPosition(part.layout, part.symbol, ref.slice(dot + 1));
      return point ? [point] : [];
    });
  }
  return result;
}

/** All contacts of two orthogonal segments (overlap boundaries included). */
export function segmentContacts(a: Point, b: Point, c: Point, d: Point): Point[] {
  const candidates = [a, b, c, d];
  if (a.x === b.x && c.y === d.y) candidates.push({ x: a.x, y: c.y });
  if (a.y === b.y && c.x === d.x) candidates.push({ x: c.x, y: a.y });
  return candidates.filter((p, i) => distanceToSegment(p, a, b) < .01 && distanceToSegment(p, c, d) < .01 &&
    candidates.findIndex((q) => q.x === p.x && q.y === p.y) === i);
}

/** Remove zero-length and redundant straight vertices, retaining reversals. */
export function cleanWirePoints(points: [number, number][]): [number, number][] {
  const out: [number, number][] = [];
  for (const p of points) {
    const last = out.at(-1);
    if (last && last[0] === p[0] && last[1] === p[1]) continue;
    out.push([...p]);
    while (out.length >= 3) {
      const [a, b, c] = out.slice(-3) as [[number, number], [number, number], [number, number]];
      const straight = (a[0] === b[0] && b[0] === c[0]) || (a[1] === b[1] && b[1] === c[1]);
      const between = b[0] >= Math.min(a[0], c[0]) && b[0] <= Math.max(a[0], c[0]) && b[1] >= Math.min(a[1], c[1]) && b[1] <= Math.max(a[1], c[1]);
      if (!straight || !between) break;
      out.splice(out.length - 2, 1);
    }
  }
  return out;
}

/** Slide one segment perpendicularly. Original endpoints remain attached;
 * same-net interior contacts retain a spur to the moved track. Nets never change. */
export function shiftWireSegment(wires: Wire[], index: number, segment: number, offset: number, pinsByNet: Record<string, Point[]> = {}): Wire[] | null {
  const wire = wires[index];
  const edge = wire && wireSegments(wire)[segment];
  if (!wire || !edge || !Number.isFinite(offset)) return null;
  const [a, b] = edge;
  if ((a.x !== b.x && a.y !== b.y) || (a.x === b.x && a.y === b.y)) return null;
  const delta = snap(offset);
  if (!delta) return null;
  const horizontal = a.y === b.y;
  const moved = (p: Point): [number, number] => horizontal ? [p.x, p.y + delta] : [p.x + delta, p.y];
  const points = cleanWirePoints([...wire.points.slice(0, segment + 1), moved(a), moved(b), ...wire.points.slice(segment + 1)]);
  const changed = { ...wire, points };
  const contacts = [...(pinsByNet[wire.net] ?? [])];
  wires.forEach((w, i) => {
    if (w.net !== wire.net) return;
    wireSegments(w).forEach(([c, d], j) => {
      if (i !== index || j !== segment) contacts.push(...segmentContacts(a, b, c, d));
    });
  });
  const seen = new Set<string>();
  const spurs: Wire[] = [];
  for (const p of contacts) {
    const key = `${p.x},${p.y}`;
    if (seen.has(key) || distanceToSegment(p, a, b) >= .01 || wireTouches(changed, p)) continue;
    seen.add(key);
    spurs.push({ net: wire.net, points: [[p.x, p.y], moved(p)] });
  }
  return [...wires.map((w, i) => i === index ? changed : w), ...spurs];
}
