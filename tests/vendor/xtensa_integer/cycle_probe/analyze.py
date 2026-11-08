#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Validate complete cycle-probe captures; summarize raw kernel costs only.

A complete log is not a calibrated instruction model or authenticated hardware
capture. No overhead subtraction, per-instruction fitting or outlier removal.
"""
import argparse
import hashlib
import json
from pathlib import Path
import statistics

PREFIX='EMBSIM_TIMING '
COUNTS=(1,4,16,64,256,1024)
CASES=('nop','addi_dependent','mull_dependent','quou_independent','quou_dependent','l32i_dram','s32i_dram','l32ai_dram','s32c1i_match_pair')
EXPECTED=2*2*len(COUNTS)*len(CASES)*17


def uint(value,minimum=0,maximum=0xffffffff):
    return type(value) is int and minimum<=value<=maximum


def analyze(text,origin='unverified'):
    contexts=[];completions=[];samples={}
    for number,line in enumerate(text.splitlines(),1):
        at=line.find(PREFIX)
        if at<0:continue
        try:row=json.loads(line[at+len(PREFIX):])
        except json.JSONDecodeError as exc:raise ValueError(f'line {number}: invalid capture JSON') from exc
        if not isinstance(row,dict) or type(row.get('schema')) is not int or row['schema']!=1:raise ValueError('unsupported capture schema')
        kind=row.get('kind')
        if kind=='context':contexts.append(row)
        elif kind=='complete':completions.append(row)
        elif kind=='sample':
            if not uint(row.get('core'),0,1) or not uint(row.get('loop_buffer'),0,1):raise ValueError('invalid core/buffer mode')
            if not uint(row.get('iterations'),1) or row['iterations'] not in COUNTS or row.get('case') not in CASES:raise ValueError('invalid kernel/count')
            if not uint(row.get('repeat'),0,16) or not uint(row.get('raw_cycles'),1):raise ValueError('invalid repeat/cycles; zero is not a physical calibration')
            key=(row['core'],row['loop_buffer'],row['case'],row['iterations'],row['repeat'])
            if key in samples:raise ValueError('duplicate sample')
            samples[key]=row['raw_cycles']
        else:raise ValueError('error or unknown record in capture')
    if len(contexts)!=1 or len(completions)!=1 or len(samples)!=EXPECTED:raise ValueError('incomplete or concatenated capture')
    context=contexts[0]
    if context.get('placement')!='IRAM' or context.get('timing_model_qualified') is not False or not uint(context.get('reported_cpu_hz'),1):raise ValueError('invalid context/scope')
    if not isinstance(context.get('idf'),str) or not context['idf'] or not uint(context.get('chip_model'),1,1) or not uint(context.get('revision_code'),0,65535):raise ValueError('missing original ESP32/SDK provenance')
    if not uint(completions[0].get('samples'),EXPECTED,EXPECTED) or completions[0].get('timing_model_qualified') is not False:raise ValueError('invalid completion/scope')
    groups=[]
    for core in range(2):
        for enabled in range(2):
            for case in CASES:
                for count in COUNTS:
                    values=[samples[(core,enabled,case,count,r)] for r in range(17)]
                    groups.append(dict(core=core,loop_buffer=enabled,case=case,iterations=count,samples=17,
                                       minimum=min(values),median=statistics.median(values),maximum=max(values)))
    return dict(schema=1,declared_origin=origin,origin_authenticated=False,context=context,samples=len(samples),
                timing_model_qualified=False,units='CCOUNT cycles for complete framed IRAM kernels',groups=groups)


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('capture',type=Path);ap.add_argument('--report',type=Path)
    ap.add_argument('--origin',choices=['unverified','physical-hardware','reference-emulator'],default='unverified')
    args=ap.parse_args()
    try:
        data=args.capture.read_bytes();result=analyze(data.decode('utf-8'),args.origin)
    except (ValueError,OSError) as exc:ap.error(str(exc))
    result['capture_sha256']=hashlib.sha256(data).hexdigest();output=json.dumps(result,indent=2)+'\n'
    if args.report:args.report.write_text(output,encoding='utf-8')
    else:print(output,end='')


if __name__=='__main__':main()
