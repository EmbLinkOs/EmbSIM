// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The project store: the canonical project JSON mirrored from project.get,
// the catalog entries it needs, and the editing commands. Each edit is a
// pure function on the project; its effect reaches the server as a
// transaction (see projectDiff.ts) and comes back through refresh().

import { CommandStack, shiftWireSegment, projectPinPoints, GRID, boardSymbol, boardRuntimeHeader, freePlacement, autoRoute, createLayout, pinPosition, rotateLayouts, snap, type BoardPort, type NetRef, type PinRef, type SceneCatalog, type Selection } from '@embsim/scene';
import { blockPortOrder, JsonRpcCode, JsonRpcError, type CircuitBlock, type HierarchyNode, type CatalogItem, type ComponentDescription, type Layout, type Project, type ProjectBoard, type ProjectComponent, type ProjectEdit, type Topology, type Wire } from '@embsim/protocol';
import { create } from 'zustand';
import { getClient, primeStores } from '../connection';
import { useInstruments } from './instruments';
import { diffProject } from './projectDiff';

export interface CatalogEntry {
  description: ComponentDescription;
  files: { symbol?: string; artwork?: string };
}

const VALUE_PARAM: Record<string, string> = {
  'emblink/resistor': 'resistance',
  'emblink/capacitor': 'capacitance',
  'emblink/vcc': 'voltage',
  'emblink/vsource': 'dc',
  'emblink/isource': 'dc',
  'emblink/function_generator': 'dc',
  'emblink/voltmeter': 'input_resistance',
  'emblink/ammeter': 'shunt_resistance',
  'emblink/scope_input': 'input_resistance',
  'emblink/led': 'color',
  'emblink/bjt_npn': 'part',
};

const PREFIX: Record<string, string> = {
  resistor: 'R',
  capacitor: 'C',
  inductor: 'L',
  led: 'D',
  diode: 'D',
  bjt_npn: 'Q',
  bjt_pnp: 'Q',
  mosfet: 'Q',
  push_button: 'SW',
  switch: 'SW',
  vcc: 'VCC',
  gnd: 'GND',
  breadboard_830: 'BB',
  breadboard_400: 'BB',
  breadboard_170: 'BB',
};

export interface ProjectState {
  project: Project | null;
  items: CatalogItem[];
  catalog: Record<string, CatalogEntry>;
  sceneCatalog: SceneCatalog;
  topology: Topology | null;
  hierarchy: HierarchyNode[];
  commands: CommandStack;
  busy: boolean;
  lastError: string | null;
  /** False once the server answered project.transaction with -32601 (method not available). */
  editable: boolean;
  loadAll(): Promise<void>;
  refresh(): Promise<void>;
  ensureCatalog(ids: string[]): Promise<void>;
  /** Runs an edit as an undoable command; `fn` returns the next project or null for no-op. */
  mutate(label: string, fn: (p: Project) => Project | null): Promise<void>;
  placeComponent(componentId: string, at: { x: number; y: number }): Promise<string | null>;
  setPlacementLocked(ids: string[], locked: boolean): Promise<void>;
  moveComponents(ids: string[], dx: number, dy: number, reroute?: boolean): Promise<void>;
  reshapeWire(index: number, segment: number, offset: number, source: Project): Promise<void>;
  setPartLayout(id: string, layout: Layout, source: Project): Promise<void>;
  setBatchParams(ids: string[], values: Record<string, string | number | boolean>, source: Project): Promise<void>;
  eraseWireGeometry(indices: number[]): Promise<void>;
  disconnectPin(pin: string): Promise<void>;
  rerouteNets(nets: string[]): Promise<string[]>;
  rotateComponents(ids: string[]): Promise<void>;
  mirrorComponents(ids: string[]): Promise<void>;
  deleteSelection(selection: Selection): Promise<void>;
  wire(from: PinRef, to: PinRef | NetRef, points: [number, number][], autoArrange?: boolean): Promise<void>;
  setParam(id: string, param: string, value: unknown): Promise<void>;
  renameNet(from: string, to: string): Promise<void>;
  /** A label naming the net of wire `index`, at the middle of its first segment. */
  labelWire(index: number): Promise<void>;
  /** A bus of the selected wires' nets, drawn beside them; its name. */
  groupIntoBus(wires: number[]): Promise<string | null>;
  removeBus(name: string): Promise<void>;
  loadProjectJson(json: Project): Promise<void>;
  nextId(componentId: string): string;
  /** Parts copied with the nets and wires wholly between them. */
  clipboard: Clip | null;
  copySelection(selection: Selection): void;
  /** Pastes the clipboard, offset from where it was copied; the new parts' ids. */
  paste(at?: { x: number; y: number }): Promise<string[]>;
  duplicate(selection: Selection): Promise<string[]>;
}

export interface Clip {
  components: ProjectComponent[];
  groups?: NonNullable<Project['layout']>['groups'];
  nets: Record<string, string[]>;
  wires: Wire[];
  /** How many times it was pasted: each paste lands further off. */
  pastes: number;
}

/** The selected parts, and the nets and wires that lie wholly between them. */
export function clipOf(p: Project, selection: Selection): Clip | null {
  const ids = new Set(selection.components);
  const components = p.components.filter((c) => ids.has(c.id));
  if (components.length === 0) return null;
  const inside = (pin: string) => ids.has(pin.split('.')[0] ?? '');
  const nets: Record<string, string[]> = {};
  for (const [net, pins] of Object.entries(p.nets)) {
    const own = pins.filter(inside);
    // a net that reaches outside the selection keeps only the copied pins
    // when two or more of them share it
    if (own.length >= 2) nets[net] = own;
  }
  const wires = (p.layout?.wires ?? []).filter((w) => w.net in nets && Object.values(p.nets[w.net] ?? []).every(inside));
  return structuredClone({ components, nets, wires, groups: (p.layout?.groups ?? []).filter((g) => g.members.every((id) => components.some((c) => c.id === id))), pastes: 0 });
}

/** The project with `clip` added at (dx, dy), its parts and nets renamed. */
export function withClip(p: Project, clip: Clip, dx: number, dy: number, nextId: (componentId: string, used: Set<string>) => string): { project: Project; ids: string[] } {
  const used = new Set(placeables(p).map((c) => c.id));
  const rename = new Map<string, string>();
  for (const c of clip.components) {
    const id = nextId(c.component, used);
    used.add(id);
    rename.set(c.id, id);
    const l = c.layout ?? { x: 0, y: 0 };
    p.components.push({ ...structuredClone(c), id, layout: { ...l, x: l.x + dx, y: l.y + dy, locked: false } });
  }
  const pin = (x: string) => {
    const [id, ...rest] = x.split('.');
    return `${rename.get(id ?? '') ?? id}.${rest.join('.')}`;
  };
  const netName = new Map<string, string>();
  let n = 1;
  for (const [net, pins] of Object.entries(clip.nets)) {
    while (`N${n}` in p.nets) n++;
    netName.set(net, `N${n}`);
    p.nets[`N${n}`] = pins.map(pin);
  }
  const wires = clip.wires.map((w) => ({ net: netName.get(w.net) ?? w.net, points: w.points.map(([x, y]) => [x + dx, y + dy] as [number, number]) }));
  if (wires.length) p.layout = { ...(p.layout ?? {}), wires: [...(p.layout?.wires ?? []), ...wires] };
  for (const group of clip.groups ?? []) {
    const groups = p.layout?.groups ?? [];
    let n = 1; while (groups.some((g) => g.id === `group-${n}`)) n++;
    p.layout = { ...p.layout, groups: [...groups, { ...group, id: `group-${n}`, name: `${group.name} copy`, members: group.members.map((id) => rename.get(id)!) }] };
  }
  return { project: p, ids: [...rename.values()] };
}

function clone<T>(v: T): T {
  return structuredClone(v);
}

function sceneEntry(entry: CatalogEntry, block?: CircuitBlock): SceneCatalog[string] {
  const d = entry.description as ComponentDescription & { kind?: string; ports?: Record<string, BoardPort>; artwork?: { hotspots?: { component?: string; input?: string; output?: string; selector?: string }[] } };
  if (d.id?.startsWith('block/')) {
    const names = block ? blockPortOrder(block) : (d.pins ?? []).map((p)=>p.name).sort();
    const pins = names.map((name)=>d.pins.find((p)=>p.name===name)).filter((p): p is NonNullable<typeof p>=>Boolean(p));
    const { symbol, svg } = boardSymbol(d.name, Object.fromEntries(pins.map((p, i) => [p.name, { connector: i % 2 === 0 ? 'L' : 'R', pin: Math.floor(i / 2) }])));
    entry.description = { ...d, symbol };
    return { name: d.name, symbol, svg };
  }
  if (d.kind === 'board') {
    // a board has no drawn symbol: a block of its ports stands for it
    const { symbol, svg } = boardRuntimeHeader(boardSymbol(d.name, d.ports ?? {}), d.artwork?.hotspots ?? []);
    // its description gains the symbol so pin geometry works as for parts
    entry.description = { ...d, symbol };
    return { name: d.name, symbol, svg, terminals: Object.keys(symbol.pins) };
  }
  const valueParam = VALUE_PARAM[d.id] ?? ['resistance', 'capacitance', 'inductance', 'voltage', 'frequency', 'part', 'color'].find((key) => d.params?.[key]);
  const parameter = valueParam ? d.params?.[valueParam] : undefined;
  return { name: d.name, symbol: d.symbol, svg: entry.files.symbol ?? '', terminals: d.pins.map((p) => p.name), pins: d.pins, valueParam, valueDefault: parameter?.default, valueUnit: parameter?.unit, wiringAid: d.bench?.wiring_aid };
}

/** The parts and boards of a project, which place, move and delete alike. */
function placeables(p: Project): (ProjectComponent | ProjectBoard)[] {
  return [...p.components, ...(p.boards ?? [])];
}
/** The first id with the part's prefix ("R3") that `used` does not hold. */
function freeId(componentId: string, used: Set<string>): string {
  const short = componentId.split('/').pop() ?? 'U';
  const prefix = PREFIX[short] ?? 'U';
  for (let n = 1; n < 10_000; n++) if (!used.has(`${prefix}${n}`)) return `${prefix}${n}`;
  return `${prefix}${Date.now()}`;
}
const catalogId = (x: ProjectComponent | ProjectBoard) => ('board' in x ? x.board : x.component);
const projectIds = (p: Project) => [...p.components.map((x) => x.component), ...(p.boards ?? []).map((b) => b.board), ...Object.entries(p.blocks ?? {}).flatMap(([id, block]) => [`block/${id}`, ...block.components.map((c) => c.component)])];

/** Applies a diff through one transaction. */
async function applyEdits(edits: ProjectEdit[]): Promise<void> {
  if (edits.length === 0) return;
  const c = getClient();
  const { tx } = await c.project.transaction({ op: 'begin' });
  if (!tx) throw new Error('transaction did not return an id');
  try {
    await c.project.transaction({ op: 'edit', tx, edits });
    const committed = await c.project.transaction({ op: 'commit', tx });
    if (committed.applied === false || committed.applied === 0) throw new Error(committed.diagnostics?.filter((d)=>d.severity==='error').map((d)=>d.message).join('; ') || 'Native validation rejected the circuit; the previous circuit is preserved.');
    if (committed.restarted) {
      useInstruments.getState().clearRecords();
      await primeStores(c);
    }
  } catch (e) {
    await c.project.transaction({ op: 'abort', tx }).catch(() => undefined);
    throw e;
  }
}

/** Only nets incident to transformed parts need new presentation geometry. */
function rerouteParts(p: Project, catalog: SceneCatalog, ids: string[]): Project {
  const affected = new Set(Object.entries(p.nets).filter(([, pins]) => pins.some((pin) => ids.includes(pin.split('.')[0]!))).map(([net]) => net));
  return autoRoute(p, catalog, affected).project;
}

export const useProject = create<ProjectState>((set, get) => ({
  project: null,
  items: [],
  catalog: {},
  sceneCatalog: {},
  topology: null,
  hierarchy: [],
  commands: new CommandStack(),
  busy: false,
  lastError: null,
  editable: true,

  async loadAll() {
    const c = getClient();
    const [{ project }, { items }] = await Promise.all([c.project.get({}), c.catalog.list({})]);
    set({ project, items });
    await get().ensureCatalog([...new Set([...items.map((i) => i.id), ...projectIds(project)])]);
    const topology = await c.project.topology({}).catch(() => null);
    const hierarchy = await c.project.hierarchy?.({}).catch(() => []) ?? [];
    set({ topology, hierarchy });
  },

  async refresh() {
    const c = getClient();
    const { project } = await c.project.get({});
    // Duplicate project.changed notifications must not invalidate active drafts.
    // A new object identity represents an actual content revision, not a fetch.
    if (JSON.stringify(get().project) !== JSON.stringify(project)) set({ project });
    await get().ensureCatalog(projectIds(project));
    const topology = await c.project.topology({}).catch(() => null);
    const hierarchy = await c.project.hierarchy?.({}).catch(() => []) ?? [];
    set({ topology, hierarchy });
  },

  async ensureCatalog(ids) {
    const missing = [...new Set(ids)].filter((id) => id.startsWith('block/') || !get().catalog[id]);
    if (missing.length === 0) return;
    const c = getClient();
    const results = await Promise.all(
      missing.map(async (id) => {
        try {
          return [id, await c.catalog.get({ id })] as const;
        } catch {
          return [id, null] as const;
        }
      }),
    );
    const catalog = { ...get().catalog };
    const sceneCatalog = { ...get().sceneCatalog };
    for (const [id, entry] of results) {
      if (!entry) continue;
      catalog[id] = entry;
      sceneCatalog[id] = sceneEntry(entry, get().project?.blocks?.[id.slice(6)]);
    }
    set({ catalog, sceneCatalog });
  },

  async mutate(label, fn) {
    if (!get().editable) throw new Error('This project is read-only.');
    if (get().busy || get().commands.isBusy) throw new Error('An edit is still finishing. Try again when it completes.');
    const before = get().project;
    if (!before) return;
    const after = fn(clone(before));
    if (!after) return;
    const forward = diffProject(before, after);
    if (forward.length === 0) return;
    const backward = diffProject(after, before);
    set({ busy: true, lastError: null });
    try {
      await get().commands.execute({
        label,
        redo: async () => {
          await applyEdits(forward);
          await get().refresh();
        },
        undo: async () => {
          await applyEdits(backward);
          await get().refresh();
        },
      });
    } catch (e) {
      set({ lastError: e instanceof Error ? e.message : String(e) });
      if (e instanceof JsonRpcError && e.code === JsonRpcCode.METHOD_NOT_FOUND) set({ editable: false });
      await get().refresh();
      throw e;
    } finally {
      set({ busy: false });
    }
  },

  nextId(componentId) {
    const project = get().project;
    return freeId(componentId, new Set(project ? placeables(project).map((c) => c.id) : []));
  },

  clipboard: null,

  copySelection(selection) {
    const project = get().project;
    const clip = project ? clipOf(project, selection) : null;
    if (clip) set({ clipboard: clip });
  },

  async paste(at) {
    const clip = get().clipboard;
    if (!clip) return [];
    const step = (clip.pastes + 1) * 4 * GRID;
    let ids: string[] = [];
    await get().mutate(`Paste ${clip.components.length > 1 ? `${clip.components.length} parts` : clip.components[0]!.id}`, (p) => {
      const dx = at ? snap(at.x) - Math.min(...clip.components.map((c) => c.layout?.x ?? 0)) : step;
      const dy = at ? snap(at.y) - Math.min(...clip.components.map((c) => c.layout?.y ?? 0)) : step;
      const r = withClip(p, clip, dx, dy, freeId);
      ids = r.ids;
      return r.project;
    });
    set({ clipboard: { ...clip, pastes: clip.pastes + 1 } });
    return ids;
  },

  async duplicate(selection) {
    const project = get().project;
    const clip = project ? clipOf(project, selection) : null;
    if (!clip) return [];
    let ids: string[] = [];
    await get().mutate(`Duplicate ${clip.components.length > 1 ? `${clip.components.length} parts` : clip.components[0]!.id}`, (p) => {
      const r = withClip(p, clip, 4 * GRID, 4 * GRID, freeId);
      ids = r.ids;
      return r.project;
    });
    return ids;
  },

  async placeComponent(componentId, at) {
    await get().ensureCatalog([componentId]);
    const entry = get().catalog[componentId];
    if (!entry) return null;
    const id = get().nextId(componentId);
    const params: Record<string, string | number | boolean> = {};
    await get().mutate(`Place ${id}`, (p) => {
      p.components.push({ id, component: componentId, params, layout: freePlacement(p, get().sceneCatalog, entry.description.symbol, at) });
      return p;
    });
    return id;
  },

  async setPlacementLocked(ids, locked) {
    await get().mutate(locked ? 'Lock placement' : 'Unlock placement', (p) => {
      for (const c of placeables(p)) if (ids.includes(c.id)) c.layout = { ...(c.layout ?? { x: 0, y: 0 }), locked };
      return p;
    });
  },

  async moveComponents(ids, dx, dy, reroute = false) {
    if (![dx, dy].every(Number.isFinite)) throw new Error('Offsets must be finite numbers.');
    dx = snap(dx); dy = snap(dy);
    if (!dx && !dy) return;
    const catalog = get().catalog;
    await get().mutate(`Move ${ids.length > 1 ? `${ids.length} components` : ids[0]}`, (p) => {
      assertUnlocked(p, ids);
      const moved = new Map<string, { x: number; y: number }>(); // net + old pin position → new
      const memberships = new Map(Object.entries(p.nets).flatMap(([net, pins]) => pins.map((pin) => [pin, net] as const)));
      for (const c of placeables(p)) {
        if (!ids.includes(c.id)) continue;
        const sym = catalog[catalogId(c)]?.description.symbol;
        const before = c.layout ?? { x: 0, y: 0 };
        const after = { ...before, x: before.x + dx, y: before.y + dy };
        if (sym) {
          for (const pin of Object.keys(sym.pins)) {
            const a = pinPosition(before, sym, pin);
            const b = pinPosition(after, sym, pin);
            const net = memberships.get(`${c.id}.${pin}`);
            if (a && b && net !== undefined) moved.set(`${net}:${a.x},${a.y}`, b);
          }
        }
        c.layout = after;
      }
      // Drag attached wire ends along, keeping segments orthogonal.
      for (const w of p.layout?.wires ?? []) {
        const pts = w.points;
        const first = pts[0];
        const last = pts[pts.length - 1];
        if (first && moved.has(`${w.net}:${first[0]},${first[1]}`)) {
          const n = moved.get(`${w.net}:${first[0]},${first[1]}`)!;
          const next = pts[1];
          pts[0] = [n.x, n.y];
          if (next && next[0] !== n.x && next[1] !== n.y) pts.splice(1, 0, next[1] === first[1] ? [n.x, next[1]] : [next[0], n.y]);
        }
        if (last && pts.length > 1 && moved.has(`${w.net}:${last[0]},${last[1]}`)) {
          const n = moved.get(`${w.net}:${last[0]},${last[1]}`)!;
          const prev = pts[pts.length - 2]!;
          pts[pts.length - 1] = [n.x, n.y];
          if (prev[0] !== n.x && prev[1] !== n.y) pts.splice(pts.length - 1, 0, prev[1] === last[1] ? [n.x, prev[1]] : [prev[0], n.y]);
        }
      }
      return reroute ? rerouteParts(p, get().sceneCatalog, ids) : p;
    });
  },

  async reshapeWire(index, segment, offset, source) {
    if (get().project !== source) throw new Error('The project changed while reshaping. Select the wire and try again.');
    await get().mutate('Reshape wire segment', (p) => {
      const wires = shiftWireSegment(p.layout?.wires ?? [], index, segment, offset, projectPinPoints(p, get().sceneCatalog));
      if (!wires) return null;
      p.layout = { ...p.layout, wires };
      return p;
    });
  },

  async setPartLayout(id, layout, source) {
    if (get().project !== source) throw new Error('The project changed. Review the position and try again.');
    if (![layout.x, layout.y].every(Number.isFinite)) throw new Error('Coordinates must be finite numbers.');
    if (layout.rotation !== undefined && ![0, 90, 180, 270].includes(layout.rotation)) throw new Error('Rotation must be a quarter turn.');
    await get().mutate(`Position ${id}`, (p) => {
      const part = placeables(p).find((c) => c.id === id);
      if (!part) return null;
      assertUnlocked(p, [id]);
      const next = { ...layout, x: snap(layout.x), y: snap(layout.y), locked: part.layout?.locked };
      const before = part.layout ?? { x: 0, y: 0 };
      if (before.x === next.x && before.y === next.y && (before.rotation ?? 0) === (next.rotation ?? 0) && !!before.mirror === !!next.mirror) return null;
      part.layout = next;
      return rerouteParts(p, get().sceneCatalog, [id]);
    });
  },

  async setBatchParams(ids, values, source) {
    if (get().project !== source) throw new Error('The project changed. Review the selected properties and try again.');
    await get().mutate(`Set properties on ${ids.length} parts`, (p) => {
      for (const c of p.components) if (ids.includes(c.id)) c.params = { ...c.params, ...values };
      return p;
    });
  },

  async eraseWireGeometry(indices) {
    await get().mutate('Remove drawn tracks (keep connections)', (p) => {
      p.layout = { ...p.layout, wires: (p.layout?.wires ?? []).filter((_, i) => !indices.includes(i)) };
      return p;
    });
  },

  async disconnectPin(pin) {
    await get().mutate(`Disconnect ${pin}`, (p) => {
      const net = Object.keys(p.nets).find((n) => p.nets[n]!.includes(pin));
      if (!net) return null;
      p.nets[net] = p.nets[net]!.filter((ref) => ref !== pin);
      if (!p.nets[net]!.length) delete p.nets[net];
      p.layout = { ...p.layout, labels: (p.layout?.labels ?? []).filter((l) => Object.hasOwn(p.nets, l.net)), buses: (p.layout?.buses ?? []).map((b) => ({ ...b, nets: b.nets.filter((n) => Object.hasOwn(p.nets, n)) })).filter((b) => b.nets.length) };
      return autoRoute(p, get().sceneCatalog, new Set([net])).project;
    });
  },

  async rerouteNets(nets) {
    let warnings: string[] = [];
    await get().mutate(`Route ${nets.length} net${nets.length === 1 ? '' : 's'}`, (p) => {
      const result = autoRoute(p, get().sceneCatalog, new Set(nets));
      warnings = result.warnings;
      return result.project;
    });
    return warnings;
  },

  async rotateComponents(ids) {
    const catalog = get().catalog;
    await get().mutate(`Rotate ${ids.join(', ')}`, (p) => {
      assertUnlocked(p, ids);
      const targets = placeables(p).filter((c) => ids.includes(c.id));
      const layouts = rotateLayouts(targets.map((c) => ({ layout: c.layout ?? { x: 0, y: 0 }, symbol: catalog[catalogId(c)]?.description.symbol ?? { width: 60, height: 20 } })));
      targets.forEach((c, i) => (c.layout = layouts[i] as Layout));
      return rerouteParts(p, get().sceneCatalog, ids);
    });
  },

  async mirrorComponents(ids) {
    await get().mutate(`Mirror ${ids.join(', ')}`, (p) => {
      assertUnlocked(p, ids);
      for (const c of placeables(p)) if (ids.includes(c.id)) c.layout = { ...(c.layout ?? { x: 0, y: 0 }), mirror: !c.layout?.mirror };
      return rerouteParts(p, get().sceneCatalog, ids);
    });
  },

  async deleteSelection(selection) {
    const catalog = get().catalog;
    if (selection.components.length === 0 && selection.wires.length === 0) return;
    await get().mutate('Delete', (p) => {
      if (p.layout?.groups) p.layout.groups = p.layout.groups.map((g) => ({ ...g, members: g.members.filter((id) => !selection.components.includes(id)) })).filter((g) => g.members.length);
      const removedPins = new Set<string>();
      const memberships = new Map(Object.entries(p.nets).flatMap(([net, pins]) => pins.map((pin) => [pin, net] as const)));
      for (const c of placeables(p)) {
        if (!selection.components.includes(c.id)) continue;
        const sym = catalog[catalogId(c)]?.description.symbol;
        for (const pin of Object.keys(sym?.pins ?? {})) {
          const pos = sym ? pinPosition(c.layout ?? { x: 0, y: 0 }, sym, pin) : null;
          const net = memberships.get(`${c.id}.${pin}`);
          if (pos && net !== undefined) removedPins.add(`${net}:${pos.x},${pos.y}`);
        }
      }
      p.components = p.components.filter((c) => !selection.components.includes(c.id));
      if (p.boards) p.boards = p.boards.filter((b) => !selection.components.includes(b.id));
      // off the bench too, so that undo puts them back where they were
      const bench = p.layout?.bench;
      if (bench) {
        for (const id of selection.components) delete bench.parts?.[id];
        if (bench.wires) bench.wires = bench.wires.filter((w) => !selection.components.includes(w.from.split('.')[0]!) && !selection.components.includes(w.to.split('.')[0]!));
      }
      for (const net of Object.keys(p.nets)) {
        p.nets[net] = p.nets[net]!.filter((pin) => !selection.components.includes(pin.split('.')[0] ?? ''));
        if (p.nets[net]!.length === 0) delete p.nets[net];
      }
      const wires = p.layout?.wires ?? [];
      const pinAt = new Map<string, string>();
      for (const c of placeables(p)) {
        const sym = catalog[catalogId(c)]?.description.symbol;
        if (!sym) continue;
        for (const pin of Object.keys(sym.pins)) {
          const pos = pinPosition(c.layout ?? { x: 0, y: 0 }, sym, pin);
          if (pos) pinAt.set(`${pos.x},${pos.y}`, `${c.id}.${pin}`);
        }
      }
      const keep: Wire[] = [];
      wires.forEach((w, i) => {
        const first = w.points[0]!;
        const last = w.points[w.points.length - 1]!;
        const touchesRemoved = removedPins.has(`${w.net}:${first[0]},${first[1]}`) || removedPins.has(`${w.net}:${last[0]},${last[1]}`);
        if (selection.wires.includes(i)) {
          // Deleting a wire disconnects the pins at its ends.
          for (const end of [first, last]) {
            const pin = pinAt.get(`${end[0]},${end[1]}`);
            if (pin && p.nets[w.net]) {
              p.nets[w.net] = p.nets[w.net]!.filter((x) => x !== pin);
              if (p.nets[w.net]!.length === 0) delete p.nets[w.net];
            }
          }
          return;
        }
        if (!touchesRemoved && Object.hasOwn(p.nets, w.net)) keep.push(w);
      });
      const buses = (p.layout?.buses ?? []).map((b) => ({ ...b, nets: b.nets.filter((n) => n in p.nets) })).filter((b) => b.nets.length > 0);
      p.layout = { ...(p.layout ?? {}), wires: keep, labels: (p.layout?.labels ?? []).filter((l) => l.net in p.nets), buses };
      return p;
    });
  },

  async wire(from, to, points, autoArrange = false) {
    await get().mutate('Wire', (p) => {
      const fromPin = from.component ? `${from.component}.${from.pin}` : null;
      const toPin = 'component' in to && to.component ? `${to.component}.${to.pin}` : null;
      const netOf = (pin: string | null) => (pin ? Object.entries(p.nets).find(([, pins]) => pins.includes(pin))?.[0] : undefined);
      let net = fromPin ? netOf(fromPin) : from.net;
      const otherNet = toPin ? netOf(toPin) : 'net' in to ? to.net : undefined;
      if (net && otherNet && net !== otherNet) {
        // Merge the two nets into the one with the "better" name (user-named over generated).
        const keep = /^N\d+$/.test(net) && !/^N\d+$/.test(otherNet) ? otherNet : net;
        const drop = keep === net ? otherNet : net;
        p.nets[keep] = [...p.nets[keep]!, ...p.nets[drop]!];
        delete p.nets[drop];
        for (const w of p.layout?.wires ?? []) if (w.net === drop) w.net = keep;
        for (const l of p.layout?.labels ?? []) if (l.net === drop) l.net = keep;
        net = keep;
      } else if (!net && otherNet) net = otherNet;
      if (!net) {
        let n = 1;
        while (`N${n}` in p.nets) n++;
        net = `N${n}`;
        p.nets[net] = [];
      }
      for (const pin of [fromPin, toPin]) if (pin && !p.nets[net]!.includes(pin)) p.nets[net]!.push(pin);
      if (points.length >= 2) p.layout = { ...(p.layout ?? {}), wires: [...(p.layout?.wires ?? []), { net, points }] };
      if (autoArrange) {
        const selected = [...new Set(p.nets[net]!.map((pin) => pin.split('.')[0]!))];
        return createLayout(p, get().sceneCatalog, { direction: 'horizontal', spacing: 'comfortable', selected, route: true }).project;
      }
      return p;
    });
  },

  async setParam(id, param, value) {
    await get().mutate(`Set ${id}.${param}`, (p) => {
      const c = p.components.find((x) => x.id === id);
      if (!c) return null;
      c.params = { ...(c.params ?? {}), [param]: value as string | number | boolean };
      return p;
    });
  },

  async renameNet(from, to) {
    const name = to.trim();
    if (!name || name === from) return;
    await get().mutate(`Rename ${from} to ${name}`, (p) => {
      if (!(from in p.nets) || name in p.nets) return null;
      p.nets[name] = p.nets[from]!;
      delete p.nets[from];
      for (const w of p.layout?.wires ?? []) if (w.net === from) w.net = name;
      for (const l of p.layout?.labels ?? []) if (l.net === from) l.net = name;
      for (const b of p.layout?.buses ?? []) b.nets = b.nets.map((n) => (n === from ? name : n));
      for (const pr of p.probes ?? []) if (pr.target === `net:${from}`) pr.target = `net:${name}`;
      return p;
    });
  },

  async labelWire(index) {
    await get().mutate('Label net', (p) => {
      const w = p.layout?.wires?.[index];
      const [a, b] = [w?.points[0], w?.points[1] ?? w?.points[0]];
      if (!w || !a || !b) return null;
      const at = { x: snap((a[0] + b[0]) / 2), y: snap((a[1] + b[1]) / 2) };
      p.layout = { ...(p.layout ?? {}), labels: [...(p.layout?.labels ?? []), { net: w.net, ...at }] };
      return p;
    });
  },

  async groupIntoBus(wires) {
    let name: string | null = null;
    await get().mutate('Group into a bus', (p) => {
      const all = p.layout?.wires ?? [];
      const chosen = wires.map((i) => all[i]).filter((w): w is Wire => Boolean(w));
      const nets = [...new Set(chosen.map((w) => w.net))].sort((a, b) => a.localeCompare(b, undefined, { numeric: true }));
      if (nets.length < 2) return null;
      // the name the nets share ("D0".."D7" give "D"), else BUSn
      let prefix = nets[0]!;
      for (const n of nets) while (!n.startsWith(prefix)) prefix = prefix.slice(0, -1);
      prefix = prefix.replace(/[_\d]+$/, '');
      const used = new Set((p.layout?.buses ?? []).map((b) => b.name));
      let candidate = prefix.length >= 1 ? prefix : 'BUS1';
      for (let k = 1; used.has(candidate); k++) candidate = `${prefix || 'BUS'}${k}`;
      name = candidate;
      // a vertical line left of the wires, over their height
      const xs = chosen.flatMap((w) => w.points.map(([x]) => x));
      const ys = chosen.flatMap((w) => w.points.map(([, y]) => y));
      const x = snap(Math.min(...xs) - 2 * GRID);
      const points: [number, number][] = [[x, snap(Math.min(...ys))], [x, snap(Math.max(...ys))]];
      p.layout = { ...(p.layout ?? {}), buses: [...(p.layout?.buses ?? []), { name: candidate, nets, points }] };
      return p;
    });
    return name;
  },

  async removeBus(name) {
    await get().mutate(`Remove bus ${name}`, (p) => {
      if (!p.layout?.buses?.some((b) => b.name === name)) return null;
      p.layout = { ...p.layout, buses: p.layout.buses.filter((b) => b.name !== name) };
      return p;
    });
  },

  async loadProjectJson(json) {
    const c = getClient();
    await c.project.load({ json });
    get().commands.clear();
    await get().refresh();
  },
}));

export { GRID };

function assertUnlocked(project: Project, ids: string[]) {
  const locked = [...project.components, ...(project.boards ?? [])].filter((p) => ids.includes(p.id) && p.layout?.locked);
  if (locked.length) throw new Error(`Unlock placement first: ${locked.map((p) => p.id).join(', ')}`);
}
