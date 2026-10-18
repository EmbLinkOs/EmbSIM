// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The bench as a model (ADR-027, SPEC-007 §2): where each part sits, where
// its legs land, which breadboard holes they are plugged into, and the
// edits that building on the bench makes to the project's nets. Every
// function here is pure: it takes a project and returns the next one, so
// the workbench sends the change as one transaction and undo restores it.
//
// The rules, as on a real bench:
// - a leg within 0.4 mm of a hole is plugged into it; the holes of a strip
//   are one conductor (the breadboard's ties), so a plugged pin joins the
//   strip's net, and every occupied hole of a strip is in that net;
// - a jumper joins the nets of its two ends;
// - lifting a part or a jumper takes its pins out of the nets it joined;
//   a net the bench alone made splits where nothing on the bench still
//   joins it, and a connection the schematic drew stays (it is shown as
//   not yet built).

import type { BenchLayout, BenchPlace, BenchWire, Project } from '@embsim/protocol';
import { PITCH, type BenchShape } from './shape';

export const PLUG_TOLERANCE = 0.4;

export type Shapes = Record<string, BenchShape>;

/** a description's ties, by the instance that has them */
export type Ties = Record<string, string[][]>;

export interface PlacedPart {
  id: string;
  shape: BenchShape;
  place: Required<BenchPlace>;
  /** whether its place comes from the project (else the default layout) */
  placed: boolean;
  /** each pin's leg on the bench, mm */
  legs: Record<string, [number, number]>;
}

export interface Hole {
  ref: string;
  x: number;
  y: number;
}

export interface BenchModel {
  parts: PlacedPart[];
  byId: Map<string, PlacedPart>;
  holes: Map<string, Hole>;
  /** pin ref -> the hole its leg is in */
  plugs: Map<string, string>;
  /** hole ref -> the key of its strip */
  strip: Map<string, string>;
  /** Physical insertions, independent of the electrical strip's membership. */
  occupied: Map<string, string[]>;
}

function instances(p: Project): string[] {
  return [...(p.boards ?? []).map((b) => b.id), ...p.components.map((c) => c.id)];
}

/** rotates (dx, dy) clockwise seen from above (y towards the viewer) */
export function turn(dx: number, dy: number, rotation: number): [number, number] {
  switch (((rotation % 360) + 360) % 360) {
    case 90:
      return [-dy, dx];
    case 180:
      return [-dx, -dy];
    case 270:
      return [dy, -dx];
    default:
      return [dx, dy];
  }
}

/** the extent of a placed part on the bench: [x0, y0, x1, y1] */
export function footprint(part: { shape: BenchShape; place: Required<BenchPlace> }): [number, number, number, number] {
  const [w, d] = part.shape.size;
  const [hx, hy] = turn(w / 2, d / 2, part.place.rotation);
  const ax = Math.abs(hx);
  const ay = Math.abs(hy);
  return [part.place.x - ax, part.place.y - ay, part.place.x + ax, part.place.y + ay];
}

export function legsOf(shape: BenchShape, place: Required<BenchPlace>): Record<string, [number, number]> {
  const out: Record<string, [number, number]> = {};
  const [w, d] = shape.size;
  for (const [pin, [x, y]] of Object.entries(shape.pins)) {
    const [dx, dy] = turn(x - w / 2, y - d / 2, place.rotation);
    out[pin] = [place.x + dx, place.y + dy];
  }
  return out;
}

/**
 * Where everything sits: the project's places, and for what has none the
 * default bench: boards along the front, breadboards behind them, the other
 * parts in rows at the back, as on a desk.
 */
export function placeAll(p: Project, shapes: Shapes): PlacedPart[] {
  const own = p.layout?.bench?.parts ?? {};
  const out: PlacedPart[] = [];
  const ids = instances(p).filter((id) => shapes[id] && shapes[id]!.package !== 'virtual');
  const boards = ids.filter((id) => shapes[id]!.package === 'board');
  const boardsOf = ids.filter((id) => shapes[id]!.package === 'breadboard');
  const rest = ids.filter((id) => !boards.includes(id) && !boardsOf.includes(id));
  // rows from the back (small y) to the front: parts, breadboards, boards; in
  // front of what already has a place, so nothing lands on it
  let left = 10;
  let y = 10;
  for (const id of ids) {
    const at = own[id];
    if (!at) continue;
    const [w, d] = shapes[id]!.size;
    const r = Math.max(w, d) / 2;
    if (y === 10 && left === 10) left = at.x - r;
    left = Math.min(left, at.x - r);
    y = Math.max(y, at.y + r + 20);
  }
  const row = (list: string[], gap: number) => {
    let x = left;
    let depth = 0;
    for (const id of list) {
      const shape = shapes[id]!;
      const [w, d] = shape.size;
      if (x > left && x + w > left + 320) {
        x = left;
        y += depth + gap;
        depth = 0;
      }
      const placed = own[id];
      const place = placed ? { x: placed.x, y: placed.y, rotation: placed.rotation ?? 0 } : { x: x + w / 2, y: y + d / 2, rotation: 0 as const };
      out.push({ id, shape, place, placed: Boolean(placed), legs: legsOf(shape, place) });
      x += w + gap;
      depth = Math.max(depth, d);
    }
    y += depth + gap * 2;
  };
  row(rest, 8);
  row(boardsOf, 10);
  row(boards, 10);
  return out;
}

function stripKeys(id: string, ties: string[][] | undefined): Map<string, string> {
  const out = new Map<string, string>();
  (ties ?? []).forEach((tie, i) => tie.forEach((pin) => out.set(`${id}.${pin}`, `${id}#${i}`)));
  return out;
}

const cell = (x: number, y: number) => `${Math.round(x / PITCH)},${Math.round(y / PITCH)}`;

export function benchModel(p: Project, shapes: Shapes, ties: Ties): BenchModel {
  const parts = placeAll(p, shapes);
  const byId = new Map(parts.map((x) => [x.id, x]));
  const holes = new Map<string, Hole>();
  const strip = new Map<string, string>();
  const grid = new Map<string, Hole[]>();
  for (const part of parts) {
    if (part.shape.package !== 'breadboard') continue;
    for (const [k, v] of stripKeys(part.id, ties[part.id])) strip.set(k, v);
    for (const [pin, [x, y]] of Object.entries(part.legs)) {
      const h = { ref: `${part.id}.${pin}`, x, y };
      holes.set(h.ref, h);
      const key = cell(x, y);
      grid.set(key, [...(grid.get(key) ?? []), h]);
    }
  }
  const plugs = new Map<string, string>();
  for (const part of parts) {
    if (part.shape.package === 'breadboard' || part.shape.package === 'board') continue;
    for (const [pin, [x, y]] of Object.entries(part.legs)) {
      const h = holeAt(grid, x, y);
      if (h) plugs.set(`${part.id}.${pin}`, h.ref);
    }
  }
  const occupied = new Map<string, string[]>();
  const occupy = (hole: string, owner: string) => occupied.set(hole, [...(occupied.get(hole) ?? []), owner]);
  for (const [pin, hole] of plugs) occupy(hole, pin);
  (p.layout?.bench?.wires ?? []).forEach((wire, i) => {
    for (const end of [wire.from, wire.to]) if (holes.has(end)) occupy(end, `jumper ${i + 1}`);
  });
  return { parts, byId, holes, plugs, strip, occupied };
}

function holeAt(grid: Map<string, Hole[]>, x: number, y: number): Hole | undefined {
  const cx = Math.round(x / PITCH);
  const cy = Math.round(y / PITCH);
  let best: Hole | undefined;
  let bestD = PLUG_TOLERANCE;
  for (let i = -1; i <= 1; i++)
    for (let j = -1; j <= 1; j++)
      for (const h of grid.get(`${cx + i},${cy + j}`) ?? []) {
        const d = Math.hypot(h.x - x, h.y - y);
        if (d <= bestD) {
          best = h;
          bestD = d;
        }
      }
  return best;
}

/** the hole nearest (x, y) within `radius`, for snapping and for jumper ends */
export function nearestHole(model: BenchModel, x: number, y: number, radius = PITCH / 2): Hole | undefined {
  let best: Hole | undefined;
  let bestD = radius;
  for (const h of model.holes.values()) {
    const d = Math.hypot(h.x - x, h.y - y);
    if (d < bestD) {
      best = h;
      bestD = d;
    }
  }
  return best;
}

export interface InsertionContact {
  pin: string;
  at: [number, number];
  hole?: string;
  occupiedBy: string[];
}

export interface InsertionPreview {
  contacts: InsertionContact[];
  valid: boolean;
  elevation: number;
}

/** Per-leg authoring feedback. No nets or layout are changed by a preview.
 * Partial insertions remain possible; body/lead collisions are not modeled. */
export function previewInsertion(model: BenchModel, id: string, place: Required<BenchPlace>): InsertionPreview {
  const part = model.byId.get(id);
  if (!part || part.shape.package === 'board' || part.shape.package === 'breadboard') return { contacts: [], valid: true, elevation: 0 };
  const contacts: InsertionContact[] = [];
  const own = new Map<string, string>();
  let elevation = 0;
  for (const [pin, at] of Object.entries(legsOf(part.shape, place))) {
    const hole = nearestHole(model, at[0], at[1], PLUG_TOLERANCE + 0.000001);
    const occupiedBy = hole ? (model.occupied.get(hole.ref) ?? []).filter((ref) => !ref.startsWith(`${id}.`)) : [];
    if (hole && own.has(hole.ref)) occupiedBy.push(own.get(hole.ref)!);
    contacts.push({ pin: `${id}.${pin}`, at, hole: hole?.ref, occupiedBy });
    if (hole) {
      own.set(hole.ref, `${id}.${pin}`);
      elevation = Math.max(elevation, model.byId.get(hole.ref.split('.')[0]!)!.shape.size[2]);
    }
  }
  return { contacts, elevation, valid: contacts.every((contact) => contact.occupiedBy.length === 0) };
}

// ---- nets -------------------------------------------------------------------

function netOf(p: Project, terminal: string): string | undefined {
  for (const [net, pins] of Object.entries(p.nets)) if (pins.includes(terminal)) return net;
  return undefined;
}

function freshNet(p: Project): string {
  let n = 1;
  while (`N${n}` in p.nets) n++;
  return `N${n}`;
}

/** merges net `b` into `a` (the user-named one is kept), returning the survivor */
function merge(p: Project, a: string, b: string): string {
  if (a === b) return a;
  const keep = /^N\d+$/.test(a) && !/^N\d+$/.test(b) ? b : a;
  const drop = keep === a ? b : a;
  p.nets[keep] = [...p.nets[keep]!, ...p.nets[drop]!.filter((t) => !p.nets[keep]!.includes(t))];
  delete p.nets[drop];
  for (const w of p.layout?.wires ?? []) if (w.net === drop) w.net = keep;
  for (const l of p.layout?.labels ?? []) if (l.net === drop) l.net = keep;
  return keep;
}

function removeTerminal(p: Project, terminal: string): void {
  for (const [net, pins] of Object.entries(p.nets)) {
    if (!pins.includes(terminal)) continue;
    p.nets[net] = pins.filter((t) => t !== terminal);
    if (p.nets[net]!.length === 0) delete p.nets[net];
  }
}

/** the net of a terminal, through its strip when it is a hole */
function stripNet(p: Project, model: BenchModel, terminal: string): string | undefined {
  const own = netOf(p, terminal);
  if (own) return own;
  const key = model.strip.get(terminal);
  if (!key) return undefined;
  for (const [hole, k] of model.strip) if (k === key) {
    const n = netOf(p, hole);
    if (n) return n;
  }
  return undefined;
}

/** joins terminals into one net, creating it when none of them has one */
function join(p: Project, model: BenchModel, terminals: string[]): string {
  let net: string | undefined;
  for (const t of terminals) {
    const n = stripNet(p, model, t);
    if (n) net = net ? merge(p, net, n) : n;
  }
  if (!net) {
    net = freshNet(p);
    p.nets[net] = [];
  }
  for (const t of terminals) {
    const n = netOf(p, t);
    if (n && n !== net) {
      // a terminal still in another net (its strip had none): bring that net along
      net = merge(p, net, n);
    } else if (!n) p.nets[net]!.push(t);
  }
  return net;
}

/** the terminals the bench uses: holes with a leg in them or a jumper's end, and both ends of every jumper */
function benchTerminals(p: Project, model: BenchModel): Set<string> {
  const used = new Set<string>();
  for (const [pin, hole] of model.plugs) {
    used.add(pin);
    used.add(hole);
  }
  for (const w of p.layout?.bench?.wires ?? []) {
    used.add(w.from);
    used.add(w.to);
  }
  return used;
}

/**
 * Splits the nets the bench alone made where nothing on the bench joins
 * them any more. A net with a terminal the bench does not explain (the
 * schematic drew it) is left whole.
 */
function settle(p: Project, model: BenchModel): void {
  const used = benchTerminals(p, model);
  const parent = new Map<string, string>();
  const find = (x: string): string => {
    let r = x;
    while (parent.get(r) !== r) r = parent.get(r)!;
    parent.set(x, r);
    return r;
  };
  const unite = (a: string, b: string) => parent.set(find(a), find(b));
  for (const pins of Object.values(p.nets)) for (const t of pins) parent.set(t, t);
  for (const [pin, hole] of model.plugs) if (parent.has(pin) && parent.has(hole)) unite(pin, hole);
  for (const w of p.layout?.bench?.wires ?? []) if (parent.has(w.from) && parent.has(w.to)) unite(w.from, w.to);
  const byStrip = new Map<string, string>();
  for (const pins of Object.values(p.nets))
    for (const t of pins) {
      const key = model.strip.get(t);
      if (!key) continue;
      const first = byStrip.get(key);
      if (first) unite(first, t);
      else byStrip.set(key, t);
    }
  for (const [net, pins] of Object.entries({ ...p.nets })) {
    if (pins.length < 2 || !pins.every((t) => used.has(t))) continue;
    const groups = new Map<string, string[]>();
    for (const t of pins) groups.set(find(t), [...(groups.get(find(t)) ?? []), t]);
    if (groups.size < 2) continue;
    const sorted = [...groups.values()].sort((a, b) => b.length - a.length);
    p.nets[net] = sorted[0]!;
    for (const g of sorted.slice(1)) {
      if (g.length < 2) continue; // a lone terminal is simply unconnected
      p.nets[freshNet(p)] = g;
    }
  }
  // holes nothing uses any more leave their nets
  for (const [net, pins] of Object.entries({ ...p.nets })) {
    const kept = pins.filter((t) => !model.holes.has(t) || used.has(t));
    if (kept.length !== pins.length) {
      if (kept.length) p.nets[net] = kept;
      else delete p.nets[net];
    }
  }
  for (const [net, pins] of Object.entries({ ...p.nets })) if (pins.length === 1 && model.holes.has(pins[0]!)) delete p.nets[net];
}

function bench(p: Project): BenchLayout {
  p.layout ??= {};
  p.layout.bench ??= {};
  return p.layout.bench;
}

export interface BenchContext {
  shapes: Shapes;
  ties: Ties;
}

/** Moving a breadboard carries inserted parts through the same rigid transform.
 * Explicit moves take precedence over inherited moves in a batch. */
export function carriedMoves(model: BenchModel, moves: Record<string, BenchPlace>): Record<string, BenchPlace> {
  const all = { ...moves };
  // what rides on a moved breadboard
  for (const [id, to] of Object.entries(moves)) {
    const from = model.byId.get(id);
    if (!from || from.shape.package !== 'breadboard') continue;
    const rotation = ((to.rotation ?? from.place.rotation) - from.place.rotation + 360) % 360;
    const riders = new Set<string>();
    for (const [pin, hole] of model.plugs) if (hole.startsWith(`${id}.`)) riders.add(pin.split('.')[0]!);
    for (const r of riders) {
      if (all[r]) continue;
      const at = model.byId.get(r)!.place;
      const [dx, dy] = turn(at.x - from.place.x, at.y - from.place.y, rotation);
      all[r] = { x: to.x + dx, y: to.y + dy, rotation: ((at.rotation + rotation) % 360) as 0 | 90 | 180 | 270 };
    }
  }
  return all;
}

/**
 * Moves (and turns) parts to new places. Legs that leave their holes come
 * out of the strips' nets; legs that land on holes are plugged in. Moving a
 * breadboard carries what is plugged into it.
 */
export function moveParts(p: Project, ctx: BenchContext, moves: Record<string, BenchPlace>): Project {
  const before = benchModel(p, ctx.shapes, ctx.ties);
  const parts = { ...(p.layout?.bench?.parts ?? {}) };
  const all = carriedMoves(before, moves);
  // every part on the bench keeps a place once one moves
  for (const part of before.parts) if (!parts[part.id]) parts[part.id] = { ...part.place };
  for (const [id, to] of Object.entries(all)) parts[id] = { x: round(to.x), y: round(to.y), rotation: to.rotation ?? parts[id]?.rotation ?? 0 };
  const candidate: Project = { ...p, layout: { ...p.layout, bench: { ...p.layout?.bench, parts } } };
  const after = benchModel(candidate, ctx.shapes, ctx.ties);
  // Validate before mutating the caller's project, including every leg moved
  // indirectly by a breadboard. An imported old conflict is not a new edit.
  for (const [pin, hole] of after.plugs) {
    if (before.plugs.get(pin) === hole) continue;
    const others = (after.occupied.get(hole) ?? []).filter((owner) => owner !== pin);
    if (others.length) throw new Error(`${hole} is occupied by ${others.join(', ')}; ${pin} cannot be inserted`);
  }
  bench(p).parts = parts;
  // unplug what left its hole
  for (const [pin, hole] of before.plugs) {
    if (after.plugs.get(pin) === hole) continue;
    removeTerminal(p, pin);
  }
  // plug in what landed on a hole
  for (const [pin, hole] of after.plugs) {
    if (before.plugs.get(pin) === hole) continue;
    removeTerminal(p, pin);
    join(p, after, [hole, pin]);
  }
  settle(p, after);
  return p;
}

function round(v: number): number {
  return Math.round(v * 100) / 100;
}

/** a jumper from one terminal to another: their nets become one */
export function addJumper(p: Project, ctx: BenchContext, wire: BenchWire): Project {
  const model = benchModel(p, ctx.shapes, ctx.ties);
  for (const ref of [wire.from, wire.to]) {
    const [id, ...pin] = ref.split('.');
    if (!model.holes.has(ref) && !model.byId.get(id ?? '')?.legs[pin.join('.')])
      throw new Error(`Unknown bench terminal ${ref}`);
  }
  if (wire.from === wire.to) return p;
  if (p.layout?.bench?.wires?.some((w) => (w.from === wire.from && w.to === wire.to) || (w.from === wire.to && w.to === wire.from))) return p;
  for (const ref of [wire.from, wire.to]) {
    const occupied = model.holes.has(ref) ? model.occupied.get(ref) : undefined;
    if (occupied?.length) throw new Error(`${ref} is occupied by ${occupied.join(', ')}; use another hole on the strip`);
  }
  const b = bench(p);
  b.wires = [...(b.wires ?? []), wire];
  join(p, model, [wire.from, wire.to]);
  return p;
}

/** takes jumper `index` out; what it alone joined comes apart */
export function removeJumper(p: Project, ctx: BenchContext, index: number): Project {
  const b = bench(p);
  const wires = b.wires ?? [];
  const gone = wires[index];
  if (!gone) return p;
  b.wires = wires.filter((_, i) => i !== index);
  const model = benchModel(p, ctx.shapes, ctx.ties);
  // an end that is a part's pin and that nothing else on the bench joins leaves the net
  const used = benchTerminals(p, model);
  for (const end of [gone.from, gone.to]) if (!model.holes.has(end) && !used.has(end)) {
    const net = netOf(p, end);
    if (net && p.nets[net]!.every((t) => t === end || used.has(t) || model.holes.has(t))) removeTerminal(p, end);
  }
  settle(p, model);
  return p;
}

export interface Connection {
  net: string;
  from: string;
  to: string;
  a: [number, number];
  b: [number, number];
}

/**
 * The connections the nets ask for that nothing on the bench makes yet: per
 * net, the groups the bench joins (strips, plugs, jumpers), and between
 * them the shortest links, drawn as wires still to be built.
 */
export function unbuilt(p: Project, model: BenchModel): Connection[] {
  const where = (t: string): [number, number] | undefined => {
    const hole = model.holes.get(t);
    if (hole) return [hole.x, hole.y];
    const [id, ...rest] = t.split('.');
    return model.byId.get(id ?? '')?.legs[rest.join('.')];
  };
  const out: Connection[] = [];
  for (const [net, pins] of Object.entries(p.nets)) {
    const shown = pins.filter((t) => where(t));
    if (shown.length < 2) continue;
    const parent = new Map(shown.map((t) => [t, t]));
    const find = (x: string): string => {
      let r = x;
      while (parent.get(r) !== r) r = parent.get(r)!;
      return r;
    };
    const unite = (a: string, b: string) => {
      if (parent.has(a) && parent.has(b)) parent.set(find(a), find(b));
    };
    for (const [pin, hole] of model.plugs) unite(pin, hole);
    for (const w of p.layout?.bench?.wires ?? []) unite(w.from, w.to);
    const strips = new Map<string, string>();
    for (const t of shown) {
      const key = model.strip.get(t);
      if (!key) continue;
      const first = strips.get(key);
      if (first) unite(first, t);
      else strips.set(key, t);
    }
    // Prim's algorithm over the groups: link the nearest pair until one remains
    const groups = new Map<string, string[]>();
    for (const t of shown) groups.set(find(t), [...(groups.get(find(t)) ?? []), t]);
    if (groups.size < 2) continue;
    const list = [...groups.values()];
    const joined = [list[0]!];
    const rest = list.slice(1);
    while (rest.length) {
      let best = { d: Infinity, i: 0, from: '', to: '' };
      rest.forEach((g, i) => {
        for (const t of g)
          for (const j of joined)
            for (const u of j) {
              const a = where(t)!;
              const b = where(u)!;
              const d = Math.hypot(a[0] - b[0], a[1] - b[1]);
              if (d < best.d) best = { d, i, from: u, to: t };
            }
      });
      out.push({ net, from: best.from, to: best.to, a: where(best.from)!, b: where(best.to)! });
      joined.push(rest[best.i]!);
      rest.splice(best.i, 1);
    }
  }
  return out;
}

/** a new note on the bench */
export function addNote(p: Project, x: number, y: number, text: string, to?: [number, number]): Project {
  const b = bench(p);
  b.notes = [...(b.notes ?? []), to ? { x, y, text, to } : { x, y, text }];
  return p;
}

export function setNote(p: Project, index: number, note: Partial<{ x: number; y: number; text: string; to: [number, number] }>): Project {
  const b = bench(p);
  const notes = [...(b.notes ?? [])];
  if (!notes[index]) return p;
  notes[index] = { ...notes[index]!, ...note };
  if (note.text === '') notes.splice(index, 1);
  b.notes = notes;
  return p;
}
