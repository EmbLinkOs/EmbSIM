// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// Verify perspective. Scenario runs and recordings work when the server
// advertises the capability (`scenario`, `record`/`record.inputs`); the
// scenario editor, reports browser and coverage are M0 placeholders.

import { Badge, Button, DockLayout, Table, useToast, type Column, type DockItem } from '@embsim/design';
import { formatTime, type Report } from '@embsim/protocol';
import { Play, Square, Disc } from 'lucide-react';
import { useState } from 'react';
import { errorMessage } from '../../actions';
import { getClient } from '../../connection';
import { useProject } from '../../stores/project';
import { useSession } from '../../stores/session';
import { Placeholder } from '../Placeholder';

export function VerifyPerspective() {
  const name = useProject((s) => s.project?.name ?? 'none');
  const items: DockItem[] = [
    { id: 'scenario', title: 'Scenario editor', weight: 2, minSize: 300, flush: true, content: <ScenarioPanel /> },
    { id: 'recordings', title: 'Recordings', weight: 1, minSize: 200, content: <RecordingsPanel /> },
    { id: 'reports', title: 'Reports', weight: 1.2, minSize: 220, flush: true, content: <ReportsPanel /> },
    { id: 'coverage', title: 'Coverage', weight: 1, minSize: 200, content: <Placeholder title="Coverage" description="Firmware coverage from the MCU runtime's analyses (M5)." requires={['gdb']} needsBoard /> },
  ];
  return (
    <div className="wb-perspective">
      <DockLayout direction="row" items={items} storageKey={`embsim.layout.verify.${name}`} />
    </div>
  );
}

/** The last report lives in module state so the Reports panel can show it. */
let lastReport: Report | null = null;
const reportListeners = new Set<() => void>();
function setLastReport(r: Report) {
  lastReport = r;
  for (const l of reportListeners) l();
}

function ScenarioPanel() {
  const scenarios = useProject((s) => s.project?.scenarios ?? []);
  const capabilities = useSession((s) => s.capabilities);
  const running = useSession((s) => s.running);
  const { toast } = useToast();
  const [busy, setBusy] = useState<string | null>(null);
  const can = capabilities.includes('scenario');
  const run = async (path: string) => {
    setBusy(path);
    try {
      const r = await getClient().scenario.run({ path });
      const report = ('report' in r ? r.report : r) as Report;
      setLastReport(report);
      const failed = report.assertions.filter((a) => !a.ok).length;
      toast({ kind: failed ? 'error' : 'success', title: failed ? `${failed} assertion(s) failed` : `${report.assertions.length} assertions passed`, description: `${path} · ended ${report.ended.reason} at ${formatTime(BigInt(report.ended.now_ps))}` });
    } catch (e) {
      toast({ kind: 'error', title: 'Scenario failed', description: errorMessage(e) });
    } finally {
      setBusy(null);
    }
  };
  return (
    <div className="wb-placeholder" role="region" aria-label="Scenarios">
      <div className="es-row">
        <span className="wb-placeholder__title">Scenarios</span>
        <Badge tone={can ? 'accent' : 'warning'}>{can ? 'scenario.run available' : 'not available in M0'}</Badge>
      </div>
      <div>Runs a scenario file (SPEC-007 §2) against the loaded project: the session is reset, inputs are replayed and assertions are checked. Editing the timeline and assertions in place comes after M0.</div>
      {!can && (
        <div>
          Requires server capability <code>scenario</code>.
        </div>
      )}
      {scenarios.length === 0 ? (
        <div className="es-muted">The project lists no scenarios.</div>
      ) : (
        <ul>
          {scenarios.map((s) => (
            <li key={s} className="es-row" style={{ marginBottom: 'var(--space-1)' }}>
              <code>{s}</code>
              <Button size="sm" icon={<Play />} disabled={!can || running || busy !== null} onClick={() => void run(s)}>
                {busy === s ? 'Running…' : 'Run'}
              </Button>
            </li>
          ))}
        </ul>
      )}
    </div>
  );
}

function RecordingsPanel() {
  const capabilities = useSession((s) => s.capabilities);
  const { toast } = useToast();
  const [recording, setRecording] = useState(false);
  const [results, setResults] = useState<{ at: string; text: string }[]>([]);
  const can = capabilities.includes('record') || capabilities.includes('record.inputs');
  const start = async () => {
    try {
      await getClient().record.start({});
      setRecording(true);
    } catch (e) {
      toast({ kind: 'error', title: 'record.start failed', description: errorMessage(e) });
    }
  };
  const stop = async () => {
    try {
      const r = await getClient().record.stop({});
      setRecording(false);
      const inputs = Array.isArray(r.inputs) ? r.inputs.length : r.inputs;
      setResults((rs) => [{ at: new Date().toLocaleTimeString(), text: `${r.path ?? '(in memory)'} · ${inputs ?? '?'} inputs${r.hash ? ` · ${r.hash.slice(0, 12)}` : ''}` }, ...rs]);
    } catch (e) {
      toast({ kind: 'error', title: 'record.stop failed', description: errorMessage(e) });
    }
  };
  return (
    <div className="wb-placeholder" role="region" aria-label="Recordings">
      <div className="es-row">
        <span className="wb-placeholder__title">Recordings</span>
        <Badge tone={can ? 'accent' : 'warning'}>{can ? 'available' : 'not available in M0'}</Badge>
      </div>
      <div>Records the session's inputs with their virtual times so a run can be replayed deterministically (SPEC-012).</div>
      {!can && (
        <div>
          Requires server capability <code>record</code>.
        </div>
      )}
      <div className="es-row">
        <Button size="sm" icon={<Disc />} variant={recording ? 'danger' : 'default'} disabled={!can || recording} onClick={() => void start()}>
          Record
        </Button>
        <Button size="sm" icon={<Square />} disabled={!can || !recording} onClick={() => void stop()}>
          Stop
        </Button>
        {recording && <Badge tone="danger">recording</Badge>}
      </div>
      {results.length > 0 && (
        <ul>
          {results.map((r, i) => (
            <li key={i}>
              <span className="es-muted">{r.at}</span> <code>{r.text}</code>
            </li>
          ))}
        </ul>
      )}
      <div className="es-muted">Replay and conversion to a scenario (record.replay, embsim record to-scenario) follow in M1.</div>
    </div>
  );
}

function ReportsPanel() {
  const [, bump] = useState(0);
  useState(() => {
    const l = () => bump((n) => n + 1);
    reportListeners.add(l);
    return l;
  });
  const report = lastReport;
  if (!report) {
    return <Placeholder title="Reports" description="The JSON report of SPEC-008 §8 for the last scenario run: assertions with observed and expected values, diagnostics and statistics." requires={['scenario']} actions={['run a scenario from the Scenario editor to see its report here']} />;
  }
  type Row = Report['assertions'][number];
  const columns: Column<Row>[] = [
    { id: 'ok', header: 'Result', width: '70px', cell: (a) => <Badge tone={a.ok ? 'accent' : 'danger'}>{a.ok ? 'pass' : 'fail'}</Badge> },
    { id: 'id', header: 'Assertion', mono: true, cell: (a) => a.id },
    { id: 'at', header: 'At', mono: true, cell: (a) => formatTime(BigInt(a.at_ps), { padZeros: false }) },
    { id: 'observed', header: 'Observed', mono: true, cell: (a) => JSON.stringify(a.observed) },
    { id: 'expected', header: 'Expected', mono: true, cell: (a) => JSON.stringify(a.expected) },
  ];
  return (
    <div>
      <div className="es-row" style={{ padding: 'var(--space-2)', gap: 'var(--space-3)', flexWrap: 'wrap' }}>
        <Badge>{report.project.name}</Badge>
        <Badge>policy {report.policy}</Badge>
        <Badge>ended {report.ended.reason} at {formatTime(BigInt(report.ended.now_ps), { padZeros: false })}</Badge>
        {report.statistics?.events !== undefined && <Badge>{report.statistics.events} events</Badge>}
        {report.diagnostics?.length ? <Badge tone="warning">{report.diagnostics.length} diagnostics</Badge> : null}
      </div>
      <Table label="Assertions" columns={columns} rows={report.assertions} rowKey={(a) => a.id} />
    </div>
  );
}
