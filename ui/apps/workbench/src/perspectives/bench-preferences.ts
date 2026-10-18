// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
export interface BenchPreferences {
  surface: 'mat' | 'paper' | 'graphite';
  labels: 'adaptive' | 'all' | 'selected';
  notes: boolean;
  list: boolean;
}

const KEY = 'embsim.bench.appearance.v1';
const DEFAULTS: BenchPreferences = { surface: 'paper', labels: 'adaptive', notes: true, list: true };

export function loadBenchPreferences(): BenchPreferences {
  try {
    const value: unknown = JSON.parse(localStorage.getItem(KEY) ?? 'null');
    if (!value || typeof value !== 'object' || Array.isArray(value)) return { ...DEFAULTS };
    const v = value as Record<string, unknown>;
    return {
      surface: v.surface === 'mat' || v.surface === 'graphite' || v.surface === 'paper' ? v.surface : DEFAULTS.surface,
      labels: v.labels === 'all' || v.labels === 'selected' || v.labels === 'adaptive' ? v.labels : DEFAULTS.labels,
      notes: typeof v.notes === 'boolean' ? v.notes : DEFAULTS.notes,
      list: typeof v.list === 'boolean' ? v.list : DEFAULTS.list,
    };
  } catch { return { ...DEFAULTS }; }
}

export function saveBenchPreferences(value: BenchPreferences): void {
  try { localStorage.setItem(KEY, JSON.stringify(value)); } catch { /* In-memory preferences still work. */ }
}
