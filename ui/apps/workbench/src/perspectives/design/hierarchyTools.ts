// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import {
  blockDependencies,
  hierarchyOf,
  parseSi,
  sheetAt,
  type CircuitBlock,
  type ParamDescription,
  type Project,
} from '@embsim/protocol';
import { clipOf, withClip, type Clip } from '../../stores/project';

const sheets = (p: Project) => [
  { path: '', sheet: p },
  ...Object.entries(p.blocks ?? {}).map(([id, sheet]) => ({
    path: `@${id}`,
    sheet,
  })),
];
export interface SheetClipboard {
  clip: Clip;
  blocks: NonNullable<Project['blocks']>;
  parameters: NonNullable<CircuitBlock['params']>;
}

/** Copy topology, never short a pasted selection to its source's boundary nets. */
export function copySheet(p: Project, path: string, ids: string[]): SheetClipboard {
  const { sheet } = sheetAt(p, path);
  const clip = clipOf(
    {
      ...p,
      components: sheet.components,
      nets: sheet.nets,
      layout: sheet.layout,
      boards: path ? [] : p.boards,
    },
    { components: ids, wires: [] },
  );
  if (!clip) throw new Error('Select components to copy');
  const blocks: NonNullable<Project['blocks']> = {},
    parameters: NonNullable<CircuitBlock['params']> = {};
  for (const c of clip.components) {
    if (c.component.startsWith('block/')) Object.assign(blocks, blockDependencies(p, c.component.slice(6)));
    for (const v of Object.values(c.params ?? {}))
      if (typeof v === 'string' && v.startsWith('$')) {
        const spec = 'ports' in sheet ? sheet.params?.[v.slice(1)] : undefined;
        if (!spec) throw new Error(`Unknown source parameter ${v}`);
        parameters[v.slice(1)] = structuredClone(spec);
      }
  }
  return { clip, blocks, parameters };
}

/** A stable JSON comparison for library conflicts; property ordering has no meaning. */
export function sameDefinition(a: unknown, b: unknown): boolean {
  const stable = (v: unknown): unknown =>
    Array.isArray(v)
      ? v.map(stable)
      : v && typeof v === 'object'
        ? Object.fromEntries(
            Object.entries(v)
              .sort(([a], [b]) => a.localeCompare(b))
              .map(([k, x]) => [k, stable(x)]),
          )
        : v;
  return JSON.stringify(stable(a)) === JSON.stringify(stable(b));
}
export function pasteSheet(p: Project, path: string, clipboard: SheetClipboard, offset = 40): string[] {
  const { sheet } = sheetAt(p, path);
  for (const [id, b] of Object.entries(clipboard.blocks)) {
    if (p.blocks?.[id] && !sameDefinition(p.blocks[id], b))
      throw new Error(`Block ${id} differs from the copied definition`);
  }
  for (const [name, spec] of Object.entries(clipboard.parameters)) {
    if (!('ports' in sheet))
      throw new Error(
        'Paste parameterized components into a block definition, or replace their parameter bindings first',
      );
    if (sheet.params?.[name] && !sameDefinition(sheet.params[name], spec))
      throw new Error(`Parameter ${name} has a different declaration on this sheet`);
    sheet.params = { ...sheet.params, [name]: structuredClone(spec) };
  }
  p.blocks ??= {};
  for (const [id, block] of Object.entries(clipboard.blocks))
    if (!p.blocks[id]) p.blocks[id] = structuredClone(block);
  const doc: Project = {
    ...p,
    components: sheet.components,
    nets: sheet.nets,
    layout: sheet.layout,
    boards: path ? [] : p.boards,
  };
  const result = withClip(doc, clipboard.clip, offset, offset, (id, used) => {
    const prefix = id.startsWith('block/')
      ? 'X'
      : id.endsWith('/resistor')
        ? 'R'
        : id.endsWith('/capacitor')
          ? 'C'
          : 'U';
    let n = 1;
    while (used.has(`${prefix}${n}`)) n++;
    return `${prefix}${n}`;
  });
  sheet.components = result.project.components;
  sheet.nets = result.project.nets;
  sheet.layout = result.project.layout;
  hierarchyOf(p);
  return result.ids;
}

export function portUses(p: Project, block: string, name: string): string[] {
  const uses: string[] = [];
  for (const { path, sheet } of sheets(p))
    for (const c of sheet.components.filter((c) => c.component === `block/${block}`)) {
      const pin = `${c.id}.${name}`;
      for (const [net, pins] of Object.entries(sheet.nets))
        if (pins.includes(pin)) uses.push(`${path || 'Main'}: ${pin} on ${net}`);
      if ('ports' in sheet)
        for (const [port, target] of Object.entries(sheet.ports))
          if (target === pin) uses.push(`${path}: exported ${port}`);
    }
  for (const node of hierarchyOf(p).filter((n) => n.block === block))
    for (const probe of p.probes ?? [])
      if (probe.target === `pin:${node.path}.${name}`) uses.push(`Probe ${probe.id}`);
  return uses;
}
export function removePort(p: Project, block: string, name: string): void {
  const uses = portUses(p, block, name);
  if (uses.length)
    throw new Error(`Port ${name} is in use. Reconnect these references first: ${uses.join('; ')}`);
  const def = p.blocks?.[block];
  if (!def || !(name in def.ports)) throw new Error('Port not found');
  delete def.ports[name];
  if (def.layout?.port_order) def.layout.port_order = def.layout.port_order.filter((n) => n !== name);
}

export function validateParameter(name: string, spec: ParamDescription): void {
  if (!/^[a-z][a-z0-9_]*$/.test(name))
    throw new Error('Parameter names use lowercase letters, digits and underscores');
  const numeric = spec.type === 'number' || spec.type === 'integer';
  const number = (v: unknown) => (typeof v === 'number' ? v : typeof v === 'string' ? parseSi(v) : undefined);
  const lo = spec.min === undefined ? undefined : number(spec.min),
    hi = spec.max === undefined ? undefined : number(spec.max);
  if (
    (spec.min !== undefined && (lo === undefined || !Number.isFinite(lo))) ||
    (spec.max !== undefined && (hi === undefined || !Number.isFinite(hi)))
  )
    throw new Error('Bounds must be finite SI numbers');
  if ((lo !== undefined || hi !== undefined) && !numeric)
    throw new Error('Only numeric parameters have bounds');
  if (lo !== undefined && hi !== undefined && lo > hi) throw new Error('Minimum exceeds maximum');
  if (spec.type === 'enum' && (!spec.enum?.length || new Set(spec.enum).size !== spec.enum.length))
    throw new Error('Declare distinct enum choices');
  const v = spec.default;
  if (v === undefined) return;
  if (numeric) {
    const n = number(v);
    if (
      n === undefined ||
      !Number.isFinite(n) ||
      (spec.type === 'integer' && !Number.isInteger(n)) ||
      (lo !== undefined && n < lo) ||
      (hi !== undefined && n > hi)
    )
      throw new Error('Default must satisfy its numeric type and bounds');
  } else if (
    spec.type === 'boolean'
      ? typeof v !== 'boolean'
      : spec.type === 'table'
        ? !Array.isArray(v) ||
          !v.length ||
          v.some((row) => !Array.isArray(row) || row.length !== 2 || row.some((n) => !Number.isFinite(n)))
        : typeof v !== 'string' || (spec.type === 'enum' && !spec.enum?.includes(v))
  )
    throw new Error('Default does not match the declared type');
}
export function saveParameter(
  p: Project,
  block: string,
  previous: string | null,
  name: string,
  spec: ParamDescription,
): void {
  validateParameter(name, spec);
  const def = p.blocks?.[block];
  if (!def) throw new Error('Block not found');
  if (previous !== name && def.params?.[name]) throw new Error('Parameter name already exists');
  if (previous && previous !== name) {
    for (const c of def.components)
      for (const [key, value] of Object.entries(c.params ?? {}))
        if (value === `$${previous}`) c.params![key] = `$${name}`;
    for (const { sheet } of sheets(p))
      for (const c of sheet.components.filter((c) => c.component === `block/${block}`))
        if (c.params && previous in c.params) {
          c.params[name] = c.params[previous]!;
          delete c.params[previous];
        }
    delete def.params?.[previous];
  }
  def.params = { ...def.params, [name]: structuredClone(spec) };
}
export function removeParameter(p: Project, block: string, name: string): void {
  const def = p.blocks?.[block];
  if (!def?.params?.[name]) throw new Error('Parameter not found');
  if (
    def.components.some((c) => Object.values(c.params ?? {}).includes(`$${name}`)) ||
    sheets(p).some(({ sheet }) =>
      sheet.components.some((c) => c.component === `block/${block}` && c.params && name in c.params),
    )
  )
    throw new Error('Remove parameter bindings and instance overrides first');
  delete def.params[name];
}

export function setBus(
  sheet: Project | CircuitBlock,
  old: string | null,
  name: string,
  nets: string[],
): void {
  name = name.trim();
  if (
    !name ||
    nets.length < 2 ||
    nets.length > 32 ||
    new Set(nets).size !== nets.length ||
    nets.some((net) => !Object.hasOwn(sheet.nets, net))
  )
    throw new Error('A bus needs a name and 2–32 distinct existing nets, ordered least-significant first');
  const buses = sheet.layout?.buses ?? [];
  if (buses.some((b) => b.name === name && b.name !== old)) throw new Error('Bus name already exists');
  const previous = buses.find((b) => b.name === old);
  const points: [number, number][] = previous?.points ?? [
    [40, 40],
    [40, 40 + nets.length * 20],
  ];
  sheet.layout = {
    ...sheet.layout,
    buses: [...buses.filter((b) => b.name !== old), { name, nets, points }],
  };
}
