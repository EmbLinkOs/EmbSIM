// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// From the project and the catalog to what the 3D bench draws: each part's
// shape, the surface it stands on, where each terminal is in 3D, where a
// dragged part's legs snap, the jumpers and the connections still to build.

import type { ComponentDescription, Project } from '@embsim/protocol';
import { carriedMoves, legsOf, previewInsertion, unbuilt, type BenchContext, type BenchModel } from './layout';
import { isSurfaceMount, PITCH, benchShape, boardStack, surfaceLeadThickness, type BenchShape } from './shape';
import type { JumperView, LinkView, Point3, SnapResult, TerminalHit } from './Bench3D';

export interface BenchCatalogEntry {
  description: ComponentDescription & { kind?: string; ports?: Record<string, unknown> };
  artwork?: string;
}

/** the shapes and ties of every instance of the project */
export function benchContext(p: Project, entry: (ref: string) => BenchCatalogEntry | undefined): BenchContext {
  const shapes: Record<string, BenchShape> = {};
  const ties: Record<string, string[][]> = {};
  for (const b of p.boards ?? []) {
    const e = entry(b.board);
    if (e) shapes[b.id] = benchShape({ ...e.description, kind: 'board' }, undefined, e.artwork);
  }
  for (const c of p.components) {
    const e = entry(c.component);
    if (!e) continue;
    shapes[c.id] = benchShape(e.description, c.params, e.artwork);
    if (e.description.ties) ties[c.id] = e.description.ties;
  }
  return { shapes, ties };
}

/** the breadboard a part's legs are plugged into, if any */
function hostOf(model: BenchModel, id: string): string | undefined {
  for (const [pin, hole] of model.plugs) if (pin.startsWith(`${id}.`)) return hole.split('.')[0];
  return undefined;
}

/** the height of what a part stands on: a breadboard's top, or the desk */
export function elevationOf(model: BenchModel, id: string): number {
  const host = hostOf(model, id);
  return host ? model.byId.get(host)?.shape.size[2] ?? 0 : 0;
}

/** where a jumper's end meets a terminal */
export function terminalPoint(model: BenchModel, ref: string): Point3 | undefined {
  const hole = model.holes.get(ref);
  if (hole) {
    const host = model.byId.get(ref.split('.')[0]!)!;
    return [hole.x, host.shape.size[2], hole.y];
  }
  const plugged = model.plugs.get(ref);
  if (plugged) return terminalPoint(model, plugged);
  const [id, ...rest] = ref.split('.');
  const part = model.byId.get(id ?? '');
  const leg = part?.legs[rest.join('.')];
  if (!part || !leg) return undefined;
  const top = part.shape.package === 'board' ? boardStack(part.shape).contact : part.shape.package === 'module' ? 6 : part.shape.package === 'box' ? part.shape.size[2] + 4 : isSurfaceMount(part.shape) ? surfaceLeadThickness(part.shape) : 1;
  return [leg[0], elevationOf(model, part.id) + top, leg[1]];
}

/** Physical contacts for camera-space picking. A plugged leg shares its
 * breadboard hole; exposing both would make the same contact ambiguous. */
export function terminalTargets(model: BenchModel): TerminalHit[] {
  const refs = new Set(model.holes.keys());
  for (const part of model.parts) {
    if (part.shape.package === 'breadboard') continue;
    for (const pin of Object.keys(part.legs)) {
      const ref = `${part.id}.${pin}`;
      if (!model.plugs.has(ref)) refs.add(ref);
    }
  }
  return [...refs].flatMap((ref) => {
    const at = terminalPoint(model, ref);
    return at ? [{ ref, at, occupiedBy: model.occupied.get(ref) }] : [];
  });
}

/** the terminal nearest (x, y): a hole within half a pitch, else a pin of `part` (or any) within 3 mm */
export function terminalNear(model: BenchModel, x: number, y: number, part?: string): TerminalHit | undefined {
  let best: { ref: string; d: number } | undefined;
  const consider = (ref: string, px: number, py: number, limit: number) => {
    const d = Math.hypot(px - x, py - y);
    if (d <= limit && (!best || d < best.d)) best = { ref, d };
  };
  for (const h of model.holes.values()) consider(h.ref, h.x, h.y, PITCH / 2);
  if (!best)
    for (const p of model.parts) {
      if (p.shape.package === 'breadboard' || (part && p.id !== part && model.byId.get(part)?.shape.package !== 'breadboard')) continue;
      for (const [pin, [px, py]] of Object.entries(p.legs)) consider(`${p.id}.${pin}`, px, py, 3);
    }
  if (!best) return undefined;
  const at = terminalPoint(model, best.ref);
  return at ? { ref: best.ref, at } : undefined;
}

/**
 * Where a part dragged to (x, y) lands: when one of its legs comes within
 * half a pitch of a hole, the part shifts so that leg is in it; the holes its
 * legs then sit in are the ones it will plug into.
 */
export function snapPart(model: BenchModel, ctx: BenchContext, id: string, x: number, y: number, rotation: number): SnapResult {
  const shape = ctx.shapes[id];
  const free = { x, y, holes: [] as Point3[], elevation: 0 };
  if (!shape || shape.package === 'board' || isSurfaceMount(shape)) return free;
  if (shape.package === 'breadboard') {
    const moves = carriedMoves(model, { [id]: { x, y, rotation: rotation as 0 | 90 | 180 | 270 } });
    return { ...free, carried: Object.entries(moves).filter(([key]) => key !== id).map(([key, place]) => ({ id: key, x: place.x, y: place.y, rotation: place.rotation ?? model.byId.get(key)!.place.rotation })) };
  }
  const holes = [...model.holes.values()].filter((h) => !h.ref.startsWith(`${id}.`));
  if (!holes.length) return free;
  const legs = Object.values(legsOf(shape, { x, y, rotation: rotation as 0 | 90 | 180 | 270 }));
  let best: { dx: number; dy: number; d: number } | undefined;
  for (const [lx, ly] of legs)
    for (const h of holes) {
      const d = Math.hypot(h.x - lx, h.y - ly);
      if (d < PITCH / 2 + 0.01 && (!best || d < best.d)) best = { dx: h.x - lx, dy: h.y - ly, d };
    }
  if (!best) return free;
  const sx = x + best.dx;
  const sy = y + best.dy;
  // Preview the same rounded placement the transaction will receive.
  const place = { x: Math.round(sx * 100) / 100, y: Math.round(sy * 100) / 100, rotation: rotation as 0 | 90 | 180 | 270 };
  const insertion = previewInsertion(model, id, place);
  const landed: Point3[] = insertion.contacts.filter((contact) => contact.hole).map((contact) => [contact.at[0], model.byId.get(contact.hole!.split('.')[0]!)!.shape.size[2], contact.at[1]]);
  return { ...place, holes: landed, elevation: insertion.elevation, insertion };
}

export const WIRE_COLORS = ['#e74c3c', '#2c3e50', '#f39c12', '#27ae60', '#2e86de', '#8e44ad', '#f1c40f', '#ecf0f1', '#e67e22', '#16a085'];

export function jumperViews(p: Project, model: BenchModel): JumperView[] {
  const out: JumperView[] = [];
  (p.layout?.bench?.wires ?? []).forEach((w, index) => {
    const a = terminalPoint(model, w.from);
    const b = terminalPoint(model, w.to);
    if (!a || !b) return;
    const onHeader = (ref: string) => !model.holes.has(ref) && !model.plugs.has(ref);
    out.push({ index, a, b, color: w.color ?? WIRE_COLORS[index % WIRE_COLORS.length]!, housings: [onHeader(w.from), onHeader(w.to)] });
  });
  return out;
}

/** a net's colour as a bench is wired: black ground, red supplies, the rest from the palette */
export function netColor(net: string, index: number): string {
  if (/^(gnd|vss|ground|agnd)/i.test(net)) return '#2d3436';
  if (/^(vcc|vdd|5v|3v3|vin|v\+|\+)/i.test(net)) return '#e74c3c';
  return WIRE_COLORS[2 + (index % (WIRE_COLORS.length - 2))]!;
}

export function linkViews(p: Project, model: BenchModel): LinkView[] {
  const nets = Object.keys(p.nets);
  return unbuilt(p, model).flatMap((c) => {
    const a = terminalPoint(model, c.from);
    const b = terminalPoint(model, c.to);
    return a && b ? [{ a, b, color: netColor(c.net, nets.indexOf(c.net)) }] : [];
  });
}
