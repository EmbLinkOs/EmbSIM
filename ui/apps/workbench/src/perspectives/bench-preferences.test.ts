// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { afterEach, describe, expect, it, vi } from 'vitest';
import { loadBenchPreferences, saveBenchPreferences } from './bench-preferences';
afterEach(() => { vi.restoreAllMocks(); localStorage.clear(); });
describe('bench appearance persistence', () => {
  it('validates stored fields independently and survives invalid JSON', () => {
    localStorage.setItem('embsim.bench.appearance.v1', '{');
    expect(loadBenchPreferences().surface).toBe('paper');
    localStorage.setItem('embsim.bench.appearance.v1', JSON.stringify({ surface: 'unknown', labels: 'selected', notes: 0, list: false }));
    expect(loadBenchPreferences()).toEqual({ surface: 'paper', labels: 'selected', notes: true, list: false });
  });
  it('continues when storage is unavailable', () => {
    vi.spyOn(Storage.prototype, 'getItem').mockImplementation(() => { throw new Error('blocked'); });
    vi.spyOn(Storage.prototype, 'setItem').mockImplementation(() => { throw new Error('full'); });
    expect(loadBenchPreferences().list).toBe(true);
    expect(() => saveBenchPreferences({ surface: 'graphite', labels: 'all', notes: false, list: false })).not.toThrow();
  });
});
