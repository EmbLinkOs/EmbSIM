// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { parseDuration, type Scenario } from '@embsim/protocol';

/** Editor feedback; the engine remains authoritative for project bindings. */
export function scenarioProblems(s: Scenario): string[] {
  const problems: string[] = [];
  const time = (value: string | undefined, label: string): bigint | undefined => {
    try {
      if (!value?.trim()) throw new Error();
      const t = parseDuration(value);
      if (t < 0n) throw new Error();
      return t;
    } catch {
      problems.push(`${label}: enter a non-negative time, such as 10ms.`);
      return undefined;
    }
  };
  if (!s.name.trim()) problems.push('Name is required.');
  const until = time(s.until, 'Until');
  const bounded = (value: string | undefined, label: string) => {
    const t = time(value, label);
    if (t !== undefined && until !== undefined && t > until) problems.push(`${label}: exceeds the run duration.`);
    return t;
  };
  s.inputs.forEach((input, i) => {
    const label = `Input ${i + 1}`;
    bounded(input.at, `${label} time`);
    if (!/^[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z0-9_]+)?$/.test(input.target)) problems.push(`${label}: enter a valid target.`);
    if (!/^[a-z][a-z0-9_]*$/.test(input.input)) problems.push(`${label}: enter a valid input name.`);
    if (!['boolean', 'number', 'string'].includes(typeof input.value) || (typeof input.value === 'number' && !Number.isFinite(input.value))) problems.push(`${label}: value must be a finite number, text or boolean.`);
  });
  const ids = new Set<string>();
  s.assertions.forEach((a, i) => {
    const label = `Assertion ${i + 1}`;
    if (!/^[A-Za-z_][A-Za-z0-9_-]*$/.test(a.id)) problems.push(`${label}: enter a valid ID.`);
    if (ids.has(a.id)) problems.push(`${label}: duplicate ID ${a.id}.`);
    ids.add(a.id);
    const modes = [a.at !== undefined, a.by !== undefined, a.between !== undefined, a.always !== undefined].filter(Boolean).length;
    if (modes !== 1) problems.push(`${label}: choose exactly one timing mode.`);
    if (a.always !== undefined) {
      if (a.always !== true) problems.push(`${label}: always must be true.`);
      const codes = [...(a.diagnostics?.forbid ?? [])];
      if (!a.diagnostics || codes.length === 0 || codes.some((code) => !/^[a-z]+\.[a-z_]+$/.test(code))) problems.push(`${label}: enter diagnostic codes, such as net.contention.`);
      return;
    }
    if (a.between) {
      const from = bounded(a.between[0], `${label} start`);
      const to = bounded(a.between[1], `${label} end`);
      if (from !== undefined && to !== undefined && from > to) problems.push(`${label}: start must not follow end.`);
    } else bounded(a.at ?? a.by, `${label} time`);
    if (!a.probe || !/^[A-Za-z_][A-Za-z0-9_-]*$/.test(a.probe)) problems.push(`${label}: enter a valid probe ID.`);
    const error = expectationProblem(a.expect);
    if (error) problems.push(`${label}: ${error}`);
  });
  return problems;
}

export function expectationProblem(value: unknown): string | undefined {
  if (!value || typeof value !== 'object' || Array.isArray(value)) return 'expectation must be a JSON object.';
  const entries = Object.entries(value);
  if (!entries.length) return 'expectation must contain a check.';
  for (const [key, v] of entries) {
    let valid: boolean;
    switch (key) {
      case 'logic': valid = ['0', '1', 'Z', 'X'].includes(v as string); break;
      case 'equals': valid = ['string', 'boolean'].includes(typeof v) || (typeof v === 'number' && Number.isFinite(v)); break;
      case 'contains': valid = typeof v === 'string'; break;
      case 'not_contains': valid = typeof v === 'string'; break;
      case 'edges': valid = typeof v === 'number' && Number.isInteger(v) && v >= 0; break;
      case 'frequency_hz': valid = typeof v === 'number' && Number.isFinite(v) && v > 0; break;
      case 'tolerance': valid = typeof v === 'number' && Number.isFinite(v) && v >= 0; break;
      case 'min': case 'max': case 'min_v': case 'max_v': valid = typeof v === 'number' && Number.isFinite(v); break;
      default: return `unknown expectation field ${key}.`;
    }
    if (!valid) return `invalid expectation value for ${key}.`;
  }
  const bounds = value as Record<string, unknown>;
  for (const [lo, hi] of [['min', 'max'], ['min_v', 'max_v']] as const) {
    const lower = bounds[lo];
    const upper = bounds[hi];
    if (typeof lower === 'number' && typeof upper === 'number' && lower > upper) return `${lo} must not exceed ${hi}.`;
  }
  return undefined;
}
