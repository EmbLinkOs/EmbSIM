// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe,expect,it } from 'vitest';
import { fftInterval,spectrumCsv } from './spectrum';
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
