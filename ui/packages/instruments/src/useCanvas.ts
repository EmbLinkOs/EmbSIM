// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { useCallback, useEffect, useRef, useState } from 'react';

export interface CanvasSize {
  width: number;
  height: number;
  dpr: number;
}

/** A 2D canvas sized to its box in device pixels; `draw` is called on size change and on `redraw()`. */
export function useCanvas(draw: (ctx: CanvasRenderingContext2D, size: CanvasSize) => void) {
  const ref = useRef<HTMLCanvasElement>(null);
  const [size, setSize] = useState<CanvasSize>({ width: 0, height: 0, dpr: 1 });
  const drawRef = useRef(draw);
  drawRef.current = draw;
  const pending = useRef<number | null>(null);

  const redraw = useCallback(() => {
    if (pending.current !== null) return;
    pending.current = requestAnimationFrame(() => {
      pending.current = null;
      const canvas = ref.current;
      if (!canvas || size.width === 0) return;
      const ctx = canvas.getContext('2d');
      if (!ctx) return;
      ctx.save();
      ctx.setTransform(size.dpr, 0, 0, size.dpr, 0, 0);
      drawRef.current(ctx, size);
      ctx.restore();
    });
  }, [size]);

  useEffect(() => {
    const canvas = ref.current;
    if (!canvas) return;
    const ro = new ResizeObserver((entries) => {
      const r = entries[0]?.contentRect;
      if (!r) return;
      const dpr = window.devicePixelRatio || 1;
      const width = Math.max(0, Math.floor(r.width));
      const height = Math.max(0, Math.floor(r.height));
      canvas.width = Math.floor(width * dpr);
      canvas.height = Math.floor(height * dpr);
      setSize({ width, height, dpr });
    });
    ro.observe(canvas);
    return () => ro.disconnect();
  }, []);

  useEffect(() => {
    redraw();
    return () => {
      if (pending.current !== null) cancelAnimationFrame(pending.current);
      pending.current = null;
    };
  }, [redraw]);

  return { ref, size, redraw };
}

/** Reads a CSS custom property from an element (for theme-aware canvas drawing). */
export function cssVar(el: Element | null, name: string, fallback: string): string {
  if (!el) return fallback;
  const v = getComputedStyle(el).getPropertyValue(name).trim();
  return v || fallback;
}
