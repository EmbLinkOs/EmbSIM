// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe, it, expect } from 'vitest';
import { Acquisition, type AcquisitionSettings } from './acquisition';
import { WaveformRecord } from './waveform';

const settings: AcquisitionSettings = { mode: 'single', source: 'signal', edge: 'rising', level: 2, hysteresis: 0.2, span: 20n, preTrigger: 0.25, holdoff: 0n };
function frame(r: WaveformRecord, pairs: [number, number][], through: number, extra = {}) {
  r.appendFrame({ stream: 's', seq: 0, probe: r.id, encoding: 'json', samples: pairs.map(([t,v]) => [String(t),v]), through_time_ps: String(through), ...extra });
}

describe('sampled acquisition', () => {
  it('freezes the complete pre/post window only after every trace reaches the frontier', () => {
    const s = new WaveformRecord('signal','analog'), response = new WaveformRecord('response','analog');
    const records = new Map([[s.id,s],[response.id,response]]), a = new Acquisition(settings, 0n);
    frame(s, [[0,0],[10,5]], 30); frame(response, [[0,0],[10,1]], 20); a.update(records);
    expect(a.phase).toBe('post-trigger'); expect(a.frame).toBeNull();
    frame(response, [[25,2]], 30); a.update(records);
    expect(a.phase).toBe('captured'); expect(a.frame?.at).toBe(10n);
    expect(a.frame?.window).toEqual({ start: 5n, span: 20n, follow: false });
    s.clear(); response.append(35n,1n,Float32Array.of(99)); a.update(records);
    expect(a.frame?.records.get('signal')?.observedValueAt(20n)).toBe(5);
    expect(a.frame?.records.get('response')?.observedValueAt(25n)).toBe(2);
  });
  it('normal acquisition and holdoff give the same result across packet boundaries', () => {
    const pairs: [number,number][] = [[0,0],[10,5],[20,0],[30,5],[40,0],[50,5],[60,0],[70,5],[80,0],[90,5],[100,0]];
    const run = (chunk: number) => {
      const s = new WaveformRecord('signal','analog'), a = new Acquisition({ ...settings, mode:'normal', holdoff: 10n },0n), records = new Map([[s.id,s]]);
      for (let i=0;i<pairs.length;i+=chunk) { const batch = pairs.slice(i,i+chunk); frame(s,batch,batch.at(-1)![0]); a.update(records); }
      frame(s,[],110); a.update(records);
      return [a.frame?.at, a.frame?.number, a.phase];
    };
    expect(run(100)).toEqual(run(1)); expect(run(2)).toEqual(run(3));
    expect(run(100)).toEqual([90n,3,'armed']);
  });
  it('does not trigger on chatter inside the hysteresis band', () => {
    const s = new WaveformRecord('signal','analog'), a = new Acquisition(settings,0n);
    frame(s,[[0,0],[5,1.99],[10,2.01],[15,1.98],[20,2.05],[30,2.2]],50); a.update(new Map([[s.id,s]]));
    expect(a.frame?.at).toBe(30n);
  });
  it.each(['rising','falling','either'] as const)('handles %s digital edges with unknown-state qualification', (edge) => {
    const s = new WaveformRecord('signal','digital'), a = new Acquisition({ ...settings, edge, preTrigger:0 },0n);
    frame(s,[[0,3],[5,1],[10,2],[15,0],[20,1],[25,0]],50); a.update(new Map([[s.id,s]]));
    expect(a.frame?.at).toBe(edge === 'falling' ? 25n : 20n);
  });
  it('does not invent an edge across a loss or accept incomplete prehistory', () => {
    const s = new WaveformRecord('signal','analog'), a = new Acquisition(settings,0n), records = new Map([[s.id,s]]);
    frame(s,[[0,0]],0); a.update(records); frame(s,[[10,5]],30,{gap:true}); a.update(records);
    expect(a.frame).toBeNull();
    frame(s,[[31,0],[32,5]],60); a.update(records); expect(a.frame?.at).toBe(32n);
  });
  it('invalidates pending capture on reset or source epoch change', () => {
    const s = new WaveformRecord('signal','analog'), a = new Acquisition(settings,0n), records = new Map([[s.id,s]]);
    frame(s,[[0,0],[10,5]],12,{capture_epoch:1}); a.update(records); expect(a.phase).toBe('post-trigger');
    frame(s,[[13,5]],40,{capture_epoch:2}); a.update(records); expect(a.phase).toBe('armed'); expect(a.frame).toBeNull();
  });
  it('arms from the current clock without triggering on historical edges', () => {
    const s = new WaveformRecord('signal','analog'), a = new Acquisition(settings,50n);
    frame(s,[[0,0],[10,5],[20,0]],40); a.update(new Map([[s.id,s]])); expect(a.frame).toBeNull();
    frame(s,[[60,5]],100); a.update(new Map([[s.id,s]])); expect(a.frame?.at).toBe(60n);
  });
  it('preserves same-time transition order', () => {
    const s = new WaveformRecord('signal','digital'), a = new Acquisition({...settings,preTrigger:0},0n);
    frame(s,[[10,0],[10,1]],40); a.update(new Map([[s.id,s]])); expect(a.frame?.at).toBe(10n);
  });
  it('discards a pending source window when its post-trigger samples are lost', () => {
    const s = new WaveformRecord('signal','analog'), a = new Acquisition(settings,0n), records = new Map([[s.id,s]]);
    frame(s,[[0,0],[10,5]],12); a.update(records); expect(a.phase).toBe('post-trigger');
    frame(s,[[30,5]],40,{gap:true}); a.update(records);
    expect(a.frame).toBeNull(); expect(a.phase).toBe('armed');
  });
  it('rejects invalid settings', () => {
    expect(() => new Acquisition({...settings,span:0n},0n)).toThrow();
    expect(() => new Acquisition({...settings,preTrigger:1.01},0n)).toThrow();
    expect(() => new Acquisition({...settings,level:NaN},0n)).toThrow();
  });
});
