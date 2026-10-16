// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { afterEach, describe, expect, it, vi } from 'vitest';
import { useCanvasAppearance } from './canvasAppearance';
describe('canvas preference storage', () => {
  afterEach(() => { vi.restoreAllMocks(); useCanvasAppearance.getState().reset(); localStorage.clear(); });
  it('persists preferences without storing or modifying a project', () => {
    useCanvasAppearance.getState().update({ palette: 'blueprint', style: 'crosses', spacing: 40 });
    expect(JSON.parse(localStorage.getItem('embsim.canvas.appearance.v1')!)).toMatchObject({ palette: 'blueprint', style: 'crosses', spacing: 40 });
  });
  it('still applies appearance when the browser refuses storage', () => {
    vi.spyOn(Storage.prototype, 'setItem').mockImplementation(() => { throw new Error('storage disabled'); });
    expect(() => useCanvasAppearance.getState().update({ style: 'dots' })).not.toThrow();
    expect(useCanvasAppearance.getState().appearance.style).toBe('dots');
  });
});
