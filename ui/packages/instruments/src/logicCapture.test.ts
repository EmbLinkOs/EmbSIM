// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import type { LogicCapture } from '@embsim/protocol';
import { describe, expect, it } from 'vitest';
import { captureToCsv, captureToVcd, LogicCaptureRecords } from './logicCapture';

// a UART byte's first edges on D0 at 1 MS/s, 1 ms in: idle high, the start bit's fall at sample 3, a 1 at 107
const read = (over: Partial<LogicCapture>): LogicCapture => ({
  kind: 'logic', state: 'capturing', sample_rate: 1e6, start_ps: '1000000000', period_ps: '1000000', trigger: -1, end: 5000,
  channels: 255, first: 0, since: 0, next: 3, full: false, transitions: [[0, 1], [3, 0], [107, 1]], ...over,
});

describe('a logic analyzer capture followed live', () => {
  it('puts each transition on the sample grid, per channel', () => {
    const rec = new LogicCaptureRecords('LA');
    rec.ingest(read({}));
    expect(rec.edges.map((e) => e.t)).toEqual([1_000_000_000n, 1_003_000_000n, 1_107_000_000n]);
    const d0 = rec.records[0]!;
    expect(d0.valueAt(1_000_000_000n)).toBe(1);
    expect(d0.valueAt(1_050_000_000n)).toBe(0); // in the start bit
    expect(d0.valueAt(1_200_000_000n)).toBe(1);
    expect(rec.records[1]!.valueAt(1_050_000_000n)).toBe(0); // D1 held low throughout
  });
  it('appends what each read adds, and starts over for a new capture', () => {
    const rec = new LogicCaptureRecords('LA');
    rec.ingest(read({}));
    rec.ingest(read({ since: 3, next: 4, transitions: [[211, 0]] }));
    expect(rec.edges).toHaveLength(4);
    expect(rec.next).toBe(4);
    rec.ingest(read({ start_ps: '9000000000', since: 0, next: 1, transitions: [[0, 0]] }));
    expect(rec.edges).toHaveLength(1);
    expect(rec.start).toBe(9_000_000_000n);
  });
  it('shows nothing while waiting for the trigger, then reads the capture again from its first record', () => {
    const rec = new LogicCaptureRecords('LA');
    rec.ingest(read({ state: 'armed', first: 0, since: 0, next: 900, transitions: [] }));
    expect(rec.state).toBe('armed');
    expect(rec.edges).toHaveLength(0);
    // triggered: the pre-trigger window kept from record 880 on; a read from 900 starts over
    rec.ingest(read({ state: 'capturing', first: 880, since: 900, next: 905, transitions: [[50, 1]] }));
    expect(rec.needsRestart).toBe(true);
    expect(rec.next).toBe(880);
    rec.ingest(read({ state: 'capturing', first: 880, since: 880, next: 883, trigger: 2, transitions: [[0, 1], [2, 0], [9, 1]] }));
    expect(rec.needsRestart).toBe(false);
    expect(rec.edges).toHaveLength(3);
    expect(rec.trigger).toBe(1_002_000_000n);
  });
  it('knows the trigger and the end in time', () => {
    const rec = new LogicCaptureRecords('LA');
    rec.ingest(read({ trigger: 3, state: 'done' }));
    expect(rec.trigger).toBe(1_003_000_000n);
    expect(rec.end).toBe(1_000_000_000n + 5000n * 1_000_000n);
    expect(rec.state).toBe('done');
  });
});

describe('exports', () => {
  it("writes Logic 2's digital CSV: seconds from the trigger, a row per change, the enabled channels", () => {
    const rec = new LogicCaptureRecords('LA');
    rec.ingest(read({ trigger: 3, channels: 0b11 }));
    expect(captureToCsv(rec)).toBe('Time [s],Channel 0,Channel 1\n-0.000003,1,0\n0,0,0\n0.000104,1,0\n');
  });
  it('writes a VCD with a wire per channel and the changes in picoseconds', () => {
    const rec = new LogicCaptureRecords('LA');
    rec.ingest(read({ channels: 0b1 }));
    const vcd = captureToVcd(rec);
    expect(vcd).toContain('$timescale 1 ps $end');
    expect(vcd).toContain('$var wire 1 ! D0 $end');
    expect(vcd).toContain('#0\n$dumpvars\n1!\n$end\n#3000000\n0!\n#107000000\n1!\n');
  });
});
