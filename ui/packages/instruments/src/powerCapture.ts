// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// A power profiler's samples (instrument.read on a PPK2's measurement core) as
// the workbench shows them: one analog record of the current, a sample every
// 10 us, followed live; a window's average, peak and charge as Nordic's Power
// Profiler app gives them for a selection; the samples exported as CSV.

import type { PowerCapture } from '@embsim/protocol';
import { WaveformRecord } from './waveform';

export class PowerCaptureRecords {
  readonly part: string;
  readonly record: WaveformRecord;
  /** the next read's `since` */
  next = 0;
  start: bigint | null = null;
  period = 10_000_000n;
  state: PowerCapture['state'] = 'idle';
  mode: PowerCapture['mode'] = 'source';
  /** every sample this capture holds, from `first` (amperes) */
  private values: number[] = [];
  private first = 0;

  constructor(part: string) {
    this.part = part;
    this.record = new WaveformRecord(`${part}.current`, 'analog', 'Current', 'A', 8_000_000);
  }

  get length(): number {
    return this.values.length;
  }

  private restart(): void {
    this.record.clear();
    this.values = [];
    this.first = 0;
    this.next = 0;
  }

  /** a read's samples appended; a new capture (its start moved, or its sequence went back) starts over */
  ingest(cap: PowerCapture): boolean {
    const start = BigInt(cap.start_ps);
    if (this.start !== null && (start !== this.start || cap.since < this.next)) this.restart();
    if (!this.values.length) this.first = cap.since;
    this.start = start;
    this.period = BigInt(cap.period_ps);
    const changed = cap.samples.length > 0 || cap.state !== this.state;
    this.state = cap.state;
    this.mode = cap.mode;
    if (cap.samples.length) {
      this.values.push(...cap.samples);
      this.record.append(start + BigInt(cap.since) * this.period, this.period, Float32Array.from(cap.samples));
    }
    this.next = cap.next;
    return changed;
  }

  /** the time of sample `i` (its window's start) */
  timeOf(i: number): bigint {
    return (this.start ?? 0n) + BigInt(this.first + i) * this.period;
  }

  /**
   * Over [from, to): the average current, the largest sample and the charge,
   * as the Power Profiler app gives them for a selection (each sample a 10 us
   * average, so the charge is their sum times 10 us).
   */
  window(from: bigint, to: bigint): { average: number; max: number; charge: number; samples: number } {
    if (this.start === null || !this.values.length) return { average: 0, max: 0, charge: 0, samples: 0 };
    const base = this.start + BigInt(this.first) * this.period;
    const lo = Math.max(0, Number((from - base + this.period - 1n) / this.period));
    const hi = Math.min(this.values.length, Number((to - base) / this.period));
    let sum = 0;
    let max = 0;
    for (let i = lo; i < hi; i++) {
      const v = this.values[i]!;
      sum += v;
      if (v > max) max = v;
    }
    const n = Math.max(0, hi - lo);
    const dt = Number(this.period) * 1e-12;
    return { average: n ? sum / n : 0, max, charge: sum * dt, samples: n };
  }

  /** the samples as CSV: time from the first sample in seconds, current in amperes */
  toCsv(): string {
    const dt = Number(this.period) * 1e-12;
    const lines = ['Time [s],Current [A]'];
    for (let i = 0; i < this.values.length; i++) lines.push(`${(i * dt).toFixed(6)},${this.values[i]!.toPrecision(6)}`);
    return `${lines.join('\n')}\n`;
  }
}
