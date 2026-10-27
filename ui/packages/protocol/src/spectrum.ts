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
