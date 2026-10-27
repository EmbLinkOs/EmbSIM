// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { stableJson, verifyManifest } from './engineering';
import type { AcRequest, AcSweep } from './types';
export function acGrid(start: number, stop: number, count: number, logarithmic = true): number[] {
  if (!Number.isFinite(start) || !Number.isFinite(stop) || start < .001 || stop > 1e12 || stop < start || !Number.isInteger(count) || count < 1 || count > 256 || (count === 1 ? start !== stop : start === stop)) throw new Error('Choose 1–256 ascending frequencies from 1 mHz to 1 THz; one point requires equal endpoints.');
  if (count === 1) return [start];
  const values = Array.from({length:count}, (_,i) => i === 0 ? start : i === count-1 ? stop : logarithmic ? Math.exp(Math.log(start)+(Math.log(stop)-Math.log(start))*i/(count-1)) : start+(stop-start)*i/(count-1));
  if (values.some((v,i) => i > 0 && v <= values[i-1]!)) throw new Error('Frequency spacing is too small to represent.');
  return values;
}
export async function verifyAc(result: AcSweep, request: AcRequest, projectJson: string): Promise<void> {
  if (result.kind !== 'ac_sweep' || result.method !== 'linear_phasor_mna' || result.excitation !== 'unit_real_source_phasor' || !result.ok || stableJson(result.request) !== stableJson(request)) throw new Error('AC result does not match the requested analysis.');
  const manifest = await verifyManifest(result.source);
  if (manifest.source !== 'native' || manifest.project.json !== projectJson) throw new Error('AC result belongs to a different circuit.');
  const source = manifest.models.find((m) => m.path === request.source);
  if (!source || ({vsource:'V/V',isource:'ohm'} as Record<string,string>)[source.model] !== result.unit || result.points.length !== request.frequencies_hz.length) throw new Error('Invalid AC source, units or point count.');
  for (const [i,point] of result.points.entries()) {
    if (point.frequency_hz !== request.frequencies_hz[i] || point.responses.length !== request.probes.length) throw new Error('AC result frequency grid differs from request.');
    for (const [j,r] of point.responses.entries()) {
      const magnitude = Math.hypot(r.real,r.imag), phase = magnitude ? Math.atan2(r.imag,r.real)*180/Math.PI : null, db = magnitude ? 20*Math.log10(magnitude) : null;
      const same = (a: number | null,b: number | null) => a === null || b === null ? a === b : Number.isFinite(a) && Number.isFinite(b) && Math.abs(a-b) <= 1e-10*Math.max(1,Math.abs(b));
      if (r.target !== request.probes[j] || ![r.real,r.imag].every(Number.isFinite) || !same(r.magnitude,magnitude) || !same(r.phase_deg,phase) || !same(r.db,db)) throw new Error('Invalid AC response coefficients or scaling.');
    }
  }
}
export function acCsv(result: AcSweep): string {
  const quote=(v:unknown) => `"${String(v ?? '').replaceAll('"','""')}"`;
  return [['Manifest SHA-256','Source','Reference','Frequency Hz','Target','Unit','Real','Imaginary','Magnitude','dB','Phase deg'],...result.points.flatMap((p) => p.responses.map((r) => [result.source.sha256,result.request.source,result.request.reference,p.frequency_hz,r.target,result.unit,r.real,r.imag,r.magnitude,r.db,r.phase_deg]))].map((row) => row.map(quote).join(',')).join('\r\n')+'\r\n';
}
