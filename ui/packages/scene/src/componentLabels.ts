// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { formatSi, parseSi } from '@embsim/protocol';
import { rectsIntersect, type Point, type Rect } from './geometry';

export function componentValue(value: unknown, unit = ''): string {
  const units: Record<string, string> = { ohm: 'Ω', ohms: 'Ω' };
  const suffix = units[unit] ?? unit;
  const number = typeof value === 'number' || typeof value === 'string' ? parseSi(value) : undefined;
  if (number !== undefined) return formatSi(number, suffix);
  return value === undefined || value === null ? '' : `${String(value)}${suffix ? ` ${suffix}` : ''}`;
}
export interface LabelPart { id: string; bounds: Rect; name: string; value: string; pins: Point[] }
export interface ComponentLabel { x: number; y: number; width: number; height: number; name: string }
/** Bounded deterministic candidate placement. Labels never move electrical geometry. */
export function placeComponentLabels(parts: LabelPart[], wires: { points: [number, number][] }[], detailed: boolean): Map<string, ComponentLabel> {
  const placed: Rect[] = [];
  const result = new Map<string, ComponentLabel>();
  for (const part of [...parts].sort((a, b) => a.id < b.id ? -1 : a.id > b.id ? 1 : 0)) {
    const name = detailed ? (part.name.length > 24 ? `${part.name.slice(0, 23)}…` : part.name) : '';
    const width = Math.max(36, Math.min(156, Math.max(part.id.length * 7, name.length * 5.5, part.value.length * 6) + 8));
    const height = 14 + (name ? 12 : 0) + (part.value ? 13 : 0);
    const b = part.bounds;
    const candidates = [
      { x: b.x + b.width + 12, y: b.y + (b.height - height) / 2 },
      { x: b.x - width - 12, y: b.y + (b.height - height) / 2 },
      { x: b.x, y: b.y - height - 12 },
      { x: b.x, y: b.y + b.height + 12 },
      { x: b.x + b.width + 12, y: b.y - height - 12 },
      { x: b.x - width - 12, y: b.y + b.height + 12 },
      { x: b.x - width - 12, y: b.y - height - 12 },
      { x: b.x + b.width + 12, y: b.y + b.height + 12 },
    ].map((p) => ({ ...p, width, height }));
    const score = (box: Rect) => parts.reduce((sum, other) => sum + (rectsIntersect(box, other.bounds) ? 1000 : 0) + other.pins.filter((pin) => rectsIntersect(box, { x: pin.x - 8, y: pin.y - 8, width: 16, height: 16 })).length * 200, 0)
      + placed.filter((label) => rectsIntersect(box, label)).length * 800
      + wires.reduce((sum, wire) => sum + wire.points.slice(1).filter(([x, y], i) => { const a = wire.points[i]!; return rectsIntersect(box, { x: Math.min(x, a[0]) - 1, y: Math.min(y, a[1]) - 1, width: Math.abs(x - a[0]) + 2, height: Math.abs(y - a[1]) + 2 }); }).length * 10, 0);
    let best = candidates[0]!, cost = score(best);
    for (const candidate of candidates.slice(1)) { const next = score(candidate); if (next < cost) { best = candidate; cost = next; } }
    placed.push(best); result.set(part.id, { ...best, name });
  }
  return result;
}
