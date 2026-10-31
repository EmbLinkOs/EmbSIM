// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// Frame times of the 3D bench against ADR-028's budgets (docs/engineering/
// bench-3d.md, stage 7). Each frame's render is a User Timing measure, so the
// browser's own profiler shows it and a harness reads it with
// performance.getEntriesByName(FRAME_MEASURE). The time is the CPU's: the
// scene's traversal and the GL calls issued; on a GPU the drawing finishes
// asynchronously, so this is a lower bound of the frame's cost (a GPU timer
// query would give the rest).

/** the measure's name */
export const FRAME_MEASURE = 'embsim.bench3d.frame';

/** ADR-028: 120 Hz where the display allows, a 60 Hz floor */
export const BUDGET_120_MS = 1000 / 120;
export const BUDGET_60_MS = 1000 / 60;

/** the measures kept: a minute at 60 Hz, then the oldest are cleared */
const KEEP = 3600;
let recorded = 0;

/** records a frame that began at `start` (performance.now()), in a render mode */
export function recordFrame(start: number, mode: 'draft' | 'quality' | 'outline'): void {
  if (typeof performance === 'undefined' || typeof performance.measure !== 'function') return;
  try {
    performance.measure(FRAME_MEASURE, { start, end: performance.now(), detail: { mode } });
    if (++recorded % KEEP === 0) performance.clearMeasures(FRAME_MEASURE);
  } catch {
    // a browser without measure options: no timing, the frame is drawn all the same
  }
}

export interface FrameSummary {
  frames: number;
  mean: number;
  p50: number;
  p95: number;
  p99: number;
  max: number;
  /** the share of frames over each budget */
  over120: number;
  over60: number;
}

/** percentiles by nearest rank */
export function summarize(durations: readonly number[]): FrameSummary {
  const d = [...durations].sort((a, b) => a - b);
  const n = d.length;
  const rank = (p: number) => (n ? d[Math.min(n - 1, Math.max(0, Math.ceil((p / 100) * n) - 1))]! : 0);
  return {
    frames: n,
    mean: n ? d.reduce((a, b) => a + b, 0) / n : 0,
    p50: rank(50),
    p95: rank(95),
    p99: rank(99),
    max: n ? d[n - 1]! : 0,
    over120: n ? d.filter((x) => x > BUDGET_120_MS).length / n : 0,
    over60: n ? d.filter((x) => x > BUDGET_60_MS).length / n : 0,
  };
}

/** whether a run meets a budget: its 95th percentile under it (ADR-028) */
export function meets(summary: FrameSummary, budgetMs: number): boolean {
  return summary.frames > 0 && summary.p95 <= budgetMs;
}

/** the GPU's time per frame, as a second measure (where the browser exposes GPU timer queries) */
export const GPU_MEASURE = 'embsim.bench3d.gpu';

interface TimerQueryExt {
  TIME_ELAPSED_EXT: number;
  GPU_DISJOINT_EXT: number;
}

/**
 * Times each frame on the GPU with EXT_disjoint_timer_query_webgl2. A query's
 * result is ready a few frames later, so the queries wait in a queue and are
 * read when available; a disjoint event (the GPU's clock changed) discards
 * them. Where the extension is missing (most software renderers, Safari) it
 * does nothing, and only the CPU's time is recorded.
 */
export class GpuTimer {
  private readonly ext: TimerQueryExt | null;
  private readonly pending: { query: WebGLQuery; mode: string }[] = [];
  private current: WebGLQuery | null = null;

  constructor(private readonly gl: WebGL2RenderingContext) {
    this.ext = (gl.getExtension('EXT_disjoint_timer_query_webgl2') as TimerQueryExt | null) ?? null;
  }

  get available(): boolean {
    return this.ext !== null;
  }

  begin(): void {
    if (!this.ext || this.current || this.pending.length > 8) return;
    const q = this.gl.createQuery();
    if (!q) return;
    this.gl.beginQuery(this.ext.TIME_ELAPSED_EXT, q);
    this.current = q;
  }

  end(mode: string): void {
    if (!this.ext || !this.current) return;
    this.gl.endQuery(this.ext.TIME_ELAPSED_EXT);
    this.pending.push({ query: this.current, mode });
    this.current = null;
  }

  /** reads the queries that are done; returns the times read, ms */
  poll(): number[] {
    if (!this.ext) return [];
    const gl = this.gl;
    const disjoint = gl.getParameter(this.ext.GPU_DISJOINT_EXT) as boolean;
    const out: number[] = [];
    while (this.pending.length) {
      const { query, mode } = this.pending[0]!;
      if (!disjoint && !gl.getQueryParameter(query, gl.QUERY_RESULT_AVAILABLE)) break;
      this.pending.shift();
      if (!disjoint) {
        const ms = (gl.getQueryParameter(query, gl.QUERY_RESULT) as number) / 1e6;
        out.push(ms);
        if (typeof performance !== 'undefined' && typeof performance.measure === 'function') {
          const end = performance.now();
          try {
            performance.measure(GPU_MEASURE, { start: end - ms, end, detail: { mode } });
          } catch {
            // as recordFrame
          }
        }
      }
      gl.deleteQuery(query);
    }
    return out;
  }
}
