// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// Protocol decoders for the logic analyzer (SPEC-011 §4: decoded protocol
// annotations). Each reads digital records (levels 0 and 1, sample-and-hold
// between changes) over a window and returns annotations: what was sent and
// when. They run in the client over the records the engine streamed; a
// project names a probe's decoder in its `decode` field (SPEC-007).
//
//   uart  one channel, idle high: {baud, bits = 8, parity = "none" | "even"
//         | "odd", stop = 1, invert = false}
//   spi   on the clock channel: {mosi?, miso?, cs?: probe ids, cpol = 0,
//         cpha = 0, bits = 8, lsb_first = false}
//   i2c   on the SCL channel: {sda: probe id}

import type { Time } from '@embsim/protocol';
import type { WaveformRecord } from './waveform';

export type AnnotationKind = 'data' | 'address' | 'start' | 'stop' | 'error';

export interface Annotation {
  start: Time;
  end: Time;
  text: string;
  kind: AnnotationKind;
}

export interface DecoderSpec {
  protocol: 'uart' | 'spi' | 'i2c' | string;
  params?: Record<string, unknown>;
}

/** The records a decoder may read besides its own, by probe id. */
export type RecordLookup = (probe: string) => WaveformRecord | undefined;

const LIMIT = 4000;

/** A byte as text: the character when printable, else hex. */
export function byteText(v: number): string {
  if (v === 0x20) return '␠';
  if (v > 0x20 && v < 0x7f) return String.fromCharCode(v);
  if (v === 0x0a) return '\\n';
  if (v === 0x0d) return '\\r';
  return `0x${v.toString(16).toUpperCase().padStart(2, '0')}`;
}

const hex = (v: number) => `0x${v.toString(16).toUpperCase().padStart(2, '0')}`;

/** The changes of a digital record in [from, to]: each level and when it began. */
export function transitions(r: WaveformRecord, from: Time, to: Time): { t: Time; v: number }[] {
  const out: { t: Time; v: number }[] = [];
  const v0 = r.valueAt(from);
  if (Number.isFinite(v0)) out.push({ t: from, v: v0 });
  for (const s of r.samples(from, to)) {
    const last = out[out.length - 1];
    if (last && last.v === s.v) continue;
    if (last && s.t === last.t) last.v = s.v;
    else out.push(s);
  }
  return out;
}

const num = (p: Record<string, unknown> | undefined, k: string, d: number) => (typeof p?.[k] === 'number' ? (p[k] as number) : d);

export function decodeUart(r: WaveformRecord, from: Time, to: Time, params?: Record<string, unknown>): Annotation[] {
  const baud = num(params, 'baud', 9600);
  const bits = num(params, 'bits', 8);
  const stopBits = num(params, 'stop', 1);
  const parity = typeof params?.parity === 'string' ? params.parity : 'none';
  const invert = params?.invert === true;
  if (baud <= 0) return [];
  const bit = BigInt(Math.max(1, Math.round(1e12 / baud)));
  const half = bit / 2n;
  const pbits = parity === 'none' ? 0 : 1;
  const frame = bit * BigInt(1 + bits + pbits + stopBits);
  const level = (t: Time) => {
    const v = r.valueAt(t);
    return invert ? (v === 0 ? 1 : v === 1 ? 0 : v) : v;
  };
  // start well before the window: frames sent back to back only line up
  // from an idle line, and a frame that began before the window counts
  const lookback = frame * 64n;
  const begin = from > lookback ? from - lookback : 0n;
  const out: Annotation[] = [];
  let next = begin;
  let prev: number | null = null;
  for (const { t, v: raw } of transitions(r, begin, to)) {
    const v = invert ? (raw === 0 ? 1 : raw === 1 ? 0 : raw) : raw;
    const falling = prev === 1 && v === 0;
    prev = v;
    if (!falling || t < next) continue;
    // the start bit is still low at its middle, or it was a glitch
    if (level(t + half) !== 0) continue;
    let value = 0;
    let ones = 0;
    for (let k = 0; k < bits; k++) {
      const b = level(t + bit * BigInt(1 + k) + half) === 1 ? 1 : 0;
      value |= b << k;
      ones += b;
    }
    let ok = true;
    if (pbits) {
      const p = level(t + bit * BigInt(1 + bits) + half) === 1 ? 1 : 0;
      ok = parity === 'even' ? (ones + p) % 2 === 0 : (ones + p) % 2 === 1;
    }
    const stopOk = level(t + bit * BigInt(1 + bits + pbits) + half) === 1;
    const end = t + frame;
    if (end >= from) out.push({ start: t, end, text: ok && stopOk ? byteText(value) : `${byteText(value)} ${stopOk ? 'parity' : 'framing'}`, kind: ok && stopOk ? 'data' : 'error' });
    next = t + bit * BigInt(1 + bits + pbits) + half;
    if (out.length >= LIMIT) break;
  }
  return out;
}

export function decodeSpi(sck: WaveformRecord, from: Time, to: Time, params: Record<string, unknown> | undefined, lookup: RecordLookup): Annotation[] {
  const mosi = typeof params?.mosi === 'string' ? lookup(params.mosi) : undefined;
  const miso = typeof params?.miso === 'string' ? lookup(params.miso) : undefined;
  const cs = typeof params?.cs === 'string' ? lookup(params.cs) : undefined;
  const cpol = num(params, 'cpol', 0);
  const cpha = num(params, 'cpha', 0);
  const bits = num(params, 'bits', 8);
  const lsbFirst = params?.lsb_first === true;
  // data is sampled on the clock's leading edge (cpha 0) or trailing edge (cpha 1)
  const sampleRising = (cpol === 0) === (cpha === 0);
  const out: Annotation[] = [];
  let n = 0;
  let mo = 0;
  let mi = 0;
  let first: Time = 0n;
  let csWasLow = false;
  let prev: number | null = null;
  for (const { t, v } of transitions(sck, from, to)) {
    const edge = prev !== null && prev !== v && v <= 1 && prev <= 1;
    prev = v;
    if (!edge || (v === 1) !== sampleRising) continue;
    const selected = cs ? cs.valueAt(t) === 0 : true;
    if (!selected || (cs && !csWasLow)) {
      n = 0; // a frame starts with the chip selected
      mo = 0;
      mi = 0;
    }
    csWasLow = selected;
    if (!selected) continue;
    if (n === 0) first = t;
    const bo = mosi?.valueAt(t) === 1 ? 1 : 0;
    const bi = miso?.valueAt(t) === 1 ? 1 : 0;
    if (lsbFirst) {
      mo |= bo << n;
      mi |= bi << n;
    } else {
      mo = (mo << 1) | bo;
      mi = (mi << 1) | bi;
    }
    n++;
    if (n === bits) {
      const text = mosi && miso ? `${hex(mo)}/${hex(mi)}` : hex(mosi || !miso ? mo : mi);
      out.push({ start: first, end: t, text, kind: 'data' });
      n = 0;
      mo = 0;
      mi = 0;
      if (out.length >= LIMIT) break;
    }
  }
  return out;
}

export function decodeI2c(scl: WaveformRecord, from: Time, to: Time, params: Record<string, unknown> | undefined, lookup: RecordLookup): Annotation[] {
  const sda = typeof params?.sda === 'string' ? lookup(params.sda) : undefined;
  if (!sda) return [];
  // both lines' changes in time order; SDA's first when they coincide
  const events = [...transitions(sda, from, to).map((e) => ({ ...e, line: 'sda' as const })), ...transitions(scl, from, to).map((e) => ({ ...e, line: 'scl' as const }))].sort((a, b) =>
    a.t < b.t ? -1 : a.t > b.t ? 1 : a.line === b.line ? 0 : a.line === 'sda' ? -1 : 1,
  );
  const out: Annotation[] = [];
  let sclLevel = scl.valueAt(from);
  let sdaLevel = sda.valueAt(from);
  let inFrame = false;
  let isAddress = false;
  let n = 0;
  let value = 0;
  let first: Time = 0n;
  for (const e of events) {
    if (e.line === 'sda') {
      const was = sdaLevel;
      sdaLevel = e.v;
      if (sclLevel === 1 && was === 1 && e.v === 0) {
        out.push({ start: e.t, end: e.t, text: 'S', kind: 'start' });
        inFrame = true;
        isAddress = true;
        n = 0;
        value = 0;
      } else if (sclLevel === 1 && was === 0 && e.v === 1 && inFrame) {
        out.push({ start: e.t, end: e.t, text: 'P', kind: 'stop' });
        inFrame = false;
      }
      continue;
    }
    const rising = sclLevel === 0 && e.v === 1;
    sclLevel = e.v;
    if (!rising || !inFrame) continue;
    const b = sdaLevel === 1 ? 1 : 0;
    if (n === 0) first = e.t;
    if (n < 8) {
      value = (value << 1) | b;
      n++;
      continue;
    }
    // the ninth clock: the receiver's acknowledge (SDA low)
    const ack = b === 0 ? 'ACK' : 'NACK';
    const text = isAddress ? `${hex(value >> 1)} ${value & 1 ? 'R' : 'W'} ${ack}` : `${hex(value)} ${ack}`;
    out.push({ start: first, end: e.t, text, kind: isAddress ? 'address' : 'data' });
    isAddress = false;
    n = 0;
    value = 0;
    if (out.length >= LIMIT) break;
  }
  return out;
}

/** Runs `spec` on `record` over [from, to]. */
export function decode(spec: DecoderSpec, record: WaveformRecord, from: Time, to: Time, lookup: RecordLookup): Annotation[] {
  if (spec.protocol === 'uart') return decodeUart(record, from, to, spec.params);
  if (spec.protocol === 'spi') return decodeSpi(record, from, to, spec.params, lookup);
  if (spec.protocol === 'i2c') return decodeI2c(record, from, to, spec.params, lookup);
  return [];
}
