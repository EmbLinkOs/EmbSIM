// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import type { Time } from '@embsim/protocol';
import type { WaveformRecord } from './waveform';
import type { TimeWindow } from './timeAxis';

export interface AcquisitionSettings {
  mode: 'normal' | 'single';
  source: string;
  edge: 'rising' | 'falling' | 'either';
  level: number;
  /** Full Schmitt band in the source's units; digital inputs use 0/1. */
  hysteresis: number;
  span: Time;
  preTrigger: number;
  holdoff: Time;
}
export interface CapturedFrame {
  at: Time;
  window: TimeWindow;
  records: Map<string, WaveformRecord>;
  number: number;
}

/** Sampled software acquisition. Never interpolates crossings or advances virtual time. */
export class Acquisition {
  readonly settings: AcquisitionSettings;
  phase: 'armed' | 'post-trigger' | 'captured' = 'armed';
  frame: CapturedFrame | null = null;
  reason = 'Waiting for a qualified edge';
  private signature = '';
  private cursor = 0;
  private armAt: Time;
  private state: 'low' | 'high' | null = null;
  private pending: { at: Time; start: Time; end: Time } | null = null;
  private count = 0;

  constructor(settings: AcquisitionSettings, now: Time) {
    if (settings.span <= 0n || settings.holdoff < 0n || !Number.isFinite(settings.level) || !Number.isFinite(settings.hysteresis) || settings.hysteresis < 0 || !Number.isFinite(settings.preTrigger) || settings.preTrigger < 0 || settings.preTrigger > 1) throw new Error('Invalid acquisition settings');
    this.settings = { ...settings };
    this.armAt = now;
  }

  update(records: ReadonlyMap<string, WaveformRecord>): void {
    if (this.phase === 'captured') return;
    const source = records.get(this.settings.source);
    if (!source) { this.reason = 'Trigger source unavailable'; return; }
    const signature = [...records].map(([id, r]) => `${id}:${r.generation}`).sort().join('|');
    if (signature !== this.signature || this.cursor < source.firstOrdinal) {
      this.reason = this.signature ? 'Capture restarted after source change, reset or lost history' : this.reason;
      this.signature = signature; this.cursor = source.firstOrdinal; this.state = null; this.pending = null; this.phase = 'armed';
    }
    // Stop at an incomplete post-trigger window. On the next packet resume at
    // the trigger cursor, preserving packet-boundary independent holdoff.
    for (;;) {
      if (this.pending) {
        const { at, start, end } = this.pending;
        if ([...records.values()].some((r) => r.captureEnd === null || r.captureEnd < end)) return;
        if (source.measurements(start, end).coverage !== 1) { this.pending = null; this.state = null; this.phase = 'armed'; this.reason = 'Discarded incomplete trigger-source window'; continue; }
        this.frame = { at, window: { start, span: end - start, follow: false }, number: ++this.count, records: new Map([...records].map(([id, r]) => [id, r.snapshot(start, end)])) };
        this.pending = null;
        if (this.settings.mode === 'single') { this.phase = 'captured'; this.reason = 'Single capture frozen; firmware continues'; return; }
        this.phase = 'armed'; this.armAt = end + this.settings.holdoff;
        this.reason = 'Capture complete; waiting for the next edge';
      }
      let found = false;
      for (const event of source.events(this.cursor)) {
        this.cursor = event.ordinal + 1;
        if (event.gap || !Number.isFinite(event.v) || (source.kind === 'digital' && event.v !== 0 && event.v !== 1)) { this.state = null; continue; }
        const level = source.kind === 'digital' ? 0.5 : this.settings.level;
        const band = source.kind === 'digital' ? 0 : this.settings.hysteresis / 2;
        const previous = this.state;
        if (event.v < level - band || (band > 0 && event.v === level - band)) this.state = 'low';
        if (event.v >= level + band) this.state = 'high';
        const rising = previous === 'low' && this.state === 'high';
        const falling = previous === 'high' && this.state === 'low';
        if (event.t < this.armAt || !(this.settings.edge === 'either' ? rising || falling : this.settings.edge === 'rising' ? rising : falling)) continue;
        const pre = BigInt(Math.round(Number(this.settings.span) * this.settings.preTrigger));
        const start = event.t - pre, end = start + this.settings.span;
        if (start < 0n || (pre > 0n && source.measurements(start, event.t).coverage !== 1)) { this.reason = 'Waiting for complete pre-trigger history'; continue; }
        this.pending = { at: event.t, start, end }; this.phase = 'post-trigger';
        this.reason = 'Collecting post-trigger samples'; found = true; break;
      }
      if (!found) return;
    }
  }
}
