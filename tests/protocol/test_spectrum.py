#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Native coherent FFT: independent DFT/analytic references, scaling, edges and record identity."""
import argparse
import cmath
import copy
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from embsim_client import StdioClient  # noqa: E402
from embsim.transport import EmbsimError  # noqa: E402


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--embsim',required=True,type=Path)
    binary=str(ap.parse_args().embsim.resolve());count=0
    def check(ok,why):
        nonlocal count
        if not ok:raise AssertionError(why)
        count+=1
    def close(a,b,tol=1e-10):check(a is not None and abs(a-b)<=tol,f'{a} != {b}')
    def spectrum(c,data,window='rectangular',remove=True,target='net:OUT'):
        return c.call('analysis.spectrum',{'capture':data,'target':target,'window':window,'remove_mean':remove})
    def reject(c,data,**opts):
        try:c.call('analysis.spectrum',{'capture':data,'target':'net:OUT','window':'hann','remove_mean':True,**opts})
        except EmbsimError:check(True,'')
        else:check(False,'invalid spectrum accepted')
    with tempfile.TemporaryDirectory(prefix='embsim-spectrum-') as tmp, StdioClient([binary,'sidecar']) as c:
        root=Path(tmp)
        check('analysis.spectrum' in c.call('session.open',{'client':'spectrum','protocol':'1.0'})['capabilities'],'missing capability')
        c.call('project.load',{'path':str(ROOT/'examples/spectrum-rc/project.json')});c.call('control.advance',{'by':'1us'})
        base=c.call('analysis.transient',json.loads((ROOT/'examples/spectrum-rc/response.transient.json').read_text(encoding='utf-8')))
        before=[c.call('project.get',{}),c.call('session.info',{})['now_ps'],c.call('probe.list',{})]
        physical=spectrum(c,base,'hann')
        close(physical['bins'][16]['peak_v'],1/math.sqrt(1+(2*math.pi*.1)**2),2e-5)
        close(physical['bins'][16]['phase_deg'],-90-math.degrees(math.atan(2*math.pi*.1)),.01)
        check(before==[c.call('project.get',{}),c.call('session.info',{})['now_ps'],c.call('probe.list',{})],'FFT changes live source/time/probes')
        input_spectrum=spectrum(c,base,'hann',target='net:IN')
        close(input_spectrum['bins'][16]['peak_v'],1,2e-6);close(input_spectrum['bins'][16]['phase_deg'],-90,.01)
        loaded=json.loads((ROOT/'examples/spectrum-rc/project.json').read_text(encoding='utf-8'))
        loaded['components'].append({'id':'M','component':'emblink/voltmeter','params':{'input_resistance':'10k'}})
        loaded['nets']['OUT'].append('M.+');loaded['nets']['GND'].append('M.-')
        c.call('project.load',{'json':loaded})
        loaded_capture=c.call('analysis.transient',base['request']);loaded_spectrum=spectrum(c,loaded_capture,'hann')
        close(loaded_spectrum['bins'][16]['peak_v'],.5/math.sqrt(1+(2*math.pi*.05)**2),2e-5)
        close(loaded_spectrum['bins'][16]['phase_deg'],-90-math.degrees(math.atan(2*math.pi*.05)),.01)
        check(physical['capture']['sha256']=='sha256:'+hashlib.sha256(physical['capture']['json'].encode()).hexdigest(),'capture digest wrong')
        check(json.loads(physical['capture']['json'])==base,'FFT changed raw capture')
        def signal(n,fn):
            d=copy.deepcopy(base);d['request'].update(start='0ps',stop=f'{n}ps',step='1ps',probes=['net:OUT']);d['ended_at_ps']=str(n)
            d['samples']=[{'at_ps':str(i),'measurements':[{'target':'net:OUT','logic':'Z','voltage':fn(i)}]} for i in range(n+1)]
            return d
        tone=signal(64,lambda i:1.5+2*math.cos(2*math.pi*7*i/64+math.pi/6)+.25*math.cos(2*math.pi*13*i/64-math.pi/4))
        for window in ['rectangular','hann']:
            out=spectrum(c,tone,window,False);close(out['mean_v'],1.5);close(out['coherent_gain'],1 if window=='rectangular' else .5);close(out['enbw_bins'],1 if window=='rectangular' else 1.5)
            b=out['bins'][7];close(b['peak_v'],2);close(b['rms_v'],math.sqrt(2));close(b['dbv_rms'],20*math.log10(math.sqrt(2)));close(b['phase_deg'],30)
            close(out['bins'][13]['peak_v'],.25);close(out['bins'][13]['phase_deg'],-45)
            values=[r['measurements'][0]['voltage'] for r in tone['samples'][:-1]];n=len(values)
            weights=[1 if window=='rectangular' else .5-.5*math.cos(2*math.pi*i/n) for i in range(n)]
            for b in out['bins']:
                ref=sum(v*w*cmath.exp(-2j*math.pi*b['index']*i/n) for i,(v,w) in enumerate(zip(values,weights))) * (1 if b['index'] in [0,n//2] else 2)/sum(weights)
                close(b['real_v'],ref.real);close(b['imag_v'],ref.imag)
        offbin=signal(64,lambda i:math.cos(2*math.pi*7.5*i/64))
        leakage=spectrum(c,offbin,'hann')
        check(.84<max(b['peak_v'] for b in leakage['bins'])<.86,'window incorrectly hides off-bin scalloping')
        alias=spectrum(c,signal(64,lambda i:math.cos(2*math.pi*57*i/64)))
        close(alias['bins'][7]['peak_v'],1)
        zero=spectrum(c,signal(16,lambda i:0));check(all(b['peak_v']==0 and b['dbv_rms'] is None and b['phase_deg'] is None for b in zero['bins']),'zero spectrum invents level/phase')
        dc=spectrum(c,signal(32,lambda i:-2),remove=False);close(dc['bins'][0]['real_v'],-2);close(dc['bins'][0]['rms_v'],2);close(abs(dc['bins'][0]['phase_deg']),180)
        removed=spectrum(c,signal(32,lambda i:2));check(all(b['peak_v']==0 for b in removed['bins']),'mean removal leaves a DC tone')
        nyquist=spectrum(c,signal(32,lambda i:.75*(-1)**i));close(nyquist['bins'][-1]['peak_v'],.75);close(nyquist['bins'][-1]['rms_v'],.75)
        endpoint=copy.deepcopy(tone);endpoint['samples'][-1]['measurements'][0]['voltage']=12345
        check(spectrum(c,endpoint)['bins']==spectrum(c,tone)['bins'],'final endpoint was duplicated in FFT')
        large=spectrum(c,signal(4096,lambda i:1.25*math.cos(2*math.pi*129*i/4096+.2)))
        check(large['used_samples']==4096 and len(large['bins'])==2049,'maximum FFT size wrong');close(large['bins'][129]['peak_v'],1.25);close(large['bins'][129]['phase_deg'],math.degrees(.2),1e-8)
        for exponent in [-250,250]:
            scaled=spectrum(c,signal(64,lambda i:10.0**exponent*math.cos(2*math.pi*7*i/64)))
            close(scaled['bins'][7]['peak_v']/10.0**exponent,1,1e-10)
        reject(c,base,window='blackman');reject(c,base,remove_mean='true');reject(c,base,target='net:missing');reject(c,base,extra=True)
        for mutate in [lambda d:d.update(ok=False,failure='error'),lambda d:d['samples'][10].update(at_ps='0'),lambda d:d['samples'][4]['measurements'][1].update(voltage=None),lambda d:d['samples'][4]['measurements'][1].update(target='net:OTHER'),lambda d:d['source'].update(sha256='sha256:'+'0'*64),lambda d:d['request'].update(step='0ps')]:
            d=copy.deepcopy(base);mutate(d);reject(c,d)
        for key in ['project','catalog']:
            d=copy.deepcopy(base);payload=json.loads(d['source']['json'])
            item=payload['project'] if key=='project' else next(iter(payload['catalog'].values()))
            item['json']+=' '
            text=json.dumps(payload,ensure_ascii=False);d['source']['json']=text;d['source']['sha256']='sha256:'+hashlib.sha256(text.encode()).hexdigest()
            reject(c,d)
        d=copy.deepcopy(base);d['request']['kind']='unknown';reject(c,d)
        reject(c,signal(100,lambda i:1));reject(c,signal(8,lambda i:1))
        # Pure captured-data analysis works without any currently loaded project.
        with StdioClient([binary,'sidecar']) as empty:
            empty.call('session.open',{'client':'offline-spectrum','protocol':'1.0'})
            check(spectrum(empty,base,'hann')==physical,'FFT requires a live circuit')
        cap=root/'capture.json';cap.write_text(json.dumps(base),encoding='utf-8')
        cli=subprocess.run([binary,'spectrum',str(cap),'--target','net:OUT'],cwd=ROOT,capture_output=True,text=True,encoding='utf-8')
        check(cli.returncode==0,cli.stderr);check(json.loads(cli.stdout)==physical,'CLI/RPC spectrum mismatch')
        out=root/'spectrum.json';out.write_text(json.dumps(physical),encoding='utf-8')
        # The offline checker independently recomputes the DFT and source identities.
        if (ROOT/'schemas/frequency-spectrum.schema.json').exists():
            valid=subprocess.run([sys.executable,str(ROOT/'tools/validate.py'),str(out),'--strict'],cwd=ROOT,capture_output=True,text=True,encoding='utf-8');check(valid.returncode==0,valid.stdout+valid.stderr)
    print(f'{count} native spectrum acceptance checks, 0 failed')


if __name__=='__main__':main()
