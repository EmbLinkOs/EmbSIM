// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The bench (ADR-027): the project's boards, breadboards and parts in 3D,
// a second editor of the same nets as the schematic. Parts dragged onto a
// breadboard plug into its holes, jumpers join holes and pins, and what the
// schematic drew but the bench has not built yet shows as dashed links that
// one click builds. Every edit is a transaction on the project, undone as
// the schematic's are. Without WebGL, the 2D bench below stands in.

import { Badge, Button, Dialog, EmptyState, IconButton, Input, SegmentedControl, Tooltip, useToast } from '@embsim/design';
import {
  addJumper,
  addNote,
  beams,
  Bench3D,
  benchContext,
  benchModel,
  elevationOf,
  jumperViews,
  linkViews,
  moveParts,
  removeJumper,
  snapPart,
  terminalNear,
  unbuilt,
  WIRE_COLORS,
  type Bench3DHandle,
  type BenchPartView,
  type BenchTool,
} from '@embsim/bench';
import type { ArtworkState, Hotspot, OutputValue, Project } from '@embsim/protocol';
import { BoardView, parseSvg, type BenchWire as Wire2D, type BoardItem } from '@embsim/scene';
import { Box, Cable, CircuitBoard, Maximize2, MousePointer2, PanelRightClose, PanelRightOpen, StickyNote, Wand2 } from 'lucide-react';
import { useCallback, useMemo, useRef, useState } from 'react';
import { errorMessage, inputs as inputActions } from '../actions';
import { useProject, type CatalogEntry } from '../stores/project';
import { useSelection } from '../stores/selection';
import { useSession } from '../stores/session';

/** the bench's parts list folded or open, as the user left it */
const LIST_KEY = 'embsim.bench.list';

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

function ledColor(params: Record<string, unknown> | undefined): Record<string, string> | undefined {
  const c = params?.color;
  return typeof c === 'string' && LED_COLORS[c] ? { '--led-color': LED_COLORS[c] } : undefined;
}

function webglAvailable(): boolean {
  try {
    const c = document.createElement('canvas');
    return Boolean(c.getContext('webgl2') ?? c.getContext('webgl'));
  } catch {
    return false;
  }
}

/** a board's artwork bindings: its LEDs' glow from their luminance, its buttons */
function boardBindings(art: BoardArtwork | undefined): { states: ArtworkState[]; hotspots: Hotspot[] } {
  const spots = art?.hotspots ?? [];
  return {
    states: spots
      .filter((hs) => hs.component && hs.output)
      .map((hs) => ({ output: `${hs.component}.${hs.output}`, selector: `${hs.selector}-glow`, attr: 'opacity', scale: { from: [0, 0.1] as [number, number] } })),
    hotspots: spots
      .filter((hs) => hs.component && hs.input)
      .map((hs) => ({ input: hs.input!, target: hs.component, selector: hs.selector, gesture: hs.input === 'pressed' ? 'hold' : 'toggle' }) as Hotspot),
  };
}

export function BoardPanel({ interactive, editable: canEdit = true }: { interactive: boolean; editable?: boolean }) {
  const project = useProject((s) => s.project);
  const catalog = useProject((s) => s.catalog);
  const editableProject = useProject((s) => s.editable);
  const outputs = useSession((s) => s.outputs);
  const inputs = useSession((s) => s.inputs);
  const connected = useSession((s) => s.status === 'open');
  const { toast } = useToast();
  const [tool, setTool] = useState<BenchTool>('select');
  const [wireColor, setWireColor] = useState(WIRE_COLORS[0]!);
  // the selected part is the workbench's selection (the inspector shows it, the schematic too); a jumper is the bench's own
  const globalPart = useSelection((s) => (s.selection.components.length === 1 ? s.selection.components[0] : undefined));
  const [jumper, setJumper] = useState<number | undefined>(undefined);
  const selected = useMemo(() => (jumper !== undefined ? { jumper } : globalPart ? { part: globalPart } : {}), [jumper, globalPart]);
  const setSelected = useCallback((sel: { part?: string; jumper?: number }) => {
    setJumper(sel.jumper);
    if (sel.part) useSelection.getState().selectComponent(sel.part);
    else useSelection.getState().clear();
  }, []);
  const [note, setNote] = useState<string | null>(null);
  const [flat, setFlat] = useState(() => typeof document === 'undefined' || !webglAvailable());
  const [listOpen, setListOpen] = useState(() => {
    try {
      return localStorage.getItem(LIST_KEY) !== 'folded';
    } catch {
      return true;
    }
  });
  const handle = useRef<Bench3DHandle>(null);
  const editable = canEdit && editableProject;

  const fail = useCallback((title: string) => (e: unknown) => toast({ kind: 'error', title, description: errorMessage(e) }), [toast]);

  const ctx = useMemo(() => (project ? benchContext(project, (ref) => entryOf(catalog[ref])) : null), [project, catalog]);
  const model = useMemo(() => (project && ctx ? benchModel(project, ctx.shapes, ctx.ties) : null), [project, ctx]);

  const parts = useMemo<BenchPartView[]>(() => {
    if (!project || !model) return [];
    const byId = new Map<string, { component: string; params?: Record<string, unknown>; board: boolean }>();
    for (const b of project.boards ?? []) byId.set(b.id, { component: b.board, board: true });
    for (const c of project.components) byId.set(c.id, { component: c.component, params: c.params, board: false });
    return model.parts.map((part) => {
      const inst = byId.get(part.id)!;
      const entry = catalog[inst.component];
      const own: Record<string, unknown> = {};
      const outs: Record<string, OutputValue> = {};
      const prefix = `${part.id}.`;
      for (const i of inputs) {
        if (i.target === part.id) own[i.input] = i.value;
        else if (inst.board && i.target.startsWith(prefix)) own[`${i.target.slice(prefix.length)}.${i.input}`] = i.value;
      }
      for (const [k, v] of Object.entries(outputs)) if (k.startsWith(prefix)) outs[k.slice(prefix.length)] = v;
      const svg = entry?.files.artwork;
      const art = entry?.description.artwork as (BoardArtwork & { states?: ArtworkState[]; hotspots?: Hotspot[] }) | undefined;
      const bindings = inst.board ? boardBindings(art) : { states: art?.states ?? [], hotspots: (art?.hotspots ?? []) as Hotspot[] };
      return {
        id: part.id,
        label: `${part.id} ${entry?.description.name ?? ''}`.trim(),
        component: inst.component,
        params: inst.params,
        shape: part.shape,
        place: part.place,
        elevation: elevationOf(model, part.id),
        artwork: svg ? { svg, states: bindings.states, hotspots: bindings.hotspots, vars: ledColor(inst.params) } : undefined,
        inputs: own,
        outputs: outs,
      };
    });
  }, [project, model, catalog, inputs, outputs]);

  const jumpers = useMemo(() => (project && model ? jumperViews(project, model) : []), [project, model]);
  const links = useMemo(() => (project && model ? linkViews(project, model) : []), [project, model]);
  const pending = useMemo(() => (project && model ? unbuilt(project, model) : []), [project, model]);
  // the world's bodies and what the distance sensors see of them (ADR-026)
  const bodies = useMemo(() => project?.world?.bodies ?? [], [project]);
  const sensed = useMemo(() => (model ? beams(model.parts, bodies) : []), [model, bodies]);

  const mutate = useCallback(
    (label: string, fn: (p: Project) => Project | null) => {
      void useProject.getState().mutate(label, fn).catch(fail(`${label} failed`));
    },
    [fail],
  );

  if (!project || !ctx || !model) {
    return (
      <EmptyState icon={<CircuitBoard />} title="No project">
        Open a project to build it on the bench.
      </EmptyState>
    );
  }

  const selectedPart = selected.part ? parts.find((p) => p.id === selected.part) : undefined;
  const selectedHoles = selectedPart ? [...model.plugs].filter(([pin]) => pin.startsWith(`${selectedPart.id}.`)).map(([pin, hole]) => `${pin.split('.').slice(1).join('.')}→${hole.split('.')[1]}`) : [];
  const selectedJumper = selected.jumper !== undefined ? project.layout?.bench?.wires?.[selected.jumper] : undefined;

  const toggleList = () =>
    setListOpen((open) => {
      try {
        localStorage.setItem(LIST_KEY, open ? 'folded' : 'open');
      } catch {
        // storage unavailable: the choice lasts the session
      }
      return !open;
    });

  const toolbar = (
    <div className="wb-bench-toolbar" role="toolbar" aria-label="Bench tools">
      {!editable && <Badge tone="warning">read-only</Badge>}
      <SegmentedControl<BenchTool>
        label="Bench tool"
        size="sm"
        value={tool}
        onValueChange={setTool}
        options={[
          { value: 'select', label: 'Move', icon: <MousePointer2 />, title: 'Move and plug parts (drag; R turns, arrows nudge, Delete removes)' },
          { value: 'wire', label: 'Jumper', icon: <Cable />, title: 'Wire a jumper from a hole or pin to another' },
        ]}
      />
      <div className="wb-bench-colors" role="radiogroup" aria-label="Jumper colour">
        {WIRE_COLORS.map((c) => (
          <button key={c} type="button" role="radio" aria-checked={c === wireColor} aria-label={`Jumper colour ${c}`} className="wb-bench-color" style={{ background: c }} onClick={() => setWireColor(c)} />
        ))}
      </div>
      <Tooltip content={pending.length ? `Wire the ${pending.length} connection${pending.length > 1 ? 's' : ''} the schematic has that the bench does not` : 'Everything the schematic connects is built'}>
        <Button
          size="sm"
          variant="ghost"
          disabled={!editable || pending.length === 0}
          onClick={() =>
            mutate('Build connections', (p) => {
              let next = p;
              pending.forEach((c, i) => {
                next = addJumper(next, ctx, { from: c.from, to: c.to, color: WIRE_COLORS[(i + 2) % WIRE_COLORS.length] });
              });
              return next;
            })
          }
        >
          <Wand2 /> Build {pending.length || ''}
        </Button>
      </Tooltip>
      <Tooltip content="Put an 830-point breadboard on the bench">
        <Button
          size="sm"
          variant="ghost"
          disabled={!editable}
          onClick={() =>
            void useProject
              .getState()
              .placeComponent('emblink/breadboard_830', { x: 0, y: 0 })
              .then((id) => id && setSelected({ part: id }))
              .catch(fail('Add breadboard failed'))
          }
        >
          <Box /> Breadboard
        </Button>
      </Tooltip>
      <Tooltip content="A note on the bench">
        <IconButton label="Add a note" size="sm" variant="ghost" disabled={!editable} onClick={() => setNote('')}>
          <StickyNote />
        </IconButton>
      </Tooltip>
      <Tooltip content="Frame everything" shortcut="F">
        <IconButton label="Frame the bench" size="sm" variant="ghost" onClick={() => handle.current?.frame()}>
          <Maximize2 />
        </IconButton>
      </Tooltip>
      <Tooltip content={listOpen ? 'Fold the list of parts: the scene takes its width' : 'The list of parts: where each leg is, plugging and jumpers from the keyboard'}>
        <IconButton label={listOpen ? 'Fold the parts list' : 'Show the parts list'} size="sm" variant="ghost" aria-pressed={listOpen} onClick={toggleList}>
          {listOpen ? <PanelRightClose /> : <PanelRightOpen />}
        </IconButton>
      </Tooltip>
      <SegmentedControl<'3d' | '2d'>
        label="Bench view"
        size="sm"
        value={flat ? '2d' : '3d'}
        onValueChange={(v) => setFlat(v === '2d')}
        options={[
          { value: '3d', label: '3D', title: 'The bench in 3D' },
          { value: '2d', label: '2D', title: 'The parts\' artwork, flat' },
        ]}
      />
      <span className="wb-bench-status" aria-live="polite">
        {selectedPart
          ? `${selectedPart.label}${selectedHoles.length ? ` · in ${selectedHoles.join(', ')}` : ' · not plugged in'}`
          : selectedJumper
            ? `Jumper ${selectedJumper.from} → ${selectedJumper.to}`
            : tool === 'wire'
              ? 'Press on a hole or a pin and release on another'
              : 'Right drag orbits · wheel zooms · drag a part onto the breadboard'}
      </span>
    </div>
  );

  /** the place that puts the part's first leg in `hole`, keeping its turn */
  const plugAt = (id: string, hole: string): { x: number; y: number; rotation: 0 | 90 | 180 | 270 } | null => {
    const part = model.byId.get(id);
    const h = model.holes.get(hole.includes('.') ? hole : `${[...model.parts].find((x) => x.shape.package === 'breadboard')?.id}.${hole}`);
    const first = part ? Object.keys(part.shape.pins)[0] : undefined;
    if (!part || !h || !first) return null;
    const [lx, ly] = part.legs[first]!;
    return { x: Math.round((part.place.x + h.x - lx) * 100) / 100, y: Math.round((part.place.y + h.y - ly) * 100) / 100, rotation: part.place.rotation };
  };

  const list = (
    <BenchList
      parts={parts}
      plugs={model.plugs}
      selected={selected}
      editable={editable}
      onSelect={setSelected}
      onPlug={(id, hole) => {
        const at = plugAt(id, hole);
        if (!at) {
          toast({ kind: 'warning', title: `No hole ${hole}`, description: 'Name a hole of a breadboard on the bench: a1 to j63, or a rail hole as TP1, TN1, BP1, BN1.' });
          return;
        }
        mutate(`Plug ${id} into ${hole}`, (p) => moveParts(p, ctx, { [id]: at }));
      }}
      onTurn={(id) => {
        const part = model.byId.get(id);
        if (!part) return;
        const rotation = ((part.place.rotation + 90) % 360) as 0 | 90 | 180 | 270;
        mutate(`Turn ${id}`, (p) => moveParts(p, ctx, { [id]: { ...part.place, rotation } }));
      }}
      onLift={(id) => {
        const part = model.byId.get(id);
        if (!part) return;
        // lifted off and set down on the desk behind everything
        const back = Math.min(...model.parts.map((x) => x.place.y - x.shape.size[1] / 2)) - part.shape.size[1] / 2 - 10;
        mutate(`Lift ${id}`, (p) => moveParts(p, ctx, { [id]: { ...part.place, y: Math.round(back * 100) / 100 } }));
      }}
      onJumper={(from, to) => mutate('Jumper', (p) => addJumper(p, ctx, { from, to, color: wireColor }))}
    />
  );

  return (
    <div className="wb-bench">
      {toolbar}
      <div className="wb-bench-body">
      {flat ? (
        <Bench2D interactive={interactive} />
      ) : (
        <Bench3D
          handle={handle}
          parts={parts}
          jumpers={jumpers}
          links={links}
          notes={project.layout?.bench?.notes ?? []}
          bodies={bodies}
          beams={sensed}
          onMoveBody={(id, at) =>
            mutate(`Move ${id}`, (p) => {
              const list = structuredClone(p.world?.bodies ?? []);
              const b = list.find((x) => x.id === id);
              if (!b) return null;
              b.at = [Math.round(at[0]), Math.round(at[1]), b.at[2]];
              p.world = { ...(p.world ?? {}), bodies: list };
              return p;
            })
          }
          tool={tool}
          live={interactive && connected}
          editable={editable}
          wireColor={wireColor}
          selected={selected}
          snap={(id, x, y, rotation) => snapPart(model, ctx, id, x, y, rotation)}
          terminalAt={(x, y, part) => terminalNear(model, x, y, part)}
          onMove={(id, place) => mutate(`Move ${id}`, (p) => moveParts(p, ctx, { [id]: place }))}
          onWire={(from, to) => mutate('Jumper', (p) => addJumper(p, ctx, { from, to, color: wireColor }))}
          onSelect={setSelected}
          onDelete={(sel) => {
            if (sel.jumper !== undefined) mutate('Remove jumper', (p) => removeJumper(p, ctx, sel.jumper!));
            else if (sel.part) void useProject.getState().deleteSelection({ components: [sel.part], wires: [] }).catch(fail('Delete failed'));
            setSelected({});
          }}
          onInput={(target, input, value) => {
            inputActions.set(target, input, value).catch((e: unknown) => toast({ kind: 'error', title: 'Input rejected', description: errorMessage(e) }));
          }}
          label={`Bench of ${project.name}`}
        />
      )}
      {listOpen && list}
      </div>
      {note !== null && (
        <Dialog
          open
          onOpenChange={(open) => !open && setNote(null)}
          title="A note on the bench"
          footer={
            <>
              <Button variant="ghost" onClick={() => setNote(null)}>
                Cancel
              </Button>
              <Button
                variant="primary"
                disabled={!note.trim()}
                onClick={() => {
                  const target = selectedPart?.place;
                  const at = target ? { x: target.x, y: target.y - 40 } : { x: 0, y: -30 };
                  mutate('Note', (p) => addNote(p, at.x, at.y, note.trim(), target ? [target.x, target.y] : undefined));
                  setNote(null);
                }}
              >
                Add
              </Button>
            </>
          }
        >
          <Input label="Text" value={note} autoFocus onChange={(e) => setNote(e.target.value)} />
        </Dialog>
      )}
    </div>
  );
}

/**
 * The bench as a list (ADR-027: every bench action has a keyboard
 * equivalent): each part with the holes its legs are in; for the one
 * selected, plugging its first leg into a named hole, turning it, lifting
 * it off; a jumper between two named terminals.
 */
function BenchList({
  parts,
  plugs,
  selected,
  editable,
  onSelect,
  onPlug,
  onTurn,
  onLift,
  onJumper,
}: {
  parts: BenchPartView[];
  plugs: Map<string, string>;
  selected: { part?: string; jumper?: number };
  editable: boolean;
  onSelect(sel: { part?: string }): void;
  onPlug(id: string, hole: string): void;
  onTurn(id: string): void;
  onLift(id: string): void;
  onJumper(from: string, to: string): void;
}) {
  const [hole, setHole] = useState('');
  const [from, setFrom] = useState('');
  const [to, setTo] = useState('');
  const holesOf = (id: string) => [...plugs].filter(([pin]) => pin.startsWith(`${id}.`)).map(([, h]) => h.split('.')[1]);
  const part = selected.part ? parts.find((p) => p.id === selected.part) : undefined;
  return (
    <aside className="wb-bench-list" aria-label="On the bench">
      <h3>On the bench</h3>
      <ul>
        {parts.map((p) => {
          const holes = holesOf(p.id);
          return (
            <li key={p.id}>
              <button type="button" aria-pressed={selected.part === p.id} onClick={() => onSelect({ part: p.id })}>
                <span className="wb-bench-list__id">{p.id}</span>
                <span className="wb-bench-list__where">{p.shape.package === 'breadboard' || p.shape.package === 'board' ? p.label.slice(p.id.length + 1) : holes.length ? `in ${holes.join(' ')}` : 'on the desk'}</span>
              </button>
            </li>
          );
        })}
      </ul>
      {part && editable && part.shape.package !== 'breadboard' && part.shape.package !== 'board' && (
        <form
          className="wb-bench-list__form"
          onSubmit={(e) => {
            e.preventDefault();
            if (hole.trim()) onPlug(part.id, hole.trim());
          }}
        >
          <Input label={`Plug ${part.id}'s first leg into`} placeholder="a20" value={hole} onChange={(e) => setHole(e.target.value)} />
          <div className="wb-bench-list__row">
            <Button size="sm" type="submit" disabled={!hole.trim()}>
              Plug in
            </Button>
            <Button size="sm" variant="ghost" onClick={() => onTurn(part.id)}>
              Turn
            </Button>
            <Button size="sm" variant="ghost" onClick={() => onLift(part.id)}>
              Lift off
            </Button>
          </div>
        </form>
      )}
      {editable && (
        <form
          className="wb-bench-list__form"
          onSubmit={(e) => {
            e.preventDefault();
            if (from.trim() && to.trim()) {
              onJumper(from.trim(), to.trim());
              setFrom('');
              setTo('');
            }
          }}
        >
          <Input label="Jumper from" placeholder="BB1.e20" value={from} onChange={(e) => setFrom(e.target.value)} />
          <Input label="to" placeholder="B1.D9" value={to} onChange={(e) => setTo(e.target.value)} />
          <Button size="sm" type="submit" disabled={!from.trim() || !to.trim()}>
            Add jumper
          </Button>
        </form>
      )}
    </aside>
  );
}

function entryOf(entry: CatalogEntry | undefined) {
  return entry ? { description: entry.description, artwork: entry.files.artwork } : undefined;
}

/** a board port's role: ground, supply or signal */
function portRole(port: string): 'ground' | 'supply' | 'signal' {
  const p = port.toUpperCase();
  if (/^(GND|VSS|AGND)/.test(p)) return 'ground';
  if (/^(5V|3V3|3\.3V|VIN|VCC|VDD|IOREF|VBUS|VSYS|3V3_EN)$/.test(p)) return 'supply';
  return 'signal';
}

const WIRES_2D = ['#f39c12', '#27ae60', '#2e86de', '#8e44ad', '#16a085', '#e84393', '#f1c40f', '#00b8d4', '#a0522d', '#7f8c8d'];

/** the flat bench: the parts' artwork in rows above the boards, the nets as wires */
function Bench2D({ interactive }: { interactive: boolean }) {
  const project = useProject((s) => s.project);
  const catalog = useProject((s) => s.catalog);
  const outputs = useSession((s) => s.outputs);
  const inputs = useSession((s) => s.inputs);
  const connected = useSession((s) => s.status === 'open');
  const { toast } = useToast();

  const { items, wires } = useMemo<{ items: BoardItem[]; wires: Wire2D[] }>(() => {
    if (!project) return { items: [], wires: [] };
    const scale = 1.4;
    const parts: BoardItem[] = [];
    const boards: BoardItem[] = [];
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
      parts.push({ id: c.id, label: `${c.id} ${entry?.description.name ?? ''}`.trim(), svg, x, y, width: w, height: h, hotspots: art.hotspots, states: art.states, inputs: own, outputs: outs, vars: ledColor(c.params) });
      x += w + 40;
      rowHeight = Math.max(rowHeight, h);
    }
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
      const { states, hotspots } = boardBindings(art);
      boards.push({ id: b.id, label: `${b.id} ${entry?.description.name ?? ''}`.trim(), svg, x, y: boardY, width: w, height: h, hotspots: hotspots.map((hs) => ({ ...hs, label: `${hs.target} ${hs.input}` })), states, inputs: own, outputs: outs });
      x += w + 40;
    }
    const list = [...parts, ...boards];
    const onBench = new Set(list.map((i) => i.id));
    const boardIds = new Set(boards.map((b) => b.id));
    const kindOf = new Map<string, string>();
    for (const c of project.components) kindOf.set(c.id, c.component);
    let signal = 0;
    const out: Wire2D[] = [];
    for (const [net, terminals] of Object.entries(project.nets)) {
      let role: 'ground' | 'supply' | 'signal' = /^(GND|VSS)/i.test(net) ? 'ground' : 'signal';
      const ends: Wire2D['ends'] = [];
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
          const m = pins.find((p) => p.name === pin)?.model as { kind?: string; role?: string } | undefined;
          if (m?.kind === 'power') {
            if (m.role === 'ground') role = 'ground';
            else if (role === 'signal') role = 'supply';
          }
        }
        if (!onBench.has(id)) continue;
        const pins = boardIds.has(id) ? [] : (catalog[kindOf.get(id) ?? '']?.description.pins ?? []);
        const order = pins.findIndex((p) => p.name === pin);
        ends.push({ item: id, pin, order: order < 0 ? 0 : order, count: pins.length || 1 });
      }
      ends.sort((a, b) => Number(boardIds.has(b.item)) - Number(boardIds.has(a.item)));
      if (ends.length < 2) continue;
      const color = role === 'ground' ? '#2b2b2b' : role === 'supply' ? '#d63031' : WIRES_2D[signal++ % WIRES_2D.length]!;
      out.push({ net, color, ends });
    }
    return { items: list, wires: out };
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
