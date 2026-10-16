// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import type { Project } from '@embsim/protocol';

/** Assigning members replaces their old membership; empty groups disappear. */
export function assignGroup(project: Project, ids: string[], name: string, groupId?: string): Project {
  const available = new Set([...project.components, ...(project.boards ?? [])].map((p) => p.id));
  const members = [...new Set(ids)].filter((id) => available.has(id)).sort();
  if (!name.trim() || !members.length) throw new Error('Enter a group name and select at least one part.');
  const groups = project.layout?.groups ?? [];
  let id = groupId ?? 'group-1';
  if (!groupId) { let n = 1; while (groups.some((g) => g.id === id)) id = `group-${++n}`; }
  const next = groups.filter((g) => g.id !== id).map((g) => ({ ...g, members: g.members.filter((m) => !members.includes(m)) })).filter((g) => g.members.length);
  next.push({ id, name: name.trim(), members });
  project.layout = { ...project.layout, groups: next };
  return project;
}

const cell = (value: string) => `"${(/^[\s]*[=+\-@\t\r]/.test(value) ? `'${value}` : value).replaceAll('"', '""')}"`;
/** Top-level assembly inventory, not a flattened manufacturing BOM. */
export function partsListCsv(project: Project): string {
  const rows = new Map<string, { kind: string; model: string; version: string; params: string; ids: string[] }>();
  for (const part of [...project.components, ...(project.boards ?? [])]) {
    const kind = 'component' in part ? 'Component' : 'Board';
    const model = 'component' in part ? part.component : part.board;
    const version = 'version' in part ? part.version ?? '' : '';
    const params = 'params' in part ? JSON.stringify(Object.fromEntries(Object.entries(part.params ?? {}).sort(([a], [b]) => a.localeCompare(b)))) : '';
    const key = JSON.stringify([kind, model, version, params]);
    const row = rows.get(key) ?? { kind, model, version, params, ids: [] };
    row.ids.push(part.id); rows.set(key, row);
  }
  return [['Kind', 'Model', 'Version', 'Quantity', 'References', 'Explicit parameters'], ...[...rows.entries()].sort(([a], [b]) => a.localeCompare(b)).map(([, r]) => [r.kind, r.model, r.version, String(r.ids.length), r.ids.sort().join(', '), r.params])].map((row) => row.map(cell).join(',')).join('\r\n') + '\r\n';
}
