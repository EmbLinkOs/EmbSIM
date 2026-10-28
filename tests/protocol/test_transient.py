#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Source-preserving transient capture, physical loading, analytic RC and truthful missing data."""
import argparse
import copy
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools'))
from embsim_client import StdioClient  # noqa: E402
from embsim.transport import EmbsimError  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__); ap.add_argument('--embsim', required=True, type=Path)
    binary = str(ap.parse_args().embsim.resolve()); count = 0
    source = json.loads((ROOT/'examples/rc-filter/project.json').read_text(encoding='utf-8'))
    request = json.loads((ROOT/'examples/rc-filter/response.transient.json').read_text(encoding='utf-8'))
    def check(ok, why):
        nonlocal count
        if not ok: raise AssertionError(why)
        count += 1
    def close(a,b,tol=2e-4): check(a is not None and abs(a-b)<tol, f'{a} != {b}')
    def snapshot(c): return [c.call(m,p) for m,p in [('project.get',{}),('observe.snapshot',{'include':['outputs']}),('probe.list',{}),('probe.read',{'probe':'out'}),('session.info',{})]]
    def reject(c, changes, text=''):
        try:c.call('analysis.transient',dict(request,**changes))
        except EmbsimError as e:check(text in str(e), str(e))
        else:check(False,f'accepted invalid capture {changes}')
    with tempfile.TemporaryDirectory(prefix='embsim-transient-') as tmp, StdioClient([binary,'sidecar']) as c:
        root=Path(tmp)
        check('analysis.transient' in c.call('session.open',{'client':'transient','protocol':'1.0'})['capabilities'],'capability missing')
        c.call('project.load',{'json':source}); c.call('control.advance',{'by':'1us'})
        before=snapshot(c); result=c.call('analysis.transient',request); after=snapshot(c)
        check(before[:4]==after[:4] and before[4]['now_ps']==after[4]['now_ps'],'capture changed source/state/probe history/clock')
        check(result['ok'] and result['failure'] is None and result['ended_at_ps']=='500000000' and len(result['samples'])==101,'bad complete capture')
        for i,row in enumerate(result['samples']):
            check(row['at_ps']==str(i*5000000),'grid not exact')
            expected=3.3*(1-math.exp(-i*5e-6/1e-4))
            close(row['measurements'][1]['voltage'], expected)
        check(c.call('analysis.transient',request)==result,'capture is not deterministic across independent resets')
        warm=c.call('analysis.transient',dict(request,start='100us',stop='200us'))
        check(len(warm['samples'])==21 and warm['samples'][0]['at_ps']=='100000000','warm-up window wrong')
        close(warm['samples'][0]['measurements'][1]['voltage'],3.3*(1-math.exp(-1)))
        loaded=copy.deepcopy(source)
        loaded['components'].append({'id':'M','component':'emblink/voltmeter','params':{'input_resistance':'10k'}})
        loaded['nets']['OUT'].append('M.+'); loaded['nets']['GND'].append('M.-')
        c.call('project.load',{'json':loaded}); measured=c.call('analysis.transient',dict(request,start='100us',stop='200us'))
        close(measured['samples'][0]['measurements'][1]['voltage'],1.65*(1-math.exp(-2)))
        check(measured['samples'][0]['measurements'][1]['voltage']<warm['samples'][0]['measurements'][1]['voltage']-.6,'instrument does not change response')
        floating=copy.deepcopy(source); floating['components'].append({'id':'TP','component':'emblink/test_point'}); floating['nets']['FLOAT']=['TP.1']
        c.call('project.load',{'json':floating}); missing=c.call('analysis.transient',dict(request,probes=['net:FLOAT']))
        check(missing['ok'] and all(r['measurements'][0]['voltage'] is None and r['measurements'][0]['logic']=='Z' for r in missing['samples']),'floating net fabricated voltages')
        c.call('project.load',{'json':source})
        for change,text in [({'start':'-1us'},'string times'),({'start':0},'string times'),({'stop':'101ms'},'100 ms'),({'step':'0ps'},'positive'),({'step':'3us'},'dividing'),({'step':'1ps'},'4097'),({'stop':'0us'},'start'),({'probes':[]},'1–8'),({'probes':['net:missing']},'existing'),({'probes':['net:OUT']*2},'Duplicate'),({'probes':['output:V1.voltage']},'existing'),({'kind':'noise'},'format'),({'schema_version':1.5},'format'),({'surprise':True},'Unknown')]:reject(c,change,text)
        tx=c.call('project.transaction',{'op':'begin'})['tx'];reject(c,{},'transaction');c.call('project.transaction',{'op':'abort','tx':tx})
        largest=c.call('analysis.transient',dict(request,stop='4096ps',step='1ps',probes=['net:OUT']))
        check(largest['ok'] and len(largest['samples'])==4097 and largest['samples'][1]['at_ps']=='1' and largest['samples'][-1]['at_ps']=='4096','maximum grid loses picosecond endpoints')
        assembly=json.loads((ROOT/'examples/assembly-variants/project.json').read_text(encoding='utf-8'))
        c.call('project.load',{'json':assembly});c.call('project.variant',{'op':'activate','name':'production'})
        derived=c.call('project.get',{})
        alternate=c.call('analysis.transient',dict(request,stop='10us',probes=['pin:A.R.2','pin:B.R.2']))
        check(alternate['ok'] and c.call('project.get',{})==derived,'derived assembly capture changes retained origin')
        close(alternate['samples'][-1]['measurements'][0]['voltage'],0)
        close(alternate['samples'][-1]['measurements'][1]['voltage'],25/3)
        functional=json.loads((ROOT/'examples/button-led/project.json').read_text(encoding='utf-8'));c.call('project.load',{'json':functional});reject(c,{},'electrical')
        board=copy.deepcopy(source);board['boards']=[{'id':'B','board':'emblink/arduino_uno'}];c.call('project.load',{'json':board});reject(c,{},'board-free')
        source_path=root/'project.json';source_path.write_text(json.dumps(source),encoding='utf-8')
        cli=subprocess.run([binary,'transient',str(source_path),'--request',str(ROOT/'examples/rc-filter/response.transient.json')],cwd=ROOT,capture_output=True,text=True,encoding='utf-8')
        check(cli.returncode==0,cli.stderr);check(json.loads(cli.stdout)['samples']==result['samples'],'CLI/RPC waveform mismatch')
        # A test-only model fails between two sample instants: preserve the real
        # prefix and diagnostics, never emit the requested future samples.
        lib=root/'faultlib';part=lib/'components'/'fault';part.mkdir(parents=True)
        (lib/'library.json').write_text(json.dumps({'schema_version':1,'kind':'library','name':'capturetest','version':'1.0.0','license':'Apache-2.0'}),encoding='utf-8')
        desc=json.loads((ROOT/'catalog/components/test_point/component.json').read_text(encoding='utf-8'))
        desc['id']='capturetest/fault';desc['model']={'script':'model.lua','api':1};desc.pop('functional',None)
        (part/'component.json').write_text(json.dumps(desc),encoding='utf-8')
        (part/'model.lua').write_text("return {reset=function(ctx) ctx.wake_at(12000000) end, on_time=function(ctx) error('capture test fault') end}",encoding='utf-8')
        broken=copy.deepcopy(source);broken['libraries']=[str(lib)];broken['components'].append({'id':'FAULT','component':'capturetest/fault'});broken['nets']['GND'].append('FAULT.1')
        c.call('project.load',{'json':broken});partial=c.call('analysis.transient',dict(request,stop='50us'))
        check(not partial['ok'] and partial['failure'] and len(partial['samples'])==3, f"runtime failure: ok={partial['ok']}, samples={len(partial['samples'])}, diagnostics={partial['diagnostics']}")
        check([r['at_ps'] for r in partial['samples']]==['0','5000000','10000000'] and 12000000<=int(partial['ended_at_ps'])<=15000000, 'failed capture misrepresents elapsed time/prefix')
        check(any('capture test fault' in d['message'] for d in partial['diagnostics']), 'native failure diagnostic missing')
        check(c.call('session.info',{})['now_ps']=='0', 'failed isolated run changed source clock')
        bad_path=root/'fault-project.json';bad_path.write_text(json.dumps(broken),encoding='utf-8')
        req_path=root/'fault-request.json';req_path.write_text(json.dumps(dict(request,stop='50us')),encoding='utf-8')
        cli=subprocess.run([binary,'transient',str(bad_path),'--request',str(req_path)],cwd=ROOT,capture_output=True,text=True,encoding='utf-8')
        check(cli.returncode==1 and json.loads(cli.stdout)['samples']==partial['samples'], 'CLI loses partial waveform or failure exit status')
        partial_path=root/'partial.json';partial_path.write_text(json.dumps(partial),encoding='utf-8')
        checked_partial=subprocess.run([sys.executable,str(ROOT/'tools/validate.py'),str(partial_path),'--strict'],cwd=ROOT,capture_output=True,text=True,encoding='utf-8')
        check(checked_partial.returncode==0,checked_partial.stdout+checked_partial.stderr)
        out=root/'capture.json';out.write_text(json.dumps(result),encoding='utf-8')
        valid=subprocess.run([sys.executable,str(ROOT/'tools/validate.py'),str(out),'--strict'],cwd=ROOT,capture_output=True,text=True,encoding='utf-8');check(valid.returncode==0,valid.stdout+valid.stderr)
    print(f'{count} native transient acceptance checks, 0 failed')


if __name__=='__main__':main()
