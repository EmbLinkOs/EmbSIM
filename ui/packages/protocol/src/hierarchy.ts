// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import type { CircuitBlock, HierarchyNode, Project } from './types';

export function sheetAt(p: Project, path: string): { sheet: Project | CircuitBlock; block: string } {
  if (path.startsWith('@')) {
    const block = path.slice(1),
      sheet = p.blocks?.[block];
    if (!sheet) throw new Error('Block definition no longer exists');
    return { sheet, block };
  }
  let sheet: Project | CircuitBlock = p,
    block = '';
  if (path)
    for (const id of path.split('.')) {
      const part = sheet.components.find((c) => c.id === id);
      block = part?.component.startsWith('block/') ? part.component.slice(6) : '';
      const child = p.blocks?.[block];
      if (!child) throw new Error('Sheet instance no longer exists');
      sheet = child;
    }
  return { sheet, block };
}

/** Structure-only aliases for the demo transport. Native clients use project.hierarchy. */
export function hierarchyOf(p: Project, trace?: string): HierarchyNode[] {
  const nodes: HierarchyNode[] = [],
    links: [string, string][] = [],
    parents = new Map<string, string>();
  const qualified = (path: string, net: string) => (path ? `${path}.${net}` : net);
  const visit = (sheet: Project | CircuitBlock, path: string, block: string, stack: string[]) => {
    if (stack.length > 8 || nodes.length >= 10000) throw new Error('Hierarchy exceeds expansion limits');
    const node: HierarchyNode = { path, block, nets: {} };
    nodes.push(node);
    for (const net of Object.keys(sheet.nets)) {
      const full = qualified(path, net);
      parents.set(full, full);
      node.nets[net] = full;
    }
    const ids = new Set<string>();
    for (const part of sheet.components) {
      if (ids.has(part.id)) throw new Error('Duplicate sheet instance');
      ids.add(part.id);
      if (!part.component.startsWith('block/')) continue;
      const key = part.component.slice(6),
        child = p.blocks?.[key],
        childPath = qualified(path, part.id);
      if (!child || stack.includes(key)) throw new Error('Unknown or recursive circuit block');
      visit(child, childPath, key, [...stack, key]);
      for (const [port, target] of Object.entries(child.ports)) {
        const local = Object.entries(child.nets).find(([, pins]) => pins.includes(target))?.[0];
        if (!local) throw new Error('Port must reference a pin in a local net');
        const outer = Object.entries(sheet.nets).find(([, pins]) => pins.includes(`${part.id}.${port}`))?.[0];
        if (outer !== undefined) links.push([qualified(path, outer), qualified(childPath, local)]);
      }
    }
  };
  visit(p, '', '', []);
  const root = (key: string): string => {
    let r = key;
    while (parents.get(r) !== r && parents.has(r)) r = parents.get(r)!;
    return r;
  };
  for (const [a, b] of links) parents.set(root(b), root(a));
  for (const node of nodes) for (const [local, full] of Object.entries(node.nets)) node.nets[local] = root(full);
  return trace
    ? nodes
        .map((n) => ({
          ...n,
          nets: Object.fromEntries(Object.entries(n.nets).filter(([, full]) => full === root(trace))),
        }))
        .filter((n) => Object.keys(n.nets).length)
    : nodes;
}

/** Stable drawing order; explicit entries first, remaining names in deterministic order. */
export function blockPortOrder(block: CircuitBlock): string[] {
  const names = Object.keys(block.ports);
  const order = (block.layout?.port_order ?? []).filter((name) => Object.hasOwn(block.ports, name));
  return [...order, ...names.filter((name) => !order.includes(name)).sort()];
}

/** Export only the selected definition and its project-owned dependencies. */
export function blockDependencies(p: Project, id: string): Record<string, CircuitBlock> {
  const found = new Set<string>();
  const visit = (key: string) => {
    if (found.has(key)) return;
    const block = p.blocks?.[key];
    if (!block) throw new Error(`Unknown block ${key}`);
    found.add(key);
    for (const c of block.components) if (c.component.startsWith('block/')) visit(c.component.slice(6));
  };
  visit(id);
  return Object.fromEntries(Object.entries(p.blocks ?? {}).filter(([key]) => found.has(key)));
}
