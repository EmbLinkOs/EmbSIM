// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// Verify perspective (SPEC-011 §4): turn an experiment into a repeatable
// check. The scenario editor edits a scenario's inputs and assertions in
// place and runs it; recordings capture a session and replay it, or become a
// scenario; the report shows the last run's assertions; coverage shows which
// of the firmware's lines ran and where its cycles went.

import { Badge, DockLayout, Table, type Column, type DockItem } from '@embsim/design';
import { formatTime, type Report } from '@embsim/protocol';
import { useProject } from '../../stores/project';
import { useVerify } from '../../stores/verify';
import { ClipboardCheck } from 'lucide-react';
import { Placeholder } from '../Placeholder';
import { CoveragePanel } from './Coverage';
import { RecordingsPanel } from './Recordings';
import { ScenarioEditor } from './ScenarioEditor';

export function VerifyPerspective() {
  const name = useProject((s) => s.project?.name ?? 'none');
  const left: DockItem[] = [
    { id: 'scenario', title: 'Scenario editor', weight: 2.4, minSize: 260, content: <ScenarioEditor /> },
    { id: 'reports', title: 'Report', weight: 1, minSize: 140, flush: true, content: <ReportsPanel /> },
  ];
  const right: DockItem[] = [
    { id: 'recordings', title: 'Recordings', weight: 1, minSize: 160, content: <RecordingsPanel /> },
    { id: 'coverage', title: 'Coverage', weight: 2, minSize: 240, content: <CoveragePanel /> },
  ];
  const items: DockItem[] = [
    { id: 'left', title: 'Scenarios', bare: true, weight: 1.5, minSize: 480, content: <DockLayout direction="column" items={left} storageKey={`embsim.layout.verify2.left.${name}`} /> },
    { id: 'right', title: 'Recordings and coverage', bare: true, weight: 1, minSize: 360, content: <DockLayout direction="column" items={right} storageKey={`embsim.layout.verify2.right.${name}`} /> },
  ];
  return (
    <div className="wb-perspective">
      <DockLayout direction="row" items={items} storageKey={`embsim.layout.verify2.${name}`} />
    </div>
  );
}

function ReportsPanel() {
  const report = useVerify((s) => s.report);
  if (!report) {
    return (
      <Placeholder
        title="Report"
        icon={<ClipboardCheck />}
        description="The last scenario run: each check with what was observed and what was expected, the diagnostics and the statistics."
        requires={['scenario']}
        actions={['run a scenario from the scenario editor to see its report here']}
      />
    );
  }
  type Row = Report['assertions'][number];
  const columns: Column<Row>[] = [
    { id: 'ok', header: 'Result', width: '70px', cell: (a) => <Badge tone={a.ok ? 'accent' : 'danger'}>{a.ok ? 'pass' : 'fail'}</Badge> },
    { id: 'id', header: 'Assertion', mono: true, cell: (a) => a.id },
    { id: 'at', header: 'At', mono: true, cell: (a) => formatTime(BigInt(a.at_ps), { padZeros: false }) },
    { id: 'observed', header: 'Observed', mono: true, cell: (a) => JSON.stringify(a.observed) },
    { id: 'expected', header: 'Expected', mono: true, cell: (a) => JSON.stringify(a.expected) },
  ];
  const failed = report.assertions.filter((a) => !a.ok).length;
  return (
    <div>
      <div className="es-row" style={{ padding: 'var(--space-2)', gap: 'var(--space-3)', flexWrap: 'wrap' }}>
        <Badge tone={failed ? 'danger' : 'accent'}>{failed ? `${failed} failed` : `${report.assertions.length} passed`}</Badge>
        <Badge>{report.project.name}</Badge>
        <Badge>policy {report.policy}</Badge>
        <Badge>ended {report.ended.reason} at {formatTime(BigInt(report.ended.now_ps), { padZeros: false })}</Badge>
        {report.statistics?.events !== undefined && <Badge>{report.statistics.events} events</Badge>}
        {report.diagnostics?.length ? <Badge tone="warning">{report.diagnostics.length} diagnostics</Badge> : null}
        {Object.entries(report.coverage ?? {}).map(([b, c]) => (
          <Badge key={b}>
            {b} lines {c.lines.hit}/{c.lines.total}
          </Badge>
        ))}
      </div>
      <Table label="Assertions" columns={columns} rows={report.assertions} rowKey={(a) => a.id} />
    </div>
  );
}
