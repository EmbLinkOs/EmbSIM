// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe, expect, it } from 'vitest';
import { canvasBackground, DEFAULT_CANVAS_APPEARANCE, normalizeAppearance, type GridStyle } from './canvasAppearance';
describe('continuous canvas appearance', () => {
  it('anchors patterns to pan coordinates even outside the nominal sheet', () => {
    const style = canvasBackground(DEFAULT_CANVAS_APPEARANCE, { x: 2500, y: -1400, k: 1 });
    expect(style.backgroundPosition).toBe('2500px -1400px, 2500px -1400px, 2500px -1400px, 2500px -1400px');
    expect(style.backgroundSize).toBe('20px 20px, 20px 20px, 100px 100px, 100px 100px');
    expect(style.backgroundColor).toBe('var(--canvas-paper)');
  });
  it('keeps zoomed-out grids legible instead of rendering subpixel noise', () => {
    const style = canvasBackground(DEFAULT_CANVAS_APPEARANCE, { x: 0, y: 0, k: .15 });
    expect(style.backgroundSize).toBe('12px 12px, 12px 12px, 60px 60px, 60px 60px');
  });
  it('supports all patterns and keeps plain backgrounds free of grid images', () => {
    for (const pattern of ['engineering', 'lines', 'dots', 'crosses', 'blank'] as GridStyle[]) {
      const style = canvasBackground({ ...DEFAULT_CANVAS_APPEARANCE, style: pattern }, { x: 0, y: 0, k: 1 });
      expect(style.backgroundImage === 'none').toBe(pattern === 'blank');
    }
    expect(canvasBackground(DEFAULT_CANVAS_APPEARANCE, { x: 0, y: 0, k: 1 }, false).backgroundImage).toBe('none');
  });
  it('rejects malformed preferences and bounds visual settings', () => {
    expect(normalizeAppearance({ style: 'injected', palette: 'url(x)', spacing: 7, weight: Infinity, contrast: NaN })).toEqual(DEFAULT_CANVAS_APPEARANCE);
    expect(normalizeAppearance({ weight: 10, contrast: -2 })).toMatchObject({ weight: 2, contrast: .05 });
    expect(normalizeAppearance(null)).toEqual(DEFAULT_CANVAS_APPEARANCE);
    expect(normalizeAppearance({ wireColor: 'url(invalid)', labels: 'invalid' })).toEqual(DEFAULT_CANVAS_APPEARANCE);
    expect(normalizeAppearance({ wireColor: 'blue', labels: 'compact' })).toMatchObject({ wireColor: 'blue', labels: 'compact' });
  });
});
