// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The plots' own measures (row heights, margins, label size) under the
// workbench's density and text size, which the Settings page puts on <html>
// (data-density, --ui-scale). A plot drawn on a canvas does not follow CSS,
// so it reads them here and redraws when they change.
import { useEffect, useState } from 'react';

export type Density = 'comfortable' | 'compact';

export interface PlotLook {
  density: Density;
  /** the text size's scale (0.92 small, 1 medium, 1.12 large) */
  scale: number;
}

export interface PlotMetrics {
  /** label size in CSS pixels */
  font: number;
  /** a digital channel's row */
  row: number;
  /** the annotation row under a decoded channel */
  ann: number;
  /** the margin for the channel names or the vertical axis */
  left: number;
  /** the margin for the time axis */
  bottom: number;
  top: number;
}

const BASE: Record<'logic' | 'analog', Record<Density, PlotMetrics>> = {
  logic: {
    comfortable: { font: 11, row: 30, ann: 18, left: 72, bottom: 22, top: 6 },
    compact: { font: 10, row: 20, ann: 14, left: 60, bottom: 16, top: 3 },
  },
  analog: {
    comfortable: { font: 11, row: 0, ann: 0, left: 52, bottom: 22, top: 8 },
    compact: { font: 10, row: 0, ann: 0, left: 44, bottom: 16, top: 4 },
  },
};

/** The measures of a plot of `kind` under `look`, whole pixels. */
export function plotMetrics(kind: 'logic' | 'analog', look: PlotLook): PlotMetrics {
  const b = BASE[kind][look.density];
  const s = Number.isFinite(look.scale) && look.scale > 0 ? look.scale : 1;
  const r = (v: number) => Math.round(v * s);
  return { font: r(b.font), row: r(b.row), ann: r(b.ann), left: r(b.left), bottom: r(b.bottom), top: r(b.top) };
}

function read(): PlotLook {
  if (typeof document === 'undefined') return { density: 'comfortable', scale: 1 };
  const root = document.documentElement;
  const scale = Number.parseFloat(getComputedStyle(root).getPropertyValue('--ui-scale')) || 1;
  return { density: root.dataset['density'] === 'compact' ? 'compact' : 'comfortable', scale };
}

/** The workbench's density and text size, followed as the settings change. */
export function usePlotLook(): PlotLook {
  const [look, setLook] = useState<PlotLook>(read);
  useEffect(() => {
    if (typeof MutationObserver === 'undefined') return;
    const mo = new MutationObserver(() => {
      const next = read();
      setLook((prev) => (prev.density === next.density && prev.scale === next.scale ? prev : next));
    });
    mo.observe(document.documentElement, { attributes: true, attributeFilter: ['data-density', 'data-text-size', 'style', 'class'] });
    return () => mo.disconnect();
  }, []);
  return look;
}
