// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { parseSi } from './units';
import type { ParameterSweep } from './types';

export function sweepValues(start: string, stop: string, count: number, logarithmic: boolean): number[] {
  const a = parseSi(start), b = parseSi(stop);
  if (a === undefined || b === undefined || !Number.isFinite(a) || !Number.isFinite(b) || !Number.isInteger(count) || count < 2 || count > 64)
    throw new Error('Use finite start/stop values and 2–64 points.');
  if (logarithmic && (a <= 0 || b <= 0)) throw new Error('Logarithmic spacing requires positive endpoints.');
  const result = Array.from({length: count}, (_, i) => i === 0 ? a : i === count - 1 ? b : logarithmic ? Math.exp(Math.log(a) + (Math.log(b) - Math.log(a)) * i / (count - 1)) : a * (1 - i / (count - 1)) + b * i / (count - 1));
  if (result.some((v) => !Number.isFinite(v))) throw new Error('Generated values exceed the numeric range.');
  return result;
}

export function sweepCsv(result: ParameterSweep): string {
  const cell = (v: unknown) => `"${String(v ?? '').replaceAll('"', '""')}"`;
  const rows: unknown[][] = [['Point', 'Requested', 'Resolved', 'At ps', 'Result', ...result.request.probes.flatMap((p) => [`${p} V`, `${p} logic`]), 'Diagnostics']];
  result.points.forEach((p, i) => rows.push([i + 1, p.requested, p.resolved, p.at_ps, p.ok ? 'sampled' : 'failed', ...p.measurements.flatMap((m) => [m.voltage, m.logic]), p.diagnostics.map((d) => `${d.code}: ${d.message}`).join('; ')]));
  return rows.map((r) => r.map(cell).join(',')).join('\r\n') + '\r\n';
}
