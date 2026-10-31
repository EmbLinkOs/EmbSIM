// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe, expect, it } from 'vitest';
import { BUDGET_120_MS, BUDGET_60_MS, FRAME_MEASURE, meets, recordFrame, summarize } from './frames';

describe('frame times', () => {
  it('summarizes by nearest rank and counts the frames over each budget', () => {
    const s = summarize([4, 5, 6, 7, 9, 10, 12, 18, 3, 2]);
    expect(s.frames).toBe(10);
    expect(s.p50).toBe(6);
    expect(s.p95).toBe(18);
    expect(s.max).toBe(18);
    expect(s.mean).toBeCloseTo(7.6);
    expect(s.over120).toBeCloseTo(0.4); // 9, 10, 12, 18 are over 8.33 ms
    expect(s.over60).toBeCloseTo(0.1);
  });
  it('a run meets a budget when its 95th percentile is under it', () => {
    const fast = summarize(Array.from({ length: 100 }, (_, i) => (i < 96 ? 5 : 30)));
    expect(meets(fast, BUDGET_120_MS)).toBe(true);
    const slow = summarize(Array.from({ length: 100 }, (_, i) => (i < 90 ? 5 : 20)));
    expect(meets(slow, BUDGET_60_MS)).toBe(false);
    expect(meets(summarize([]), BUDGET_60_MS)).toBe(false);
  });
  it('records each frame as a User Timing measure', () => {
    performance.clearMeasures(FRAME_MEASURE);
    recordFrame(performance.now() - 3, 'draft');
    const [m] = performance.getEntriesByName(FRAME_MEASURE);
    expect(m?.duration).toBeGreaterThanOrEqual(2);
    expect((m as PerformanceMeasure).detail).toEqual({ mode: 'draft' });
  });
});
