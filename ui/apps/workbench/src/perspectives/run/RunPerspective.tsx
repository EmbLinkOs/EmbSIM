// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { Button, DockLayout, IconButton, Tabs, Tooltip, useToast, type Command, type DockItem } from '@embsim/design';
import { followWindow, LogicAnalyzer, Oscilloscope, SerialTerminal, type ScopeTrace } from '@embsim/instruments';
import { AnalogLegend, SchematicCanvas } from '@embsim/scene';
import { Download, Locate, Trash2 } from 'lucide-react';
import { useCallback, useMemo, useState } from 'react';
import { errorMessage, exportCsv, exportVcd, inputs as inputActions, probes as probeActions } from '../../actions';
import { platform } from '../../platform/platform';
import { useRegisterCommands } from '../../shell/commands';
import { DiagnosticsPanel } from '../../shell/DiagnosticsPanel';
import { useInstruments } from '../../stores/instruments';
import { useProject } from '../../stores/project';
import { useSelection } from '../../stores/selection';
import { useCanvasAppearance } from '../../stores/canvasAppearance';
import { useSession } from '../../stores/session';
import { BoardPanel } from '../BoardPanel';
import { ControlsPanel } from './ControlsPanel';
import { MetersPanel } from './MetersPanel';

const TRACE_COLORS = ['#5fe3b1', '#f0975a', '#7cbcf5', '#f27d84', '#e9bd55', '#c48cf0'];

export function RunPerspective() {
  const appearance = useCanvasAppearance((s) => s.appearance);
  const now = useSession((s) => s.now);
  const nets = useSession((s) => s.nets);
  const project = useProject((s) => s.project);
  const sceneCatalog = useProject((s) => s.sceneCatalog);
  const selection = useSelection((s) => s.selection);
  const setSelection = useSelection((s) => s.setSelection);
  const inst = useInstruments();
  const { toast } = useToast();
  const [bottomTab, setBottomTab] = useState<'serial' | 'diagnostics' | 'meters' | 'schematic'>('meters');

  const win = followWindow(inst.window, now);
  const scopeTraces = useMemo<ScopeTrace[]>(
    () =>
      inst.probes
        .filter((p) => p.kind === 'analog' || p.kind === 'value')
        .map((p, i) => ({ record: inst.records.get(p.probe as string)!, color: TRACE_COLORS[i % TRACE_COLORS.length]! }))
        .filter((t) => t.record),
    [inst.probes, inst.records],
  );
  const records = inst.records;
  const lookupRecord = useCallback((id: string) => records.get(id), [records]);
  // a digital probe's decoder comes from its definition in the project
  const logicChannels = useMemo(
    () =>
      inst.probes
        .filter((p) => p.kind === 'digital')
        .map((p) => ({
          record: inst.records.get(p.probe as string)!,
          decode: p.decode ?? project?.probes?.find((d) => d.id === (p.id ?? String(p.probe)))?.decode,
        }))
        .filter((c) => c.record),
    [inst.probes, inst.records, project?.probes],
  );

  const exportAs = async (kind: 'csv' | 'vcd') => {
    const ids = inst.probes.map((p) => p.probe as string);
    const from = win.start;
    const to = win.start + win.span;
    const text = kind === 'csv' ? exportCsv(ids, from, to) : exportVcd(ids, from, to);
    const p = await platform();
    const where = await p.saveTextFile(`embsim-${project?.name ?? 'session'}.${kind}`, text, kind === 'csv' ? 'text/csv' : 'text/plain');
    if (where) toast({ kind: 'success', title: `Exported ${where}` });
  };
  const guard = (fn: () => Promise<unknown>) => () => void fn().catch((e: unknown) => toast({ kind: 'error', title: 'Action failed', description: errorMessage(e) }));

  const commands = useMemo<Command[]>(
    () => [
      { id: 'run.export.csv', label: 'Export window as CSV', group: 'Instruments', icon: <Download />, run: guard(() => exportAs('csv')) },
      { id: 'run.export.vcd', label: 'Export window as VCD', group: 'Instruments', icon: <Download />, run: guard(() => exportAs('vcd')) },
      { id: 'run.follow', label: inst.window.follow ? 'Stop following the clock' : 'Follow the clock', group: 'Instruments', shortcut: 'F', run: () => inst.setWindow({ ...inst.window, follow: !inst.window.follow }) },
      { id: 'run.clear-cursors', label: 'Clear cursors', group: 'Instruments', run: () => inst.setCursors({ a: null, b: null }) },
      ...(project ? Object.keys(project.nets).map((net) => ({ id: `probe.${net}`, label: `Add probe on net ${net}`, group: 'Probes', keywords: ['probe', net], run: guard(() => probeActions.add(`net:${net}`, 'analog', 200_000)) })) : []),
      ...inst.probes.map((p) => ({ id: `unprobe.${p.probe}`, label: `Remove probe ${p.probe}`, group: 'Probes', icon: <Trash2 />, run: guard(() => probeActions.remove(p.probe as string)) })),
      { id: 'input.press', label: 'Press and release the button (SW1)', group: 'Inputs', keywords: ['button'], run: guard(async () => {
        await inputActions.press('SW1');
        await new Promise((r) => setTimeout(r, 150));
        await inputActions.release('SW1');
      }) },
    ],
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [inst.window, inst.probes, project],
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
    { id: 'board', title: 'Board', content: <BoardPanel interactive />, flush: true, weight: 1.4, minSize: 160 },
    { id: 'controls', title: 'Controls', content: <ControlsPanel />, weight: 1, minSize: 120 },
  ];
  const centre: DockItem[] = [
    { id: 'scope', title: 'Oscilloscope', flush: true, weight: 1.3, minSize: 140, actions: instrumentActions, content: (
      <Oscilloscope traces={scopeTraces} window={win} onWindowChange={inst.setWindow} cursors={inst.cursors} onCursorsChange={inst.setCursors} now={now} selected={inst.selectedScope} onSelect={inst.setSelectedScope} revision={inst.revision} />
    ) },
    { id: 'logic', title: 'Logic analyzer', flush: true, weight: 1, minSize: 120, content: (
      <LogicAnalyzer channels={logicChannels} lookup={lookupRecord} window={win} onWindowChange={inst.setWindow} cursors={inst.cursors} onCursorsChange={inst.setCursors} now={now} selected={inst.selectedLogic} onSelect={inst.setSelectedLogic} revision={inst.revision} />
    ) },
    {
      id: 'bottom',
      title: { serial: 'Serial terminal', diagnostics: 'Diagnostics', meters: 'Meters', schematic: 'Schematic' }[bottomTab],
      flush: true,
      weight: 1,
      minSize: 140,
      actions: bottomTab === 'schematic' ? <AnalogLegend /> : bottomTab === 'meters' ? probesList : undefined,
      content: (
        <Tabs
          label="Run details"
          value={bottomTab}
          onValueChange={setBottomTab}
          items={[
            { value: 'meters', label: 'Meters', content: <MetersPanel /> },
            { value: 'schematic', label: 'Schematic', content: project ? (
              <SchematicCanvas
                appearance={appearance}
                project={project}
                catalog={sceneCatalog}
                mode="probe"
                selection={selection}
                onSelectionChange={setSelection}
                live
                netStates={nets}
                probes={inst.probes}
                readOnly
                onNetClick={(net) => guard(() => probeActions.add(`net:${net}`, 'analog', 200_000).then((id) => toast({ kind: 'success', title: `Probe ${id} on ${net}` })))()}
                label="Live schematic"
              />
            ) : null },
            { value: 'serial', label: 'Serial', content: (
              <SerialTerminal
                target={inst.serialTarget}
                targets={inst.probes.filter((p) => p.kind === 'serial').map((p) => p.target.replace(/^serial:/, ''))}
                onTargetChange={inst.setSerialTarget}
                lines={inst.serialLines}
                onSend={(target, text) => guard(() => inputActions.serial(target, text))()}
                disabled={!inst.probes.some((p) => p.kind === 'serial')}
                disabledReason="No serial probe in this project (add a board with a UART)"
              />
            ) },
            { value: 'diagnostics', label: 'Diagnostics', content: <DiagnosticsPanel /> },
          ]}
        />
      ),
    },
  ];
  const items: DockItem[] = [
    { id: 'left', title: 'Board and controls', bare: true, weight: 1, minSize: 260, content: <DockLayout direction="column" items={left} storageKey={`embsim.layout.run.left.${project?.name ?? 'none'}`} /> },
    { id: 'centre', title: 'Instruments', bare: true, weight: 2.6, minSize: 480, content: <DockLayout direction="column" items={centre} storageKey={`embsim.layout.run.centre.${project?.name ?? 'none'}`} /> },
  ];
  return (
    <div className="wb-perspective">
      <DockLayout direction="row" items={items} storageKey={`embsim.layout.run.${project?.name ?? 'none'}`} />
    </div>
  );
}
