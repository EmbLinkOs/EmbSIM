// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe, expect, it } from 'vitest';
import { followWindow, panWindow, ticks, timeToX, xToTime, zoomWindow } from './timeAxis';

describe('time axis', () => {
  const w = { start: 1_000_000_000n, span: 10_000_000_000n, follow: false }; // 1 ms .. 11 ms

  it('maps times to pixels and back', () => {
    expect(timeToX(1_000_000_000n, w, 500)).toBe(0);
    expect(timeToX(6_000_000_000n, w, 500)).toBe(250);
    expect(xToTime(250, w, 500)).toBe(6_000_000_000n);
  });

  it('zooms about an anchor and clamps at zero', () => {
    const z = zoomWindow(w, 2, 0.5);
    expect(z.span).toBe(5_000_000_000n);
    expect(z.start).toBe(3_500_000_000n);
    expect(zoomWindow({ ...w, start: 0n }, 0.5, 0).start).toBe(0n);
    expect(panWindow(w, -1).start).toBe(0n);
  });

  it('follows the clock', () => {
    expect(followWindow({ ...w, follow: true }, 20_000_000_000n).start).toBe(10_000_000_000n);
    expect(followWindow(w, 20_000_000_000n)).toBe(w);
  });

  it('places nice ticks with unit labels', () => {
    const { ticks: t, unit, step } = ticks(w, 900);
    expect(unit).toBe('ms');
    expect(step).toBe(1_000_000_000n);
    expect(t[0]?.label).toBe('1');
    expect(t.length).toBe(11);
    const fine = ticks({ start: 0n, span: 1_000n, follow: false }, 400);
    expect(fine.unit).toBe('ps');
  });
});
