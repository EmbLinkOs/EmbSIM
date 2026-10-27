// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { renderToStaticMarkup } from 'react-dom/server';
import { describe,expect,it } from 'vitest';
import type { AcSweep } from '@embsim/protocol';
import { AcPlot } from './AcSweeps';
const data={unit:'V/V',points:[{frequency_hz:10,responses:[{db:0,phase_deg:179}]},{frequency_hz:100,responses:[{db:null,phase_deg:null}]},{frequency_hz:1000,responses:[{db:-10,phase_deg:-179}]}]} as AcSweep;
describe('AC Bode plots',()=>{
  it('breaks at zeros without inventing phase or a finite logarithm',()=>{
    const svg=renderToStaticMarkup(<AcPlot data={data} channel={0} selected={0} phase={false}/>);
    expect(svg.match(/<path d="([^"]+)"/)?.[1].match(/M/g)).toHaveLength(2);
    expect(svg).not.toMatch(/NaN|Infinity/);
  });
  it('breaks wrapped phase and keeps a single-frequency cursor finite',()=>{
    const wrapped={...data,points:[data.points[0]!,data.points[2]!]};
    const svg=renderToStaticMarkup(<AcPlot data={wrapped} channel={0} selected={0} phase/>);
    expect(svg.match(/<path d="([^"]+)"/)?.[1].match(/M/g)).toHaveLength(2);
    const single=renderToStaticMarkup(<AcPlot data={{...data,points:[data.points[0]!]}} channel={0} selected={0} phase/>);
    expect(single).toContain('<circle');expect(single).not.toMatch(/NaN|Infinity/);
  });
});
