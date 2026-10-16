// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The inside of a board or a composite part, as a read-only sheet: its
// parts on a grid, each connected pin with a short stub and the name of its
// net (catalog descriptions carry no drawing), and the map from its nets to
// the session's, so the sheet shows them live. A board's MCU is a block of
// the pins its nets use.

import type { NetLabel, Project, ProjectComponent, SymbolDescription, Wire } from '@embsim/protocol';
import { boardSymbol, GRID, pinPosition, type SceneCatalog } from '@embsim/scene';

interface InnerPart {
  id: string;
  component: string;
  params?: Record<string, string | number | boolean>;
}

/** What a board or composite description says about its inside. */
export interface InsideSource {
  kind: 'board' | 'component';
  name: string;
  /** The parts, nets and outer connections (ports) of the inside. */
  components: InnerPart[];
  nets: Record<string, string[]>;
  /** Port (an outer pin) -> the inner net it is (board) or the inner pin it is (composite). */
  ports: Record<string, string>;
  /** A board's MCU instance and description id. */
  mcu?: { instance: string; ref: string };
}

/** The inside of a catalog description, or null when it has none. */
export function insideSource(description: unknown): InsideSource | null {
  const d = description as {
    kind?: string;
    name?: string;
    components?: InnerPart[];
    nets?: Record<string, string[]>;
    ports?: Record<string, { net?: string }>;
    mcu?: { instance?: string; ref?: string };
    model?: { composite?: { components?: InnerPart[]; nets?: Record<string, string[]>; ports?: Record<string, string> } };
  };
  if (d.kind === 'board' && d.components && d.nets) {
    const ports: Record<string, string> = {};
    for (const [name, p] of Object.entries(d.ports ?? {})) if (p.net) ports[name] = p.net;
    return {
      kind: 'board',
      name: d.name ?? '',
      components: d.components,
      nets: d.nets,
      ports,
      mcu: d.mcu?.instance ? { instance: d.mcu.instance, ref: d.mcu.ref ?? 'mcu' } : undefined,
    };
  }
  const c = d.model?.composite;
  if (c?.components && c.nets) return { kind: 'component', name: d.name ?? '', components: c.components, nets: c.nets, ports: c.ports ?? {} };
  return null;
}

export interface InsideView {
  project: Project;
  /** Symbols the sheet needs beyond the catalog's (the MCU block). */
  scene: SceneCatalog;
  /** Inner net -> the session's net of the same signal. */
  alias: Record<string, string>;
}

const STUB = 2 * GRID;

/**
 * Lays out `src` (inside instance `id` of `outer`) with the symbols in
 * `catalog`. Parts without a symbol are left out of the drawing.
 */
export function insideView(id: string, src: InsideSource, catalog: SceneCatalog, outer: Project): InsideView {
  const scene: SceneCatalog = {};
  const parts: InnerPart[] = [...src.components];
  if (src.mcu) {
    // the MCU: the pins the board's nets use, half down each side
    const used = [...new Set(Object.values(src.nets).flat().filter((p) => p.startsWith(`${src.mcu!.instance}.`)).map((p) => p.slice(src.mcu!.instance.length + 1)))].sort((a, b) =>
      a.localeCompare(b, undefined, { numeric: true }),
    );
    const half = Math.ceil(used.length / 2);
    const ports = Object.fromEntries(used.map((p, i) => [p, { connector: i < half ? 'L' : 'R', pin: i }]));
    const key = `mcu:${src.mcu.ref}`;
    const { symbol, svg } = boardSymbol(src.mcu.ref.split('/').pop()!.toUpperCase(), ports);
    scene[key] = { name: src.mcu.ref, symbol, svg };
    parts.unshift({ id: src.mcu.instance, component: key });
  }
  const sym = (c: string): SymbolDescription | undefined => scene[c]?.symbol ?? catalog[c]?.symbol;

  // a grid, widest first: the MCU or the largest part leads
  const drawn = parts.filter((p) => sym(p.component));
  const components: ProjectComponent[] = [];
  let x = 0;
  let y = 0;
  let rowH = 0;
  const maxW = 1200;
  for (const p of drawn) {
    const s = sym(p.component)!;
    const w = s.width + 10 * GRID;
    if (x > 0 && x + w > maxW) {
      x = 0;
      y += rowH + 6 * GRID;
      rowH = 0;
    }
    components.push({ id: p.id, component: p.component, params: p.params, layout: { x: x + 4 * GRID, y: y + 4 * GRID } });
    x += w;
    rowH = Math.max(rowH, s.height);
  }

  // a stub and a label at every connected pin
  const wires: Wire[] = [];
  const labels: NetLabel[] = [];
  const netOf = new Map<string, string>();
  for (const [net, pins] of Object.entries(src.nets)) for (const pin of pins) netOf.set(pin, net);
  for (const c of components) {
    const s = sym(c.component)!;
    for (const [pin, anchor] of Object.entries(s.pins)) {
      const net = netOf.get(`${c.id}.${pin}`);
      if (!net) continue;
      const at = pinPosition(c.layout!, s, pin);
      if (!at) continue;
      const d = { left: [-STUB, 0], right: [STUB, 0], top: [0, -STUB], bottom: [0, STUB] }[anchor.side] ?? [STUB, 0];
      const end: [number, number] = [at.x + d[0]!, at.y + d[1]!];
      wires.push({ net, points: [[at.x, at.y], end] });
      labels.push({ net, x: end[0], y: end[1] });
    }
  }

  // the session's names: the outer net a port joins, else "<id>.<net>"
  const alias: Record<string, string> = {};
  for (const net of Object.keys(src.nets)) alias[net] = `${id}.${net}`;
  for (const [port, inner] of Object.entries(src.ports)) {
    const outerNet = Object.entries(outer.nets).find(([, pins]) => pins.includes(`${id}.${port}`))?.[0];
    if (!outerNet) continue;
    const net = src.kind === 'board' ? inner : netOf.get(inner);
    if (net) alias[net] = outerNet;
  }

  const project: Project = {
    schema_version: 1,
    kind: 'project',
    name: `${id} (${src.name})`,
    components,
    nets: src.nets,
    layout: { wires, labels },
  };
  return { project, scene, alias };
}
