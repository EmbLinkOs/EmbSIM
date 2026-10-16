// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { Badge, Button, Dialog, DockLayout, IconButton, Input, SegmentedControl, Tabs, Tooltip, useToast, type Command, type DockItem } from '@embsim/design';
import { EMPTY_SELECTION, SchematicCanvas, type CanvasMode, type SchematicCanvasHandle } from '@embsim/scene';
import { ArrowLeft, Cable, Maximize2, MousePointer2, ZoomIn, ZoomOut } from 'lucide-react';
import { useCallback, useMemo, useRef, useState } from 'react';
import { errorMessage, probes as probeActions } from '../../actions';
import { useRegisterCommands } from '../../shell/commands';
import { insideSource, insideView } from '../../stores/inside';
import { openInside } from '../../stores/openInside';
import { useLayout } from '../../stores/layout';
import { useProject } from '../../stores/project';
import { useSelection } from '../../stores/selection';
import { useSession } from '../../stores/session';
import { BoardPanel } from '../BoardPanel';
import { InspectorPanel } from './InspectorPanel';
import { LibraryPanel } from './LibraryPanel';
import { NetsPanel } from './NetsPanel';

export function DesignPerspective() {
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
  const [rename, setRename] = useState<{ net: string; value: string } | null>(null);
  const [bottomTab, setBottomTab] = useState<'nets' | 'board'>('nets');
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
    [selection, store, sceneCatalog, placeAtCentre, setMode, setSelection, fail, toast, project?.layout?.buses],
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

  const canvasActions = insideOf ? (
    <div className="wb-canvas-actions">
      <Badge tone="accent" title="The inside of a board or part: read-only, its nets live while it runs">
        inside {insideOf.project.name}
      </Badge>
      <Button size="sm" variant="ghost" icon={<ArrowLeft />} onClick={() => setInside(null)}>
        Back to the project
      </Button>
    </div>
  ) : (
    <div className="wb-canvas-actions">
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

  const canvas = insideOf ? (
    <SchematicCanvas
      key={`inside-${inside}`}
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
      project={project}
      catalog={sceneCatalog}
      mode={mode}
      selection={selection}
      onSelectionChange={setSelection}
      live={running}
      netStates={nets}
      readOnly={!editable}
      onMove={(ids, dx, dy) => void store.moveComponents(ids, dx, dy).catch(fail('Move failed'))}
      onRotate={(ids) => void store.rotateComponents(ids).catch(fail('Rotate failed'))}
      onMirror={(ids) => void store.mirrorComponents(ids).catch(fail('Mirror failed'))}
      onLabelWire={(w) => void store.labelWire(w).catch(fail('Label failed'))}
      onDelete={(sel) => {
        void store.deleteSelection(sel).catch(fail('Delete failed'));
        setSelection({ components: [], wires: [] });
      }}
      onWire={(from, to, points) => void store.wire(from, to, points).catch(fail('Wire failed'))}
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

  const centre: DockItem[] = [
    { id: 'canvas', title: 'Schematic', content: canvas, actions: canvasActions, flush: true, weight: 3, minSize: 200 },
    {
      id: 'bottom',
      title: bottomTab === 'nets' ? 'Nets' : 'Board',
      flush: true,
      weight: 1,
      minSize: 120,
      content: (
        <Tabs<'nets' | 'board'>
          label="Design details"
          value={bottomTab}
          onValueChange={setBottomTab}
          items={[
            { value: 'nets', label: 'Nets', content: <NetsPanel live={running} /> },
            { value: 'board', label: 'Board', content: <BoardPanel interactive={false} /> },
          ]}
        />
      ),
    },
  ];

  const items: DockItem[] = [
    { id: 'library', title: 'Library', content: <LibraryPanel onPlace={placeAtCentre} />, flush: true, weight: 1, minSize: 220 },
    { id: 'centre', title: 'Schematic', bare: true, weight: 3.2, minSize: 360, content: <DockLayout direction="column" items={centre} storageKey={`embsim.layout.design.centre.${project?.name ?? 'none'}`} /> },
    { id: 'inspector', title: 'Inspector', content: <InspectorPanel />, weight: 1.2, minSize: 260 },
  ];

  return (
    <div className="wb-perspective">
      <DockLayout direction="row" items={items} storageKey={`embsim.layout.design.${project?.name ?? 'none'}`} />
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
