// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
// Drawing/structure checks, not electrical-rule or simulator validation.
import type { Project } from '@embsim/protocol';
import { layoutParts } from './autoLayout';
import { distanceToSegment, findAirwires, pinPosition, rectsIntersect, symbolBounds, wireSegments, type Point, type Rect } from './geometry';
import { projectPinPoints, segmentContacts } from './wireEditing';
import type { SceneCatalog } from './SchematicCanvas';

export type CheckSeverity = 'error' | 'warning' | 'info';
export interface SchematicIssue {
  id: string;
  code: string;
  severity: CheckSeverity;
  title: string;
  detail: string;
  components: string[];
  wires: number[];
  net?: string;
  point?: Point;
}

function crossesBounds(a: Point, b: Point, r: Rect) {
  if (a.y === b.y) return a.y > r.y && a.y < r.y + r.height && Math.min(Math.max(a.x, b.x), r.x + r.width) > Math.max(Math.min(a.x, b.x), r.x);
  if (a.x === b.x) return a.x > r.x && a.x < r.x + r.width && Math.min(Math.max(a.y, b.y), r.y + r.height) > Math.max(Math.min(a.y, b.y), r.y);
  return false;
}

export function checkSchematic(project: Project, catalog: SceneCatalog): SchematicIssue[] {
  const issues: SchematicIssue[] = [];
  const add = (code: string, key: string, severity: CheckSeverity, title: string, detail: string, target: Partial<SchematicIssue> = {}) => {
    issues.push({ id: `${code}:${key}`, code, severity, title, detail, components: [], wires: [], ...target });
  };
  const parts = layoutParts(project, catalog);
  const byId = new Map(parts.map((p) => [p.id, p]));
  const pinsByNet = projectPinPoints(project, catalog);
  const pinNets = new Map<string, string[]>();
  const wires = project.layout?.wires ?? [];
  // a wiring aid's terminals (a breadboard's holes) are not drawn: the bench checks them
  const aids = new Set(project.components.filter((c) => catalog[c.component]?.wiringAid).map((c) => c.id));
  for (const [net, pins] of Object.entries(project.nets)) {
    if (pins.length < 2) add('small-net', net, 'warning', `Net ${net} has ${pins.length} pins`, 'A net with fewer than two pins cannot connect different terminals.', { net, components: pins.map((p) => p.split('.')[0]!) });
    for (const pin of pins) {
      const list = pinNets.get(pin) ?? [];
      list.push(net); pinNets.set(pin, list);
      const dot = pin.indexOf('.');
      if (aids.has(pin.slice(0, dot))) continue;
      const part = byId.get(pin.slice(0, dot));
      if (dot < 1 || !part || (catalog[part.ref] && !(catalog[part.ref]!.terminals ?? Object.keys(part.symbol.pins)).includes(pin.slice(dot + 1)))) {
        add('invalid-pin', `${net}:${pin}`, 'error', `Unknown terminal ${pin}`, `Net ${net} references a terminal absent from the loaded board/component description.`, { net, components: part ? [part.id] : [] });
      } else if (part && catalog[part.ref] && !part.symbol.pins[pin.slice(dot + 1)]) {
        add('undrawn-terminal', `${net}:${pin}`, 'info', `${pin} has no symbol anchor`, 'The terminal exists in the component description, but routing and visual connectivity checks cannot locate it on this symbol.', { net, components: [part.id] });
      }
    }
    const missing = findAirwires(pinsByNet[net] ?? [], wires.filter((w) => w.net === net), (project.layout?.labels ?? []).filter((l) => l.net === net));
    if (missing.length) add('undrawn-net', net, 'info', `${net}: ${missing.length} undrawn link${missing.length === 1 ? '' : 's'}`, 'The net is electrically connected, but its drawn geometry has separated pin groups. Route this net or draw the missing links.', { net, point: missing[0]!.a, components: [...new Set(pins.map((p) => p.split('.')[0]!))], wires: wires.flatMap((w, i) => w.net === net ? [i] : []) });
  }
  for (const [pin, nets] of pinNets) if (nets.length > 1) add('duplicate-terminal', pin, 'error', `${pin} appears more than once`, `Memberships: ${nets.join(', ')}. A terminal must belong to one net once.`, { components: [pin.split('.')[0]!] });
  const usedIds = new Set<string>();
  for (const part of parts) {
    if (usedIds.has(part.id)) add('duplicate-id', part.id, 'error', `Duplicate reference ${part.id}`, 'Each board and component needs a unique reference.', { components: [part.id] });
    usedIds.add(part.id);
    if (!catalog[part.ref]) {
      add('missing-symbol', part.id, 'warning', `Description unavailable: ${part.id}`, `Cannot verify terminals or symbol geometry for ${part.ref}.`, { components: [part.id] });
      continue;
    }
    const unconnected = (catalog[part.ref]!.terminals ?? Object.keys(part.symbol.pins)).filter((pin) => !pinNets.has(`${part.id}.${pin}`));
    const inputs = catalog[part.ref]!.pins?.filter((pin) => pin.model?.kind === 'digital_in' && unconnected.includes(pin.name) && !['up', 'down'].includes(String(pin.model?.pull))) ?? [];
    if (inputs.length) add('unconnected-input', part.id, 'warning', `${part.id}: ${inputs.length} unconnected digital input${inputs.length === 1 ? '' : 's'}`, `${inputs.map((p) => p.name).join(', ')} has no declared connection or input pull. Wire unused inputs to a valid level according to the part's datasheet.`, { components: [part.id], point: pinPosition(part.layout, part.symbol, inputs[0]!.name) ?? undefined });
    if (unconnected.length) add('unconnected-pins', part.id, 'info', `${part.id}: ${unconnected.length} unconnected terminal${unconnected.length === 1 ? '' : 's'}`, `${unconnected.join(', ')}. Unused pins may be intentional; this is not an electrical fault diagnosis.`, { components: [part.id], point: pinPosition(part.layout, part.symbol, unconnected[0]!) ?? undefined });
    if (part.layout.x % 10 !== 0 || part.layout.y % 10 !== 0) add('off-grid', part.id, 'info', `${part.id} is off the editing grid`, 'Symbol origin is not a multiple of 10 sheet units. Exact placement snaps origins to the editing grid.', { components: [part.id] });
  }
  const bounds = parts.filter((p) => catalog[p.ref]).map((p) => ({ id: p.id, bounds: symbolBounds(p.layout, p.symbol) })).sort((a, b) => a.bounds.x - b.bounds.x);
  for (let i = 0; i < bounds.length; i++) {
    const a = bounds[i]!;
    for (let j = i + 1; j < bounds.length; j++) {
      const b = bounds[j]!;
      if (b.bounds.x >= a.bounds.x + a.bounds.width) break;
      if (rectsIntersect(a.bounds, b.bounds)) add('overlap', `${a.id}:${b.id}`, 'warning', `${a.id} overlaps ${b.id}`, 'Symbol bounding rectangles overlap. This includes symbol whitespace; review the drawing before rearranging.', { components: [a.id, b.id] });
    }
  }
  const segments: { a: Point; b: Point; wire: number; net: string; minX: number; maxX: number }[] = [];
  wires.forEach((wire, index) => {
    if (!Object.hasOwn(project.nets, wire.net)) add('orphan-wire', String(index), 'error', `Wire ${index + 1} names an absent net`, `${wire.net} is not declared in the project's nets.`, { wires: [index] });
    const invalid = wire.points.length < 2 || wire.points.some((p) => !p.every(Number.isFinite));
    const edges = invalid ? [] : wireSegments(wire);
    if (invalid || edges.some(([a, b]) => a.x !== b.x && a.y !== b.y)) add('invalid-wire', String(index), 'warning', `Wire ${index + 1} has invalid drafting geometry`, 'Expected finite coordinates and horizontal/vertical segments. Automatic routing can replace this geometry.', { wires: [index], net: wire.net });
    for (const part of bounds) if (edges.some(([a, b]) => crossesBounds(a, b, part.bounds))) add('wire-through-symbol', `${index}:${part.id}`, 'warning', `${wire.net} crosses ${part.id}'s bounds`, 'A wire passes through the symbol bounding rectangle. This is a visual check, not a physical clearance rule.', { wires: [index], net: wire.net, components: [part.id] });
    for (const [a, b] of edges) if (a.x === b.x || a.y === b.y) segments.push({ a, b, wire: index, net: wire.net, minX: Math.min(a.x, b.x), maxX: Math.max(a.x, b.x) });
  });
  segments.sort((a, b) => a.minX - b.minX);
  const reported = new Set<string>();
  for (let i = 0; i < segments.length; i++) {
    const a = segments[i]!;
    for (let j = i + 1; j < segments.length; j++) {
      const b = segments[j]!;
      if (b.minX > a.maxX) break;
      if (a.net === b.net || a.wire === b.wire) continue;
      const key = [a.wire, b.wire].sort((x, y) => x - y).join(':');
      if (reported.has(key)) continue;
      const contact = segmentContacts(a.a, a.b, b.a, b.b).find((p) => {
        const endpoint = [a.a, a.b, b.a, b.b].some((q) => Math.hypot(q.x - p.x, q.y - p.y) < .01);
        return endpoint || (a.a.x === a.b.x && b.a.x === b.b.x) || (a.a.y === a.b.y && b.a.y === b.b.y);
      });
      if (contact) {
        reported.add(key);
        add('ambiguous-contact', key, 'warning', `${a.net} and ${b.net} touch visually`, 'These are different electrical nets. An endpoint touch or overlapping track can imply a connection that does not exist. Ordinary perpendicular crossings are allowed.', { wires: [a.wire, b.wire], point: contact });
      }
    }
  }
  // A pin touching foreign geometry is equally ambiguous even without its own wire.
  for (const [net, pins] of Object.entries(pinsByNet)) pins.forEach((point, n) => {
    const wireIndex = wires.findIndex((w) => w.net !== net && wireSegments(w).some(([a, b]) => distanceToSegment(point, a, b) < .01));
    if (wireIndex >= 0) add('pin-foreign-wire', `${net}:${n}`, 'warning', `A ${net} terminal touches another net's wire`, `The terminal is not electrically connected to ${wires[wireIndex]!.net}. Drawing contact alone does not join nets.`, { net, point, wires: [wireIndex] });
  });
  const rank = { error: 0, warning: 1, info: 2 };
  return issues.sort((a, b) => rank[a.severity] - rank[b.severity] || (a.id < b.id ? -1 : a.id > b.id ? 1 : 0));
}
