// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { Badge, Button, Dialog, DockLayout, IconButton, Input, SegmentedControl, Tabs, Tooltip, useMediaQuery, useToast, type Command, type DockItem } from '@embsim/design';
import { EMPTY_SELECTION, SchematicCanvas, routeConnection, type CanvasContext, type CanvasMode, type SchematicIssue, type SchematicCanvasHandle } from '@embsim/scene';
import { ArrowLeft, Box, Cable, Hand, Maximize2, MousePointer2, Workflow, ZoomIn, ZoomOut } from 'lucide-react';
import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { errorMessage, inputs as inputActions, probes as probeActions } from '../../actions';
import { useRegisterCommands } from '../../shell/commands';
import { insideSource, insideView } from '../../stores/inside';
import { openInside } from '../../stores/openInside';
import { useCanvasAppearance } from '../../stores/canvasAppearance';
import { useLayout, type DesignView } from '../../stores/layout';
import { useProject } from '../../stores/project';
import { useSelection } from '../../stores/selection';
import { useSession } from '../../stores/session';
import { useSchematicOutputs } from '../../stores/schematicDisplay';
import { SchematicDisplayOptions } from '../run/SchematicDisplayOptions';
import { benchInstrumentOf, openFrontPanel } from '../run/benchInstruments';
import { BoardPanel } from '../BoardPanel';
import { WorldPanel } from '../WorldPanel';
import { InspectorPanel } from './InspectorPanel';
import { LibraryPanel } from './LibraryPanel';
import { ObjectsPanel } from './ObjectsPanel';
import { NetsPanel } from './NetsPanel';
import { SchematicChecksPanel } from './SchematicChecksPanel';
import { useSchematicChecks } from './useSchematicChecks';
import { SchematicContextMenu, type ContextAction } from './SchematicContextMenu';
import { LayoutStudio, SchematicToolbar, type CanvasDisplay, type LayoutPreview } from './SchematicTools';
import { t as tr } from '../../i18n';

export function DesignPerspective() {
  const compact = useMediaQuery('(max-width: 1199px)');
  const project = useProject((s) => s.project);
  const sceneCatalog = useProject((s) => s.sceneCatalog);
  const store = useProject();
  const selection = useSelection((s) => s.selection);
  const setSelection = useSelection((s) => s.setSelection);
  const selectNet = useSelection((s) => s.selectNet);
  const mode = useLayout((s) => s.canvasMode);
  const setMode = useLayout((s) => s.setCanvasMode);
  const running = useSession((s) => s.running);
  const editable = useProject((s) => s.editable);
  const nets = useSession((s) => s.nets);
  const outputs = useSchematicOutputs();
  const inputs = useSession((s) => s.inputs);
  const { toast } = useToast();
  const canvasRef = useRef<SchematicCanvasHandle>(null);
  const [context, setContext] = useState<{ source: typeof project; value: CanvasContext } | null>(null);
  const closeContext = useCallback(() => setContext(null), []);
  const [rename, setRename] = useState<{ net: string; value: string } | null>(null);
  const [bottomTab, setBottomTab] = useState<'nets' | 'studio' | 'checks' | 'objects' | 'world'>('nets');
  const [preview, setPreview] = useState<LayoutPreview | null>(null);
  const [display, setDisplay] = useState<CanvasDisplay>({ airwires: true, minimap: true, smartWires: true, autoArrange: false, guides: true, focusMode: false });
  const appearance = useCanvasAppearance((s) => s.appearance);
  const checks = useSchematicChecks(project, sceneCatalog);
  const [selectedIssue, setSelectedIssue] = useState<string | null>(null);
  const activeIssue = checks.issues.find((i) => i.id === selectedIssue);
  const spotlight = useSelection((s) => s.net);
  const previewProject = preview?.source === project ? preview.result.project : null;
  useEffect(() => {
    if (previewProject) canvasRef.current?.zoomToFit();
  }, [previewProject]);
  const designView = useLayout((s) => s.designView);
  const setDesignView = useLayout((s) => s.setDesignView);
  const inside = useLayout((s) => s.inside);
  const setInside = useLayout((s) => s.setInside);
  const catalog = useProject((s) => s.catalog);

  const fail = useCallback((title: string) => (e: unknown) => toast({ kind: 'error', title, description: errorMessage(e) }), [toast]);

  const sendInput = useCallback((target: string, input: string, value: unknown) => void inputActions.set(target, input, value).catch(fail(tr('fail.input'))), [fail]);
  const sendInsideInput = useCallback((target: string, input: string, value: unknown) => sendInput(`${inside}.${target}`, input, value), [inside, sendInput]);

  const placeAtCentre = useCallback(
    (componentId: string) => {
      const vp = canvasRef.current?.getViewport() ?? { x: 0, y: 0, k: 1 };
      const el = document.querySelector('.es-scene__svg');
      const w = el?.clientWidth ?? 800;
      const h = el?.clientHeight ?? 600;
      const at = { x: (w / 2 - vp.x) / vp.k, y: (h / 2 - vp.y) / vp.k };
      store
        .placeComponent(componentId, at)
        .then((id) => {
          if (id) {
            setSelection({ components: [id], wires: [] });
            toast({ kind: 'success', title: `Placed ${id}`, duration: 2000 });
          }
        })
        .catch(fail(tr('fail.place')));
    },
    [store, setSelection, toast, fail],
  );

  const commands = useMemo<Command[]>(
    () => [
      { id: 'canvas.fit', label: tr('design.zoomFit'), group: tr('design.group.schematic'), icon: <Maximize2 />, shortcut: '0', run: () => canvasRef.current?.zoomToFit() },
      { id: 'canvas.focus-mode', label: display.focusMode ? tr('design.showSidePanels') : tr('design.focusCanvas'), group: tr('design.group.schematic'), run: () => setDisplay((d) => ({ ...d, focusMode: !d.focusMode })) },
      { id: 'canvas.objects', label: tr('design.openObjects'), group: tr('design.group.schematic'), keywords: ['organize', 'lock', 'parts list'], run: () => setBottomTab('objects') },
      { id: 'canvas.checks', label: tr('design.openChecks'), group: tr('design.group.schematic'), keywords: ['diagnostics', 'issues', 'validate'], run: () => setBottomTab('checks') },
      { id: 'canvas.auto-layout', label: tr('design.openStudio'), group: tr('design.group.schematic'), keywords: ['auto', 'arrange', 'route'], run: () => setBottomTab('studio') },
      { id: 'canvas.focus-selection', label: tr('design.focusSelection'), group: tr('design.group.schematic'), shortcut: 'F', disabled: !selection.components.length, run: () => canvasRef.current?.focusComponents(selection.components) },
      { id: 'canvas.zoom-in', label: tr('design.zoomIn'), group: tr('design.group.schematic'), icon: <ZoomIn />, shortcut: '+', run: () => canvasRef.current?.zoomBy(1.25) },
      { id: 'canvas.zoom-out', label: tr('design.zoomOut'), group: tr('design.group.schematic'), icon: <ZoomOut />, shortcut: '-', run: () => canvasRef.current?.zoomBy(0.8) },
      { id: 'canvas.mode.select', label: tr('design.selectTool'), group: tr('design.group.schematic'), icon: <MousePointer2 />, keywords: ['pointer'], run: () => setMode('select') },
      { id: 'canvas.mode.wire', label: tr('design.wireTool'), group: tr('design.group.schematic'), icon: <Cable />, run: () => setMode('wire') },
      {
        id: 'canvas.delete',
        label: tr('design.deleteSelection'),
        group: tr('design.group.schematic'),
        shortcut: 'Del',
        disabled: selection.components.length + selection.wires.length === 0,
        run: () => void store.deleteSelection(selection).catch(fail(tr('fail.delete'))),
      },
      {
        id: 'canvas.rotate',
        label: tr('design.rotateSelection'),
        group: tr('design.group.schematic'),
        shortcut: 'R',
        disabled: selection.components.length === 0,
        run: () => void store.rotateComponents(selection.components).catch(fail(tr('fail.rotate'))),
      },
      {
        id: 'canvas.copy',
        label: tr('design.copySelection'),
        group: tr('design.group.schematic'),
        shortcut: 'Ctrl+C',
        disabled: selection.components.length === 0,
        run: () => store.copySelection(selection),
      },
      {
        id: 'canvas.paste',
        label: tr('design.paste'),
        group: tr('design.group.schematic'),
        shortcut: 'Ctrl+V',
        disabled: !store.clipboard,
        run: () => void store.paste().then((ids) => ids.length && setSelection({ components: ids, wires: [] })).catch(fail(tr('fail.paste'))),
      },
      {
        id: 'canvas.duplicate',
        label: tr('design.duplicateSelection'),
        group: tr('design.group.schematic'),
        shortcut: 'Ctrl+D',
        disabled: selection.components.length === 0,
        run: () => void store.duplicate(selection).then((ids) => ids.length && setSelection({ components: ids, wires: [] })).catch(fail(tr('fail.duplicate'))),
      },
      {
        id: 'canvas.label',
        label: tr('design.labelWire'),
        group: tr('design.group.schematic'),
        shortcut: 'L',
        disabled: selection.wires.length !== 1,
        run: () => void store.labelWire(selection.wires[0]!).catch(fail(tr('fail.label'))),
      },
      {
        id: 'canvas.bus',
        label: tr('design.groupBus'),
        group: tr('design.group.schematic'),
        disabled: selection.wires.length < 2,
        run: () =>
          void store
            .groupIntoBus(selection.wires)
            .then((name) => name && toast({ kind: 'success', title: tr('design.bus', { name }), duration: 2000 }))
            .catch(fail(tr('fail.bus'))),
      },
      ...(project?.layout?.buses ?? []).map((b) => ({
        id: `bus.remove.${b.name}`,
        label: tr('design.removeBus', { name: b.name }),
        group: tr('design.group.schematic'),
        keywords: ['bus', ...b.nets],
        run: () => void store.removeBus(b.name).catch(fail(tr('fail.remove'))),
      })),
      ...Object.entries(sceneCatalog).map(([id, s]) => ({
        id: `place.${id}`,
        label: tr('design.place', { name: s.name }),
        group: tr('design.library'),
        keywords: [id],
        run: () => placeAtCentre(id),
      })),
    ],
    [selection, store, sceneCatalog, placeAtCentre, setMode, setSelection, fail, toast, project?.layout?.buses, display.focusMode],
  );
  useRegisterCommands('design', commands);

  /** Double-click: a board or a composite part opens on its own sheet. */
  const openInsideOf = useCallback(
    (id: string) => {
      setSelection({ components: [id], wires: [] });
      openInside(id).catch(fail(tr('fail.open')));
    },
    [setSelection, fail],
  );

  const insideOf = useMemo(() => {
    if (!inside || !project) return null;
    const ref = project.components.find((c) => c.id === inside)?.component ?? project.boards?.find((b) => b.id === inside)?.board;
    const src = ref ? insideSource(catalog[ref]?.description) : null;
    if (!src) return null;
    const view = insideView(inside, src, sceneCatalog, project);
    const states: typeof nets = {};
    for (const [net, outer] of Object.entries(view.alias)) if (nets[outer]) states[net] = nets[outer]!;
    const childOutputs = Object.fromEntries(Object.entries(outputs).filter(([key]) => key.startsWith(`${inside}.`)).map(([key, value]) => [key.slice(inside.length + 1), value]));
    const childInputs = inputs.filter((entry) => entry.target.startsWith(`${inside}.`)).map((entry) => ({ ...entry, target: entry.target.slice(inside.length + 1) }));
    return { ...view, states, outputs: childOutputs, inputs: childInputs, catalog: { ...sceneCatalog, ...view.scene } };
  }, [inside, project, catalog, sceneCatalog, nets, outputs, inputs]);

  const viewSwitch = (
    <SegmentedControl<DesignView>
      label={tr('design.view')}
      size="sm"
      value={designView}
      onValueChange={setDesignView}
      options={[
        { value: 'schematic', label: tr('design.group.schematic'), icon: <Workflow />, title: tr('design.view.schematic.hint') },
        { value: 'bench', label: tr('design.bench'), icon: <Box />, title: tr('design.view.bench.hint') },
      ]}
    />
  );

  const canvasActions = insideOf ? (
    <div className="wb-canvas-actions">
      <Badge tone="accent" title={tr('design.inside.hint')}>
        {tr('design.insideBadge', { name: insideOf.project.name })}
      </Badge>
      <Button size="sm" variant="ghost" icon={<ArrowLeft />} onClick={() => setInside(null)}>
        {tr('design.backToProject')}
      </Button>
    </div>
  ) : designView === 'bench' ? (
    <div className="wb-canvas-actions">{viewSwitch}</div>
  ) : (
    <div className="wb-canvas-actions">
      {viewSwitch}
      {!editable && (
        <Badge tone="warning" title={tr('design.readOnly.hint')}>
          {tr('design.readOnly')}
        </Badge>
      )}
      <SegmentedControl<CanvasMode>
        label={tr('design.canvasTool')}
        size="sm"
        value={mode}
        onValueChange={setMode}
        options={[
          { value: 'select', label: tr('design.tool.select'), icon: <MousePointer2 />, title: tr('design.tool.select.hint') },
          { value: 'wire', label: tr('design.tool.wire'), icon: <Cable />, title: tr('design.tool.wire.hint') },
          { value: 'interact', label: tr('design.tool.operate'), icon: <Hand />, title: tr('design.tool.operate.hint') },
          { value: 'probe', label: tr('design.tool.probe'), title: tr('design.tool.probe.hint') },
        ]}
      />
      <SchematicDisplayOptions />
      <Tooltip content={tr('design.zoomFit')} shortcut="0">
        <IconButton label={tr('design.zoomFit')} size="sm" variant="ghost" onClick={() => canvasRef.current?.zoomToFit()}>
          <Maximize2 />
        </IconButton>
      </Tooltip>
    </div>
  );

  const canvas = !insideOf && designView === 'bench' ? (
    <BoardPanel interactive={running} canvasFocused={display.focusMode} onFocusCanvas={() => setDisplay((d) => ({ ...d, focusMode: !d.focusMode }))} />
  ) : insideOf ? (
    <SchematicCanvas
      key={`inside-${inside}`}
      appearance={appearance}
      project={insideOf.project}
      catalog={insideOf.catalog}
      mode="interact"
      outputs={insideOf.outputs}
      inputs={insideOf.inputs}
      onInput={sendInsideInput}
      selection={EMPTY_SELECTION}
      onSelectionChange={() => undefined}
      live={running}
      netStates={insideOf.states}
      readOnly
      onOpenComponent={() => setInside(null)}
      label={tr('design.insideOf', { name: insideOf.project.name })}
    />
  ) : project ? (
    <SchematicCanvas
      handle={canvasRef}
      project={previewProject ?? project}
      catalog={sceneCatalog}
      mode={mode}
      selection={selection}
      onSelectionChange={previewProject ? () => undefined : setSelection}
      live={Object.keys(nets).length > 0}
      netStates={nets}
      outputs={outputs}
      inputs={inputs}
      onInput={sendInput}
      readOnly={!editable || !!previewProject}
      appearance={appearance}
      showGrid={appearance.style !== 'blank'}
      showAirwires={display.airwires}
      showMinimap={display.minimap}
      smartWires={display.smartWires && !display.autoArrange}
      alignmentGuides={display.guides}
      highlightNet={spotlight}
      onHighlightNet={selectNet}
      issueMarker={activeIssue?.point ? { point: activeIssue.point, label: activeIssue.title, severity: activeIssue.severity } : undefined}
      onCanvasContext={(value) => setContext({ source: project, value })}
      onReshapeWire={(index, segment, offset, source) => void store.reshapeWire(index, segment, offset, source).catch(fail(tr('fail.reshape')))}
      onMove={(ids, dx, dy) => void store.moveComponents(ids, dx, dy, display.smartWires).catch(fail(tr('fail.move')))}
      onRotate={(ids) => void store.rotateComponents(ids).catch(fail(tr('fail.rotate')))}
      onMirror={(ids) => void store.mirrorComponents(ids).catch(fail(tr('fail.mirror')))}
      onLabelWire={(w) => void store.labelWire(w).catch(fail(tr('fail.label')))}
      onDelete={(sel) => {
        void store.deleteSelection(sel).catch(fail(tr('fail.delete')));
        setSelection({ components: [], wires: [] });
      }}
      onWire={(from, to, points, manual) => {
        const path = !manual && display.smartWires && !display.autoArrange ? routeConnection(project, sceneCatalog, from, 'component' in to ? to : { point: to.point, net: to.net }) : points;
        void store.wire(from, to, path ?? [], !manual && display.autoArrange).then(() => {
          if (!path) toast({ kind: 'warning', title: tr('design.airwire'), description: tr('design.airwire.desc') });
        }).catch(fail(tr('fail.wire')));
      }}
      onPlace={(id, at) => void store.placeComponent(id, at).then((cid) => cid && setSelection({ components: [cid], wires: [] })).catch(fail(tr('fail.place')))}
      onRenameNet={(net) => setRename({ net, value: net })}
      onNetClick={(net) => {
        probeActions
          .add(`net:${net}`, 'analog', 200_000)
          .then((id) => toast({ kind: 'success', title: `Probe ${id} on ${net}` }))
          .catch(fail(tr('fail.probe')));
      }}
      onOpenComponent={openInsideOf}
      label={tr('design.canvasLabel')}
    />
  ) : null;

  function focusIssue(issue: SchematicIssue) {
    setPreview(null);
    setSelectedIssue(issue.id);
    setSelection({ components: issue.components, wires: issue.wires });
    if (issue.net) selectNet(issue.net);
    requestAnimationFrame(() => {
      if (issue.point) canvasRef.current?.focusPoint(issue.point);
      else if (issue.components.length) canvasRef.current?.focusComponents(issue.components);
      else if (issue.net) canvasRef.current?.focusNet(issue.net);
      else canvasRef.current?.focusWires(issue.wires);
    });
  }
  function routeNet(net: string) {
    void store.rerouteNets([net]).then((warnings) => { setSelection(EMPTY_SELECTION); selectNet(net); toast({ kind: warnings.length ? 'warning' : 'success', title: warnings.length ? tr('design.routingAttention') : tr('design.routed', { net }), description: warnings.join(' ') || undefined }); }).catch(fail(tr('fail.routing')));
  }

  const centre: DockItem[] = [
    { id: 'canvas', title: designView === 'bench' && !insideOf ? tr('design.bench') : tr('design.group.schematic'), content: <div className="wb-schematic-workspace">{canvas}{previewProject && <div className="wb-schematic-preview-banner"><Badge tone="accent">{tr('design.layoutPreview')}</Badge><span>{tr('design.reviewInStudio')}</span><Button size="sm" variant="ghost" onClick={() => setPreview(null)}>{tr('common.discard')}</Button></div>}</div>, actions: canvasActions, flush: true, weight: 3, minSize: 200 },
    {
      id: 'bottom',
      title: bottomTab === 'objects' ? tr('design.tab.objects') : bottomTab === 'nets' ? tr('design.tab.nets') : bottomTab === 'studio' ? tr('design.tab.studio') : bottomTab === 'world' ? tr('design.tab.world') : tr('design.tab.checks'),
      flush: true,
      weight: 1,
      collapsible: true,
      minSize: bottomTab === 'checks' || bottomTab === 'objects' ? 260 : 120,
      content: (
        <Tabs<'nets' | 'studio' | 'checks' | 'objects' | 'world'>
          className={bottomTab === 'checks' ? 'wb-design-details--checks' : undefined}
          label={tr('design.details')}
          value={bottomTab}
          onValueChange={setBottomTab}
          items={[
            { value: 'objects', label: tr('design.tab.objects'), content: <ObjectsPanel writable={!previewProject && !insideOf} onFocus={(ids) => { setPreview(null); if (previewProject) requestAnimationFrame(() => canvasRef.current?.focusComponents(ids)); else canvasRef.current?.focusComponents(ids); }} /> },
            { value: 'checks', label: `${tr('design.tab.checks')}${checks.pending ? ' …' : ` ${checks.issues.length}`}`, content: <SchematicChecksPanel {...checks} selected={selectedIssue} onFocus={focusIssue} onRoute={routeNet} onRefresh={checks.refresh} writable={editable && !store.busy && !previewProject && !insideOf} /> },
            { value: 'nets', label: tr('design.tab.nets'), content: <NetsPanel live={running} /> },
            { value: 'world', label: tr('design.tab.world'), content: <WorldPanel /> },
            { value: 'studio', label: tr('design.tab.studio'), content: <LayoutStudio preview={preview} onPreview={setPreview} onApplied={() => canvasRef.current?.zoomToFit()} /> },
          ]}
        />
      ),
    },
  ];

  // the Inspector is contextual: a rail while nothing is selected (the canvas
  // takes its width), open when something is, and always on a compact
  // window until the user opens it; folding or unfolding it by hand wins
  const nothingSelected = !selection.components.length && !selection.wires.length && !spotlight;
  const items: DockItem[] = [
    ...(display.focusMode ? [] : [{ id: 'library', title: tr('design.library'), content: <LibraryPanel onPlace={placeAtCentre} />, flush: true, weight: 1, minSize: 220, collapsible: true }]),
    { id: 'centre', title: tr('design.group.schematic'), bare: true, weight: 3.2, minSize: 360, content: <DockLayout direction="column" items={centre} storageKey={`embsim.layout.design.centre.${project?.name ?? 'none'}`} /> },
    ...(display.focusMode ? [] : [{ id: 'inspector', title: tr('design.inspector'), content: <InspectorPanel />, weight: 1.2, minSize: 260, collapsible: true, defaultCollapsed: compact || nothingSelected }]),
  ];

  const contextSelection = context?.value.selection ?? selection;
  const selectedNets = [...new Set((project?.layout?.wires ?? []).filter((_, i) => contextSelection.wires.includes(i)).map((w) => w.net))];
  const writable = editable && !store.busy && !previewProject && !insideOf;
  const hasParts = contextSelection.components.length > 0;
  const contextInstrument = contextSelection.components.length === 1 ? benchInstrumentOf(project?.components.find((c) => c.id === contextSelection.components[0])?.component) : undefined;
  const copyable = hasParts && contextSelection.components.every((id) => project?.components.some((c) => c.id === id));
  const routeSelected = () => void store.rerouteNets(selectedNets).then((warnings) => { setSelection(EMPTY_SELECTION); if (selectedNets.length === 1) selectNet(selectedNets[0]!); toast({ kind: warnings.length ? 'warning' : 'success', title: warnings.length ? tr('design.routingAttention') : tr('design.netsRouted'), description: warnings.join(' ') || undefined }); }).catch(fail(tr('fail.routing')));
  const contextActions: ContextAction[] = [
    { label: tr('design.focusSelection'), shortcut: 'F', disabled: !hasParts && !selectedNets.length, run: () => hasParts ? canvasRef.current?.focusComponents(contextSelection.components) : canvasRef.current?.focusNet(selectedNets[0]!) },
    { label: tr('design.ctx.copy'), shortcut: '⌘/Ctrl C', disabled: !copyable, run: () => store.copySelection(contextSelection) },
    { label: tr('design.ctx.duplicate'), shortcut: '⌘/Ctrl D', disabled: !copyable || !writable, run: () => void store.duplicate(contextSelection).then((ids) => setSelection({ components: ids, wires: [] })).catch(fail(tr('fail.duplicate'))) },
    { label: tr('design.ctx.pasteHere'), shortcut: '⌘/Ctrl V', disabled: !store.clipboard || !writable, run: () => void store.paste(context?.value.point).then((ids) => setSelection({ components: ids, wires: [] })).catch(fail(tr('fail.paste'))) },
    ...(contextInstrument ? [{ label: tr('design.ctx.frontPanel'), run: () => openFrontPanel(contextInstrument, contextSelection.components[0]!) }] : []),
    { label: tr('design.ctx.rotate'), shortcut: 'R', disabled: !hasParts || !writable, run: () => void store.rotateComponents(contextSelection.components).catch(fail(tr('fail.rotate'))) },
    { label: tr('design.ctx.mirror'), shortcut: 'M', disabled: !hasParts || !writable, run: () => void store.mirrorComponents(contextSelection.components).catch(fail(tr('fail.mirror'))) },
    { label: tr('design.ctx.routeNets'), disabled: !selectedNets.length || !writable, run: routeSelected },
    { label: tr('design.ctx.removeTracks'), disabled: !contextSelection.wires.length || !writable, run: () => void store.eraseWireGeometry(contextSelection.wires).then(() => setSelection(EMPTY_SELECTION)).catch(fail(tr('fail.removeTracks'))) },
    { label: tr('design.ctx.renameNet'), disabled: selectedNets.length !== 1 || !writable, run: () => setRename({ net: selectedNets[0]!, value: selectedNets[0]! }) },
    { label: tr('design.ctx.addLabel'), shortcut: 'L', disabled: contextSelection.wires.length !== 1 || !writable, run: () => void store.labelWire(contextSelection.wires[0]!).catch(fail(tr('fail.label'))) },
    { label: tr('design.ctx.selectAll'), shortcut: '⌘/Ctrl A', run: () => setSelection({ components: [...(project?.components ?? []), ...(project?.boards ?? [])].map((c) => c.id), wires: (project?.layout?.wires ?? []).map((_, i) => i) }) },
    { label: tr('design.ctx.fitSheet'), shortcut: '0', run: () => canvasRef.current?.zoomToFit() },
    { label: tr('design.deleteSelection'), shortcut: '⌫', disabled: (!hasParts && !contextSelection.wires.length) || !writable, danger: true, run: () => void store.deleteSelection(contextSelection).then(() => setSelection(EMPTY_SELECTION)).catch(fail(tr('fail.delete'))) },
  ];

  return (
    <div className="wb-perspective wb-design-studio">
      {!insideOf && designView === 'schematic' && <SchematicToolbar onChecks={() => setBottomTab('checks')} checkCount={checks.issues.length} checking={checks.pending} canvas={canvasRef} display={display} onDisplay={setDisplay} onStudio={() => setBottomTab('studio')} onPreview={setPreview} />}
      <DockLayout direction="row" items={items} storageKey={`embsim.layout.design.${project?.name ?? 'none'}.${display.focusMode ? 'focus' : 'panels'}`} />
      {context && context.source === project && <SchematicContextMenu context={context.value} actions={contextActions} onClose={closeContext} />}
      <Dialog
        open={rename !== null}
        onOpenChange={(open) => !open && setRename(null)}
        title={tr('design.renameNet', { net: rename?.net ?? '' })}
        footer={
          <>
            <button type="button" className="es-button" onClick={() => setRename(null)}>
              {tr('common.cancel')}
            </button>
            <button
              type="button"
              className="es-button es-button--primary"
              onClick={() => {
                if (!rename) return;
                store
                  .renameNet(rename.net, rename.value)
                  .then(() => selectNet(rename.value))
                  .catch(fail(tr('fail.rename')));
                setRename(null);
              }}
            >
              {tr('design.rename')}
            </button>
          </>
        }
      >
        <Input
          label={tr('design.newName')}
          mono
          autoFocus
          value={rename?.value ?? ''}
          onChange={(e) => setRename((r) => (r ? { ...r, value: e.target.value } : r))}
          onKeyDown={(e) => {
            if (e.key === 'Enter' && rename) {
              store.renameNet(rename.net, rename.value).catch(fail(tr('fail.rename')));
              setRename(null);
            }
          }}
        />
      </Dialog>
    </div>
  );
}
