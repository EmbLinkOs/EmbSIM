// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The status bar: the simulation first (its state, simulated time, the
// project and its boards' timing, the diagnostics), the engine connection
// last, with the transport details (address, protocol, round trip, frames)
// in its tooltip rather than on the bar.
import { Tooltip } from '@embsim/design';
import { AlertTriangle, Cpu, FolderOpen, Plug, XCircle } from 'lucide-react';
import { useT } from '../i18n';
import { useInstruments } from '../stores/instruments';
import { useLayout } from '../stores/layout';
import { useProject } from '../stores/project';
import { diagnosticSummary, useSession } from '../stores/session';
import { useSimState, useSimTimeFormat } from './simState';
import { SimStateChip } from './Toolbar';

export function WorkbenchStatusBar() {
  const t = useT();
  const engine = useSession((s) => s.engine);
  const protocol = useSession((s) => s.protocol);
  const status = useSession((s) => s.status);
  const label = useSession((s) => s.transportLabel);
  const error = useSession((s) => s.error);
  const boards = useSession((s) => s.boards);
  const timingTiers = useSession((s) => s.timingTiers);
  const latency = useSession((s) => s.latencyMs);
  const now = useSession((s) => s.now);
  const diagnostics = useSession((s) => s.diagnostics);
  const setDiagnosticsOpen = useLayout((s) => s.setDiagnosticsOpen);
  const projectName = useProject((s) => s.project?.name) ?? useSession.getState().projectName;
  const frames = useInstruments((s) => s.frames);
  const gaps = useInstruments((s) => s.gaps);
  const state = useSimState();
  const fmt = useSimTimeFormat();
  const summary = diagnosticSummary(diagnostics);

  const tiers = boards.length ? boards.map((b) => `${b.id} ${timingTiers[b.id] ?? b.timing_tier}`).join(' · ') : t('status.noBoards');
  const connText =
    status === 'open'
      ? t('status.engineConnected')
      : status === 'connecting'
        ? t('status.connection.connecting')
        : status === 'closed'
          ? t('status.connection.closed')
          : status === 'error'
            ? t('status.connection.error')
            : t('status.connection.idle');
  const connDetails = [
    engine ? t('status.engineDetails', { engine, protocol: protocol ?? '?', transport: label }) : label,
    t('status.latency', { ms: latency.toFixed(1) }),
    gaps ? t('status.framesGaps', { n: frames, gaps }) : t('status.frames', { n: frames }),
    error ?? '',
  ]
    .filter(Boolean)
    .join('\n');

  return (
    <footer className="wb-statusbar" aria-label={t('status.bar')} tabIndex={0}>
      <span className="wb-statusbar__item">
        <SimStateChip state={state} compact />
        <span className="wb-statusbar__value es-mono">{fmt(now)}</span>
      </span>
      <span className="wb-statusbar__sep" aria-hidden="true" />
      <span className="wb-statusbar__item" title={t('status.project')}>
        <FolderOpen aria-hidden="true" />
        <span className="wb-statusbar__value">{projectName ?? '—'}</span>
      </span>
      <span className="wb-statusbar__sep" aria-hidden="true" />
      <Tooltip content={t('status.timing.hint')}>
        <span className="wb-statusbar__item" tabIndex={0}>
          <Cpu aria-hidden="true" />
          <span className="wb-statusbar__label">{t('status.timing')}</span>
          <span className="wb-statusbar__value es-mono">{tiers}</span>
        </span>
      </Tooltip>
      <span className="wb-statusbar__spacer" />
      <button type="button" className="wb-statusbar__item wb-statusbar__button" onClick={() => setDiagnosticsOpen(true)} aria-label={t('header.diagnostics')}>
        <XCircle aria-hidden="true" className={summary.errors ? 'wb-statusbar__icon--error' : undefined} />
        <span>{t.plural('status.errors', summary.errors)}</span>
        <AlertTriangle aria-hidden="true" className={summary.warnings ? 'wb-statusbar__icon--warning' : undefined} />
        <span>{t.plural('status.warnings', summary.warnings)}</span>
      </button>
      <span className="wb-statusbar__sep" aria-hidden="true" />
      <Tooltip content={<span className="wb-pre">{connDetails}</span>}>
        <span className={`wb-statusbar__item wb-conn wb-conn--${status}`} tabIndex={0} data-testid="connection">
          <Plug aria-hidden="true" />
          <span>{connText}</span>
        </span>
      </Tooltip>
    </footer>
  );
}
