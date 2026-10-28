// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { EngineeringReports } from '../engineering/EngineeringReports';
import { AcSweeps } from '../engineering/AcSweeps';
import { TransientCapture } from '../engineering/TransientCapture';
import { ParameterSweeps } from '../engineering/ParameterSweeps';
import { AssemblyVariants } from '../engineering/AssemblyVariants';
import { Badge, Button, IconButton, Input, Select, useToast } from '@embsim/design';
import type { Project } from '@embsim/protocol';
import { type Arrangement, type LayoutOptions, type LayoutResult, type SchematicCanvasHandle } from '@embsim/scene';
import { AlignStartHorizontal, AlignStartVertical, ArrowDownUp, ArrowLeftRight, ClipboardCheck, Focus, Grid2X2, Network, Route, Search, Sparkles, X } from 'lucide-react';
import { useMemo, useState, type RefObject } from 'react';
import { errorMessage } from '../../actions';
import { useProject } from '../../stores/project';
import { useSelection } from '../../stores/selection';
import { planLayout } from './layoutPlanner';
import { CanvasAppearanceControls } from './CanvasAppearanceControls';
import { useCanvasAppearance } from '../../stores/canvasAppearance';

export interface LayoutPreview { source: Project; result: LayoutResult; label: string }
export interface CanvasDisplay { airwires: boolean; minimap: boolean; smartWires: boolean; autoArrange: boolean; guides: boolean; focusMode: boolean }

/** Only presentation fields can enter a layout transaction, even if a future
 * planner accidentally returns changed parameters or connectivity. */
export function applyLayoutFields(current: Project, planned: Project): Project {
  const layouts = new Map([...planned.components, ...(planned.boards ?? [])].map((c) => [c.id, c.layout]));
  for (const c of [...current.components, ...(current.boards ?? [])]) {
    const layout = layouts.get(c.id);
    if (layout && !c.layout?.locked) c.layout = { ...structuredClone(layout), locked: c.layout?.locked };
  }
  current.layout = { ...(current.layout ?? {}), wires: structuredClone(planned.layout?.wires ?? []) };
  return current;
}

export function SchematicToolbar({ canvas, onStudio, onPreview, display, onDisplay, onChecks, checkCount, checking, onSheets }: {
  canvas: RefObject<SchematicCanvasHandle | null>;
  onSheets?(): void;
  onChecks?(): void;
  checkCount?: number;
  checking?: boolean;
  onStudio(): void;
  onPreview(preview: LayoutPreview): void;
  display: CanvasDisplay;
  onDisplay(display: CanvasDisplay): void;
}) {
  const project = useProject((s) => s.project);
  const catalog = useProject((s) => s.sceneCatalog);
  const busy = useProject((s) => s.busy);
  const editable = useProject((s) => s.editable);
  const selection = useSelection((s) => s.selection);
  const [finder, setFinder] = useState(false);
  const [query, setQuery] = useState('');
  const [menu, setMenu] = useState<'arrange' | 'view' | null>(null);
  const [planning, setPlanning] = useState(false);
  const { toast } = useToast();
  const appearance = useCanvasAppearance((s) => s.appearance);
  const updateAppearance = useCanvasAppearance((s) => s.update);
  const selectedNet = useSelection((s) => s.net);
  const entries = useMemo(() => {
    if (!project) return [];
    const q = query.trim().toLowerCase();
    return [
      ...project.components.map((c) => ({ id: c.id, detail: catalog[c.component]?.name ?? c.component, kind: 'component' as const })),
      ...(project.boards ?? []).map((b) => ({ id: b.id, detail: catalog[b.board]?.name ?? b.board, kind: 'component' as const })),
      ...Object.entries(project.nets).map(([id, pins]) => ({ id, detail: `${pins.length} pins · ${pins.join(', ')}`, kind: 'net' as const })),
    ].filter((e) => `${e.id} ${e.detail}`.toLowerCase().includes(q)).slice(0, 60);
  }, [project, catalog, query]);
  const focusEntry = (entry: typeof entries[number]) => {
    if (entry.kind === 'component') { useSelection.getState().selectComponent(entry.id); canvas.current?.focusComponents([entry.id]); }
    else {
      useSelection.getState().setSelection({ components: [], wires: (project?.layout?.wires ?? []).flatMap((w, i) => w.net === entry.id ? [i] : []) });
      useSelection.getState().selectNet(entry.id); canvas.current?.focusNet(entry.id);
    }
    setFinder(false); canvas.current?.focus();
  };
  const alignment: { value: Arrangement; label: string }[] = [
    { value: 'left', label: 'Align left' }, { value: 'center-x', label: 'Align horizontal centers' }, { value: 'right', label: 'Align right' },
    { value: 'top', label: 'Align top' }, { value: 'center-y', label: 'Align vertical centers' }, { value: 'bottom', label: 'Align bottom' },
    { value: 'distribute-x', label: 'Distribute horizontally' }, { value: 'distribute-y', label: 'Distribute vertically' },
  ];
  return (
    <div className="wb-schematic-ribbon">
      <div className="wb-schematic-ribbon__identity"><Network size={18} /><div><strong>Schematic</strong><span>{project?.components.length ?? 0} parts · {Object.keys(project?.nets ?? {}).length} nets</span></div></div>
      <div className="wb-schematic-ribbon__tools" role="toolbar" aria-label="Schematic tools">
        <EngineeringReports />
        <AssemblyVariants />
        <ParameterSweeps />
        <TransientCapture />
        <AcSweeps />
        {onSheets && <Button size="sm" variant="ghost" onClick={onSheets}>Sheets and reusable blocks</Button>}
        <Button size="sm" icon={<Sparkles />} disabled={!project || !editable || busy} onClick={onStudio}>Auto layout</Button>
        <Button size="sm" variant="ghost" icon={<AlignStartHorizontal />} disabled={!editable || busy || selection.components.length < 2} onClick={() => { setMenu(menu === 'arrange' ? null : 'arrange'); setFinder(false); }}>Arrange</Button>
        <Button size="sm" variant="ghost" icon={<Focus />} disabled={!selection.components.length && !selectedNet} onClick={() => selectedNet ? canvas.current?.focusNet(selectedNet) : canvas.current?.focusComponents(selection.components)}>Focus</Button>
        {onChecks && <Button size="sm" variant="ghost" icon={<ClipboardCheck />} onClick={onChecks}>Checks{checking ? ' …' : checkCount ? ` ${checkCount}` : ''}</Button>}
        <Button size="sm" variant="ghost" icon={<Search />} onClick={() => { setFinder(!finder); setMenu(null); }}>Find</Button>
        <Button size="sm" variant="ghost" icon={<Grid2X2 />} onClick={() => { setMenu(menu === 'view' ? null : 'view'); setFinder(false); }}>View</Button>
      </div>
      {selectedNet && <button type="button" className="wb-schematic-net-focus" onClick={() => useSelection.getState().selectNet(null)} title="Clear net spotlight">{selectedNet} <X size={12} /></button>}
      {menu && (
        <div className="wb-schematic-popover" role="region" aria-label={menu === 'arrange' ? 'Arrange components' : 'Canvas display'} onKeyDown={(e) => { if (e.key === 'Escape') setMenu(null); }}>
          <div className="wb-schematic-popover__heading"><strong>{menu === 'arrange' ? 'Arrange selection' : 'Canvas display'}</strong><IconButton label="Close tools" size="sm" variant="ghost" onClick={() => setMenu(null)}><X /></IconButton></div>
          {menu === 'arrange' ? alignment.map((a) => (
            <button type="button" key={a.value} disabled={planning || busy || !editable || selection.components.length < (a.value.startsWith('distribute') ? 3 : 2)} onClick={() => {
              if (!project) return;
              setPlanning(true); setMenu(null); onStudio();
              void planLayout({ kind: 'arrange', project, catalog, ids: selection.components, action: a.value })
                .then((result) => onPreview({ source: project, result, label: a.label }))
                .catch((e: unknown) => toast({ kind: 'error', title: 'Cannot arrange selection', description: errorMessage(e) }))
                .finally(() => setPlanning(false));
            }}>{a.value.startsWith('distribute') ? <ArrowLeftRight size={15} /> : <AlignStartVertical size={15} />}{a.label}</button>
          )) : <><CanvasAppearanceControls /><div className="wb-canvas-appearance__heading"><span>EDITING & NAVIGATION</span></div><label><input type="checkbox" checked={appearance.style !== 'blank'} onChange={(e) => updateAppearance({ style: e.target.checked ? 'engineering' : 'blank' })} />Show grid</label>{(Object.keys(display) as (keyof CanvasDisplay)[]).map((key) => (
            <label key={key}><input type="checkbox" checked={display[key]} onChange={(e) => onDisplay({ ...display, [key]: e.target.checked })} />{key === 'airwires' ? 'Unrouted airwires' : key === 'smartWires' ? 'Obstacle-aware wiring' : key === 'autoArrange' ? 'Auto arrange after connecting' : key === 'guides' ? 'Magnetic alignment guides' : key === 'focusMode' ? 'Focus canvas (hide side panels)' : 'Sheet navigator'}</label>
          ))}</>}
        </div>
      )}
      {finder && (
        <div className="wb-schematic-popover wb-schematic-finder" role="region" aria-label="Find in schematic" onKeyDown={(e) => { if (e.key === 'Escape') setFinder(false); }}>
          <div className="wb-schematic-popover__heading"><strong>Find in this sheet</strong><IconButton label="Close finder" size="sm" variant="ghost" onClick={() => setFinder(false)}><X /></IconButton></div>
          <Input label="Component or net" placeholder="R1, UART, temperature…" autoFocus value={query} onChange={(e) => setQuery(e.target.value)} onKeyDown={(e) => { if (e.key === 'Enter' && entries[0]) focusEntry(entries[0]); }} />
          <div className="wb-schematic-finder__results">
            {entries.length ? entries.map((e) => <button type="button" key={`${e.kind}:${e.id}`} onClick={() => focusEntry(e)}><span><strong>{e.id}</strong><Badge>{e.kind === 'net' ? 'net' : 'part'}</Badge></span><small>{e.detail}</small></button>) : <p>No matching components or nets.</p>}
          </div>
        </div>
      )}
    </div>
  );
}

export function LayoutStudio({ preview, onPreview, onApplied }: {
  preview: LayoutPreview | null;
  onPreview(preview: LayoutPreview | null): void;
  onApplied(): void;
}) {
  const project = useProject((s) => s.project);
  const catalog = useProject((s) => s.sceneCatalog);
  const editable = useProject((s) => s.editable);
  const busy = useProject((s) => s.busy);
  const selection = useSelection((s) => s.selection);
  const [direction, setDirection] = useState<LayoutOptions['direction']>('horizontal');
  const [spacing, setSpacing] = useState<LayoutOptions['spacing']>('comfortable');
  const [scope, setScope] = useState<'sheet' | 'selection'>('sheet');
  const [planning, setPlanning] = useState(false);
  const { toast } = useToast();
  const stale = !!preview && preview.source !== project;
  const disabled = !project || !editable || busy || planning;
  const plan = (routeOnly: boolean) => {
    if (!project || disabled) return;
    if (scope === 'selection' && !selection.components.length) return;
    setPlanning(true);
    const selected = scope === 'selection' ? selection.components : undefined;
    const affected = selected ? Object.entries(project.nets).filter(([, pins]) => pins.some((p) => selected.includes(p.split('.')[0]!))).map(([net]) => net) : undefined;
    void planLayout(routeOnly ? { kind: 'route', project, catalog, affected } : { kind: 'layout', project, catalog, options: { direction, spacing, selected, route: true } })
      .then((result) => onPreview({ source: project, result, label: routeOnly ? 'Route connections' : 'Auto layout' }))
      .catch((e: unknown) => toast({ kind: 'error', title: 'Cannot create layout', description: errorMessage(e) }))
      .finally(() => setPlanning(false));
  };

  const apply = async () => {
    if (!preview || stale || disabled) return;
    try {
      if (useProject.getState().project !== preview.source) throw new Error('The project changed. Generate a fresh preview.');
      await useProject.getState().mutate(preview.label, (p) => applyLayoutFields(p, preview.result.project));
      onPreview(null); onApplied();
      toast({ kind: 'success', title: 'Layout applied', description: 'Undo restores the previous placement and wires.' });
    } catch (e) { toast({ kind: 'error', title: 'Cannot apply layout', description: errorMessage(e) }); }
  };
  return (
    <div className="wb-layout-studio" role="region" aria-label="Layout studio">
      <div className="wb-layout-studio__intro"><span className="wb-layout-studio__eyebrow"><Sparkles size={14} />LAYOUT STUDIO</span><strong>Arrange by connectivity</strong><p>Arrange connected groups and route around symbols. Preview first; apply as one undoable change.</p></div>
      <div className="wb-layout-studio__controls">
        <Select label="Layout scope" size="sm" value={scope} onValueChange={setScope} options={[{ value: 'sheet', label: 'Whole sheet' }, { value: 'selection', label: `Selection (${selection.components.length})` }]} />
        <Select label="Layout direction" size="sm" value={direction} onValueChange={setDirection} options={[{ value: 'horizontal', label: 'Left to right' }, { value: 'vertical', label: 'Top to bottom' }]} />
        <Select label="Layout spacing" size="sm" value={spacing} onValueChange={setSpacing} options={[{ value: 'comfortable', label: 'Comfortable spacing' }, { value: 'compact', label: 'Compact spacing' }]} />
        <div className="es-row"><Button size="sm" icon={<Sparkles />} disabled={disabled || (scope === 'selection' && !selection.components.length)} onClick={() => plan(false)}>{planning ? 'Planning…' : 'Preview layout'}</Button><Button size="sm" variant="ghost" icon={<Route />} disabled={disabled || (scope === 'selection' && !selection.components.length)} onClick={() => plan(true)}>Route only</Button></div>
      </div>
      <div className="wb-layout-studio__review">
        {preview ? <>
          <div className="es-row"><Badge tone={stale ? 'warning' : 'accent'}>{stale ? 'Preview out of date' : 'Preview · not applied'}</Badge><span className="es-muted">{preview.label}</span></div>
          <div className="wb-layout-studio__metrics"><span><strong>{preview.result.moved}</strong>moved</span><span><strong>{preview.result.routed}</strong>routes</span><span><strong>{preview.result.unresolved}</strong>unresolved</span></div>
          {stale && <p role="alert">The project changed. Generate a fresh preview.</p>}
          {preview.result.warnings.length > 0 && <details><summary>{preview.result.warnings.length} routing notes</summary><ul>{preview.result.warnings.slice(0, 30).map((w, i) => <li key={i}>{w}</li>)}</ul></details>}
          <div className="es-row"><Button size="sm" variant="primary" disabled={disabled || stale} onClick={() => void apply()}>Apply layout</Button><Button size="sm" variant="ghost" onClick={() => onPreview(null)}>Discard preview</Button></div>
        </> : <div className="wb-layout-studio__empty"><ArrowDownUp size={22} /><span>Generate a layout to review it on the canvas.</span><small>Electrical nets, values and firmware stay intact. Selected-only placement keeps other parts fixed.</small></div>}
      </div>
    </div>
  );
}
