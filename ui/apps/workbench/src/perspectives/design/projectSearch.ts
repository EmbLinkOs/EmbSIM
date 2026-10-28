// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { sheetAt, type HierarchyNode, type Project } from '@embsim/protocol';
export interface ProjectSearchHit {
  id: string;
  path: string;
  kind: 'component' | 'net' | 'port' | 'bus';
  name: string;
  detail: string;
  component?: string;
  net?: string;
}
/** Index shared definitions once and concrete instances separately, without expanding installed device internals. */
export function projectSearch(p: Project, nodes: HierarchyNode[], query: string): ProjectSearchHit[] {
  const terms = query.toLowerCase().trim().split(/\s+/).filter(Boolean);
  if (!terms.length) return [];
  const hits: ProjectSearchHit[] = [];
  const add = (hit: ProjectSearchHit) => {
    if (
      terms.every((t) =>
        `${hit.path || 'Main'} ${hit.path ? hit.path + '.' : ''}${hit.name} ${hit.kind} ${hit.name} ${hit.detail}`
          .toLowerCase()
          .includes(t),
      )
    )
      hits.push(hit);
  };
  for (const path of [
    ...new Set(['', ...nodes.map((n) => n.path), ...Object.keys(p.blocks ?? {}).map((id) => `@${id}`)]),
  ]) {
    let sheet: ReturnType<typeof sheetAt>['sheet'];
    try {
      sheet = sheetAt(p, path).sheet;
    } catch {
      continue;
    } // An older native index may arrive during refresh.
    for (const c of sheet.components)
      add({
        id: `${path}:c:${c.id}`,
        path,
        kind: 'component',
        name: c.id,
        component: c.id,
        detail: `${c.component} ${Object.entries(c.params ?? {})
          .map(([k, v]) => `${k}=${v}`)
          .join(' ')}`,
      });
    if (!path)
      for (const b of p.boards ?? [])
        add({
          id: `:c:${b.id}`,
          path,
          kind: 'component',
          name: b.id,
          component: b.id,
          detail: b.board,
        });
    for (const [net, pins] of Object.entries(sheet.nets))
      add({
        id: `${path}:n:${net}`,
        path,
        kind: 'net',
        name: net,
        net,
        detail: pins.join(' · '),
      });
    if ('ports' in sheet)
      for (const [port, pin] of Object.entries(sheet.ports))
        add({
          id: `${path}:p:${port}`,
          path,
          kind: 'port',
          name: port,
          component: pin.split('.')[0],
          net: Object.entries(sheet.nets).find(([, pins]) => pins.includes(pin))?.[0],
          detail: pin,
        });
    for (const bus of sheet.layout?.buses ?? [])
      add({
        id: `${path}:b:${bus.name}`,
        path,
        kind: 'bus',
        name: bus.name,
        net: bus.nets[0],
        detail: bus.nets.join(' · '),
      });
  }
  return hits;
}
