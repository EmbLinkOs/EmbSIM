// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe, expect, it } from 'vitest';
import { captureTraces, transientCsv } from './transient';
import type { TransientCapture } from './types';
const data = {source: {sha256: 'sha256:source'}, request: {probes: ['net:IN', 'net:OUT']}, samples: [
  {at_ps: '0', measurements: [{voltage: 3.3, logic: 'Z'}, {voltage: 0, logic: '0'}]},
  {at_ps: '5000000', measurements: [{voltage: null, logic: 'X'}, {voltage: 2, logic: 'Z'}]},
]} as TransientCapture;
describe('transient waveform evidence', () => {
  it('subtracts captured voltages without turning unknowns into zeros', () => {
    const traces = captureTraces(data, 0, 1);
    expect(traces[2]!.values).toEqual([3.3, null]);
    expect(captureTraces(data, 1, 0)[2]!.values).toEqual([-3.3, null]);
    expect(traces[0]!.values).toEqual([3.3, null]);
  });
  it('keeps missing voltages distinct from zero and binds CSV rows to the source', () => {
    const csv = transientCsv(data);
    expect(csv).toContain('"sha256:source","0","0","3.3","Z","0","0"');
    expect(csv).toContain('"5000000","0.000005","","X","2","Z"');
  });
});
