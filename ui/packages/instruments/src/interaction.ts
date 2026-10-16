// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// Shared pointer and keyboard behaviour of the time-based instruments:
// wheel zooms about the cursor, Shift+wheel and drag pan, click places cursor
// A, Shift+click (or right-click) cursor B; arrow keys pan, +/- zoom, A/B
// place cursors at the centre, F toggles follow.

import type { Time } from '@embsim/protocol';
import { useEffect, useRef, type KeyboardEvent, type PointerEvent as ReactPointerEvent, type RefObject } from 'react';
import { panWindow, xToTime, zoomWindow, type TimeWindow } from './timeAxis';

export interface Cursors {
  a: Time | null;
  b: Time | null;
}

export interface TimeInteractionOptions {
  window: TimeWindow;
  onWindowChange: (w: TimeWindow) => void;
  cursors: Cursors;
  onCursorsChange: (c: Cursors) => void;
  /** Left inset of the plot area in px (labels live there). */
  plotLeft: number;
  width: number;
}

export function useTimeInteraction(ref: RefObject<HTMLElement | null>, opts: TimeInteractionOptions) {
  const optsRef = useRef(opts);
  optsRef.current = opts;
  const drag = useRef<{ startX: number; start: Time; moved: boolean } | null>(null);

  useEffect(() => {
    const el = ref.current;
    if (!el) return;
    const onWheel = (e: WheelEvent) => {
      e.preventDefault();
      const o = optsRef.current;
      const rect = el.getBoundingClientRect();
      const plotWidth = Math.max(1, o.width - o.plotLeft);
      if (e.shiftKey) {
        o.onWindowChange(panWindow(o.window, (e.deltaY || e.deltaX) / plotWidth));
        return;
      }
      const frac = Math.max(0, Math.min(1, (e.clientX - rect.left - o.plotLeft) / plotWidth));
      o.onWindowChange(zoomWindow(o.window, Math.exp(-e.deltaY * (e.ctrlKey ? 0.01 : 0.002)), frac));
    };
    el.addEventListener('wheel', onWheel, { passive: false });
    return () => el.removeEventListener('wheel', onWheel);
  }, [ref]);

  const plotWidth = () => Math.max(1, optsRef.current.width - optsRef.current.plotLeft);

  const onPointerDown = (e: ReactPointerEvent<HTMLElement>) => {
    const el = ref.current;
    if (!el) return;
    el.focus({ preventScroll: true });
    const rect = el.getBoundingClientRect();
    const x = e.clientX - rect.left - optsRef.current.plotLeft;
    if (x < 0) return;
    el.setPointerCapture(e.pointerId);
    drag.current = { startX: e.clientX, start: optsRef.current.window.start, moved: false };
    e.preventDefault();
  };

  const onPointerMove = (e: ReactPointerEvent<HTMLElement>) => {
    const d = drag.current;
    if (!d) return;
    const dx = e.clientX - d.startX;
    if (!d.moved && Math.abs(dx) < 3) return;
    d.moved = true;
    const o = optsRef.current;
    const w = panWindow({ ...o.window, start: d.start }, -dx / plotWidth());
    o.onWindowChange(w);
  };

  const onPointerUp = (e: ReactPointerEvent<HTMLElement>) => {
    const d = drag.current;
    drag.current = null;
    const el = ref.current;
    if (el?.hasPointerCapture(e.pointerId)) el.releasePointerCapture(e.pointerId);
    if (!d || d.moved || !el) return;
    const o = optsRef.current;
    const rect = el.getBoundingClientRect();
    const x = e.clientX - rect.left - o.plotLeft;
    const t = xToTime(x, o.window, plotWidth());
    if (e.shiftKey || e.button === 2) o.onCursorsChange({ ...o.cursors, b: t });
    else o.onCursorsChange({ ...o.cursors, a: t });
  };

  const onKeyDown = (e: KeyboardEvent<HTMLElement>) => {
    const o = optsRef.current;
    const centre = o.window.start + o.window.span / 2n;
    switch (e.key) {
      case 'ArrowLeft':
        e.preventDefault();
        o.onWindowChange(panWindow(o.window, e.shiftKey ? -0.5 : -0.1));
        break;
      case 'ArrowRight':
        e.preventDefault();
        o.onWindowChange(panWindow(o.window, e.shiftKey ? 0.5 : 0.1));
        break;
      case '+':
      case '=':
        e.preventDefault();
        o.onWindowChange(zoomWindow(o.window, 1.5));
        break;
      case '-':
        e.preventDefault();
        o.onWindowChange(zoomWindow(o.window, 1 / 1.5));
        break;
      case 'a':
      case 'A':
        e.preventDefault();
        o.onCursorsChange({ ...o.cursors, a: centre });
        break;
      case 'b':
      case 'B':
        e.preventDefault();
        o.onCursorsChange({ ...o.cursors, b: centre });
        break;
      case 'f':
      case 'F':
        e.preventDefault();
        o.onWindowChange({ ...o.window, follow: !o.window.follow });
        break;
      case 'Escape':
        o.onCursorsChange({ a: null, b: null });
        break;
      default:
        break;
    }
  };

  return { onPointerDown, onPointerMove, onPointerUp, onKeyDown, onContextMenu: (e: React.MouseEvent) => e.preventDefault() };
}
