// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { Button, EmptyState, Table, type Column } from '@embsim/design';
import { formatTime, type DiagnosticRecord } from '@embsim/protocol';
import { AlertTriangle, CheckCircle2, Info, OctagonAlert } from 'lucide-react';
import { useEffect } from 'react';
import { useLayout } from '../stores/layout';
import { useProject } from '../stores/project';
import { useSelection } from '../stores/selection';
import { useSession } from '../stores/session';

const ICON: Record<DiagnosticRecord['severity'], React.ReactNode> = {
  info: <Info />,
  warning: <AlertTriangle />,
  error: <OctagonAlert />,
  fatal: <OctagonAlert />,
};

/** The docs page for a diagnostic code (docs/user/diagnostics.md). */
export function docsLink(code: string): string {
  return `https://github.com/EmbLinkOs/EmbSIM/blob/main/docs/user/diagnostics.md#${code.replace(/\./g, '')}`;
}

export function DiagnosticsPanel() {
  const diagnostics = useSession((s) => s.diagnostics);
  const clear = useSession((s) => s.clearDiagnostics);
  const markRead = useSession((s) => s.markDiagnosticsRead);
  const selectNet = useSelection((s) => s.selectNet);
  const selectComponent = useSelection((s) => s.selectComponent);
  const setPerspective = useLayout((s) => s.setPerspective);
  const project = useProject((s) => s.project);

  useEffect(() => {
    markRead();
  }, [diagnostics.length, markRead]);

  const locate = (r: DiagnosticRecord) => {
    if (!r.subject) return;
    if (r.subject.kind === 'net') selectNet(r.subject.name);
    else if (r.subject.kind === 'pin' || r.subject.kind === 'component') {
      const id = r.subject.name.split('.')[0] ?? '';
      if (project?.components.some((c) => c.id === id)) {
        selectComponent(id);
        setPerspective('design');
      }
    }
  };

  const columns: Column<DiagnosticRecord & { key: string }>[] = [
    {
      id: 'severity',
      header: 'Severity',
      width: '90px',
      cell: (r) => (
        <span className={`wb-diagnostics__severity wb-diagnostics__severity--${r.severity}`}>
          {ICON[r.severity]}
          {r.severity}
        </span>
      ),
    },
    {
      id: 'code',
      header: 'Code',
      mono: true,
      cell: (r) => (
        <a href={docsLink(r.code)} target="_blank" rel="noreferrer" title="Open the documentation for this code">
          {r.code}
        </a>
      ),
    },
    { id: 'time', header: 'Time', mono: true, width: '110px', cell: (r) => (r.time_ps ? formatTime(BigInt(r.time_ps), { padZeros: false }) : '—') },
    {
      id: 'subject',
      header: 'Subject',
      mono: true,
      cell: (r) =>
        r.subject ? (
          <button type="button" className="es-button es-button--ghost es-button--sm" onClick={() => locate(r)} title="Select the affected element">
            {r.subject.kind}:{r.subject.name}
          </button>
        ) : (
          '—'
        ),
    },
    { id: 'message', header: 'Message', cell: (r) => r.message },
  ];
  const rows = diagnostics.map((d, i) => ({ ...d, key: `${i}` }));
  if (rows.length === 0) {
    return (
      <EmptyState icon={<CheckCircle2 />} title="No diagnostics">
        Warnings and errors from the engine appear here, with a link to the documentation of each code.
      </EmptyState>
    );
  }
  return (
    <div>
      <div className="es-row" style={{ justifyContent: 'flex-end', padding: 'var(--space-1) var(--space-2)' }}>
        <Button size="sm" variant="ghost" onClick={clear}>
          Clear
        </Button>
      </div>
      <Table label="Diagnostics" columns={columns} rows={rows} rowKey={(r) => r.key} />
    </div>
  );
}
