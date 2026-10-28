// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import type { FrequencySpectrum, TransientCapture } from './types';
import { parseDuration } from './time';
import { stableJson, verifyManifest } from './engineering';
export function fftInterval(start: string, stop: string, intervals = 256): string {
  const duration = parseDuration(stop)-parseDuration(start);
  if (!Number.isInteger(intervals) || intervals < 16 || intervals > 4096 || (intervals & (intervals-1)) || duration <= 0n || duration % BigInt(intervals))
    throw new Error('Choose a positive time range divisible by the FFT interval count in picoseconds.');
  return `${duration/BigInt(intervals)}ps`;
}
export async function verifySpectrum(result: FrequencySpectrum, expected: TransientCapture, subtle: SubtleCrypto | undefined = globalThis.crypto?.subtle): Promise<void> {
  if (!subtle || result.kind !== 'frequency_spectrum' || result.method !== 'radix2_coherent_amplitude' || result.record_convention !== 'half_open_drop_endpoint' || result.phase_reference !== 'first_sample_cosine') throw new Error('Unsupported spectrum evidence or missing secure SHA-256 support.');
  const bytes = new TextEncoder().encode(result.capture.json), hash = await subtle.digest('SHA-256', bytes);
  const value = 'sha256:'+Array.from(new Uint8Array(hash), (b) => b.toString(16).padStart(2,'0')).join('');
  if (value !== result.capture.sha256) throw new Error('Spectrum capture hash mismatch.');
  const capture = JSON.parse(result.capture.json) as TransientCapture;
  if (stableJson(capture) !== stableJson(expected)) throw new Error('Spectrum belongs to a different captured record.');
  await verifyManifest(capture.source, subtle);
}
export function spectrumCsv(result: FrequencySpectrum): string {
  const quote = (v: unknown) => `"${String(v ?? '').replaceAll('"', '""')}"`;
  return [['Capture SHA-256','Target','Window','Mean removed','Bin','Frequency Hz','Peak V','RMS bin V','dBV RMS bin','Phase deg','Real V','Imag V'],...result.bins.map((b) => [result.capture.sha256,result.target,result.window,result.remove_mean,b.index,b.frequency_hz,b.peak_v,b.rms_v,b.dbv_rms,b.phase_deg,b.real_v,b.imag_v])].map((r) => r.map(quote).join(',')).join('\r\n')+'\r\n';
}

/** A finite-record channel ratio, not an AC solver or a transfer estimator. */
export interface SpectrumComparison {
  method: 'captured_bin_ratio';
  minimum_reference_peak_v: number;
  output: FrequencySpectrum;
  reference: FrequencySpectrum;
  bins: {index: number; frequency_hz: number; status: 'valid' | 'weak_reference' | 'unrepresentable'; gain: number | null; gain_db: number | null; phase_deg: number | null}[];
}
export function compareSpectra(output: FrequencySpectrum, reference: FrequencySpectrum, minimumReferencePeakV: number): SpectrumComparison {
  if (!Number.isFinite(minimumReferencePeakV) || minimumReferencePeakV < 0) throw new Error('Reference threshold must be a finite nonnegative peak voltage.');
  if (output.capture.sha256 !== reference.capture.sha256 || output.capture.json !== reference.capture.json ||
      output.window !== reference.window || output.remove_mean !== reference.remove_mean ||
      output.used_samples !== reference.used_samples || output.sample_rate_hz !== reference.sample_rate_hz ||
      output.bin_width_hz !== reference.bin_width_hz || output.nyquist_hz !== reference.nyquist_hz ||
      output.bins.length !== reference.bins.length) throw new Error('Channel comparison requires the same captured record, FFT grid and analysis settings.');
  const bins = output.bins.map((b, i) => {
    const r = reference.bins[i]!;
    if (b.index !== r.index || b.frequency_hz !== r.frequency_hz || ![b.real_v,b.imag_v,r.real_v,r.imag_v].every(Number.isFinite)) throw new Error('Channel comparison contains incompatible or invalid bins.');
    const base = {index:b.index,frequency_hz:b.frequency_hz,gain:null,gain_db:null,phase_deg:null};
    const denominator = Math.hypot(r.real_v,r.imag_v), numerator = Math.hypot(b.real_v,b.imag_v);
    if (denominator <= minimumReferencePeakV) return {...base,status:'weak_reference' as const};
    const gain = numerator/denominator;
    if (!Number.isFinite(gain) || !Number.isFinite(denominator) || !Number.isFinite(numerator) || (gain === 0 && numerator !== 0)) return {...base,status:'unrepresentable' as const};
    const rawPhase = (Math.atan2(b.imag_v,b.real_v)-Math.atan2(r.imag_v,r.real_v))*180/Math.PI;
    return {...base,status:'valid' as const,gain,gain_db:numerator === 0 ? null : 20*(Math.log10(numerator)-Math.log10(denominator)),phase_deg:numerator === 0 ? null : ((rawPhase+540)%360)-180};
  });
  return {method:'captured_bin_ratio',minimum_reference_peak_v:minimumReferencePeakV,output,reference,bins};
}
export function spectrumComparisonCsv(result: SpectrumComparison): string {
  const quote = (v: unknown) => `"${String(v ?? '').replaceAll('"','""')}"`;
  return [['Capture SHA-256','Output','Reference','Window','Mean removed','Minimum reference peak V','Bin','Frequency Hz','Status','Gain V/V','Gain dB','Phase deg'],...result.bins.map((b) => [result.output.capture.sha256,result.output.target,result.reference.target,result.output.window,result.output.remove_mean,result.minimum_reference_peak_v,b.index,b.frequency_hz,b.status,b.gain,b.gain_db,b.phase_deg])].map((r) => r.map(quote).join(',')).join('\r\n')+'\r\n';
}
