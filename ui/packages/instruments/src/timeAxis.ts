// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The time window shared by the instruments (SPEC-011 §3): a start and a
// span in picoseconds, with helpers for zoom, pan, tick placement and pixel
// mapping. Instruments receive the window and ask the owner to change it.

import { formatTime, pickUnit, scaleTime, splitTime, type Time, type TimeUnit } from '@embsim/protocol';

export interface TimeWindow {
  start: Time;
  span: Time;
  /** Keep the right edge at the virtual clock while running. */
  follow: boolean;
}

export const MIN_SPAN: Time = 100n;
export const MAX_SPAN: Time = 10n ** 15n; // 1000 s

export function defaultWindow(): TimeWindow {
  return { start: 0n, span: 20_000_000_000n, follow: true }; // 20 ms
}

export function endOf(w: TimeWindow): Time {
  return w.start + w.span;
}

export function clampSpan(span: Time): Time {
  return span < MIN_SPAN ? MIN_SPAN : span > MAX_SPAN ? MAX_SPAN : span;
}

/** Zooms by a factor keeping the time at `anchorFraction` (0..1 across the width) fixed. */
export function zoomWindow(w: TimeWindow, factor: number, anchorFraction = 0.5): TimeWindow {
  const span = clampSpan(scaleTime(w.span, 1 / factor));
  const anchorTime = w.start + scaleTime(w.span, anchorFraction);
  const start = anchorTime - scaleTime(span, anchorFraction);
  return { start: start < 0n ? 0n : start, span, follow: false };
}

export function panWindow(w: TimeWindow, fraction: number): TimeWindow {
  const start = w.start + scaleTime(w.span, fraction);
  return { ...w, start: start < 0n ? 0n : start, follow: false };
}

/** Moves the window so its right edge sits at `now` when following. */
export function followWindow(w: TimeWindow, now: Time): TimeWindow {
  if (!w.follow) return w;
  const start = now - w.span;
  return { ...w, start: start < 0n ? 0n : start };
}

export function timeToX(t: Time, w: TimeWindow, width: number): number {
  return (Number(t - w.start) / Number(w.span)) * width;
}

export function xToTime(x: number, w: TimeWindow, width: number): Time {
  const frac = width > 0 ? x / width : 0;
  return w.start + scaleTime(w.span, frac);
}

export interface Tick {
  t: Time;
  label: string;
  major: boolean;
}

const NICE = [1n, 2n, 5n];

/** Nice tick positions for a window at a pixel width; about one major tick per 90 px. */
export function ticks(w: TimeWindow, width: number, targetPx = 90): { ticks: Tick[]; unit: TimeUnit; step: Time } {
  const wanted = Math.max(2, Math.floor(width / targetPx));
  const rough = w.span / BigInt(wanted);
  let step = 1n;
  outer: for (let mag = 1n; mag <= MAX_SPAN; mag *= 10n) {
    for (const n of NICE) {
      if (n * mag >= rough) {
        step = n * mag;
        break outer;
      }
    }
  }
  const unit = pickUnit(step);
  const out: Tick[] = [];
  const first = (w.start / step) * step;
  const end = endOf(w);
  const decimals = unitDecimals(step, unit);
  for (let t = first; t <= end; t += step) {
    if (t < w.start) continue;
    const { sign, whole, frac } = splitTime(t, unit, decimals);
    out.push({ t, label: `${sign}${whole}${frac ? '.' + frac : ''}`, major: true });
    if (out.length > 200) break;
  }
  return { ticks: out, unit, step };
}

function unitDecimals(step: Time, unit: TimeUnit): number {
  const scale: Record<TimeUnit, Time> = { ps: 1n, ns: 1_000n, us: 1_000_000n, ms: 1_000_000_000n, s: 1_000_000_000_000n };
  const s = scale[unit];
  let d = 0;
  let probe = step;
  while (probe < s && d < 6) {
    probe *= 10n;
    d++;
  }
  return d;
}

export function formatWindowSpan(w: TimeWindow): string {
  return formatTime(w.span, { padZeros: false });
}

export function unitLabel(unit: TimeUnit): string {
  return unit === 'us' ? 'µs' : unit;
}
