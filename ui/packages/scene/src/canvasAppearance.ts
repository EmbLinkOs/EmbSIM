// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import type { CSSProperties } from 'react';

export type GridStyle = 'engineering' | 'lines' | 'dots' | 'crosses' | 'blank';
export type CanvasPalette = 'paper' | 'warm' | 'graphite' | 'blueprint';
export interface CanvasAppearance {
  style: GridStyle;
  palette: CanvasPalette;
  spacing: 10 | 20 | 40;
  weight: number;
  contrast: number;
}
export const DEFAULT_CANVAS_APPEARANCE: CanvasAppearance = {
  style: 'engineering', palette: 'paper', spacing: 20, weight: .8, contrast: .12,
};
export function normalizeAppearance(value: unknown): CanvasAppearance {
  const v = value && typeof value === 'object' ? value as Record<string, unknown> : {};
  return {
    style: ['engineering', 'lines', 'dots', 'crosses', 'blank'].includes(String(v.style)) ? v.style as GridStyle : DEFAULT_CANVAS_APPEARANCE.style,
    palette: ['paper', 'warm', 'graphite', 'blueprint'].includes(String(v.palette)) ? v.palette as CanvasPalette : DEFAULT_CANVAS_APPEARANCE.palette,
    spacing: [10, 20, 40].includes(Number(v.spacing)) ? Number(v.spacing) as CanvasAppearance['spacing'] : DEFAULT_CANVAS_APPEARANCE.spacing,
    weight: typeof v.weight === 'number' && Number.isFinite(v.weight) ? Math.max(.5, Math.min(2, v.weight)) : DEFAULT_CANVAS_APPEARANCE.weight,
    contrast: typeof v.contrast === 'number' && Number.isFinite(v.contrast) ? Math.max(.05, Math.min(.5, v.contrast)) : DEFAULT_CANVAS_APPEARANCE.contrast,
  };
}

/** Screen-covering CSS patterns, anchored to world zero. Coarser subdivisions
 * replace subpixel grids during zoom-out; no finite sheet edge can be exposed. */
export function canvasBackground(appearance: CanvasAppearance, viewport: { x: number; y: number; k: number }, showGrid = true): CSSProperties {
  const a = normalizeAppearance(appearance);
  let step = a.spacing * Math.max(.01, viewport.k);
  while (step < 12) step *= 2;
  const minor = `color-mix(in srgb, var(--canvas-ink) ${a.contrast * 100}%, transparent)`;
  const major = `color-mix(in srgb, var(--canvas-ink) ${Math.min(.65, a.contrast * 1.7) * 100}%, transparent)`;
  const line = (axis: 'right' | 'bottom', color: string, width = a.weight) => `linear-gradient(to ${axis}, ${color} ${width}px, transparent ${width}px)`;
  let images: string[] = [];
  let sizes: string[] = [];
  let positions: string[] = [];
  if (showGrid && a.style !== 'blank') {
    if (a.style === 'dots' || a.style === 'crosses') {
      images = a.style === 'dots' ? [`radial-gradient(circle at center, ${major} ${a.weight}px, transparent ${a.weight + .2}px)`]
        : [`radial-gradient(ellipse ${a.weight * 4}px ${a.weight}px at center, ${major} 90%, transparent 100%)`, `radial-gradient(ellipse ${a.weight}px ${a.weight * 4}px at center, ${major} 90%, transparent 100%)`];
      sizes = images.map(() => `${step}px ${step}px`);
      positions = images.map(() => `${viewport.x - step / 2}px ${viewport.y - step / 2}px`);
    } else {
      images = [line('right', minor), line('bottom', minor)];
      sizes = images.map(() => `${step}px ${step}px`);
      positions = images.map(() => `${viewport.x}px ${viewport.y}px`);
      if (a.style === 'engineering') {
        images.push(line('right', major), line('bottom', major));
        sizes.push(`${step * 5}px ${step * 5}px`, `${step * 5}px ${step * 5}px`);
        positions.push(`${viewport.x}px ${viewport.y}px`, `${viewport.x}px ${viewport.y}px`);
      }
    }
  }
  return { backgroundColor: 'var(--canvas-paper)', backgroundImage: images.join(', ') || 'none', backgroundSize: sizes.join(', ') || 'auto', backgroundPosition: positions.join(', ') || '0 0' };
}
