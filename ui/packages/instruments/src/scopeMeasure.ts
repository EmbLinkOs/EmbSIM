// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// An oscilloscope's automatic measurements on one channel of an acquisition,
// as the DS1054Z computes them from what it acquired: maximum, minimum,
// peak-to-peak, average, RMS, and from the crossings of the middle level,
// period, frequency and positive duty; the rise time between 10 % and 90 %.

export interface ScopeMeasurements {
  max: number;
  min: number;
  vpp: number;
  avg: number;
  rms: number;
  /** seconds; NaN without two rising crossings */
  period: number;
  /** hertz; NaN without a period */
  freq: number;
  /** 0..1; NaN without a period */
  duty: number;
  /** seconds from 10 % to 90 % on the first rising edge; NaN without one */
  rise: number;
}

/** `points` dx seconds apart */
export function measure(points: number[], dx: number): ScopeMeasurements {
  const n = points.length;
  if (!n) return { max: NaN, min: NaN, vpp: NaN, avg: NaN, rms: NaN, period: NaN, freq: NaN, duty: NaN, rise: NaN };
  let max = -Infinity;
  let min = Infinity;
  let sum = 0;
  let sq = 0;
  for (const v of points) {
    if (v > max) max = v;
    if (v < min) min = v;
    sum += v;
    sq += v * v;
  }
  const mid = (max + min) / 2;
  // rising and falling crossings of the middle, placed between samples
  const at = (i: number) => (i - 1 + (mid - points[i - 1]!) / (points[i]! - points[i - 1]!)) * dx;
  const rises: number[] = [];
  const falls: number[] = [];
  if (max - min > 1e-12) {
    for (let i = 1; i < n; i++) {
      if (points[i - 1]! < mid && points[i]! >= mid) rises.push(at(i));
      else if (points[i - 1]! > mid && points[i]! <= mid) falls.push(at(i));
    }
  }
  let period = NaN;
  let duty = NaN;
  if (rises.length >= 2) {
    period = (rises[rises.length - 1]! - rises[0]!) / (rises.length - 1);
    const fall = falls.find((f) => f > rises[0]!);
    if (fall !== undefined) duty = (fall - rises[0]!) / period;
  }
  let rise = NaN;
  if (max - min > 1e-12) {
    const lo = min + 0.1 * (max - min);
    const hi = min + 0.9 * (max - min);
    let tLo = NaN;
    for (let i = 1; i < n; i++) {
      if (Number.isNaN(tLo) && points[i - 1]! < lo && points[i]! >= lo) tLo = (i - 1 + (lo - points[i - 1]!) / (points[i]! - points[i - 1]!)) * dx;
      if (!Number.isNaN(tLo) && points[i - 1]! < hi && points[i]! >= hi) {
        rise = (i - 1 + (hi - points[i - 1]!) / (points[i]! - points[i - 1]!)) * dx - tLo;
        break;
      }
    }
  }
  return { max, min, vpp: max - min, avg: sum / n, rms: Math.sqrt(sq / n), period, freq: period > 0 ? 1 / period : NaN, duty, rise };
}
