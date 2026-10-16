// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe, expect, it } from 'vitest';
import { byteText, decodeI2c, decodeSpi, decodeUart } from './decoders';
import { WaveformRecord } from './waveform';

/** A digital record from (time, level) changes, as the engine streams them. */
function record(id: string, changes: [bigint, number][]): WaveformRecord {
  const r = new WaveformRecord(id, 'digital');
  r.appendFrame({ stream: id, seq: 0, encoding: 'json', samples: changes.map(([t, v]) => [t.toString(), v]) });
  return r;
}

/** UART frames for `bytes`, 8N1, idle high, starting at `t0`. */
function uart(bytes: number[], bit: bigint, t0: bigint, parity: 'none' | 'even' = 'none', idle = 3n): [bigint, number][] {
  const out: [bigint, number][] = [[0n, 1]];
  let t = t0;
  for (const b of bytes) {
    const levels = [0];
    for (let k = 0; k < 8; k++) levels.push((b >> k) & 1);
    if (parity === 'even') levels.push(levels.slice(1).reduce((a, x) => a + x, 0) % 2);
    levels.push(1);
    for (const v of levels) {
      out.push([t, v]);
      t += bit;
    }
    t += bit * idle; // idle between frames
  }
  return out;
}

describe('decoders', () => {
  const bit = 8_680_556n; // 115200 baud in picoseconds

  it('decodes UART bytes at the baud rate', () => {
    const r = record('tx', uart([0x4f, 0x4b, 0x0a], bit, 1_000_000n));
    const a = decodeUart(r, 0n, 10_000_000_000n, { baud: 115200 });
    expect(a.map((x) => x.text)).toEqual(['O', 'K', '\\n']);
    expect(a.every((x) => x.kind === 'data')).toBe(true);
    expect(a[0]!.start).toBe(1_000_000n);
  });

  it('checks UART parity and finds frames that began before the window', () => {
    const r = record('tx', uart([0x31], bit, 1_000_000n, 'even'));
    expect(decodeUart(r, 0n, 10n ** 10n, { baud: 115200, parity: 'even' })[0]!.kind).toBe('data');
    expect(decodeUart(r, 0n, 10n ** 10n, { baud: 115200, parity: 'odd' })[0]!.kind).toBe('error');
    // the window starts in the middle of the frame
    expect(decodeUart(r, 1_000_000n + bit * 4n, 10n ** 10n, { baud: 115200, parity: 'even' }).map((x) => x.text)).toEqual(['1']);
  });

  it('keeps frames sent back to back in step when the window opens mid-stream', () => {
    const text = 'ABCDEFGH';
    const r = record('tx', uart([...text].map((c) => c.charCodeAt(0)), bit, 1_000_000n, 'none', 0n));
    const from = 1_000_000n + bit * 10n * 3n + bit * 3n; // inside the fourth frame
    expect(decodeUart(r, from, 10n ** 10n, { baud: 115200 }).map((x) => x.text).join('')).toBe('DEFGH');
  });

  it('decodes SPI mode 0 with chip select', () => {
    const half = 500n;
    const sck: [bigint, number][] = [[0n, 0]];
    const mosi: [bigint, number][] = [[0n, 0]];
    const value = 0xa5;
    let t = 1000n;
    for (let k = 7; k >= 0; k--) {
      mosi.push([t - 100n, (value >> k) & 1]);
      sck.push([t, 1], [t + half, 0]);
      t += 2n * half;
    }
    const cs = record('cs', [[0n, 1], [500n, 0], [t + 100n, 1]]);
    const records: Record<string, WaveformRecord> = { mosi: record('mosi', mosi), cs };
    const a = decodeSpi(record('sck', sck), 0n, t + 1000n, { mosi: 'mosi', cs: 'cs' }, (id) => records[id]);
    expect(a.map((x) => x.text)).toEqual(['0xA5']);
  });

  it('decodes I2C start, address, data, acknowledge and stop', () => {
    const scl: [bigint, number][] = [[0n, 1]];
    const sda: [bigint, number][] = [[0n, 1]];
    let t = 1000n;
    sda.push([t, 0]); // start: SDA falls while SCL is high
    t += 500n;
    scl.push([t, 0]);
    const send = (byte: number, ack: boolean) => {
      for (let k = 8; k >= 0; k--) {
        const b = k === 0 ? (ack ? 0 : 1) : (byte >> (k - 1)) & 1;
        sda.push([t + 100n, b]);
        scl.push([t + 250n, 1], [t + 500n, 0]);
        t += 500n;
      }
    };
    send((0x3c << 1) | 0, true);
    send(0x7e, false);
    sda.push([t + 100n, 0]);
    scl.push([t + 250n, 1]);
    sda.push([t + 400n, 1]); // stop: SDA rises while SCL is high
    const records: Record<string, WaveformRecord> = { sda: record('sda', sda) };
    const a = decodeI2c(record('scl', scl), 0n, t + 1000n, { sda: 'sda' }, (id) => records[id]);
    expect(a.map((x) => x.text)).toEqual(['S', '0x3C W ACK', '0x7E NACK', 'P']);
  });

  it('shows bytes as characters or hex', () => {
    expect(byteText(0x41)).toBe('A');
    expect(byteText(0x20)).toBe('␠');
    expect(byteText(0x0d)).toBe('\\r');
    expect(byteText(0xff)).toBe('0xFF');
  });
});
