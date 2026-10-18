// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
// Deterministic presentation tools. Electrical connectivity is never inferred
// from drawn crossings and is never changed by placement or routing.
import type { Layout, Project, SymbolDescription, Wire } from '@embsim/protocol';
import { pinPosition, rectsIntersect, snap, symbolBounds, type Point, type Rect } from './geometry';
import type { SceneCatalog } from './SchematicCanvas';

export interface LayoutOptions {
  direction: 'horizontal' | 'vertical';
  spacing: 'compact' | 'comfortable';
  /** Empty means all; other components and boards remain fixed obstacles. */
  selected?: string[];
  route: boolean;
}
export interface LayoutResult {
  project: Project;
  moved: number;
  routed: number;
  unresolved: number;
  warnings: string[];
}
export type Arrangement = 'left' | 'right' | 'top' | 'bottom' | 'center-x' | 'center-y' | 'distribute-x' | 'distribute-y';
const compareIds = (a: string, b: string) => a < b ? -1 : a > b ? 1 : 0;
const FALLBACK: SymbolDescription = { svg: '', width: 60, height: 40, pins: {} };
export function layoutParts(project: Project, catalog: SceneCatalog) {
  return [...project.components.filter((c) => !catalog[c.component]?.wiringAid).map((c) => ({ id: c.id, ref: c.component, layout: c.layout ?? { x: 0, y: 0 } })),
    ...(project.boards ?? []).map((b) => ({ id: b.id, ref: b.board, layout: b.layout ?? { x: 0, y: 0 } }))]
    .map((p) => ({ ...p, symbol: catalog[p.ref]?.symbol ?? FALLBACK }));
}
function assign(project: Project, layouts: Map<string, Layout>) {
  for (const c of [...project.components, ...(project.boards ?? [])]) {
    const layout = layouts.get(c.id);
    if (layout) c.layout = layout;
  }
}
function owner(pin: string) { return pin.split('.')[0]!; }
function graph(project: Project, ids: Set<string>) {
  const out = new Map([...ids].sort().map((id) => [id, new Set<string>()]));
  for (const [net, pins] of Object.entries(project.nets)) {
    // Shared supply rails are not signal-flow hints. Route them normally later.
    if (/^(gnd|ground|vcc|vdd|vss|3v3|5v|12v|power)(_|$)/i.test(net)) continue;
    const members = [...new Set(pins.map(owner).filter((id) => ids.has(id)))].sort();
    // A star bounds work on large buses instead of creating a quadratic clique.
    for (const id of members.slice(1)) { out.get(members[0]!)!.add(id); out.get(id)!.add(members[0]!); }
  }
  // Keep supply-only symbols near a connected owner without allowing a
  // shared rail to collapse otherwise independent signal groups into a star.
  for (const [net, pins] of Object.entries(project.nets)) {
    if (!/^(gnd|ground|vcc|vdd|vss|3v3|5v|12v|power)(_|$)/i.test(net)) continue;
    const members = [...new Set(pins.map(owner).filter((id) => ids.has(id)))];
    const hub = members.filter((id) => out.get(id)!.size > 0).sort((a, b) => out.get(b)!.size - out.get(a)!.size || compareIds(a, b))[0];
    if (!hub) continue;
    for (const id of members) if (id !== hub && !out.get(id)!.size) { out.get(hub)!.add(id); out.get(id)!.add(hub); }
  }
  return out;
}

/** Layered connected groups, deterministic ID tie breaking, fixed obstacles. */
export function autoPlace(project: Project, catalog: SceneCatalog, options: LayoutOptions): Project {
  const p = structuredClone(project);
  const parts = layoutParts(p, catalog);
  const selected = new Set(options.selected?.length ? options.selected : parts.map((c) => c.id));
  const active = parts.filter((c) => selected.has(c.id) && !c.layout.locked);
  const byId = new Map(active.map((c) => [c.id, c]));
  const links = graph(p, new Set(byId.keys()));
  const gap = options.spacing === 'compact' ? 70 : 120;
  const fixed = parts.filter((c) => !selected.has(c.id) || c.layout.locked).map((c) => symbolBounds(c.layout, c.symbol, 30));
  const layouts = new Map<string, Layout>();
  const unseen = new Set([...byId.keys()].sort());
  let groupY = active.length ? Math.max(60, snap(Math.min(...active.map((c) => c.layout.y)))) : 60;
  const originX = active.length ? Math.max(60, snap(Math.min(...active.map((c) => c.layout.x)))) : 60;
  while (unseen.size) {
    const root = [...unseen].sort((a, b) => links.get(b)!.size - links.get(a)!.size || compareIds(a, b))[0]!;
    const layers: string[][] = [[root]];
    unseen.delete(root);
    for (let depth = 0; depth < layers.length; depth++) {
      const next: string[] = [];
      for (const id of layers[depth]!) for (const neighbor of [...links.get(id)!].sort()) {
        if (unseen.delete(neighbor)) next.push(neighbor);
      }
      if (next.length) layers.push(next);
    }
    let x = originX;
    let groupBottom = groupY;
    for (const layer of layers) {
      let y = groupY;
      let columnWidth = 0;
      for (const id of layer) {
        const c = byId.get(id)!;
        // Respect existing rotations and mirrors; use transformed bounds.
        const shape = symbolBounds({ ...c.layout, x: 0, y: 0 }, c.symbol);
        const horizontal = options.direction === 'horizontal';
        const bounds = { x: horizontal ? x : y, y: horizontal ? y : x, width: shape.width, height: shape.height };
        for (;;) {
          const collisions = fixed.filter((r) => rectsIntersect(r, bounds));
          if (!collisions.length) break;
          if (horizontal) bounds.y = snap(Math.max(...collisions.map((r) => r.y + r.height)) + 30);
          else bounds.x = snap(Math.max(...collisions.map((r) => r.x + r.width)) + 30);
        }
        layouts.set(id, { ...c.layout, x: snap(bounds.x - shape.x), y: snap(bounds.y - shape.y) });
        fixed.push({ x: bounds.x - 20, y: bounds.y - 20, width: bounds.width + 40, height: bounds.height + 40 });
        y = (horizontal ? bounds.y + bounds.height : bounds.x + bounds.width) + gap;
        columnWidth = Math.max(columnWidth, horizontal ? shape.width : shape.height);
        groupBottom = Math.max(groupBottom, y);
      }
      x += columnWidth + gap;
    }
    groupY = groupBottom + gap;
  }
  assign(p, layouts);
  return p;
}

interface Obstacle extends Rect { id: string }
type Track = [Point, Point];
interface Anchor extends Point { id: string; exit: Point }
function anchor(point: Point, id: string, bounds: Rect): Anchor {
  const candidates = [
    { x: bounds.x - 20, y: point.y }, { x: bounds.x + bounds.width + 20, y: point.y },
    { x: point.x, y: bounds.y - 20 }, { x: point.x, y: bounds.y + bounds.height + 20 },
  ];
  candidates.sort((a, b) => Math.abs(a.x - point.x) + Math.abs(a.y - point.y) - Math.abs(b.x - point.x) - Math.abs(b.y - point.y));
  return { ...point, id, exit: candidates[0]! };
}
function blocked(a: Point, b: Point, obstacles: Obstacle[], except?: string): boolean {
  return obstacles.some((r) => r.id !== except && (a.x === b.x
    ? a.x > r.x - 5 && a.x < r.x + r.width + 5 && Math.max(a.y, b.y) > r.y - 5 && Math.min(a.y, b.y) < r.y + r.height + 5
    : a.y > r.y - 5 && a.y < r.y + r.height + 5 && Math.max(a.x, b.x) > r.x - 5 && Math.min(a.x, b.x) < r.x + r.width + 5));
}
function simplify(points: Point[]): [number, number][] {
  const out: Point[] = [];
  for (const p of points) {
    const last = out.at(-1);
    if (last?.x === p.x && last.y === p.y) continue;
    while (out.length >= 2) {
      const a = out.at(-2)!; const b = out.at(-1)!;
      if ((a.x === b.x && b.x === p.x) || (a.y === b.y && b.y === p.y)) out.pop(); else break;
    }
    out.push(p);
  }
  return out.map((p) => [p.x, p.y]);
}

/** Visibility-grid A*: exact pin anchors, obstacle clearance, bend penalty,
 * deterministic heap, bounded work. Failure returns null, never a body crossing. */
function route(a: Anchor, b: Anchor, obstacles: Obstacle[], occupied: Track[] = []): [number, number][] | null {
  for (const end of [a, b]) {
    if (!blocked(end, end.exit, obstacles, end.id)) continue;
    const original = end.exit;
    for (const clearance of [10, 5]) {
      const candidate = { x: original.x - Math.sign(original.x - end.x) * (20 - clearance), y: original.y - Math.sign(original.y - end.y) * (20 - clearance) };
      if (!blocked(end, candidate, obstacles, end.id)) { end.exit = candidate; break; }
    }
    if (blocked(end, end.exit, obstacles, end.id)) return null;
  }
  const pad = 100;
  const region: Rect = { x: Math.min(a.exit.x, b.exit.x) - pad, y: Math.min(a.exit.y, b.exit.y) - pad,
    width: Math.abs(a.exit.x - b.exit.x) + pad * 2, height: Math.abs(a.exit.y - b.exit.y) + pad * 2 };
  const nearby = obstacles.filter((r) => rectsIntersect(region, r));
  const tracks = occupied.filter(([c, d]) => Math.max(c.x, d.x) >= region.x && Math.min(c.x, d.x) <= region.x + region.width && Math.max(c.y, d.y) >= region.y && Math.min(c.y, d.y) <= region.y + region.height);
  const overlapsTrack = (p: Point, q: Point) => tracks.some(([c, d]) =>
    p.x === q.x && c.x === d.x && p.x === c.x ? Math.min(Math.max(p.y, q.y), Math.max(c.y, d.y)) > Math.max(Math.min(p.y, q.y), Math.min(c.y, d.y)) :
      p.y === q.y && c.y === d.y && p.y === c.y && Math.min(Math.max(p.x, q.x), Math.max(c.x, d.x)) > Math.max(Math.min(p.x, q.x), Math.min(c.x, d.x)));
  if (overlapsTrack(a, a.exit) || overlapsTrack(b, b.exit)) return null;
  const xs = [...new Set([a.exit.x, b.exit.x, region.x, region.x + region.width,
    ...nearby.flatMap((r) => [r.x - 20, r.x + r.width + 20]), ...tracks.flatMap(([c, d]) => [c.x - 10, c.x + 10, d.x - 10, d.x + 10])])].sort((x, y) => x - y);
  const ys = [...new Set([a.exit.y, b.exit.y, region.y, region.y + region.height,
    ...nearby.flatMap((r) => [r.y - 20, r.y + r.height + 20]), ...tracks.flatMap(([c, d]) => [c.y - 10, c.y + 10, d.y - 10, d.y + 10])])].sort((x, y) => x - y);
  const startX = xs.indexOf(a.exit.x); const startY = ys.indexOf(a.exit.y);
  const goalX = xs.indexOf(b.exit.x); const goalY = ys.indexOf(b.exit.y);
  type State = { x: number; y: number; dir: number; cost: number; score: number; key: string; parent?: State };
  const heap: State[] = [];
  const push = (v: State) => {
    heap.push(v); let i = heap.length - 1;
    while (i > 0) { const parent = (i - 1) >> 1; if (heap[parent]!.score <= v.score) break; heap[i] = heap[parent]!; i = parent; }
    heap[i] = v;
  };
  const pop = () => {
    const first = heap[0]!; const last = heap.pop()!;
    if (heap.length) { let i = 0; while (i * 2 + 1 < heap.length) {
      let child = i * 2 + 1; if (child + 1 < heap.length && heap[child + 1]!.score < heap[child]!.score) child++;
      if (heap[child]!.score >= last.score) break; heap[i] = heap[child]!; i = child;
    } heap[i] = last; } return first;
  };
  push({ x: startX, y: startY, dir: 0, cost: 0, score: 0, key: `${startX},${startY},0` });
  const best = new Map<string, number>();
  let visited = 0;
  while (heap.length && visited++ < 12000) {
    const current = pop();
    if ((best.get(current.key) ?? Infinity) < current.cost) continue;
    if (current.x === goalX && current.y === goalY) {
      const path: Point[] = [];
      let node: State | undefined = current;
      while (node) { path.push({ x: xs[node.x]!, y: ys[node.y]! }); node = node.parent; }
      return simplify([a, ...path.reverse(), b]);
    }
    const from = { x: xs[current.x]!, y: ys[current.y]! };
    for (const [dx, dy] of [[1, 0], [0, 1], [-1, 0], [0, -1]]) {
      const x = current.x + dx!; const y = current.y + dy!;
      if (x < 0 || x >= xs.length || y < 0 || y >= ys.length) continue;
      const to = { x: xs[x]!, y: ys[y]! };
      if (blocked(from, to, nearby) || overlapsTrack(from, to)) continue;
      const dir = dx ? 1 : 2;
      const cost = current.cost + Math.abs(from.x - to.x) + Math.abs(from.y - to.y) + (current.dir && dir !== current.dir ? 25 : 0);
      const key = `${x},${y},${dir}`;
      if (cost >= (best.get(key) ?? Infinity)) continue;
      best.set(key, cost);
      push({ x, y, dir, cost, score: cost + Math.abs(to.x - b.exit.x) + Math.abs(to.y - b.exit.y), key, parent: current });
    }
  }
  return null;
}

function wireTracks(wires: Wire[]): Track[] {
  return wires.flatMap((wire) => wire.points.slice(1).map((p, i): Track => [{ x: wire.points[i]![0], y: wire.points[i]![1] }, { x: p[0], y: p[1] }]));
}

/** Regenerates affected nets only; unknown pins keep their existing wiring. */
export function autoRoute(project: Project, catalog: SceneCatalog, affected?: Set<string>): LayoutResult {
  const p = structuredClone(project);
  const parts = layoutParts(p, catalog);
  const byId = new Map(parts.map((c) => [c.id, c]));
  const obstacles = parts.map((c) => ({ id: c.id, ...symbolBounds(c.layout, c.symbol) }));
  const wires: Wire[] = (p.layout?.wires ?? []).filter((w) => affected && !affected.has(w.net));
  const warnings: string[] = [];
  let unresolved = 0; let routed = 0;
  for (const [net, pins] of Object.entries(p.nets).sort(([a], [b]) => compareIds(a, b))) {
    if (affected && !affected.has(net)) continue;
    const anchors: Anchor[] = [];
    let unknown = false;
    for (const ref of pins) {
      const id = owner(ref); const c = byId.get(id);
      const point = c && pinPosition(c.layout, c.symbol, ref.slice(id.length + 1));
      if (!c || !point) { unknown = true; continue; }
      anchors.push(anchor(point, id, symbolBounds(c.layout, c.symbol)));
    }
    if (unknown) {
      wires.push(...(p.layout?.wires ?? []).filter((w) => w.net === net));
      warnings.push(`${net}: unknown pin anchors; existing wires retained.`); unresolved++; continue;
    }
    if (anchors.length < 2) continue;
    // Prim's nearest-tree traversal keeps routes local and connects all pins.
    const connected = [anchors.shift()!];
    while (anchors.length) {
      let ai = 0; let ci = 0; let distance = Infinity;
      anchors.forEach((a, i) => connected.forEach((c, j) => {
        const d = Math.abs(a.x - c.x) + Math.abs(a.y - c.y);
        if (d < distance) { distance = d; ai = i; ci = j; }
      }));
      const next = anchors.splice(ai, 1)[0]!;
      const points = route(connected[ci]!, next, obstacles, wireTracks(wires.filter((w) => w.net !== net)));
      if (points) { if (points.length > 1) wires.push({ net, points }); routed++; }
      else { unresolved++; warnings.push(`${net}: no clear route to ${next.id}; shown as an airwire.`); }
      connected.push(next);
    }
  }
  p.layout = { ...(p.layout ?? {}), wires };
  return { project: p, moved: 0, routed, unresolved, warnings };
}
function changedNets(p: Project, ids: Set<string>) {
  return new Set(Object.entries(p.nets).filter(([, pins]) => pins.some((pin) => ids.has(owner(pin)))).map(([net]) => net));
}
export function createLayout(project: Project, catalog: SceneCatalog, options: LayoutOptions): LayoutResult {
  const placed = autoPlace(project, catalog, options);
  const ids = new Set(options.selected?.length ? options.selected : layoutParts(project, catalog).map((c) => c.id));
  const result = options.route ? autoRoute(placed, catalog, changedNets(project, ids)) : { project: placed, moved: 0, routed: 0, unresolved: 0, warnings: [] };
  const originals = new Map(layoutParts(project, catalog).map((p) => [p.id, p]));
  result.moved = layoutParts(placed, catalog).filter((c) => {
    const old = originals.get(c.id)!;
    return old.layout.x !== c.layout.x || old.layout.y !== c.layout.y;
  }).length;
  // Moving without rerouting leaves old artwork misleading; drop affected wire
  // geometry and let existing airwires describe the unchanged connectivity.
  if (!options.route) {
    const affected = changedNets(project, ids);
    result.project.layout = { ...(placed.layout ?? {}), wires: (placed.layout?.wires ?? []).filter((w) => !affected.has(w.net)) };
  }
  return result;
}

export function arrange(project: Project, catalog: SceneCatalog, ids: string[], action: Arrangement): LayoutResult {
  const p = structuredClone(project);
  const parts = layoutParts(p, catalog).filter((c) => ids.includes(c.id));
  if (parts.some((c) => c.layout.locked)) return { project: p, moved: 0, routed: 0, unresolved: 0, warnings: ['Unlock the selected parts before arranging them.'] };
  if (parts.length < 2) return { project: p, moved: 0, routed: 0, unresolved: 0, warnings: [] };
  const boxes = parts.map((c) => ({ c, b: symbolBounds(c.layout, c.symbol) }));
  const minX = Math.min(...boxes.map(({ b }) => b.x)); const maxX = Math.max(...boxes.map(({ b }) => b.x + b.width));
  const minY = Math.min(...boxes.map(({ b }) => b.y)); const maxY = Math.max(...boxes.map(({ b }) => b.y + b.height));
  const layouts = new Map<string, Layout>();
  const axis = action.endsWith('x') ? 'x' : 'y';
  if (action.startsWith('distribute')) {
    const dim = axis === 'x' ? 'width' : 'height';
    const sorted = [...boxes].sort((a, b) => a.b[axis] - b.b[axis] || compareIds(a.c.id, b.c.id));
    const first = sorted[0]!.b[axis]; const last = sorted.at(-1)!.b;
    const span = last[axis] + last[dim] - first;
    const gap = (span - sorted.reduce((sum, { b }) => sum + b[dim], 0)) / (parts.length - 1);
    let cursor = first;
    for (const { c, b } of sorted) { layouts.set(c.id, { ...c.layout, [axis]: snap(c.layout[axis] + cursor - b[axis]) }); cursor += b[dim] + gap; }
  } else for (const { c, b } of boxes) {
    const dx = action === 'left' ? minX - b.x : action === 'right' ? maxX - b.x - b.width : action === 'center-x' ? (minX + maxX - b.width) / 2 - b.x : 0;
    const dy = action === 'top' ? minY - b.y : action === 'bottom' ? maxY - b.y - b.height : action === 'center-y' ? (minY + maxY - b.height) / 2 - b.y : 0;
    layouts.set(c.id, { ...c.layout, x: snap(c.layout.x + dx), y: snap(c.layout.y + dy) });
  }
  assign(p, layouts);
  const result = autoRoute(p, catalog, changedNets(p, new Set(ids)));
  result.moved = parts.filter((c) => { const l = layouts.get(c.id)!; return l.x !== c.layout.x || l.y !== c.layout.y; }).length;
  return result;
}

/** Route a new connection without changing its net membership. Net targets
 * have no owner; component endpoints may exit only their own symbol. */
export function routeConnection(project: Project, catalog: SceneCatalog, from: { point: Point; component?: string; net?: string }, to: { point: Point; component?: string; net?: string }): [number, number][] | null {
  const parts = layoutParts(project, catalog);
  const obstacles = parts.map((c) => ({ id: c.id, ...symbolBounds(c.layout, c.symbol) }));
  const endpoint = (p: typeof from): Anchor => {
    const body = obstacles.find((c) => c.id === p.component);
    return body ? anchor(p.point, body.id, body) : { ...p.point, id: '', exit: p.point };
  };
  return route(endpoint(from), endpoint(to), obstacles, wireTracks((project.layout?.wires ?? []).filter((w) => w.net !== from.net && w.net !== to.net)));
}

/** Find a nearby grid location without covering an existing symbol. */
export function freePlacement(project: Project, catalog: SceneCatalog, symbol: SymbolDescription, center: Point): Layout {
  const obstacles = layoutParts(project, catalog).map((p) => symbolBounds(p.layout, p.symbol, 40));
  const origin = { x: snap(center.x - symbol.width / 2), y: snap(center.y - symbol.height / 2), rotation: 0 };
  for (let ring = 0; ring <= 100; ring++) {
    const candidates: Point[] = ring === 0 ? [{ x: 0, y: 0 }] : [];
    for (let n = -ring; n <= ring; n++) {
      if (ring > 0) { candidates.push({ x: n * 20, y: -ring * 20 }, { x: n * 20, y: ring * 20 });
        if (Math.abs(n) !== ring) candidates.push({ x: -ring * 20, y: n * 20 }, { x: ring * 20, y: n * 20 }); }
    }
    for (const offset of candidates) {
      const layout = { ...origin, x: origin.x + offset.x, y: origin.y + offset.y };
      if (!obstacles.some((r) => rectsIntersect(r, symbolBounds(layout, symbol)))) return layout;
    }
  }
  // A very dense sheet still gets a deterministic clear location to its right.
  return { ...origin, x: snap(Math.max(origin.x, ...obstacles.map((r) => r.x + r.width)) + 40) };
}
