// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { webcrypto } from 'node:crypto';
import { describe,expect,it,vi } from 'vitest';
import { acGrid,acCsv,verifyAc } from './ac';
import type { AcSweep } from './types';
describe('AC grids and exports',()=>{
  it('retains exact endpoints and supports single points and both spacings',()=>{
    const grid=acGrid(10,10000,4);
    expect(grid[0]).toBe(10);expect(grid[3]).toBe(10000);expect(grid[1]).toBeCloseTo(100,10);expect(grid[2]).toBeCloseTo(1000,10);
    expect(acGrid(10,40,4,false)).toEqual([10,20,30,40]);
    expect(acGrid(1000,1000,1)).toEqual([1000]);
    expect(acGrid(.001,1e12,256)).toHaveLength(256);
  });
  it('rejects invalid, collapsed and unrepresentable grids',()=>{
    for(const args of [[0,1000,4],[10,1,4],[1,1,2],[1,2,1],[1,2,0],[1,2,257],[NaN,10,4],[1,Infinity,4],[1,2,1.5],[1e-4,10,4]])expect(()=>acGrid(args[0]!,args[1]!,args[2]!)).toThrow();
    expect(()=>acGrid(1,1+Number.EPSILON,256)).toThrow();
  });
  it('exports all raw coefficients, identities, units and undefined zero semantics',()=>{
    const result={source:{sha256:'sha256:source'},request:{source:'V1',reference:'net:GND'},unit:'ohm',points:[{frequency_hz:1000,responses:[{target:'net:OUT',real:0,imag:0,magnitude:0,db:null,phase_deg:null}]}]} as AcSweep;
    expect(acCsv(result)).toContain('"sha256:source","V1","net:GND","1000","net:OUT","ohm","0","0","0","",""');
  });
});

it('verifies native source identity, requested grid and every response scaling field',async()=>{
  vi.stubGlobal('crypto',webcrypto);
  try {
    const document=async(json:string) => ({json,sha256:'sha256:'+Array.from(new Uint8Array(await webcrypto.subtle.digest('SHA-256',new TextEncoder().encode(json))),(b)=>b.toString(16).padStart(2,'0')).join('')});
    const project=await document('{}');
    const source=await document(JSON.stringify({schema_version:1,kind:'engineering_manifest',source:'native',project,catalog:{},models:[{path:'V1',model:'vsource'}]}));
    const request={source:'V1',reference:'net:GND',frequencies_hz:[1000],probes:['net:OUT']};
    const result={schema_version:1,kind:'ac_sweep',method:'linear_phasor_mna',ok:true,excitation:'unit_real_source_phasor',unit:'V/V',source,request,points:[{frequency_hz:1000,responses:[{target:'net:OUT',real:1,imag:-1,magnitude:Math.SQRT2,db:20*Math.log10(Math.SQRT2),phase_deg:-45}]}]} as AcSweep;
    await expect(verifyAc(result,request,'{}')).resolves.toBeUndefined();
    await expect(verifyAc(result,request,'other')).rejects.toThrow('different circuit');
    for(const field of ['magnitude','db','phase_deg'] as const){const bad=structuredClone(result);bad.points[0]!.responses[0]![field]=999;await expect(verifyAc(bad,request,'{}')).rejects.toThrow('scaling');}
    const bad=structuredClone(result);bad.source.sha256='wrong';await expect(verifyAc(bad,request,'{}')).rejects.toThrow('hash mismatch');
    await expect(verifyAc(result,{...request,frequencies_hz:[2000]},'{}')).rejects.toThrow('requested analysis');
  }finally{vi.unstubAllGlobals();}
});
