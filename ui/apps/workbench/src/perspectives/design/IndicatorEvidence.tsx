// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import type { IndicatorQualification } from '@embsim/protocol';

/** Declared, bounded model evidence. A source hash is not a certification. */
export function IndicatorEvidence({ evidence }: { evidence: IndicatorQualification }) {
  const holdouts = evidence.curves.reduce((n, c) => n + c.points.filter((p) => p.role === 'holdout').length, 0);
  return <section className="wb-indicator-evidence" aria-label="Indicator model evidence">
    <h4>Nominal DC envelope</h4>
    <p>{evidence.temperature_c} °C · {evidence.current_a.map((v) => Number((v * 1000).toPrecision(4))).join('–')} mA</p>
    <p>Curve acceptance ±{evidence.voltage_tolerance_v} V · {holdouts} source holdout points.</p>
    <p>Typical curve fit. Manufacturer variation, thermal damage and calibrated light output are outside this evidence.</p>
    <details><summary>Source snapshot</summary>
      <p>{evidence.source.revision} · pages {evidence.source.pages.join(', ')}</p>
      <code title={evidence.source.sha256}>SHA-256 {evidence.source.sha256}</code>
    </details>
  </section>;
}
