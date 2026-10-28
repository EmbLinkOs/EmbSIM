// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import {
  sheetAt,
  type ComponentDescription,
  type HierarchyNode,
  type NetState,
  type PinDescription,
  type Project,
} from '@embsim/protocol';
import { checkSchematic, type SceneCatalog, type SchematicIssue } from '@embsim/scene';

export interface ProjectIssue extends SchematicIssue {
  path: string;
  source: 'drawing' | 'electrical' | 'live' | 'coverage';
}
interface Terminal {
  path: string;
  component: string;
  ref: string;
  pin: PinDescription;
  net?: string;
  local?: string;
}
export type CheckCatalog = Record<string, { description: ComponentDescription }>;

/** ERC is advisory. Native model construction/solving remains authoritative. */
export function checkProject(
  p: Project,
  nodes: HierarchyNode[],
  catalog: CheckCatalog,
  scene: SceneCatalog,
): ProjectIssue[] {
  const issues: ProjectIssue[] = [],
    terminals: Terminal[] = [];
  const add = (
    path: string,
    source: ProjectIssue['source'],
    code: string,
    key: string,
    severity: ProjectIssue['severity'],
    title: string,
    detail: string,
    target: Partial<ProjectIssue> = {},
  ) =>
    issues.push({
      id: `${path}:${code}:${key}`,
      path,
      source,
      code,
      severity,
      title,
      detail,
      components: [],
      wires: [],
      ...target,
    });
  // Drawing defects belong to definitions; don't repeat them once per instance.
  for (const path of ['', ...Object.keys(p.blocks ?? {}).map((id) => `@${id}`)]) {
    const { sheet } = sheetAt(p, path);
    const view = {
      ...p,
      components: sheet.components,
      nets: sheet.nets,
      layout: sheet.layout,
      boards: path ? [] : p.boards,
    };
    for (const issue of checkSchematic(view, scene)) {
      // Exported one-pin nets are intentionally connected through their ports.
      if (
        issue.code === 'small-net' &&
        'ports' in sheet &&
        Object.values(sheet.ports).some((pin) => sheet.nets[issue.net ?? '']?.includes(pin))
      )
        continue;
      if (
        issue.code === 'small-net' &&
        sheet.nets[issue.net ?? '']?.some((pin) =>
          sheet.components.some((c) => c.component.startsWith('block/') && pin.startsWith(c.id + '.')),
        )
      )
        continue;
      issues.push({
        ...issue,
        id: `${path}:${issue.id}`,
        path,
        source: 'drawing',
      });
    }
    for (const bus of sheet.layout?.buses ?? []) {
      if (new Set(bus.nets).size !== bus.nets.length || bus.nets.some((n) => !Object.hasOwn(sheet.nets, n)))
        add(
          path,
          'drawing',
          'invalid-bus',
          bus.name,
          'error',
          `Bus ${bus.name} has invalid members`,
          'Bus members must be distinct existing nets. Their order defines bit order; no electrical connection is created.',
        );
    }
  }
  // Resolve boundary ports to leaf terminals, using native aliases, not label matching.
  for (const node of nodes) {
    let sheet: ReturnType<typeof sheetAt>['sheet'];
    try {
      sheet = sheetAt(p, node.path).sheet;
    } catch {
      continue;
    } // Native index refresh may lag the document.
    for (const c of sheet.components) {
      if (c.component.startsWith('block/')) continue;
      const desc = catalog[c.component]?.description;
      if (!desc) {
        add(
          node.path,
          'coverage',
          'erc-description-unavailable',
          c.id,
          'warning',
          `${c.id}: electrical metadata unavailable`,
          `Cannot check ${c.component}'s terminal rules until its description is loaded.`,
          { components: [c.id] },
        );
        continue;
      }
      if (c.populated === false) continue;
      for (const pin of desc.pins ?? []) {
        const local = Object.entries(sheet.nets).find(([, pins]) =>
          pins.some(
            (ref) =>
              ref === `${c.id}.${pin.name}` || pin.aliases?.some((alias) => ref === `${c.id}.${alias}`),
          ),
        )?.[0];
        terminals.push({
          path: node.path,
          component: c.id,
          ref: `${node.path ? node.path + '.' : ''}${c.id}.${pin.name}`,
          pin,
          local,
          net: local === undefined ? undefined : node.nets[local],
        });
      }
    }
  }
  const byNet = new Map<string, Terminal[]>();
  for (const t of terminals)
    if (t.net !== undefined) {
      const group = byNet.get(t.net) ?? [];
      group.push(t);
      byNet.set(t.net, group);
    }
  const root = nodes.find((n) => !n.path);
  const boardNets = new Set(
    Object.entries(p.nets)
      .filter(([, pins]) => pins.some((ref) => p.boards?.some((b) => ref.startsWith(b.id + '.'))))
      .map(([net]) => root?.nets[net])
      .filter((n): n is string => n !== undefined),
  );
  for (const t of terminals) {
    const group = t.net === undefined ? [] : (byNet.get(t.net) ?? []);
    const connected = group.length > 1 || (t.net !== undefined && boardNets.has(t.net));
    if (!connected && t.pin.required === true)
      add(
        t.path,
        'electrical',
        'required-terminal',
        t.ref,
        'error',
        `${t.ref}: required terminal is unconnected`,
        'This terminal is explicitly marked required in the component description. Connect it through a net and an actual external terminal.',
        { components: [t.component], net: t.local },
      );
    else if (!connected && t.pin.model?.kind === 'power')
      add(
        t.path,
        'electrical',
        'power-connection',
        t.ref,
        t.pin.required === false ? 'info' : 'warning',
        `${t.ref}: no external power connection`,
        t.pin.required === false
          ? 'This model declares the terminal optional and may use an implicit rail. This does not establish a physically powered circuit; consult its fidelity declaration.'
          : 'No other declared terminal reaches this power pin. Check the supply and return connections.',
        { components: [t.component], net: t.local },
      );
    else if (
      !connected &&
      t.pin.model?.kind === 'digital_in' &&
      !['up', 'down'].includes(String(t.pin.model.pull))
    )
      add(
        t.path,
        'electrical',
        'unconnected-digital-input',
        t.ref,
        'warning',
        `${t.ref}: unconnected digital input`,
        'No external terminal or declared internal pull reaches this input. Follow the component datasheet for unused inputs.',
        { components: [t.component], net: t.local },
      );
  }
  for (const [net, group] of byNet) {
    const first = group[0]!,
      target = {
        components: [...new Set(group.filter((t) => t.path === first.path).map((t) => t.component))],
        net: first.local,
      };
    const pushPull = group.filter(
      (t) => t.pin.model?.kind === 'digital_out' && t.pin.model.drive === 'push_pull',
    );
    if (pushPull.length > 1)
      add(
        first.path,
        'electrical',
        'multiple-push-pull',
        net,
        'warning',
        `${net}: multiple push-pull outputs`,
        `${pushPull.map((t) => t.ref).join(', ')}. This topology can contend; it is not proof of current contention. Check output enables and native live state.`,
        target,
      );
    const inputs = group.filter((t) => t.pin.model?.kind === 'digital_in');
    if (
      !boardNets.has(net) &&
      inputs.length > 1 &&
      inputs.length === group.length &&
      !inputs.some((t) => ['up', 'down'].includes(String(t.pin.model?.pull)))
    )
      add(
        first.path,
        'electrical',
        'input-only-net',
        net,
        'warning',
        `${net}: connected inputs without a declared source`,
        `${inputs.map((t) => t.ref).join(', ')}. This net contains only digital inputs with no declared pull.`,
        target,
      );
  }
  if (p.boards?.length)
    add(
      '',
      'coverage',
      'board-erc-boundary',
      'boards',
      'info',
      'Board internals use their existing native validation',
      'Project ERC recognizes board-connected nets as external connections, but does not infer MCU pin direction from board header artwork. Firmware-configured direction, board internals and installed composite internals require native diagnostics and live observations.',
    );
  const rank = { error: 0, warning: 1, info: 2 };
  return issues.sort((a, b) => rank[a.severity] - rank[b.severity] || a.id.localeCompare(b.id));
}

/** Read current native state without repeating geometric checks at observation rate. */
export function liveProjectChecks(nodes: HierarchyNode[], states: Record<string, NetState>): ProjectIssue[] {
  const locations = new Map<string, { path: string; net: string }>();
  for (const n of nodes)
    for (const [local, native] of Object.entries(n.nets))
      if (!locations.has(native)) locations.set(native, { path: n.path, net: local });
  const issues: ProjectIssue[] = [];
  for (const [native, state] of Object.entries(states)) {
    const location = locations.get(native);
    if (!location) continue;
    const conflict = state.contention === true;
    if (!conflict && (!state.receivers?.length || (state.logic !== 'X' && state.logic !== 'Z'))) continue;
    const code = conflict ? 'live-contention' : state.logic === 'Z' ? 'live-floating' : 'live-unknown';
    issues.push({
      id: `live:${native}:${code}`,
      source: 'live',
      code,
      severity: conflict ? 'error' : 'warning',
      title: `${native}: ${conflict ? 'engine reports contention' : state.logic === 'Z' ? 'floating input' : 'unknown input'}`,
      detail: conflict
        ? 'The native engine reports opposing active drives. Inspect output enables and driven levels.'
        : `Native receivers ${(state.receivers ?? []).join(', ')} observe ${state.logic}. X alone does not establish contention; zero analog voltage alone does not establish floating.`,
      ...location,
      components: [],
      wires: [],
    });
  }
  return issues;
}
