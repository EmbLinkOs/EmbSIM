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
import { t as tr } from '../../i18n';

export function VerifyPerspective() {
  const name = useProject((s) => s.project?.name ?? 'none');
  const left: DockItem[] = [
    { id: 'scenario', title: tr('verify.editor'), weight: 2.4, minSize: 260, content: <ScenarioEditor /> },
    { id: 'reports', title: tr('verify.report'), weight: 1, minSize: 140, flush: true, content: <ReportsPanel /> },
  ];
  const right: DockItem[] = [
    { id: 'recordings', title: tr('verify.recordings'), weight: 1, minSize: 160, content: <RecordingsPanel /> },
    { id: 'coverage', title: tr('verify.coverage'), weight: 2, minSize: 240, content: <CoveragePanel /> },
  ];
  const items: DockItem[] = [
    { id: 'left', title: tr('verify.scenarios'), bare: true, weight: 1.5, minSize: 480, content: <DockLayout direction="column" items={left} storageKey={`embsim.layout.verify2.left.${name}`} /> },
    { id: 'right', title: tr('verify.recCov'), bare: true, weight: 1, minSize: 360, content: <DockLayout direction="column" items={right} storageKey={`embsim.layout.verify2.right.${name}`} /> },
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
        title={tr('verify.report')}
        icon={<ClipboardCheck />}
        description={tr('verify.reportDesc')}
        requires={['scenario']}
        actions={['run a scenario from the scenario editor to see its report here']}
      />
    );
  }
  type Row = Report['assertions'][number];
  const columns: Column<Row>[] = [
    { id: 'ok', header: tr('se.result'), width: '70px', cell: (a) => <Badge tone={a.ok ? 'accent' : 'danger'}>{a.ok ? tr('verify.pass') : tr('verify.fail')}</Badge> },
    { id: 'id', header: tr('verify.assertion'), mono: true, cell: (a) => a.id },
    { id: 'at', header: tr('verify.at'), mono: true, cell: (a) => formatTime(BigInt(a.at_ps), { padZeros: false }) },
    { id: 'observed', header: tr('verify.observedH'), mono: true, cell: (a) => JSON.stringify(a.observed) },
    { id: 'expected', header: tr('verify.expectedH'), mono: true, cell: (a) => JSON.stringify(a.expected) },
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
      <Table label={tr('verify.assertions')} columns={columns} rows={report.assertions} rowKey={(a) => a.id} />
    </div>
  );
}
