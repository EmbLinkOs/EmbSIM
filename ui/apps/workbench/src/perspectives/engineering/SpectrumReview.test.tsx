// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { renderToStaticMarkup } from 'react-dom/server';
import { describe,expect,it } from 'vitest';
import type { FrequencySpectrum } from '@embsim/protocol';
import { SpectrumPlot } from './SpectrumReview';
const data={bins:[{frequency_hz:0,peak_v:0,dbv_rms:null},{frequency_hz:1000,peak_v:1,dbv_rms:-3.0103},{frequency_hz:2000,peak_v:0,dbv_rms:null}]} as FrequencySpectrum;
describe('spectrum displays',()=>{
  it('renders discrete bins and a selected marker without inventing zero dBV',()=>{
    const svg=renderToStaticMarkup(<SpectrumPlot data={data} first={0} last={2} selected={1} scale="dbv" floor={-140} />);
    const path=svg.match(/<path d="([^"]+)"/)?.[1] ?? '';
    expect(path.match(/M/g)).toHaveLength(1);expect(svg).toContain('<circle');expect(svg).not.toMatch(/NaN|Infinity/);
  });
  it('keeps a single-bin view finite and preserves raw data under floor clipping',()=>{
    const before=structuredClone(data);
    const svg=renderToStaticMarkup(<SpectrumPlot data={data} first={1} last={1} selected={1} scale="dbv" floor={0} />);
    expect(svg).not.toMatch(/NaN|Infinity/);expect(svg).not.toContain('<circle');expect(data).toEqual(before);
  });
});
