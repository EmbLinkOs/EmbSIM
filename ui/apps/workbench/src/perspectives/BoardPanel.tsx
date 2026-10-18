// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The board view of the current project: the bench. The parts with artwork
// sit in rows above the boards, as on a desk, each labelled, with the
// project's nets drawn as wires between their pins (ground black, supplies
// red, signals in a palette); hotspots stay live.

import { EmptyState, useToast } from '@embsim/design';
import { BoardView, parseSvg, type BenchWire, type BoardItem } from '@embsim/scene';
import { CircuitBoard } from 'lucide-react';
import { useMemo } from 'react';
import { errorMessage, inputs as inputActions } from '../actions';
import { useProject } from '../stores/project';
import { useSession } from '../stores/session';

/** the colours an LED's `color` parameter names, for its artwork's --led-color */
const LED_COLORS: Record<string, string> = {
  red: '#ff3b3b',
  green: '#2fd35a',
  blue: '#3b7bff',
  yellow: '#ffd23b',
  white: '#f4f6ff',
};

interface BoardArtwork {
  width_mm?: number;
  height_mm?: number;
  hotspots?: { selector: string; component?: string; input?: string; output?: string; port?: string }[];
}

/** the colours signal wires take, in turn */
const WIRE_COLORS = ['#f39c12', '#27ae60', '#2e86de', '#8e44ad', '#16a085', '#e84393', '#f1c40f', '#00b8d4', '#a0522d', '#7f8c8d'];
const GROUND = '#2b2b2b';
const SUPPLY = '#d63031';

/** a board port's role: ground, supply or signal */
function portRole(port: string): 'ground' | 'supply' | 'signal' {
  const p = port.toUpperCase();
  if (/^(GND|VSS|AGND)/.test(p)) return 'ground';
  if (/^(5V|3V3|3\.3V|VIN|VCC|VDD|IOREF|VBUS|VSYS|3V3_EN)$/.test(p)) return 'supply';
  return 'signal';
}

function ledColor(params: Record<string, unknown> | undefined): Record<string, string> | undefined {
  const c = params?.color;
  return typeof c === 'string' && LED_COLORS[c] ? { '--led-color': LED_COLORS[c] } : undefined;
}

export function BoardPanel({ interactive }: { interactive: boolean }) {
  const project = useProject((s) => s.project);
  const catalog = useProject((s) => s.catalog);
  const outputs = useSession((s) => s.outputs);
  const inputs = useSession((s) => s.inputs);
  const connected = useSession((s) => s.status === 'open');
  const { toast } = useToast();

  const { items, wires } = useMemo<{ items: BoardItem[]; wires: BenchWire[] }>(() => {
    if (!project) return { items: [], wires: [] };
    const scale = 1.4;
    const parts: BoardItem[] = [];
    const boards: BoardItem[] = [];
    // the parts in rows across the top of the bench
    const rowWidth = 900;
    let x = 20;
    let y = 26;
    let rowHeight = 0;
    for (const c of project.components) {
      const entry = catalog[c.component];
      const art = entry?.description.artwork;
      const svg = entry?.files.artwork;
      if (!art || !svg) continue;
      const vb = parseSvg(svg).viewBox;
      const w = (art.width ?? vb?.width ?? 80) * scale;
      const h = (art.height ?? vb?.height ?? 80) * scale;
      if (x > 20 && x + w > rowWidth) {
        x = 20;
        y += rowHeight + 40;
        rowHeight = 0;
      }
      const own: Record<string, unknown> = {};
      for (const i of inputs) if (i.target === c.id) own[i.input] = i.value;
      const outs: Record<string, boolean | number | string> = {};
      for (const [k, v] of Object.entries(outputs)) if (k.startsWith(`${c.id}.`)) outs[k.slice(c.id.length + 1)] = v;
      parts.push({
        id: c.id,
        label: `${c.id} ${entry?.description.name ?? ''}`.trim(),
        svg,
        x,
        y,
        width: w,
        height: h,
        hotspots: art.hotspots,
        states: art.states,
        inputs: own,
        outputs: outs,
        vars: ledColor(c.params),
      });
      x += w + 40;
      rowHeight = Math.max(rowHeight, h);
    }
    // the boards under them, with their LEDs, buttons and jumpers live
    x = 20;
    const boardY = parts.length ? y + rowHeight + 80 : 22;
    for (const b of project.boards ?? []) {
      const entry = catalog[b.board];
      const art = (entry?.description as { artwork?: BoardArtwork } | undefined)?.artwork;
      const svg = entry?.files.artwork;
      if (!art || !svg) continue;
      const vb = parseSvg(svg).viewBox;
      const h = 220;
      const w = vb ? (h * vb.width) / vb.height : (h * (art.width_mm ?? 53)) / (art.height_mm ?? 23);
      const prefix = `${b.id}.`;
      const own: Record<string, unknown> = {};
      for (const i of inputs) if (i.target.startsWith(prefix)) own[`${i.target.slice(prefix.length)}.${i.input}`] = i.value;
      const outs: Record<string, boolean | number | string> = {};
      for (const [k, v] of Object.entries(outputs)) if (k.startsWith(prefix)) outs[k.slice(prefix.length)] = v;
      const spots = art.hotspots ?? [];
      boards.push({
        id: b.id,
        label: `${b.id} ${entry?.description.name ?? ''}`.trim(),
        svg,
        x,
        y: boardY,
        width: w,
        height: h,
        hotspots: spots
          .filter((hs) => hs.component && hs.input)
          .map((hs) => ({ input: hs.input!, target: hs.component, selector: hs.selector, gesture: hs.input === 'pressed' ? 'hold' : 'toggle', label: `${hs.component} ${hs.input}` })),
        // an LED's glow follows its luminance
        states: spots
          .filter((hs) => hs.component && hs.output)
          .map((hs) => ({ output: `${hs.component}.${hs.output}`, selector: `${hs.selector}-glow`, attr: 'opacity', scale: { from: [0, 0.1] as [number, number] } })),
        inputs: own,
        outputs: outs,
      });
      x += w + 40;
    }
    const list = [...parts, ...boards];
    // the nets between what is on the bench, as wires
    const onBench = new Set(list.map((i) => i.id));
    const boardIds = new Set(boards.map((b) => b.id));
    const kindOf = new Map<string, string>();
    for (const c of project.components) kindOf.set(c.id, c.component);
    let signal = 0;
    const benchWires: BenchWire[] = [];
    for (const [net, terminals] of Object.entries(project.nets)) {
      let role: 'ground' | 'supply' | 'signal' = /^(GND|VSS)/i.test(net) ? 'ground' : 'signal';
      const ends: BenchWire['ends'] = [];
      for (const t of terminals) {
        const dot = t.indexOf('.');
        if (dot < 0) continue;
        const id = t.slice(0, dot);
        const pin = t.slice(dot + 1);
        if (boardIds.has(id)) {
          const r = portRole(pin);
          if (r === 'ground' || (r === 'supply' && role === 'signal')) role = r;
        } else {
          const pins = catalog[kindOf.get(id) ?? '']?.description.pins ?? [];
          const model = pins.find((p) => p.name === pin)?.model as { kind?: string; role?: string } | undefined;
          if (model?.kind === 'power') {
            if (model.role === 'ground') role = 'ground';
            else if (role === 'signal') role = 'supply';
          }
        }
        if (!onBench.has(id)) continue;
        const pins = boardIds.has(id) ? [] : (catalog[kindOf.get(id) ?? '']?.description.pins ?? []);
        const order = pins.findIndex((p) => p.name === pin);
        ends.push({ item: id, pin, order: order < 0 ? 0 : order, count: pins.length || 1 });
      }
      // the board's pins first: each part's wire runs to the nearest end already wired
      ends.sort((a, b) => Number(boardIds.has(b.item)) - Number(boardIds.has(a.item)));
      if (ends.length < 2) continue;
      const color = role === 'ground' ? GROUND : role === 'supply' ? SUPPLY : WIRE_COLORS[signal++ % WIRE_COLORS.length]!;
      benchWires.push({ net, color, ends });
    }
    return { items: list, wires: benchWires };
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
        wires={wires}
        height={Math.max(160, ...items.map((i) => i.y + i.height + 24))}
        disabled={!interactive || !connected}
        label={project?.boards?.length ? `Board ${project.boards[0]!.board}` : 'Component panel'}
        onInput={(target, input, value) => {
          inputActions.set(target, input, value).catch((e: unknown) => toast({ kind: 'error', title: 'Input rejected', description: errorMessage(e) }));
        }}
      />
    </div>
  );
}
