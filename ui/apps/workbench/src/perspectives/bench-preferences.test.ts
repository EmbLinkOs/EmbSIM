// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { afterEach, describe, expect, it, vi } from 'vitest';
import { ALL_LAYERS } from '@embsim/bench/inspect';
import { loadBenchPreferences, saveBenchPreferences } from './bench-preferences';
afterEach(() => { vi.restoreAllMocks(); localStorage.clear(); });
describe('bench appearance persistence', () => {
  it('validates stored fields independently and survives invalid JSON', () => {
    localStorage.setItem('embsim.bench.appearance.v1', '{');
    expect(loadBenchPreferences().surface).toBe('paper');
    localStorage.setItem('embsim.bench.appearance.v1', JSON.stringify({ surface: 'unknown', labels: 'selected', notes: 0, list: false }));
    expect(loadBenchPreferences()).toEqual({ surface: 'paper', labels: 'selected', notes: true, list: false, projection: 'perspective', quality: 'draft', layers: ALL_LAYERS });
  });
  it('keeps the 3D view\'s projection, quality and each layer', () => {
    localStorage.setItem('embsim.bench.appearance.v1', JSON.stringify({ projection: 'orthographic', quality: 'quality', layers: { jumpers: false, world: 'no' } }));
    const p = loadBenchPreferences();
    expect(p.projection).toBe('orthographic');
    expect(p.quality).toBe('quality');
    expect(p.layers).toEqual({ ...ALL_LAYERS, jumpers: false });
  });
  it('continues when storage is unavailable', () => {
    vi.spyOn(Storage.prototype, 'getItem').mockImplementation(() => { throw new Error('blocked'); });
    vi.spyOn(Storage.prototype, 'setItem').mockImplementation(() => { throw new Error('full'); });
    expect(loadBenchPreferences().list).toBe(true);
    expect(() => saveBenchPreferences({ surface: 'graphite', labels: 'all', notes: false, list: false, projection: 'perspective', quality: 'draft', layers: ALL_LAYERS })).not.toThrow();
  });
});
