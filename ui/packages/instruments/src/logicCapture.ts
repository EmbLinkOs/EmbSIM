// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// A logic analyzer part's capture (instrument.read) as the workbench shows it:
// eight digital records on the capture's sample grid, followed live, and the
// capture exported as the tools engineers open it in (Saleae Logic 2's
// digital CSV, and VCD for PulseView, GTKWave or Logic 2's import).

import type { LogicCapture, WaveformSamples } from '@embsim/protocol';
import { WaveformRecord } from './waveform';

export const LOGIC_CHANNELS = 8;

/** The capture's transitions in absolute time: [time_ps, inputs]. */
export interface CaptureEdge {
  t: bigint;
  bits: number;
}

/**
 * Follows one part's capture across reads: appends what each read adds to
 * eight records (one per channel, named `${part}.D${n}`), starting over when a
 * new capture begins (its start moves, or the sequence goes back).
 */
export class LogicCaptureRecords {
  readonly part: string;
  readonly records: WaveformRecord[];
  readonly edges: CaptureEdge[] = [];
  /** the next read's `since` */
  next = 0;
  /** the sequence number of the first record held, as the last read gave it */
  first = 0;
  start: bigint | null = null;
  period = 1n;
  state: LogicCapture['state'] = 'idle';
  sampleRate = 0;
  trigger: bigint | null = null;
  end: bigint | null = null;
  channels = 0xff;
  full = false;
  /** the last read began after the capture's first record: read again from `next` */
  needsRestart = false;

  constructor(part: string) {
    this.part = part;
    this.records = Array.from({ length: LOGIC_CHANNELS }, (_, n) => new WaveformRecord(`${part}.D${n}`, 'digital', `D${n}`));
  }

  private restart(): void {
    for (const r of this.records) r.clear();
    this.edges.length = 0;
    this.next = 0;
  }

  /**
   * A read's result appended; returns whether anything changed. A capture
   * waiting for its trigger is not shown (Logic 2 shows nothing until it
   * triggers: its pre-trigger window is still moving). A read continues the
   * capture held only if its start, its first record and `since` are the ones
   * this one has; otherwise the capture starts over from `first`, and when
   * this read began later than that, `needsRestart` asks for a read from
   * `next` again.
   */
  ingest(cap: LogicCapture): boolean {
    const start = BigInt(cap.start_ps);
    const period = BigInt(cap.period_ps);
    if (cap.state === 'armed') {
      const changed = this.state !== 'armed' || this.edges.length > 0;
      this.restart();
      this.state = 'armed';
      this.start = start;
      this.first = cap.first;
      this.next = cap.next;
      this.needsRestart = false;
      return changed;
    }
    if (this.start !== start || this.first !== cap.first || cap.since !== this.next || this.state === 'armed') {
      this.restart();
      this.start = start;
      this.first = cap.first;
      this.next = cap.first;
      this.state = cap.state;
      if (cap.since !== cap.first) {
        this.needsRestart = true;
        return true;
      }
    }
    this.needsRestart = false;
    this.start = start;
    this.period = period;
    const changed = cap.transitions.length > 0 || cap.state !== this.state;
    this.state = cap.state;
    this.sampleRate = cap.sample_rate;
    this.channels = cap.channels;
    this.full = cap.full;
    this.trigger = cap.trigger >= 0 ? start + BigInt(cap.trigger) * period : null;
    this.end = cap.end >= 0 ? start + BigInt(cap.end) * period : null;
    const prev = this.edges[this.edges.length - 1];
    let last = prev?.bits;
    const pairs: [string, number][][] = this.records.map(() => []);
    for (const [k, bits] of cap.transitions) {
      const t = start + BigInt(k) * period;
      this.edges.push({ t, bits });
      for (let n = 0; n < LOGIC_CHANNELS; n++) {
        const v = (bits >> n) & 1;
        if (last === undefined || ((last >> n) & 1) !== v) pairs[n]!.push([t.toString(), v]);
      }
      last = bits;
    }
    const through = this.end ?? (this.edges.length ? this.edges[this.edges.length - 1]!.t : start);
    this.records.forEach((r, n) => {
      const frame: WaveformSamples = { stream: r.id, seq: 0, encoding: 'json', samples: pairs[n], through_time_ps: through.toString() };
      r.appendFrame(frame);
    });
    this.next = cap.next;
    return changed;
  }
}

const secs = (ps: bigint): string => {
  // exact decimal seconds from picoseconds (12 fractional digits, trailing zeros trimmed)
  const neg = ps < 0n;
  const a = neg ? -ps : ps;
  const whole = a / 1_000_000_000_000n;
  const frac = (a % 1_000_000_000_000n).toString().padStart(12, '0').replace(/0+$/, '');
  return `${neg ? '-' : ''}${whole}${frac ? `.${frac}` : ''}`;
};

/**
 * Saleae Logic 2's digital export: a header `Time [s],Channel 0,...`, then a
 * row at the start and at each change with every channel's value, times in
 * seconds from the trigger (or the capture's start without one).
 */
export function captureToCsv(rec: Pick<LogicCaptureRecords, 'edges' | 'trigger' | 'start' | 'channels'>): string {
  const zero = rec.trigger ?? rec.start ?? 0n;
  const chans = Array.from({ length: LOGIC_CHANNELS }, (_, n) => n).filter((n) => (rec.channels >> n) & 1);
  const lines = [`Time [s],${chans.map((n) => `Channel ${n}`).join(',')}`];
  for (const e of rec.edges) lines.push(`${secs(e.t - zero)},${chans.map((n) => (e.bits >> n) & 1).join(',')}`);
  return `${lines.join('\n')}\n`;
}

/** A Value Change Dump (IEEE 1364): one wire per enabled channel, time in picoseconds from the capture's start. */
export function captureToVcd(rec: Pick<LogicCaptureRecords, 'edges' | 'start' | 'channels' | 'part'>, date = new Date(0)): string {
  const start = rec.start ?? 0n;
  const chans = Array.from({ length: LOGIC_CHANNELS }, (_, n) => n).filter((n) => (rec.channels >> n) & 1);
  const id = (n: number) => String.fromCharCode(33 + n); // !, ", #, ...
  const out = [
    `$date ${date.toISOString()} $end`,
    '$version EmbSIM Saleae Logic 8 capture $end',
    '$timescale 1 ps $end',
    `$scope module ${rec.part.replace(/[^A-Za-z0-9_]/g, '_')} $end`,
    ...chans.map((n) => `$var wire 1 ${id(n)} D${n} $end`),
    '$upscope $end',
    '$enddefinitions $end',
  ];
  let last: number | undefined;
  for (const e of rec.edges) {
    const changed = chans.filter((n) => last === undefined || ((last >> n) & 1) !== ((e.bits >> n) & 1));
    if (!changed.length) continue;
    out.push(`#${e.t - start}`);
    if (last === undefined) out.push('$dumpvars', ...changed.map((n) => `${(e.bits >> n) & 1}${id(n)}`), '$end');
    else out.push(...changed.map((n) => `${(e.bits >> n) & 1}${id(n)}`));
    last = e.bits;
  }
  return `${out.join('\n')}\n`;
}
