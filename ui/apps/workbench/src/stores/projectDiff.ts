// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// Computes the project.transaction edits that turn one project into another
// (SPEC-008 §5, SPEC-007). Every editing command is expressed as a pure
// function on the project; the diff is sent forward on execute and the
// reverse diff on undo, so the server stays the single source of truth.

import type { Project, ProjectEdit } from '@embsim/protocol';

function pinMap(p: Project): Map<string, string> {
  const m = new Map<string, string>();
  for (const [net, pins] of Object.entries(p.nets)) for (const pin of pins) m.set(pin, net);
  return m;
}

function sameSet(a: string[], b: string[]): boolean {
  if (a.length !== b.length) return false;
  const s = new Set(a);
  return b.every((x) => s.has(x));
}

function layoutEqual(a: Project['components'][number]['layout'], b: Project['components'][number]['layout']): boolean {
  return (a?.x ?? 0) === (b?.x ?? 0) && (a?.y ?? 0) === (b?.y ?? 0) && (a?.rotation ?? 0) === (b?.rotation ?? 0) && Boolean(a?.mirror) === Boolean(b?.mirror);
}

export function diffProject(before: Project, after: Project): ProjectEdit[] {
  const edits: ProjectEdit[] = [];
  const beforeById = new Map(before.components.map((c) => [c.id, c]));
  const afterById = new Map(after.components.map((c) => [c.id, c]));

  // Components added, changed.
  for (const c of after.components) {
    const old = beforeById.get(c.id);
    if (!old) {
      edits.push({ add_component: { id: c.id, component: c.component, version: c.version, params: c.params, layout: c.layout } });
      continue;
    }
    const keys = new Set([...Object.keys(old.params ?? {}), ...Object.keys(c.params ?? {})]);
    for (const k of keys) {
      if (old.params?.[k] !== c.params?.[k]) edits.push({ set_param: { id: c.id, param: k, value: c.params?.[k] } });
    }
    if (!layoutEqual(old.layout, c.layout)) edits.push({ set_layout: { id: c.id, layout: c.layout ?? { x: 0, y: 0 } } });
  }

  // Net renames: a vanished net whose pin set reappears under a new name.
  const renamed = new Map<string, string>();
  for (const [name, pins] of Object.entries(before.nets)) {
    if (name in after.nets) continue;
    const match = Object.entries(after.nets).find(([n, p]) => !(n in before.nets) && sameSet(p, pins));
    if (match) {
      renamed.set(name, match[0]);
      edits.push({ rename_net: { from: name, to: match[0] } });
    }
  }

  // Pin connections.
  const pb = pinMap(before);
  const pa = pinMap(after);
  for (const [pin, net] of pa) {
    const oldNet = pb.get(pin);
    const effectiveOld = oldNet !== undefined ? renamed.get(oldNet) ?? oldNet : undefined;
    if (effectiveOld !== net && afterById.has(pin.split('.')[0] ?? '')) edits.push({ connect: { net, pin } });
  }
  for (const [pin] of pb) {
    if (!pa.has(pin) && afterById.has(pin.split('.')[0] ?? '')) edits.push({ disconnect: { pin } });
  }

  // Components removed (the server drops their pins).
  for (const c of before.components) if (!afterById.has(c.id)) edits.push({ remove_component: { id: c.id } });

  // Presentation layout.
  const wiresChanged = JSON.stringify(before.layout?.wires ?? []) !== JSON.stringify(after.layout?.wires ?? []);
  const labelsChanged = JSON.stringify(before.layout?.labels ?? []) !== JSON.stringify(after.layout?.labels ?? []);
  if (wiresChanged || labelsChanged) {
    edits.push({ set_layout: { wires: after.layout?.wires ?? [], labels: after.layout?.labels ?? [] } });
  }
  return edits;
}
