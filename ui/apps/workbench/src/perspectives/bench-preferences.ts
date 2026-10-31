// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { ALL_LAYERS, type BenchLayers } from '@embsim/bench/inspect';

export interface BenchPreferences {
  surface: 'mat' | 'paper' | 'graphite';
  labels: 'adaptive' | 'all' | 'selected';
  notes: boolean;
  list: boolean;
  /** the 3D view's camera and render (docs/engineering/bench-3d.md, stage 1) */
  projection: 'perspective' | 'orthographic';
  quality: 'draft' | 'quality';
  layers: BenchLayers;
}

const KEY = 'embsim.bench.appearance.v1';
const DEFAULTS: BenchPreferences = { surface: 'paper', labels: 'adaptive', notes: true, list: true, projection: 'perspective', quality: 'draft', layers: ALL_LAYERS };

function layers(v: unknown): BenchLayers {
  const out = { ...ALL_LAYERS };
  if (v && typeof v === 'object' && !Array.isArray(v))
    for (const k of Object.keys(out) as (keyof BenchLayers)[]) {
      const x = (v as Record<string, unknown>)[k];
      if (typeof x === 'boolean') out[k] = x;
    }
  return out;
}

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
      projection: v.projection === 'orthographic' ? 'orthographic' : 'perspective',
      quality: v.quality === 'quality' ? 'quality' : 'draft',
      layers: layers(v.layers),
    };
  } catch { return { ...DEFAULTS }; }
}

export function saveBenchPreferences(value: BenchPreferences): void {
  try { localStorage.setItem(KEY, JSON.stringify(value)); } catch { /* In-memory preferences still work. */ }
}
