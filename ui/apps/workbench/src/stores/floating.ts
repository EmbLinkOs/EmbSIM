// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// Instrument front panels taken out of the Run perspective's dock into
// windows of their own over the workbench, with each window's place and size
// remembered for the next session.

import { create } from 'zustand';

export interface FloatingRect {
  x: number;
  y: number;
  width: number;
  height: number;
}

export interface FloatingState {
  /** the parts whose panel floats, the last one on top */
  floating: string[];
  rects: Record<string, FloatingRect>;
  float(part: string): void;
  dock(part: string): void;
  raise(part: string): void;
  place(part: string, rect: FloatingRect): void;
}

const KEY = 'embsim.floatingPanels';

function read(): Pick<FloatingState, 'floating' | 'rects'> {
  try {
    const v = JSON.parse(localStorage.getItem(KEY) ?? 'null') as unknown;
    if (!v || typeof v !== 'object') return { floating: [], rects: {} };
    const o = v as { floating?: unknown; rects?: unknown };
    const floating = Array.isArray(o.floating) ? o.floating.filter((p): p is string => typeof p === 'string') : [];
    const rects: Record<string, FloatingRect> = {};
    if (o.rects && typeof o.rects === 'object')
      for (const [k, r] of Object.entries(o.rects as Record<string, unknown>)) {
        const q = r as Partial<FloatingRect>;
        if ([q.x, q.y, q.width, q.height].every((n) => typeof n === 'number' && Number.isFinite(n))) rects[k] = q as FloatingRect;
      }
    return { floating, rects };
  } catch {
    return { floating: [], rects: {} };
  }
}

function write(s: Pick<FloatingState, 'floating' | 'rects'>) {
  try {
    localStorage.setItem(KEY, JSON.stringify({ floating: s.floating, rects: s.rects }));
  } catch {
    // storage unavailable: the windows last for the session
  }
}

export const useFloating = create<FloatingState>((set, get) => {
  const commit = (next: Pick<FloatingState, 'floating' | 'rects'>) => {
    set(next);
    write(next);
  };
  return {
    ...read(),
    float: (part) => commit({ floating: [...get().floating.filter((p) => p !== part), part], rects: get().rects }),
    dock: (part) => commit({ floating: get().floating.filter((p) => p !== part), rects: get().rects }),
    raise: (part) => {
      const f = get().floating;
      if (f[f.length - 1] !== part && f.includes(part)) commit({ floating: [...f.filter((p) => p !== part), part], rects: get().rects });
    },
    place: (part, rect) => commit({ floating: get().floating, rects: { ...get().rects, [part]: rect } }),
  };
});
