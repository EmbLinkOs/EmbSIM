// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import type { IndicatorQualification } from '@embsim/protocol';
import { t as tr } from '../../i18n';

/** Declared, bounded model evidence. A source hash is not a certification. */
export function IndicatorEvidence({ evidence }: { evidence: IndicatorQualification }) {
  const holdouts = evidence.curves.reduce((n, c) => n + c.points.filter((p) => p.role === 'holdout').length, 0);
  return <section className="wb-indicator-evidence" aria-label={tr('ev.region')}>
    <h4>{tr('ev.envelope')}</h4>
    <p>{evidence.temperature_c} °C · {evidence.current_a.map((v) => Number((v * 1000).toPrecision(4))).join('–')} mA</p>
    <p>{tr('ev.acceptance', { tol: evidence.voltage_tolerance_v, n: holdouts })}</p>
    <p>{tr('ev.scope')}</p>
    <details><summary>{tr('ev.snapshot')}</summary>
      <p>{evidence.source.revision} · {tr('ev.pages', { list: evidence.source.pages.join(', ') })}</p>
      <code title={evidence.source.sha256}>SHA-256 {evidence.source.sha256}</code>
    </details>
  </section>;
}
