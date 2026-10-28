// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { parseSi, sheetAt, type HierarchyNode, type Project, type ProjectComponent } from '@embsim/protocol';
import type { SceneCatalog } from '@embsim/scene';
import { routeSheet } from '../design/hierarchyEditing';
import { sameDefinition } from '../design/hierarchyTools';

export const INSTRUMENT_KINDS: Record<
  string,
  { title: string; resistance: string; fallback: number; current: string }
> = {
  'emblink/voltmeter': {
    title: 'Voltmeter',
    resistance: 'input_resistance',
    fallback: 1e7,
    current: 'current',
  },
  'emblink/ammeter': { title: 'Ammeter', resistance: 'shunt_resistance', fallback: 1, current: 'current' },
  'emblink/scope_input': {
    title: 'Scope input',
    resistance: 'input_resistance',
    fallback: 1e6,
    current: 'current',
  },
  'emblink/function_generator': {
    title: 'Generator',
    resistance: 'output_resistance',
    fallback: 50,
    current: 'i',
  },
};
export const isWiredInstrument = (reference: string) => Object.hasOwn(INSTRUMENT_KINDS, reference);
export interface InstrumentInstance {
  path: string;
  block: string;
  qualified: string;
  component: ProjectComponent;
  values: Record<string, unknown>;
  positive: string;
  negative: string;
  nets: string[];
  shared: number;
}
export function instrumentInstances(p: Project | null, hierarchy: HierarchyNode[]): InstrumentInstance[] {
  if (!p) return [];
  const result: InstrumentInstance[] = [];
  const counts = new Map<string, number>();
  for (const node of hierarchy) counts.set(node.block, (counts.get(node.block) ?? 0) + 1);
  for (const node of hierarchy.length ? hierarchy : [{ path: '', block: '', nets: {} }]) {
    let resolved: ReturnType<typeof sheetAt>;
    try {
      resolved = sheetAt(p, node.path);
    } catch {
      continue;
    }
    const { sheet, block } = resolved;
    for (const c of sheet.components.filter((c) => isWiredInstrument(c.component))) {
      const value = (v: unknown) =>
        typeof v === 'string' && v.startsWith('$')
          ? (node.params?.[v.slice(1)] ??
            ('ports' in sheet ? sheet.params?.[v.slice(1)]?.default : undefined))
          : v;
      const net = (pin: string) =>
        Object.entries(sheet.nets).find(([, pins]) => pins.includes(`${c.id}.${pin}`))?.[0] ?? '';
      result.push({
        path: node.path,
        block,
        qualified: node.path ? `${node.path}.${c.id}` : c.id,
        component: c,
        values: Object.fromEntries(Object.entries(c.params ?? {}).map(([k, v]) => [k, value(v)])),
        positive: net('+'),
        negative: net('-'),
        nets: Object.keys(sheet.nets),
        shared: block ? (counts.get(block) ?? 0) : 1,
      });
    }
  }
  return result;
}
export interface InstrumentFingerprint {
  component: string;
  params: ProjectComponent['params'];
  positive: string;
  negative: string;
}
export const instrumentFingerprint = (i: InstrumentInstance): InstrumentFingerprint => ({
  component: i.component.component,
  params: structuredClone(i.component.params ?? {}),
  positive: i.positive,
  negative: i.negative,
});
export interface InstrumentDraft {
  positive: string;
  negative: string;
  values: Record<string, string>;
  expected: InstrumentFingerprint;
}

/** Changing physical leads/impedance is a circuit transaction, not moving an ideal observation. */
export function configureInstrument(
  p: Project,
  path: string,
  id: string,
  draft: InstrumentDraft,
  scene: SceneCatalog,
): Project {
  const { sheet } = sheetAt(p, path),
    c = sheet.components.find((c) => c.id === id);
  const kind = c && INSTRUMENT_KINDS[c.component];
  if (!c || !kind) throw new Error('Instrument no longer exists');
  const net = (pin: string) =>
    Object.entries(sheet.nets).find(([, pins]) => pins.includes(`${id}.${pin}`))?.[0] ?? '';
  if (
    !sameDefinition(
      { component: c.component, params: c.params ?? {}, positive: net('+'), negative: net('-') },
      draft.expected,
    )
  )
    throw new Error('Instrument changed while configuring. Reopen its settings before applying.');
  for (const name of [draft.positive, draft.negative])
    if (name && !Object.hasOwn(sheet.nets, name)) throw new Error(`Net ${name} no longer exists`);
  const allowed = [kind.resistance, ...(c.component === 'emblink/scope_input' ? ['input_capacitance'] : [])];
  for (const [key, raw] of Object.entries(draft.values)) {
    if (!allowed.includes(key)) throw new Error(`Unsupported instrument parameter ${key}`);
    if (typeof c.params?.[key] === 'string' && String(c.params[key]).startsWith('$'))
      throw new Error(`${key} is bound to a block parameter; edit its parent instance value`);
    const value = parseSi(raw);
    if (
      value === undefined ||
      !Number.isFinite(value) ||
      (key === 'input_capacitance' ? value < 0 : value <= 0)
    )
      throw new Error(
        `Enter a finite ${key === 'input_capacitance' ? 'nonnegative capacitance' : 'positive resistance'}`,
      );
    const previous =
      parseSi(c.params?.[key] as string | number | undefined) ??
      (key === 'input_capacitance' ? parseSi('15p')! : kind.fallback);
    if (value !== previous) c.params = { ...c.params, [key]: value };
  }
  const touched = new Set<string>();
  for (const [pin, next] of [
    ['+', draft.positive],
    ['-', draft.negative],
  ] as const) {
    const ref = `${id}.${pin}`,
      previous = net(pin);
    if (previous === next) continue;
    if (!next && 'ports' in sheet && Object.values(sheet.ports).includes(ref))
      throw new Error(`Remap the exported port on ${ref} before disconnecting it`);
    if (previous) {
      sheet.nets[previous] = sheet.nets[previous]!.filter((p) => p !== ref);
      touched.add(previous);
    }
    if (next) {
      sheet.nets[next]!.push(ref);
      touched.add(next);
    }
  }
  if (touched.size) {
    sheet.layout = { ...sheet.layout, wires: (sheet.layout?.wires ?? []).filter((w) => !touched.has(w.net)) };
    sheet.layout = routeSheet(
      {
        ...p,
        components: sheet.components,
        boards: path ? [] : p.boards,
        nets: sheet.nets,
        layout: sheet.layout,
      },
      scene,
      touched,
    ).layout;
  }
  return p;
}
