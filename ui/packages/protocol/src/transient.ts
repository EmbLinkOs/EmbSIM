// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import type { TransientCapture } from './types';
export interface CaptureTrace {id: string; label: string; values: (number | null)[]}
export function captureTraces(data: TransientCapture, positive?: number, negative?: number): CaptureTrace[] {
  const raw = data.request.probes.map((target, i) => ({id: target, label: target, values: data.samples.map((s) => s.measurements[i]?.voltage ?? null)}));
  if (positive !== undefined && negative !== undefined && raw[positive] && raw[negative]) raw.push({id: 'difference', label: `${raw[positive]!.label} − ${raw[negative]!.label}`, values: data.samples.map((_, i) => {
    const a = raw[positive]!.values[i], b = raw[negative]!.values[i];
    return a == null || b == null || !Number.isFinite(a - b) ? null : a - b;
  })});
  return raw;
}
export function transientCsv(data: TransientCapture): string {
  const cell = (v: unknown) => `"${String(v ?? '').replaceAll('"', '""')}"`;
  return [['Source SHA-256', 'At ps', 'At s', ...data.request.probes.flatMap((p) => [`${p} V`, `${p} logic`])], ...data.samples.map((s) => [data.source.sha256, s.at_ps, Number(BigInt(s.at_ps))/1e12, ...s.measurements.flatMap((m) => [m.voltage, m.logic])])].map((r) => r.map(cell).join(',')).join('\r\n')+'\r\n';
}
