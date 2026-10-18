// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { Badge, Button, Dialog, DockLayout, IconButton, Input, SegmentedControl, Tabs, Tooltip, useMediaQuery, useToast, type Command, type DockItem } from '@embsim/design';
import { EMPTY_SELECTION, SchematicCanvas, routeConnection, type CanvasContext, type CanvasMode, type SchematicIssue, type SchematicCanvasHandle } from '@embsim/scene';
import { ArrowLeft, Box, Cable, Maximize2, MousePointer2, Workflow, ZoomIn, ZoomOut } from 'lucide-react';
import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { errorMessage, probes as probeActions } from '../../actions';
import { useRegisterCommands } from '../../shell/commands';
import { insideSource, insideView } from '../../stores/inside';
import { openInside } from '../../stores/openInside';
import { useCanvasAppearance } from '../../stores/canvasAppearance';
import { useLayout, type DesignView } from '../../stores/layout';
import { useProject } from '../../stores/project';
import { useSelection } from '../../stores/selection';
import { useSession } from '../../stores/session';
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
        .catch(fail('Place failed'));
    },
    [store, setSelection, toast, fail],
  );

  const commands = useMemo<Command[]>(
    () => [
      { id: 'canvas.fit', label: 'Zoom to fit', group: 'Schematic', icon: <Maximize2 />, shortcut: '0', run: () => canvasRef.current?.zoomToFit() },
      { id: 'canvas.focus-mode', label: display.focusMode ? 'Show schematic side panels' : 'Focus canvas: hide side panels', group: 'Schematic', run: () => setDisplay((d) => ({ ...d, focusMode: !d.focusMode })) },
      { id: 'canvas.objects', label: 'Open schematic objects and groups', group: 'Schematic', keywords: ['organize', 'lock', 'parts list'], run: () => setBottomTab('objects') },
      { id: 'canvas.checks', label: 'Open schematic checks', group: 'Schematic', keywords: ['diagnostics', 'issues', 'validate'], run: () => setBottomTab('checks') },
      { id: 'canvas.auto-layout', label: 'Open Layout Studio', group: 'Schematic', keywords: ['auto', 'arrange', 'route'], run: () => setBottomTab('studio') },
      { id: 'canvas.focus-selection', label: 'Focus selection', group: 'Schematic', shortcut: 'F', disabled: !selection.components.length, run: () => canvasRef.current?.focusComponents(selection.components) },
      { id: 'canvas.zoom-in', label: 'Zoom in', group: 'Schematic', icon: <ZoomIn />, shortcut: '+', run: () => canvasRef.current?.zoomBy(1.25) },
      { id: 'canvas.zoom-out', label: 'Zoom out', group: 'Schematic', icon: <ZoomOut />, shortcut: '-', run: () => canvasRef.current?.zoomBy(0.8) },
      { id: 'canvas.mode.select', label: 'Select tool', group: 'Schematic', icon: <MousePointer2 />, keywords: ['pointer'], run: () => setMode('select') },
      { id: 'canvas.mode.wire', label: 'Wire tool', group: 'Schematic', icon: <Cable />, run: () => setMode('wire') },
      {
        id: 'canvas.delete',
        label: 'Delete selection',
        group: 'Schematic',
        shortcut: 'Del',
        disabled: selection.components.length + selection.wires.length === 0,
        run: () => void store.deleteSelection(selection).catch(fail('Delete failed')),
      },
      {
        id: 'canvas.rotate',
        label: 'Rotate selection',
        group: 'Schematic',
        shortcut: 'R',
        disabled: selection.components.length === 0,
        run: () => void store.rotateComponents(selection.components).catch(fail('Rotate failed')),
      },
      {
        id: 'canvas.copy',
        label: 'Copy selection',
        group: 'Schematic',
        shortcut: 'Ctrl+C',
        disabled: selection.components.length === 0,
        run: () => store.copySelection(selection),
      },
      {
        id: 'canvas.paste',
        label: 'Paste',
        group: 'Schematic',
        shortcut: 'Ctrl+V',
        disabled: !store.clipboard,
        run: () => void store.paste().then((ids) => ids.length && setSelection({ components: ids, wires: [] })).catch(fail('Paste failed')),
      },
      {
        id: 'canvas.duplicate',
        label: 'Duplicate selection',
        group: 'Schematic',
        shortcut: 'Ctrl+D',
        disabled: selection.components.length === 0,
        run: () => void store.duplicate(selection).then((ids) => ids.length && setSelection({ components: ids, wires: [] })).catch(fail('Duplicate failed')),
      },
      {
        id: 'canvas.label',
        label: 'Label the selected wire’s net',
        group: 'Schematic',
        shortcut: 'L',
        disabled: selection.wires.length !== 1,
        run: () => void store.labelWire(selection.wires[0]!).catch(fail('Label failed')),
      },
      {
        id: 'canvas.bus',
        label: 'Group the selected wires into a bus',
        group: 'Schematic',
        disabled: selection.wires.length < 2,
        run: () =>
          void store
            .groupIntoBus(selection.wires)
            .then((name) => name && toast({ kind: 'success', title: `Bus ${name}`, duration: 2000 }))
            .catch(fail('Bus failed')),
      },
      ...(project?.layout?.buses ?? []).map((b) => ({
        id: `bus.remove.${b.name}`,
        label: `Remove bus ${b.name}`,
        group: 'Schematic',
        keywords: ['bus', ...b.nets],
        run: () => void store.removeBus(b.name).catch(fail('Remove failed')),
      })),
      ...Object.entries(sceneCatalog).map(([id, s]) => ({
        id: `place.${id}`,
        label: `Place ${s.name}`,
        group: 'Library',
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
      openInside(id).catch(fail('Open failed'));
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
    return { ...view, states, catalog: { ...sceneCatalog, ...view.scene } };
  }, [inside, project, catalog, sceneCatalog, nets]);

  const viewSwitch = (
    <SegmentedControl<DesignView>
      label="Design view"
      size="sm"
      value={designView}
      onValueChange={setDesignView}
      options={[
        { value: 'schematic', label: 'Schematic', icon: <Workflow />, title: 'The schematic' },
        { value: 'bench', label: 'Bench', icon: <Box />, title: 'The bench in 3D: breadboards, parts and jumpers, the same nets' },
      ]}
    />
  );

  const canvasActions = insideOf ? (
    <div className="wb-canvas-actions">
      <Badge tone="accent" title="The inside of a board or part: read-only, its nets live while it runs">
        inside {insideOf.project.name}
      </Badge>
      <Button size="sm" variant="ghost" icon={<ArrowLeft />} onClick={() => setInside(null)}>
        Back to the project
      </Button>
    </div>
  ) : designView === 'bench' ? (
    <div className="wb-canvas-actions">{viewSwitch}</div>
  ) : (
    <div className="wb-canvas-actions">
      {viewSwitch}
      {!editable && (
        <Badge tone="warning" title="This server does not implement project.transaction; the schematic is read-only">
          read-only
        </Badge>
      )}
      <SegmentedControl<CanvasMode>
        label="Canvas tool"
        size="sm"
        value={mode}
        onValueChange={setMode}
        options={[
          { value: 'select', label: 'Select', icon: <MousePointer2 />, title: 'Select and move (V)' },
          { value: 'wire', label: 'Wire', icon: <Cable />, title: 'Wire pin to pin (W)' },
        ]}
      />
      <Tooltip content="Zoom to fit" shortcut="0">
        <IconButton label="Zoom to fit" size="sm" variant="ghost" onClick={() => canvasRef.current?.zoomToFit()}>
          <Maximize2 />
        </IconButton>
      </Tooltip>
    </div>
  );

  const canvas = !insideOf && designView === 'bench' ? (
    <BoardPanel interactive={running} />
  ) : insideOf ? (
    <SchematicCanvas
      key={`inside-${inside}`}
      appearance={appearance}
      project={insideOf.project}
      catalog={insideOf.catalog}
      mode="select"
      selection={EMPTY_SELECTION}
      onSelectionChange={() => undefined}
      live={running}
      netStates={insideOf.states}
      readOnly
      onOpenComponent={() => setInside(null)}
      label={`Inside ${insideOf.project.name}`}
    />
  ) : project ? (
    <SchematicCanvas
      handle={canvasRef}
      project={previewProject ?? project}
      catalog={sceneCatalog}
      mode={mode}
      selection={selection}
      onSelectionChange={previewProject ? () => undefined : setSelection}
      live={running}
      netStates={nets}
      readOnly={!editable || !!previewProject}
      appearance={appearance}
      showGrid={appearance.style !== 'blank'}
      showAirwires={display.airwires}
      showMinimap={display.minimap}
      smartWires={display.smartWires && !display.autoArrange}
      alignmentGuides={display.guides}
      highlightNet={spotlight}
      issueMarker={activeIssue?.point ? { point: activeIssue.point, label: activeIssue.title, severity: activeIssue.severity } : undefined}
      onCanvasContext={(value) => setContext({ source: project, value })}
      onReshapeWire={(index, segment, offset, source) => void store.reshapeWire(index, segment, offset, source).catch(fail('Reshape failed'))}
      onMove={(ids, dx, dy) => void store.moveComponents(ids, dx, dy, display.smartWires).catch(fail('Move failed'))}
      onRotate={(ids) => void store.rotateComponents(ids).catch(fail('Rotate failed'))}
      onMirror={(ids) => void store.mirrorComponents(ids).catch(fail('Mirror failed'))}
      onLabelWire={(w) => void store.labelWire(w).catch(fail('Label failed'))}
      onDelete={(sel) => {
        void store.deleteSelection(sel).catch(fail('Delete failed'));
        setSelection({ components: [], wires: [] });
      }}
      onWire={(from, to, points, manual) => {
        const path = !manual && display.smartWires && !display.autoArrange ? routeConnection(project, sceneCatalog, from, 'component' in to ? to : { point: to.point, net: to.net }) : points;
        void store.wire(from, to, path ?? [], !manual && display.autoArrange).then(() => {
          if (!path) toast({ kind: 'warning', title: 'Connected with an airwire', description: 'No clear drawn route. Rearrange the parts or use Layout Studio to route the connection.' });
        }).catch(fail('Wire failed'));
      }}
      onPlace={(id, at) => void store.placeComponent(id, at).then((cid) => cid && setSelection({ components: [cid], wires: [] })).catch(fail('Place failed'))}
      onRenameNet={(net) => setRename({ net, value: net })}
      onNetClick={(net) => {
        probeActions
          .add(`net:${net}`, 'analog', 200_000)
          .then((id) => toast({ kind: 'success', title: `Probe ${id} on ${net}` }))
          .catch(fail('Probe failed'));
      }}
      onOpenComponent={openInsideOf}
      label="Schematic canvas"
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
    void store.rerouteNets([net]).then((warnings) => { setSelection(EMPTY_SELECTION); selectNet(net); toast({ kind: warnings.length ? 'warning' : 'success', title: warnings.length ? 'Routing needs attention' : `Routed ${net}`, description: warnings.join(' ') || undefined }); }).catch(fail('Routing failed'));
  }

  const centre: DockItem[] = [
    { id: 'canvas', title: designView === 'bench' && !insideOf ? 'Bench' : 'Schematic', content: <div className="wb-schematic-workspace">{canvas}{previewProject && <div className="wb-schematic-preview-banner"><Badge tone="accent">Layout preview</Badge><span>Review in Layout Studio · not applied</span><Button size="sm" variant="ghost" onClick={() => setPreview(null)}>Discard</Button></div>}</div>, actions: canvasActions, flush: true, weight: 3, minSize: 200 },
    {
      id: 'bottom',
      title: bottomTab === 'objects' ? 'Objects' : bottomTab === 'nets' ? 'Nets' : bottomTab === 'studio' ? 'Layout Studio' : bottomTab === 'world' ? 'World' : 'Checks',
      flush: true,
      weight: 1,
      collapsible: true,
      minSize: bottomTab === 'checks' || bottomTab === 'objects' ? 260 : 120,
      content: (
        <Tabs<'nets' | 'studio' | 'checks' | 'objects' | 'world'>
          className={bottomTab === 'checks' ? 'wb-design-details--checks' : undefined}
          label="Design details"
          value={bottomTab}
          onValueChange={setBottomTab}
          items={[
            { value: 'objects', label: 'Objects', content: <ObjectsPanel writable={!previewProject && !insideOf} onFocus={(ids) => { setPreview(null); if (previewProject) requestAnimationFrame(() => canvasRef.current?.focusComponents(ids)); else canvasRef.current?.focusComponents(ids); }} /> },
            { value: 'checks', label: `Checks${checks.pending ? ' …' : ` ${checks.issues.length}`}`, content: <SchematicChecksPanel {...checks} selected={selectedIssue} onFocus={focusIssue} onRoute={routeNet} onRefresh={checks.refresh} writable={editable && !store.busy && !previewProject && !insideOf} /> },
            { value: 'nets', label: 'Nets', content: <NetsPanel live={running} /> },
            { value: 'world', label: 'World', content: <WorldPanel /> },
            { value: 'studio', label: 'Layout Studio', content: <LayoutStudio preview={preview} onPreview={setPreview} onApplied={() => canvasRef.current?.zoomToFit()} /> },
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
    ...(display.focusMode ? [] : [{ id: 'library', title: 'Library', content: <LibraryPanel onPlace={placeAtCentre} />, flush: true, weight: 1, minSize: 220, collapsible: true }]),
    { id: 'centre', title: 'Schematic', bare: true, weight: 3.2, minSize: 360, content: <DockLayout direction="column" items={centre} storageKey={`embsim.layout.design.centre.${project?.name ?? 'none'}`} /> },
    ...(display.focusMode ? [] : [{ id: 'inspector', title: 'Inspector', content: <InspectorPanel />, weight: 1.2, minSize: 260, collapsible: true, defaultCollapsed: compact || nothingSelected }]),
  ];

  const contextSelection = context?.value.selection ?? selection;
  const selectedNets = [...new Set((project?.layout?.wires ?? []).filter((_, i) => contextSelection.wires.includes(i)).map((w) => w.net))];
  const writable = editable && !store.busy && !previewProject && !insideOf;
  const hasParts = contextSelection.components.length > 0;
  const copyable = hasParts && contextSelection.components.every((id) => project?.components.some((c) => c.id === id));
  const routeSelected = () => void store.rerouteNets(selectedNets).then((warnings) => { setSelection(EMPTY_SELECTION); if (selectedNets.length === 1) selectNet(selectedNets[0]!); toast({ kind: warnings.length ? 'warning' : 'success', title: warnings.length ? 'Routing needs attention' : 'Selected nets routed', description: warnings.join(' ') || undefined }); }).catch(fail('Routing failed'));
  const contextActions: ContextAction[] = [
    { label: 'Focus selection', shortcut: 'F', disabled: !hasParts && !selectedNets.length, run: () => hasParts ? canvasRef.current?.focusComponents(contextSelection.components) : canvasRef.current?.focusNet(selectedNets[0]!) },
    { label: 'Copy', shortcut: '⌘/Ctrl C', disabled: !copyable, run: () => store.copySelection(contextSelection) },
    { label: 'Duplicate', shortcut: '⌘/Ctrl D', disabled: !copyable || !writable, run: () => void store.duplicate(contextSelection).then((ids) => setSelection({ components: ids, wires: [] })).catch(fail('Duplicate failed')) },
    { label: 'Paste here', shortcut: '⌘/Ctrl V', disabled: !store.clipboard || !writable, run: () => void store.paste(context?.value.point).then((ids) => setSelection({ components: ids, wires: [] })).catch(fail('Paste failed')) },
    { label: 'Rotate 90°', shortcut: 'R', disabled: !hasParts || !writable, run: () => void store.rotateComponents(contextSelection.components).catch(fail('Rotate failed')) },
    { label: 'Mirror', shortcut: 'M', disabled: !hasParts || !writable, run: () => void store.mirrorComponents(contextSelection.components).catch(fail('Mirror failed')) },
    { label: 'Route selected nets', disabled: !selectedNets.length || !writable, run: routeSelected },
    { label: 'Remove drawn tracks (keep connections)', disabled: !contextSelection.wires.length || !writable, run: () => void store.eraseWireGeometry(contextSelection.wires).then(() => setSelection(EMPTY_SELECTION)).catch(fail('Remove tracks failed')) },
    { label: 'Rename net…', disabled: selectedNets.length !== 1 || !writable, run: () => setRename({ net: selectedNets[0]!, value: selectedNets[0]! }) },
    { label: 'Add net label', shortcut: 'L', disabled: contextSelection.wires.length !== 1 || !writable, run: () => void store.labelWire(contextSelection.wires[0]!).catch(fail('Label failed')) },
    { label: 'Select all', shortcut: '⌘/Ctrl A', run: () => setSelection({ components: [...(project?.components ?? []), ...(project?.boards ?? [])].map((c) => c.id), wires: (project?.layout?.wires ?? []).map((_, i) => i) }) },
    { label: 'Fit sheet', shortcut: '0', run: () => canvasRef.current?.zoomToFit() },
    { label: 'Delete selection', shortcut: '⌫', disabled: (!hasParts && !contextSelection.wires.length) || !writable, danger: true, run: () => void store.deleteSelection(contextSelection).then(() => setSelection(EMPTY_SELECTION)).catch(fail('Delete failed')) },
  ];

  return (
    <div className="wb-perspective wb-design-studio">
      {!insideOf && <SchematicToolbar onChecks={() => setBottomTab('checks')} checkCount={checks.issues.length} checking={checks.pending} canvas={canvasRef} display={display} onDisplay={setDisplay} onStudio={() => setBottomTab('studio')} onPreview={setPreview} />}
      <DockLayout direction="row" items={items} storageKey={`embsim.layout.design.${project?.name ?? 'none'}.${display.focusMode ? 'focus' : 'panels'}`} />
      {context && context.source === project && <SchematicContextMenu context={context.value} actions={contextActions} onClose={closeContext} />}
      <Dialog
        open={rename !== null}
        onOpenChange={(open) => !open && setRename(null)}
        title={`Rename net ${rename?.net ?? ''}`}
        footer={
          <>
            <button type="button" className="es-button" onClick={() => setRename(null)}>
              Cancel
            </button>
            <button
              type="button"
              className="es-button es-button--primary"
              onClick={() => {
                if (!rename) return;
                store
                  .renameNet(rename.net, rename.value)
                  .then(() => selectNet(rename.value))
                  .catch(fail('Rename failed'));
                setRename(null);
              }}
            >
              Rename
            </button>
          </>
        }
      >
        <Input
          label="New name"
          mono
          autoFocus
          value={rename?.value ?? ''}
          onChange={(e) => setRename((r) => (r ? { ...r, value: e.target.value } : r))}
          onKeyDown={(e) => {
            if (e.key === 'Enter' && rename) {
              store.renameNet(rename.net, rename.value).catch(fail('Rename failed'));
              setRename(null);
            }
          }}
        />
      </Dialog>
    </div>
  );
}
