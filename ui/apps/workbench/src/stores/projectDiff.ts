// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// Computes the project.transaction edits that turn one project into another
// (SPEC-008 §5, SPEC-007). Every editing command is expressed as a pure
// function on the project; the diff is sent forward on execute and the
// reverse diff on undo, so the server stays the single source of truth.

import type { Layout, Project, ProjectEdit } from '@embsim/protocol';

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
  return (a?.x ?? 0) === (b?.x ?? 0) && (a?.y ?? 0) === (b?.y ?? 0) && (a?.rotation ?? 0) === (b?.rotation ?? 0) && Boolean(a?.mirror) === Boolean(b?.mirror) && Boolean(a?.locked) === Boolean(b?.locked);
}

function transactionLayout(before: Layout | undefined, after: Layout | undefined): Layout {
  const layout = after ?? { x: 0, y: 0 };
  return before?.locked && !after?.locked ? { ...layout, locked: false } : layout;
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
    if (!layoutEqual(old.layout, c.layout)) edits.push({ set_layout: { id: c.id, layout: transactionLayout(old.layout, c.layout) } });
  }

  // Boards added, moved, removed.
  const boardsBefore = new Map((before.boards ?? []).map((b) => [b.id, b]));
  const boardsAfter = new Map((after.boards ?? []).map((b) => [b.id, b]));
  for (const b of after.boards ?? []) {
    const old = boardsBefore.get(b.id);
    if (!old) edits.push({ add_board: { id: b.id, board: b.board, layout: b.layout, firmware: b.firmware } });
    else if (!layoutEqual(old.layout, b.layout)) edits.push({ set_layout: { id: b.id, layout: transactionLayout(old.layout, b.layout) } });
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
  const present = (pin: string) => {
    const id = pin.split('.')[0] ?? '';
    return afterById.has(id) || boardsAfter.has(id);
  };
  const pb = pinMap(before);
  const pa = pinMap(after);
  for (const [pin, net] of pa) {
    const oldNet = pb.get(pin);
    const effectiveOld = oldNet !== undefined ? renamed.get(oldNet) ?? oldNet : undefined;
    if (effectiveOld !== net && present(pin)) edits.push({ connect: { net, pin } });
  }
  for (const [pin] of pb) {
    if (!pa.has(pin) && present(pin)) edits.push({ disconnect: { pin } });
  }

  // Components removed (the server drops their pins).
  for (const c of before.components) if (!afterById.has(c.id)) edits.push({ remove_component: { id: c.id } });
  for (const b of before.boards ?? []) if (!boardsAfter.has(b.id)) edits.push({ remove_board: { id: b.id } });

  if (JSON.stringify(before.layout?.groups ?? []) !== JSON.stringify(after.layout?.groups ?? [])) edits.push({ set_layout: { groups: after.layout?.groups ?? [] } });

  // Presentation layout.
  const wiresChanged = JSON.stringify(before.layout?.wires ?? []) !== JSON.stringify(after.layout?.wires ?? []);
  const labelsChanged = JSON.stringify(before.layout?.labels ?? []) !== JSON.stringify(after.layout?.labels ?? []);
  const busesChanged = JSON.stringify(before.layout?.buses ?? []) !== JSON.stringify(after.layout?.buses ?? []);
  if (wiresChanged || labelsChanged || busesChanged) {
    edits.push({ set_layout: { wires: after.layout?.wires ?? [], labels: after.layout?.labels ?? [], buses: after.layout?.buses ?? [] } });
  }
  return edits;
}
