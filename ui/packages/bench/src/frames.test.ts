// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe, expect, it } from 'vitest';
import { BUDGET_120_MS, BUDGET_60_MS, FRAME_MEASURE, GpuTimer, meets, recordFrame, summarize } from './frames';

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

describe('GPU frame times', () => {
  function fakeGl(ext: boolean) {
    const results = new Map<object, { available: boolean; ns: number }>();
    let disjoint = false;
    let n = 0;
    const gl = {
      QUERY_RESULT_AVAILABLE: 1, QUERY_RESULT: 2,
      getExtension: () => (ext ? { TIME_ELAPSED_EXT: 10, GPU_DISJOINT_EXT: 11 } : null),
      createQuery: () => { const q = { id: n++ }; results.set(q, { available: false, ns: 0 }); return q; },
      beginQuery: () => undefined,
      endQuery: () => undefined,
      deleteQuery: (q: object) => results.delete(q),
      getParameter: () => disjoint,
      getQueryParameter: (q: object, p: number) => (p === 1 ? results.get(q)!.available : results.get(q)!.ns),
    };
    return { gl: gl as unknown as WebGL2RenderingContext, results, setDisjoint: (d: boolean) => { disjoint = d; } };
  }

  it('reads each frame\'s query once the GPU has its result, in order', () => {
    const { gl, results } = fakeGl(true);
    const t = new GpuTimer(gl);
    expect(t.available).toBe(true);
    t.begin(); t.end('draft');
    t.begin(); t.end('draft');
    expect(t.poll()).toEqual([]); // not ready yet
    const [a, b] = [...results.keys()];
    results.set(b!, { available: true, ns: 3e6 });
    expect(t.poll()).toEqual([]); // the first is still pending: results come in order
    results.set(a!, { available: true, ns: 5e6 });
    expect(t.poll()).toEqual([5, 3]);
    expect(results.size).toBe(0);
  });

  it('drops the queries a disjoint event spoiled, and does nothing without the extension', () => {
    const { gl, results, setDisjoint } = fakeGl(true);
    const t = new GpuTimer(gl);
    t.begin(); t.end('quality');
    setDisjoint(true);
    expect(t.poll()).toEqual([]);
    expect(results.size).toBe(0);
    const none = new GpuTimer(fakeGl(false).gl);
    expect(none.available).toBe(false);
    none.begin(); none.end('draft');
    expect(none.poll()).toEqual([]);
  });
});
