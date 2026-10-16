// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The board view of the current project. The demo project has no board, so
// the view shows the artwork of every component that has one, laid out in a
// row with live hotspots; a project with a board shows the board artwork.

import { EmptyState, useToast } from '@embsim/design';
import { BoardView, parseSvg, type BoardItem } from '@embsim/scene';
import { CircuitBoard } from 'lucide-react';
import { useMemo } from 'react';
import { errorMessage, inputs as inputActions } from '../actions';
import { useProject } from '../stores/project';
import { useSession } from '../stores/session';

export function BoardPanel({ interactive }: { interactive: boolean }) {
  const project = useProject((s) => s.project);
  const catalog = useProject((s) => s.catalog);
  const outputs = useSession((s) => s.outputs);
  const inputs = useSession((s) => s.inputs);
  const connected = useSession((s) => s.status === 'open');
  const { toast } = useToast();

  const items = useMemo<BoardItem[]>(() => {
    if (!project) return [];
    const list: BoardItem[] = [];
    let x = 20;
    const scale = 1.4;
    for (const c of project.components) {
      const entry = catalog[c.component];
      const art = entry?.description.artwork;
      const svg = entry?.files.artwork;
      if (!art || !svg) continue;
      const vb = parseSvg(svg).viewBox;
      const w = (art.width ?? vb?.width ?? 80) * scale;
      const h = (art.height ?? vb?.height ?? 80) * scale;
      const own: Record<string, unknown> = {};
      for (const i of inputs) if (i.target === c.id) own[i.input] = i.value;
      const outs: Record<string, boolean | number | string> = {};
      for (const [k, v] of Object.entries(outputs)) if (k.startsWith(`${c.id}.`)) outs[k.slice(c.id.length + 1)] = v;
      list.push({
        id: c.id,
        label: `${c.id} ${entry?.description.name ?? ''}`.trim(),
        svg,
        x,
        y: 20 + (120 - h) / 2,
        width: w,
        height: h,
        hotspots: art.hotspots,
        states: art.states,
        inputs: own,
        outputs: outs,
      });
      x += w + 36;
    }
    return list;
  }, [project, catalog, inputs, outputs]);

  if (items.length === 0) {
    return (
      <EmptyState icon={<CircuitBoard />} title="No board artwork">
        Add a board, or components with artwork (buttons, LEDs), to interact with them here.
      </EmptyState>
    );
  }
  return (
    <div className="wb-board">
      <BoardView
        items={items}
        height={160}
        disabled={!interactive || !connected}
        label={project?.boards?.length ? `Board ${project.boards[0]!.board}` : 'Component panel'}
        onInput={(target, input, value) => {
          inputActions.set(target, input, value).catch((e: unknown) => toast({ kind: 'error', title: 'Input rejected', description: errorMessage(e) }));
        }}
      />
    </div>
  );
}
