// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { Button, DockLayout, IconButton, SegmentedControl, Tabs, Tooltip, useToast, type Command, type DockItem } from '@embsim/design';
import { followWindow, LogicAnalyzer, Oscilloscope, SerialTerminal, type ScopeTrace } from '@embsim/instruments';
import { SchematicCanvas, type CanvasMode, type Point, type SchematicCanvasHandle } from '@embsim/scene';
import { Download, Locate, Maximize2, Trash2 } from 'lucide-react';
import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { errorMessage, exportCsv, exportVcd, inputs as inputActions, probes as probeActions } from '../../actions';
import { platform } from '../../platform/platform';
import { useRegisterCommands } from '../../shell/commands';
import { DiagnosticsPanel } from '../../shell/DiagnosticsPanel';
import { useInstruments } from '../../stores/instruments';
import { useHierarchy } from '../../stores/hierarchy';
import { useLayout } from '../../stores/layout';
import { useProject } from '../../stores/project';
import { useSelection } from '../../stores/selection';
import { useCanvasAppearance } from '../../stores/canvasAppearance';
import { useSession } from '../../stores/session';
import { WorldPanel } from '../WorldPanel';
import { VoltageComparison } from './VoltageComparison';
import { AcquisitionPanel } from './AcquisitionPanel';
import { AirPanel } from './AirPanel';
import { ControlsPanel } from './ControlsPanel';
import { MetersPanel } from './MetersPanel';
import { useHostPort } from './useHostPort';
import { SchematicDisplayOptions } from './SchematicDisplayOptions';
import { useSchematicOutputs } from '../../stores/schematicDisplay';
import { isWiredInstrument, WiredInstrumentsPanel } from './WiredInstrumentsPanel';

const TRACE_COLORS = ['#5fe3b1', '#f0975a', '#7cbcf5', '#f27d84', '#e9bd55', '#c48cf0'];

export function RunPerspective() {
  const appearance = useCanvasAppearance((s) => s.appearance);
  const now = useSession((s) => s.now);
  const nets = useSession((s) => s.nets);
  const outputs = useSchematicOutputs();
  const inputs = useSession((s) => s.inputs);
  const [mode, setMode] = useState<CanvasMode>('interact');
  const [comparison, setComparison] = useState<{project: unknown; a?: string; b?: string; aPoint?: Point; bPoint?: Point}>({project:null});
  const [pickLead, setPickLead] = useState<'a'|'b'|null>(null);
  const project = useProject((s) => s.project);
  const leads = comparison.project === project ? comparison : {};
  const changeLead = useCallback((side: 'a'|'b', net: string, at?: Point) => setComparison((old) => ({ ...(old.project === project ? old : {}), project, [side]: net || undefined, [`${side}Point`]: at })), [project]);
  const sceneCatalog = useProject((s) => s.sceneCatalog);
  const selection = useSelection((s) => s.selection);
  const setSelection = useSelection((s) => s.setSelection);
  const selectedNet = useSelection((s) => s.net);
  const selectNet = useSelection((s) => s.selectNet);
  const inst = useInstruments();
  const serialTargets = inst.probes.filter((p) => p.kind === 'serial').map((p) => p.target.replace(/^serial:/, ''));
  // the port chosen, else the first: a terminal can send before one is picked
  const serialTarget = inst.serialTarget && serialTargets.includes(inst.serialTarget) ? inst.serialTarget : (serialTargets[0] ?? null);
  const hostPort = useHostPort(serialTarget);
  const { toast } = useToast();
  const [bottomTab, setBottomTab] = useState<'serial' | 'diagnostics' | 'meters' | 'air'>('meters');
  const schematicRef = useRef<SchematicCanvasHandle>(null);
  const [focusInstrument, setFocusInstrument] = useState<{ id: string } | null>(null);
  useEffect(() => {
    if (!focusInstrument) return;
    const frame = requestAnimationFrame(() => schematicRef.current?.focusComponents([focusInstrument.id]));
    return () => cancelAnimationFrame(frame);
  }, [bottomTab, focusInstrument]);

  const capture = inst.acquisition?.frame;
  const records = capture?.records ?? inst.records;
  const plotNow = capture ? capture.window.start + capture.window.span : now;
  const win = followWindow(inst.window, plotNow);
  const scopeTraces = useMemo<ScopeTrace[]>(
    () => capture
      ? [...records.values()].filter((r) => r.kind === 'analog').map((record, i) => ({ record, color: TRACE_COLORS[i % TRACE_COLORS.length]! }))
      : inst.probes
        .filter((p) => p.kind === 'analog' || p.kind === 'value')
        .map((p, i) => ({ record: records.get(p.probe as string)!, color: TRACE_COLORS[i % TRACE_COLORS.length]! }))
        .filter((t) => t.record),
    [inst.probes, records, capture],
  );
  const lookupRecord = useCallback((id: string) => records.get(id), [records]);
  // a digital probe's decoder comes from its definition in the project
  const logicChannels = useMemo(
    () => capture
      ? [...records.values()].filter((r) => r.kind === 'digital').map((record) => ({ record, decode: project?.probes?.find((p) => p.id === record.id)?.decode }))
      : inst.probes
        .filter((p) => p.kind === 'digital')
        .map((p) => ({
          record: records.get(p.probe as string)!,
          decode: p.decode ?? project?.probes?.find((d) => d.id === (p.id ?? String(p.probe)))?.decode,
        }))
        .filter((c) => c.record),
    [inst.probes, records, project?.probes, capture],
  );

  const noScope = scopeTraces.length === 0;
  const fitCapture = () => {
    const captured = [...records.values()].filter((r) => r.start !== null && r.captureEnd !== null);
    if (!captured.length) return;
    const start = captured.reduce((v, r) => r.start! < v ? r.start! : v, captured[0]!.start!);
    const frontier = captured.reduce((v, r) => r.captureEnd! > v ? r.captureEnd! : v, start);
    const end = frontier < plotNow ? frontier : plotNow;
    inst.setWindow({ start, span: end > start ? end - start : 1n, follow: false });
  };

  const exportAs = async (kind: 'csv' | 'vcd') => {
    const ids = [...records.keys()];
    const from = win.start;
    const to = win.start + win.span;
    const text = kind === 'csv' ? exportCsv(ids, from, to) : exportVcd(ids, from, to);
    const p = await platform();
    const where = await p.saveTextFile(`embsim-${project?.name ?? 'session'}.${kind}`, text, kind === 'csv' ? 'text/csv' : 'text/plain');
    if (where) toast({ kind: 'success', title: `Exported ${where}` });
  };
  const guard = (fn: () => Promise<unknown>) => () => void fn().catch((e: unknown) => toast({ kind: 'error', title: 'Action failed', description: errorMessage(e) }));

  const sendInput = useCallback((target: string, input: string, value: unknown) => void inputActions.set(target, input, value).catch((e: unknown) => toast({ kind: 'error', title: 'Input rejected', description: errorMessage(e) })), [toast]);

  const commands = useMemo<Command[]>(
    () => [
      { id: 'run.export.csv', label: 'Export window as CSV', group: 'Instruments', icon: <Download />, run: guard(() => exportAs('csv')) },
      { id: 'run.export.vcd', label: 'Export window as VCD', group: 'Instruments', icon: <Download />, run: guard(() => exportAs('vcd')) },
      { id: 'run.follow', label: inst.window.follow ? 'Stop following the clock' : 'Follow the clock', group: 'Instruments', shortcut: 'F', run: () => inst.setWindow({ ...inst.window, follow: !inst.window.follow }) },
      { id: 'run.clear-cursors', label: 'Clear cursors', group: 'Instruments', run: () => inst.setCursors({ a: null, b: null }) },
      { id: 'run.fit-capture', label: 'Fit captured waveforms', group: 'Instruments', run: fitCapture },
      ...(project ? Object.keys(project.nets).map((net) => ({ id: `probe.${net}`, label: `Add probe on net ${net}`, group: 'Probes', keywords: ['probe', net], run: guard(() => probeActions.add(`net:${net}`, 'analog', 200_000)) })) : []),
      ...inst.probes.map((p) => ({ id: `unprobe.${p.probe}`, label: `Remove probe ${p.probe}`, group: 'Probes', icon: <Trash2 />, run: guard(() => probeActions.remove(p.probe as string)) })),
      { id: 'input.press', label: 'Press and release the button (SW1)', group: 'Inputs', keywords: ['button'], run: guard(async () => {
        await inputActions.press('SW1');
        await new Promise((r) => setTimeout(r, 150));
        await inputActions.release('SW1');
      }) },
    ],
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [inst.window, inst.probes, project, now, inst.records],
  );
  useRegisterCommands('run', commands);

  const probesList = (
    <div className="es-row" style={{ flexWrap: 'nowrap', gap: 'var(--space-1)' }}>
      {inst.probes.map((p) => (
        <span key={p.probe} className="es-badge" title={p.target}>
          {p.probe}
          <IconButton label={`Remove probe ${p.probe}`} size="sm" variant="ghost" style={{ width: 16, height: 16 }} onClick={guard(() => probeActions.remove(p.probe as string))}>
            <Trash2 />
          </IconButton>
        </span>
      ))}
    </div>
  );

  const instrumentActions = (
    <>
      <Tooltip content="Fit captured waveforms">
        <IconButton label="Fit captured waveforms" size="sm" variant="ghost" onClick={fitCapture}><Maximize2 /></IconButton>
      </Tooltip>
      <Tooltip content={inst.window.follow ? 'Following the clock (F)' : 'Follow the clock (F)'}>
        <IconButton label={inst.window.follow ? 'Stop following the clock' : 'Follow the clock'} size="sm" variant="ghost" pressed={inst.window.follow} onClick={() => inst.setWindow({ ...inst.window, follow: !inst.window.follow })}>
          <Locate />
        </IconButton>
      </Tooltip>
      <Button size="sm" variant="ghost" icon={<Download />} onClick={guard(() => exportAs('csv'))}>
        CSV
      </Button>
      <Button size="sm" variant="ghost" icon={<Download />} onClick={guard(() => exportAs('vcd'))}>
        VCD
      </Button>
    </>
  );

  const left: DockItem[] = [
    { id: 'acquisition', title: 'Acquisition and probes', weight: 1.5, minSize: 240, collapsible: true, defaultCollapsed: false, content: <AcquisitionPanel /> },
    { id: 'wired-instruments', title: 'Wired instruments', weight: 1.2, minSize: 140, collapsible: true, defaultCollapsed: !project?.components.some((c) => isWiredInstrument(c.component)), content: <WiredInstrumentsPanel onLocate={(id) => { setSelection({ components: [id], wires: [] }); setFocusInstrument({ id }); }} onTrace={(target) => guard(() => probeActions.add(target, 'value', 200_000))()} /> },
    { id: 'voltage-comparison', title: 'Voltage comparison', weight: 1, minSize: 170, collapsible: true, defaultCollapsed: true, content: <VoltageComparison names={Object.keys(project?.nets ?? {}).sort()} nets={nets} leads={leads} onChange={changeLead} active={pickLead} onPick={(side) => { setPickLead(side); if (side) setMode('probe'); }} /> },
    { id: 'controls', title: 'Controls', content: <ControlsPanel />, collapsible: true, defaultCollapsed: inputs.length === 0, weight: 1, minSize: 120 },
    { id: 'world', title: 'World', content: <WorldPanel />, weight: 1, minSize: 120, collapsible: true, defaultCollapsed: true },
  ];
  const centre: DockItem[] = [
    { id: 'schematic', title: 'Live schematic', weight: 2.4, minSize: 240, flush: true, actions: <><SegmentedControl label="Schematic interaction" value={mode} onValueChange={(next) => { setMode(next); setPickLead(null); }} options={[{value:'interact',label:'Operate'},{value:'select',label:'Inspect'},{value:'probe',label:'Probe'}]} /><SchematicDisplayOptions /><IconButton label="Fit live schematic" size="sm" variant="ghost" onClick={() => schematicRef.current?.zoomToFit()}><Maximize2 /></IconButton></>, content: project ? (
              <SchematicCanvas
                handle={schematicRef}
                appearance={appearance}
                project={project}
                catalog={sceneCatalog}
                mode={mode}
                selection={selection}
                onSelectionChange={setSelection}
                live
                netStates={nets}
                outputs={outputs}
                inputs={inputs}
                onInput={sendInput}
                highlightNet={selectedNet}
                onHighlightNet={selectNet}
                voltageComparison={leads}
                onCompareNet={changeLead}
                probes={inst.probes}
                onOpenComponent={(id) => { if (project.components.find((c)=>c.id===id)?.component.startsWith('block/')) { useHierarchy.getState().show(id); useLayout.getState().setPerspective('design'); } }}
                onMoveProbe={async (id, net) => { try { await probeActions.move(id, `net:${net}`); } catch (e) { toast({ kind: 'error', title: 'Probe move rejected', description: errorMessage(e) }); throw e; } }}
                readOnly
                onNetClick={(net, at) => { if (pickLead) { changeLead(pickLead, net, at); setPickLead(null); return; } guard(() => probeActions.add(`net:${net}`, 'analog', 200_000).then((id) => toast({ kind: 'success', title: `Probe ${id} on ${net}` })))(); }}
                label="Live schematic"
              />
    ) : null },
    // an instrument with no probe of its kind folds to its rail until the user opens it; the
    // shared window's actions go with the one that shows something
    { id: 'scope', title: 'Oscilloscope', flush: true, weight: 1.2, minSize: 140, collapsible: true, defaultCollapsed: noScope, actions: noScope && logicChannels.length > 0 ? undefined : instrumentActions, content: (
      <Oscilloscope triggerAt={capture?.at} traces={scopeTraces} window={win} onWindowChange={inst.setWindow} cursors={inst.cursors} onCursorsChange={inst.setCursors} now={plotNow} selected={inst.selectedScope} onSelect={inst.setSelectedScope} revision={inst.revision} />
    ) },
    { id: 'logic', title: 'Logic analyzer', flush: true, weight: 1, minSize: 120, collapsible: true, defaultCollapsed: logicChannels.length === 0, actions: noScope && logicChannels.length > 0 ? instrumentActions : undefined, content: (
      <LogicAnalyzer channels={logicChannels} lookup={lookupRecord} window={win} onWindowChange={inst.setWindow} cursors={inst.cursors} onCursorsChange={inst.setCursors} now={plotNow} selected={inst.selectedLogic} onSelect={inst.setSelectedLogic} revision={inst.revision} />
    ) },
    {
      id: 'bottom',
      title: { serial: 'Serial terminal', diagnostics: 'Diagnostics', meters: 'Meters', air: 'The air' }[bottomTab],
      flush: true,
      weight: 1,
      minSize: 140,
      actions: bottomTab === 'meters' ? probesList : undefined,
      content: (
        <Tabs
          label="Run details"
          value={bottomTab}
          onValueChange={setBottomTab}
          items={[
            { value: 'meters', label: 'Meters', content: <MetersPanel /> },
            { value: 'serial', label: 'Serial', content: (
              <SerialTerminal
                target={serialTarget}
                targets={serialTargets}
                onTargetChange={inst.setSerialTarget}
                lines={inst.serialLines.filter((l) => !l.target || l.target === serialTarget)}
                onSend={(target, text) => guard(() => inputActions.serial(target, text))()}
                disabled={!inst.probes.some((p) => p.kind === 'serial')}
                disabledReason="No serial probe in this project (add a board with a UART)"
                hostPort={hostPort.port}
                hostPortError={hostPort.error}
                onHostPort={hostPort.toggle}
              />
            ) },
            { value: 'air', label: 'Air', content: <AirPanel /> },
            { value: 'diagnostics', label: 'Diagnostics', content: <DiagnosticsPanel /> },
          ]}
        />
      ),
    },
  ];
  const items: DockItem[] = [
    { id: 'left', title: 'Controls and instruments', flush: true, collapsible: true, defaultCollapsed: inputs.length === 0 && !project?.components.some((c) => isWiredInstrument(c.component)), weight: 1, minSize: 260, content: <DockLayout direction="column" items={left} storageKey={`embsim.layout.run.left.${project?.name ?? 'none'}`} /> },
    { id: 'centre', title: 'Instruments', bare: true, weight: 2.6, minSize: 480, content: <DockLayout direction="column" items={centre} storageKey={`embsim.layout.run.schematic.centre.${project?.name ?? 'none'}`} /> },
  ];
  return (
    <div className="wb-perspective">
      <DockLayout direction="row" items={items} storageKey={`embsim.layout.run.${project?.name ?? 'none'}`} />
    </div>
  );
}
