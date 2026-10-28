#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Shared editable hierarchy: native loading, scopes, ports and atomic edits."""
import argparse
import copy
import json
from pathlib import Path
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from embsim_client import StdioClient  # noqa: E402
from embsim.transport import EmbsimError  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--embsim', required=True)
    args = parser.parse_args()
    checked = 0
    def check(ok, message):
        nonlocal checked
        if not ok: raise AssertionError(message)
        checked += 1
    with tempfile.TemporaryDirectory(prefix='embsim-hierarchy-') as tmp:
        with StdioClient([str(Path(args.embsim).resolve()), 'sidecar', str(ROOT / 'examples/hierarchical-divider/project.json')]) as c:
            c.call('session.open', {'client':'hierarchy-acceptance','protocol':'1.0'})
            def project(): return c.call('project.get', {})['project']
            def replace(p):
                tx = c.call('project.transaction', {'op':'begin'})['tx']
                fields = {k:p[k] for k in ['components','nets','blocks','layout','probes'] if k in p}
                try:
                    c.call('project.transaction', {'op':'edit','tx':tx,'edits':[{'set_circuit':fields}]})
                    return c.call('project.transaction', {'op':'commit','tx':tx})
                except EmbsimError:
                    c.call('project.transaction', {'op':'abort','tx':tx})
                    raise
            tree=c.call('project.hierarchy', {})
            check([n['path'] for n in tree]==['','A','A.D','B'], 'nested instances missing')
            aliases={n['path']:n['nets'] for n in tree}
            check(aliases['A']['OUT']==aliases['A.D']['OUT']==aliases['']['AOUT'], 'port chain does not reach the native root net')
            check(aliases['B']['OUT']!=aliases['A']['OUT'], 'same local net names short separate instances')
            trace=c.call('project.hierarchy', {'trace':'A.D.OUT'})
            check([n['path'] for n in trace]==['','A','A.D'], 'cross-sheet trace includes an unrelated branch')
            desc=c.call('catalog.get', {'id':'block/divider'})['description']
            check({p['name'] for p in desc['pins']}=={'IN','OUT','GND'}, 'project block pin metadata missing')
            probe=c.call('probe.add', {'target':'pin:A.OUT','kind':'analog'})
            c.call('control.advance', {'by':'1ms'})
            def voltage(net):
                return c.call('observe.snapshot', {'include':['nets']})['nets'][net]['voltage']
            check(abs(voltage('AOUT')-2)<1e-5 and abs(voltage('BOUT')-1.65)<1e-5, 'loaded and isolated divider voltages are wrong')
            check(abs(c.call('probe.read', {'probe':probe.get('id',probe['probe'])})['samples'][-1][1]-2)<1e-5, 'block port probe did not resolve its internal pin')
            before=project(); drawing=copy.deepcopy(before)
            drawing['blocks']['divider']['components'][0]['layout']['x']+=20
            result=replace(drawing)
            check(result['applied'] and not result['restarted'], 'child drawing edit restarted the circuit')
            check(c.call('session.info', {})['now_ps']=='1000000000', 'child drawing edit lost virtual time')
            check(c.call('probe.list', {})['probes'][-1]['probe']==probe['probe'], 'drawing edit changed probe identity')
            reordered=copy.deepcopy(drawing)
            reordered['blocks']['divider']['ports']=dict(reversed(list(reordered['blocks']['divider']['ports'].items())))
            result=replace(reordered)
            check(not result['restarted'] and c.call('session.info',{})['now_ps']=='1000000000','JSON port key order restarted a drawing-only edit')
            changed=copy.deepcopy(drawing); changed['blocks']['divider']['components'][0]['params']['resistance']=2000
            result=replace(changed)
            check(result['applied'] and result['restarted'], 'shared electrical edit must restart and report it')
            c.call('control.advance', {'by':'1ms'})
            check(abs(voltage('AOUT')-1.25)<1e-5 and abs(voltage('BOUT')-1.1)<1e-5, 'shared definition did not affect both native instances')
            # Reverse edits are the same transaction protocol used by GUI undo.
            replace(drawing); c.call('control.advance', {'by':'1ms'})
            check(abs(voltage('AOUT')-2)<1e-5 and abs(voltage('BOUT')-1.65)<1e-5, 'reverse edit failed to restore the circuit')
            retained=project(); now=c.call('session.info', {})['now_ps']
            template=copy.deepcopy(retained)
            template['blocks']['divider']['params']={'layout':{'type':'number','unit':'ohm','default':1000}}
            template['blocks']['divider']['components'][0]['params']['resistance']='$layout'
            replace(template);c.call('control.advance',{'by':'1ms'})
            check(abs(voltage('AOUT')-2)<1e-5,'block parameter substitution failed')
            override=copy.deepcopy(template)
            next(c for c in override['components'] if c['id']=='B')['params']={'layout':2000}
            replace(override);c.call('control.advance',{'by':'1ms'})
            check(abs(voltage('AOUT')-2)<1e-5 and abs(voltage('BOUT')-1.1)<1e-5,'per-instance parameter override leaked into a sibling')
            check(next(n for n in c.call('project.hierarchy',{}) if n['path']=='B')['params']['layout']==2000,'resolved instance parameter metadata is wrong')
            template['blocks']['divider']['params']['layout']['default']=2000
            result=replace(template);c.call('control.advance',{'by':'1ms'})
            check(result['restarted'] and abs(voltage('AOUT')-1.25)<1e-5,'a physical parameter named layout was treated as drawing geometry')
            replace(retained);c.call('control.advance',{'by':'1ms'})
            bad=[]
            recursive=copy.deepcopy(retained);recursive['blocks']['divider']['components'].append({'id':'LOOP','component':'block/stage'});bad.append(recursive)
            unknown=copy.deepcopy(retained);unknown['blocks']['divider']['ports']['OUT']='RT.MISSING';bad.append(unknown)
            duplicate=copy.deepcopy(retained);duplicate['blocks']['divider']['nets']['OTHER']=['RT.1'];bad.append(duplicate)
            unused=copy.deepcopy(retained);unused['blocks']['unused']={'name':'Unused','components':[{'id':'X','component':'block/unused'}],'nets':{},'ports':{}};bad.append(unused)
            overlap=copy.deepcopy(retained);overlap['nets']['A.OUT']=overlap['nets'].pop('AOUT');bad.append(overlap)
            for field,value in [('ports',[]),('components',{}),('params',[])]:
                malformed=copy.deepcopy(retained);malformed['blocks']['divider'][field]=value;bad.append(malformed)
            malformed=copy.deepcopy(retained);malformed['blocks']['divider']=[];bad.append(malformed)
            for broken in bad:
                try:
                    result=replace(broken)
                    check(not result['applied'], 'invalid hierarchy applied')
                except EmbsimError:
                    checked+=1
                check(project()==retained and c.call('session.info', {})['now_ps']==now, 'failed hierarchy edit changed the active circuit or clock')
            tx=c.call('project.transaction',{'op':'begin'})['tx']
            try:
                c.call('project.transaction',{'op':'edit','tx':tx,'edits':[{'set_circuit':{'components':[],'nets':{},'blocks':[]}}]})
                raise AssertionError('malformed circuit patch accepted')
            except EmbsimError:pass
            c.call('project.transaction',{'op':'commit','tx':tx})
            check(project()==retained and c.call('session.info',{})['now_ps']==now,'failed patch partially modified its staged transaction')
            saved=Path(tmp)/'saved.json';c.call('project.save',{'path':str(saved)})
            check(json.loads(saved.read_text())['blocks']==retained['blocks'], 'save flattened or lost reusable definitions')
            check(len(c.call('project.hierarchy',{}))==4, 'failed staged sessions polluted the live hierarchy')
    print(f'{checked} native hierarchy checks, 0 failures')


if __name__=='__main__':main()
