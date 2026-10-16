// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// Waveform records: sample storage for one probe fed by waveform.samples
// notifications, with range queries, min/max decimation for drawing long
// records, and the measurements shown by the instruments. Times are bigint
// picoseconds; only differences within a window become floats.

import { base64ToBytes, type Time, type WaveformSamples } from '@embsim/protocol';

export type SampleArray = Float32Array | Float64Array | Uint8Array;

export interface Segment {
  first: Time;
  period: Time;
  values: SampleArray;
  gapBefore: boolean;
}

export type RecordKind = 'digital' | 'analog';

/** Digital sample codes (SPEC-008 §5): 0, 1, 2 = Z, 3 = X. */
export const DIGITAL_Z = 2;
export const DIGITAL_X = 3;

export class WaveformRecord {
  readonly id: string;
  readonly kind: RecordKind;
  readonly label: string;
  readonly unit: string;
  private segments: Segment[] = [];
  private total = 0;
  /** Oldest samples are dropped past this many. */
  readonly capacity: number;
  private version = 0;

  constructor(id: string, kind: RecordKind, label = id, unit = kind === 'analog' ? 'V' : '', capacity = 4_000_000) {
    this.id = id;
    this.kind = kind;
    this.label = label;
    this.unit = unit;
    this.capacity = capacity;
  }

  /** Increments on every append; cheap change detection for renderers. */
  get revision(): number {
    return this.version;
  }

  get length(): number {
    return this.total;
  }

  get start(): Time | null {
    return this.segments[0]?.first ?? null;
  }

  get end(): Time | null {
    const s = this.segments[this.segments.length - 1];
    if (!s) return null;
    return s.first + s.period * BigInt(Math.max(0, s.values.length - 1));
  }

  clear(): void {
    this.segments = [];
    this.total = 0;
    this.version++;
  }

  /** Appends a frame, merging with the previous segment when contiguous. */
  append(first: Time, period: Time, values: SampleArray, gap = false): void {
    if (values.length === 0) return;
    const last = this.segments[this.segments.length - 1];
    if (last && !gap && last.period === period && last.values.constructor === values.constructor) {
      const expected = last.first + period * BigInt(last.values.length);
      if (expected === first) {
        const merged = new (values.constructor as new (n: number) => SampleArray)(last.values.length + values.length);
        merged.set(last.values as never, 0);
        merged.set(values as never, last.values.length);
        last.values = merged;
        this.total += values.length;
        this.trim();
        this.version++;
        return;
      }
    }
    this.segments.push({ first, period, values, gapBefore: gap });
    this.total += values.length;
    this.trim();
    this.version++;
  }

  /**
   * Appends from a protocol notification: a base64 payload with a sample
   * period, or `json` pairs of [time_ps, value] (event-driven probes). Pairs
   * at equal spacing are packed into one segment; a change of spacing starts
   * a new one, so a step waveform costs one segment per edge.
   */
  appendFrame(frame: WaveformSamples): void {
    if (frame.encoding === 'json' || frame.samples) {
      const pairs = frame.samples ?? [];
      let i = 0;
      while (i < pairs.length) {
        const t0 = BigInt(pairs[i]![0]);
        let period = 1n;
        let j = i + 1;
        if (j < pairs.length) {
          period = BigInt(pairs[j]![0]) - t0;
          if (period <= 0n) period = 1n;
          while (j + 1 < pairs.length && BigInt(pairs[j + 1]![0]) - BigInt(pairs[j]![0]) === period) j++;
          if (j === i + 1 && j + 1 < pairs.length) j = i + 1; // a run of two still packs
        }
        const run = pairs.slice(i, j === i ? i + 1 : j + 1);
        const arr = this.kind === 'digital' ? Uint8Array.from(run, (p) => p[1]) : Float32Array.from(run, (p) => p[1]);
        this.append(t0, period, arr, Boolean(frame.gap) && i === 0);
        i += run.length;
      }
      return;
    }
    if (!frame.data || !frame.first_time_ps) return;
    const bytes = base64ToBytes(frame.data);
    const period = frame.period_ps ? BigInt(frame.period_ps) : 1n;
    let values: SampleArray;
    if (frame.encoding === 'u8' || frame.encoding === 'bytes') values = bytes;
    else if (frame.encoding === 'f64') values = new Float64Array(bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength - (bytes.byteLength % 8)));
    else values = new Float32Array(bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength - (bytes.byteLength % 4)));
    this.append(BigInt(frame.first_time_ps), period, values, Boolean(frame.gap));
  }

  private trim(): void {
    while (this.total > this.capacity && this.segments.length > 0) {
      const s = this.segments[0]!;
      const excess = this.total - this.capacity;
      if (s.values.length <= excess) {
        this.segments.shift();
        this.total -= s.values.length;
      } else {
        s.values = s.values.subarray(excess) as SampleArray;
        s.first += s.period * BigInt(excess);
        this.total -= excess;
      }
    }
  }

  /** Index of the last segment starting at or before t, or -1. */
  private segmentIndex(t: Time): number {
    let lo = 0;
    let hi = this.segments.length - 1;
    let found = -1;
    while (lo <= hi) {
      const mid = (lo + hi) >> 1;
      if (this.segments[mid]!.first <= t) {
        found = mid;
        lo = mid + 1;
      } else hi = mid - 1;
    }
    return found;
  }

  /** The value at or just before t (sample-and-hold), or NaN before the record starts. */
  valueAt(t: Time): number {
    const i = this.segmentIndex(t);
    if (i < 0) return NaN;
    const s = this.segments[i]!;
    const idx = Number((t - s.first) / s.period);
    return s.values[Math.min(idx, s.values.length - 1)]!;
  }

  /** Iterates samples in [from, to] in time order. */
  *samples(from: Time, to: Time): IterableIterator<{ t: Time; v: number }> {
    const start = Math.max(0, this.segmentIndex(from));
    for (let k = start; k < this.segments.length; k++) {
      const s = this.segments[k]!;
      const segEnd = s.first + s.period * BigInt(s.values.length - 1);
      if (segEnd < from || s.first > to) continue;
      const startIdx = from > s.first ? Number((from - s.first) / s.period) : 0;
      for (let i = startIdx; i < s.values.length; i++) {
        const t = s.first + s.period * BigInt(i);
        if (t > to) return;
        yield { t, v: s.values[i]! };
      }
    }
  }

  /**
   * Min/max decimation: for `buckets` equal slices of [from, to], the min and
   * max sample in each (NaN where empty) and whether a gap starts there.
   */
  decimate(from: Time, to: Time, buckets: number): { min: Float32Array; max: Float32Array; first: Float32Array; last: Float32Array; count: Uint32Array } {
    const min = new Float32Array(buckets).fill(NaN);
    const max = new Float32Array(buckets).fill(NaN);
    const first = new Float32Array(buckets).fill(NaN);
    const last = new Float32Array(buckets).fill(NaN);
    const count = new Uint32Array(buckets);
    const span = Number(to - from);
    if (span <= 0 || buckets <= 0) return { min, max, first, last, count };
    const startSeg = Math.max(0, this.segmentIndex(from));
    for (let k = startSeg; k < this.segments.length; k++) {
      const s = this.segments[k]!;
      const n = s.values.length;
      const segEnd = s.first + s.period * BigInt(n - 1);
      if (segEnd < from) continue;
      if (s.first > to) break;
      const period = Number(s.period);
      const offset = Number(s.first - from); // may be negative
      let i = offset < 0 ? Math.ceil(-offset / period) : 0;
      // Fast path: when a bucket spans many samples step through directly.
      for (; i < n; i++) {
        const rel = offset + i * period;
        if (rel > span) break;
        const b = Math.min(buckets - 1, Math.floor((rel / span) * buckets));
        const v = s.values[i]!;
        if (count[b] === 0) {
          min[b] = v;
          max[b] = v;
          first[b] = v;
        } else {
          if (v < min[b]!) min[b] = v;
          if (v > max[b]!) max[b] = v;
        }
        last[b] = v;
        count[b] = count[b]! + 1;
      }
    }
    return { min, max, first, last, count };
  }

  /** Min, max and mean over [from, to]. */
  stats(from: Time, to: Time): { min: number; max: number; mean: number; count: number } {
    let min = Infinity;
    let max = -Infinity;
    let sum = 0;
    let count = 0;
    for (const { v } of this.samples(from, to)) {
      if (this.kind === 'digital' && v > 1) continue;
      if (v < min) min = v;
      if (v > max) max = v;
      sum += v;
      count++;
    }
    return count ? { min, max, mean: sum / count, count } : { min: NaN, max: NaN, mean: NaN, count: 0 };
  }

  /**
   * Rising-edge times in [from, to]: digital 0→1 transitions, or analog
   * upward crossings of `threshold` (default the mid-point of min and max).
   */
  risingEdges(from: Time, to: Time, threshold?: number, limit = 10_000): Time[] {
    const edges: Time[] = [];
    let th = threshold;
    if (th === undefined) {
      if (this.kind === 'digital') th = 0.5;
      else {
        const s = this.stats(from, to);
        if (!Number.isFinite(s.min) || s.max - s.min < 1e-9) return edges;
        th = (s.min + s.max) / 2;
      }
    }
    let prev: number | null = null;
    for (const { t, v } of this.samples(from, to)) {
      if (this.kind === 'digital' && v > 1) {
        prev = null;
        continue;
      }
      if (prev !== null && prev < th && v >= th) {
        edges.push(t);
        if (edges.length >= limit) break;
      }
      prev = v;
    }
    return edges;
  }

  /** Period (mean distance between rising edges) and frequency over a window. */
  periodAndFrequency(from: Time, to: Time): { period: Time | null; frequencyHz: number | null; edges: number } {
    const edges = this.risingEdges(from, to);
    if (edges.length < 2) return { period: null, frequencyHz: null, edges: edges.length };
    const total = edges[edges.length - 1]! - edges[0]!;
    const period = total / BigInt(edges.length - 1);
    return { period, frequencyHz: period > 0n ? 1e12 / Number(period) : null, edges: edges.length };
  }
}

export function decodeFrameValues(frame: WaveformSamples): SampleArray {
  const r = new WaveformRecord('tmp', frame.encoding === 'u8' ? 'digital' : 'analog');
  r.appendFrame(frame);
  const out: number[] = [];
  const end = r.end;
  if (r.start !== null && end !== null) for (const { v } of r.samples(r.start, end)) out.push(v);
  return frame.encoding === 'u8' ? Uint8Array.from(out) : Float32Array.from(out);
}
