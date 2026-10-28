// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe,expect,it } from 'vitest';
import { compareSpectra,spectrumComparisonCsv,fftInterval,spectrumCsv } from './spectrum';
import type { FrequencySpectrum } from './types';
describe('spectrum preparation and exports',()=>{
  it('proposes exact half-open FFT grids without padding data',()=>{
    expect(fftInterval('16ms','32ms')).toBe('62500000ps');
    expect(fftInterval('0us','500us')).toBe('1953125ps');
    expect(()=>fftInterval('0ps','257ps')).toThrow();
    expect(()=>fftInterval('10us','0us')).toThrow();
  });
  it('keeps zero-bin logarithm/phase undefined in source-bound CSV',()=>{
    const data={capture:{sha256:'sha256:record'},target:'net:OUT',window:'hann',remove_mean:true,bins:[{index:0,frequency_hz:0,peak_v:0,rms_v:0,dbv_rms:null,phase_deg:null,real_v:0,imag_v:0}]} as FrequencySpectrum;
    const csv=spectrumCsv(data);expect(csv).toContain('"sha256:record","net:OUT","hann","true","0","0","0","0","","","0","0"');
  });
});

describe('captured channel ratios',()=>{
  const spectrum=(real:number,imag:number):FrequencySpectrum => ({capture:{sha256:'same',json:'same'},target:'net:OUT',window:'hann',remove_mean:true,used_samples:16,sample_rate_hz:16000,bin_width_hz:1000,nyquist_hz:8000,bins:[{index:1,frequency_hz:1000,real_v:real,imag_v:imag}]} as FrequencySpectrum);
  it('recovers analytic RC gain and phase relative to the input',()=>{
    const wrc=2*Math.PI*.1;
    const output=spectrum(1/(1+wrc*wrc),-wrc/(1+wrc*wrc));
    const result=compareSpectra(output,spectrum(1,0),1e-6), b=result.bins[0]!;
    expect(b.gain).toBeCloseTo(1/Math.sqrt(1+wrc*wrc),12);
    expect(b.gain_db).toBeCloseTo(-10*Math.log10(1+wrc*wrc),12);
    expect(b.phase_deg).toBeCloseTo(-Math.atan(wrc)*180/Math.PI,12);
    expect(spectrumComparisonCsv(result)).toContain('"valid"');
    expect(result.output).toBe(output);
  });
  it('rejects source, settings and frequency mismatches',()=>{
    const a=spectrum(1,0);
    for(const patch of [{capture:{sha256:'other',json:'same'}},{capture:{sha256:'same',json:'other'}},{window:'rectangular'},{remove_mean:false},{used_samples:32},{sample_rate_hz:32000},{bins:[{...a.bins[0],frequency_hz:2000}]}]) expect(()=>compareSpectra(a,{...a,...patch} as FrequencySpectrum,0)).toThrow();
    for(const t of [-1,NaN,Infinity]) expect(()=>compareSpectra(a,a,t)).toThrow();
  });
  it('excludes zero and weak reference bins, retaining a true zero output',()=>{
    for(const v of [0,1e-12,1e-6]) expect(compareSpectra(spectrum(1,0),spectrum(v,0),1e-6).bins[0]).toMatchObject({status:'weak_reference',gain:null,gain_db:null,phase_deg:null});
    expect(compareSpectra(spectrum(0,0),spectrum(1,0),0).bins[0]).toMatchObject({status:'valid',gain:0,gain_db:null,phase_deg:null});
  });
  it('wraps relative phase and refuses overflow or underflow',()=>{
    expect(compareSpectra(spectrum(-1,-.01),spectrum(-1,.01),0).bins[0]!.phase_deg).toBeCloseTo(1.1458774,6);
    for(const [a,b] of [[1e308,1e-308],[1e-308,1e308]]) expect(compareSpectra(spectrum(a!,0),spectrum(b!,0),0).bins[0]!.status).toBe('unrepresentable');
    expect(compareSpectra(spectrum(1e-300,0),spectrum(1e-300,0),0).bins[0]!.gain).toBe(1);
  });
});
