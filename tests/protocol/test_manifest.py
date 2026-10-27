#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Atomic native engineering projection, package counts, resolved values and content identities."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from embsim_client import StdioClient  # noqa: E402
from embsim.transport import EmbsimError  # noqa: E402


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--embsim',required=True,type=Path)
    binary=str(parser.parse_args().embsim.resolve());count=0
    def check(ok,why):
        nonlocal count
        if not ok:raise AssertionError(why)
        count+=1
    def unpack(envelope):
        check(envelope['sha256']=='sha256:'+hashlib.sha256(envelope['json'].encode()).hexdigest(),'manifest bytes do not match identity')
        check(envelope.get('schema_version')==1 and envelope.get('kind')=='engineering_manifest','artifact envelope identity missing')
        data=json.loads(envelope['json'])
        for document in [data['project'],*data['catalog'].values()]:
            check(document['sha256']=='sha256:'+hashlib.sha256(document['json'].encode()).hexdigest(),'document bytes do not match identity')
        check(data['source']=='native' and data['kind']=='engineering_manifest','native evidence source missing')
        return data
    project=ROOT/'examples/hierarchical-instruments/project.json'
    cli=subprocess.run([binary,'manifest',str(project),'--json'],cwd=ROOT,capture_output=True,text=True,encoding="utf-8",check=True)
    exported=unpack(json.loads(cli.stdout))
    human=subprocess.run([binary,'manifest',str(project)],cwd=ROOT,capture_output=True,text=True,encoding="utf-8",check=True)
    check('Project inventory instances: 6' in human.stdout and 'Manifest identity: sha256:' in human.stdout,'human summary is missing native counts/identity')
    with StdioClient([binary,'sidecar',str(project)]) as c:
        c.call('session.open',{'client':'engineering-acceptance','protocol':'1.0'})
        c.call('control.advance',{'by':'1ms'})
        before=c.call('session.info',{});probes=c.call('probe.list',{});tree=c.call('project.hierarchy',{})
        data=unpack(c.call('project.manifest',{}))
        check(c.call('session.info',{})['now_ps']==before['now_ps'] and c.call('probe.list',{})==probes,'manifest mutates time or probes')
        check(c.call('project.hierarchy',{})==tree,'manifest changes native scope/aliases')
        check(data['project']['sha256']==exported['project']['sha256'],'CLI and RPC disagree on the canonical source')
        check(data['catalog']==exported['catalog'] and data['instances']==exported['instances'],'CLI and RPC disagree on loaded model facts')
        check(data['now_ps']=='1000000000','manifest time loses native picoseconds')
        rows={r['path']:r for r in data['instances']}
        check(set(rows)=={'V','G','A.R','A.METER','B.R','B.METER'},'repeated definitions not expanded exactly once per instance')
        check(rows['A.METER']['params']['input_resistance']==1e6 and rows['B.METER']['params']['input_resistance']==1e7,'resolved instance overrides lost')
        check(rows['A.R']['params']['resistance']==1e6 and rows['B.R']['params']['resistance']==1e6,'native defaults missing')
        check(all(r['resolution']=='native' for r in rows.values()),'available component parameters marked unresolved')
        check(len(data['catalog'])>=4 and not any(r['reference'].startswith('block/') for r in rows.values()),'project wrappers treated as packages or metadata duplicated')
        check(json.loads(data['project']['json'])==c.call('project.get',{})['project'],'manifest does not preserve canonical hierarchy')
        check(any(lib['name'] for lib in data['libraries']),'loaded library identities missing')
    with StdioClient([binary,'sidecar',str(ROOT/'examples/lm358p-dynamics/project.json')]) as c:
        c.call('session.open',{'client':'engineering-packages','protocol':'1.0'})
        data=unpack(c.call('project.manifest',{}));rows={r['path']:r for r in data['instances']}
        models={r['path']:r for r in data['models']}
        check('U1.A' in models and 'U1.B' in models and models['U1.A']['params']['gbw']==700000,'compiled dependencies and their actual resolved values missing')
        check(any(json.loads(doc['json']).get('id')=='emblink/opamp' for doc in data['catalog'].values()),'loaded child model description not captured')
        check('U1' in rows and 'U1.A' not in rows and 'U1.B' not in rows,'catalog package implementation counted as extra ICs')
        check(rows['U1']['params']['gbw']==700000 and rows['U1']['params']['slew']==.3,'named package native dynamic defaults missing')
        desc=json.loads(data['catalog'][rows['U1']['catalog']]['json'])
        check(desc['part']['mpn']=='LM358P' and desc['version']=='1.1.0','exact package/version metadata missing')
        check(desc['provenance']['sources'][0]['hash'].startswith('sha256:'),'source identity not retained')
    with StdioClient([binary,'sidecar']) as c:
        c.call('session.open',{'client':'engineering-state','protocol':'1.0'})
        try:c.call('project.manifest',{})
        except EmbsimError:check(True,'')
        else:check(False,'unloaded session returns an invented manifest')
        c.call('project.load',{'json':{'schema_version':1,'kind':'project','name':'Board assembly','policy':'functional','boards':[{'id':'B1','board':'emblink/arduino_uno'}],'components':[],'nets':{}}})
        data=unpack(c.call('project.manifest',{}))
        check(len(data['instances'])==1 and data['instances'][0]['kind']=='board' and data['instances'][0]['resolution']=='board-assembly','board interior duplicated into the assembly BOM')
    # Native output is UTF-8 on every host, independent of Python's locale.
    with tempfile.TemporaryDirectory(prefix='embsim-manifest-unicode-') as temp:
        path=Path(temp)/'project.json'
        source={'schema_version':1,'kind':'project','name':'Précision µΩ ±', 'policy':'electrical','components':[], 'nets':{}}
        path.write_text(json.dumps(source,ensure_ascii=False),encoding='utf-8')
        result=subprocess.run([binary,'manifest',str(path),'--json'],cwd=ROOT,capture_output=True,text=True,encoding='utf-8',check=True)
        data=unpack(json.loads(result.stdout))
        check(json.loads(data['project']['json'])['name']==source['name'],'UTF-8 project identity corrupted by host code page')
    invalid=subprocess.run([binary,'manifest','--invalid'],cwd=ROOT,capture_output=True,text=True,encoding="utf-8")
    check(invalid.returncode==2 and not invalid.stdout,'invalid CLI invocation emits a misleading manifest')
    missing=subprocess.run([binary,'manifest',str(ROOT/'missing-manifest-project.json'),'--json'],cwd=ROOT,capture_output=True,text=True,encoding="utf-8")
    check(missing.returncode==2 and not missing.stdout,'load failure emits a misleading artifact')
    print(f'{count} native engineering manifest checks, 0 failed')


if __name__=='__main__':main()
