// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { beforeEach, expect, it } from 'vitest';
import { useInstruments } from './instruments';
import { exportCsv, exportVcd } from '../actions';

beforeEach(() => { useInstruments.getState().live(); useInstruments.getState().setProbes([]); });
function probe(kind: 'analog' | 'digital' = 'analog', epoch = 2) {
  useInstruments.getState().setProbes([{probe:'p',kind,target:'net:A',capture_epoch:epoch}]);
  return useInstruments.getState().records.get('p')!;
}
it('metadata cannot erase the new frame or admit obsolete source samples', () => {
  const r = probe();
  useInstruments.getState().appendFrame({stream:'s',seq:1,probe:'p',capture_epoch:3,encoding:'json',samples:[['10',5]],through_time_ps:'20'});
  useInstruments.getState().setProbes([{probe:'p',kind:'analog',target:'net:B',capture_epoch:3}]);
  expect(r.length).toBe(1); expect(r.observedValueAt(15n)).toBe(5);
  useInstruments.getState().appendFrame({stream:'s',seq:2,probe:'p',capture_epoch:2,restart:true,encoding:'json',samples:[['0',0]]});
  expect(r.start).toBe(10n);
});
it('fresh sessions accept their own epochs; same-session clears still reject older epochs', () => {
  const r = probe('analog',50); r.clear(); expect(r.setCaptureEpoch(2)).toBe(false);
  useInstruments.getState().clearRecords(true);
  useInstruments.getState().setProbes([{probe:'p',kind:'analog',target:'net:A',capture_epoch:2}]);
  const next = useInstruments.getState().records.get('p')!;
  expect(next.captureEpoch).toBe(2); expect(next).not.toBe(r);
});
it('CSV leaves loss/future blank and VCD refuses to invent analog continuity', () => {
  const r = probe(); r.append(0n,1n,Float32Array.of(1)); r.append(10n,1n,Float32Array.of(2),true);
  expect(exportCsv(['p'],0n,12n,12)).toContain('5,\n');
  expect(exportCsv(['p'],0n,12n,12)).toContain('12,');
  expect(() => exportVcd(['p'],0n,10n)).toThrow('complete analog coverage');
});
it('VCD seeds the left endpoint and marks digital loss as X', () => {
  const r = probe('digital'); r.append(0n,1n,Uint8Array.of(1)); r.append(10n,1n,Uint8Array.of(0),true);
  const text = exportVcd(['p'],0n,10n);
  expect(text).toContain('#0\n1!'); expect(text).toContain('#1\nx!'); expect(text).toContain('#10\n0!');
});
it('serial probes do not block numeric acquisition waiting for a nonexistent frontier', () => {
  probe(); useInstruments.getState().setProbes([...useInstruments.getState().probes,{probe:'serial',kind:'serial',target:'serial:UART'}]);
  expect([...useInstruments.getState().records.keys()]).toEqual(['p']);
});
