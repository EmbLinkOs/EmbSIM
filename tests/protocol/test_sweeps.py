#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Native isolated parameter sweep: actual loading, independent sheets and truthful failures."""
import argparse
import copy
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from embsim_client import StdioClient  # noqa: E402
from embsim.transport import EmbsimError  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__); ap.add_argument('--embsim', required=True, type=Path)
    binary = str(ap.parse_args().embsim.resolve()); count = 0
    source = json.loads((ROOT / 'examples/assembly-variants/project.json').read_text(encoding='utf-8'))
    request = json.loads((ROOT / 'examples/assembly-variants/loading.sweep.json').read_text(encoding='utf-8'))
    def check(ok, reason):
        nonlocal count
        if not ok: raise AssertionError(reason)
        count += 1
    def close(actual, expected, reason): check(actual is not None and abs(actual - expected) < 2e-5, f'{reason}: {actual} != {expected}')
    def unchanged(c): return [c.call(m, p) for m, p in [('project.get', {}), ('session.info', {}), ('observe.snapshot', {'include': ['outputs']}), ('probe.list', {})]]
    def reject(c, changes, text=''):
        q = dict(request, **changes)
        try: c.call('analysis.sweep', q)
        except EmbsimError as e: check(text in str(e), str(e))
        else: check(False, f'invalid request accepted: {changes}')
    with tempfile.TemporaryDirectory(prefix='embsim-sweep-') as temp, StdioClient([binary, 'sidecar']) as c:
        root = Path(temp)
        check('analysis.sweep' in c.call('session.open', {'client': 'sweep', 'protocol': '1.0'})['capabilities'], 'capability missing')
        c.call('project.load', {'json': source}); c.call('control.advance', {'by': '1us'})
        c.call('probe.add', {'id': 'retained', 'target': 'pin:A.R.2', 'kind': 'analog'})
        before = unchanged(c); data = c.call('analysis.sweep', request)
        after = unchanged(c)
        # RPC statistics may change; configuration, time, outputs and probes must not.
        check(before[0] == after[0] and before[2:] == after[2:] and before[1]['now_ps'] == after[1]['now_ps'], 'analysis mutates source/time/output/probes')
        check(data['ok'] and len(data['points']) == 4 and data['method'] == 'independent_reset_sample', str(data))
        check(json.loads(data['source']['json'])['project']['json'] == json.loads(c.call('project.manifest', {})['json'])['project']['json'], 'wrong native source identity')
        for p, r in zip(data['points'], [1e5, 1e6, 2e6, 1e7]):
            check(p['resolved'] == r and p['at_ps'] == '1000000', 'resolved value/time mismatch')
            close(p['measurements'][0]['voltage'], 10e6 / (r + 1e6), 'loaded divider A')
            close(p['measurements'][1]['voltage'], 100/11, 'sibling changed')
        check(c.call('analysis.sweep', request) == data, 'independent repeat not deterministic')
        loaded = c.call('analysis.sweep', dict(request, target='A.METER', parameter='input_resistance', values=['100k', '1meg', '10meg']))
        for p, r in zip(loaded['points'], [1e5, 1e6, 1e7]): close(p['measurements'][0]['voltage'], 10*r/(1e6+r), 'meter loading not compiled')
        failed = c.call('analysis.sweep', dict(request, values=['1meg', -1, '2meg']))
        check(not failed['ok'] and [p['ok'] for p in failed['points']] == [True, False, True], 'one invalid point hides usable neighbors')
        bad = failed['points'][1]
        check(bad['resolved'] is None and bad['diagnostics'] and all(m['voltage'] is None for m in bad['measurements']), 'failed point fabricated a voltage')
        close(failed['points'][2]['measurements'][0]['voltage'], 10/3, 'failure leaks state into next point')
        for changes, text in [({'target': 'missing'}, 'fitted'), ({'parameter': 'made_up'}, 'numeric'), ({'values': []}, '1–64'), ({'values': [1]*65}, '1–64'), ({'values': [True]}, 'finite'), ({'values': ['not a number']}, 'finite'), ({'sample_at': 1000}, 'Sample time'), ({'sample_at': '-1us'}, 'Sample time'), ({'sample_at': '2ms'}, 'Sample time'), ({'sample_at': '1ms', 'values': [1]*11}, 'total'), ({'probes': []}, '1–8'), ({'probes': ['device:A.R.current']}, 'existing'), ({'probes': ['net:missing']}, 'existing'), ({'probes': ['pin:A.R.2']*2}, 'Duplicate'), ({'extra': True}, 'Unknown'), ({'kind': 'ac_analysis'}, 'format'), ({'schema_version': 1.5}, 'format')]: reject(c, changes, text)
        tx = c.call('project.transaction', {'op': 'begin'})['tx']; reject(c, {}, 'transaction'); c.call('project.transaction', {'op': 'abort', 'tx': tx})
        c.call('project.variant', {'op': 'activate', 'name': 'production'})
        reject(c, {}, 'fitted')
        derived = c.call('project.get', {})
        changed = c.call('analysis.sweep', dict(request, target='B.R'))
        check(changed['ok'] and c.call('project.get', {}) == derived, 'derived sweep lost origin or mutated source')
        close(changed['points'][0]['measurements'][0]['voltage'], 0, 'derived DNP leaks')
        c.call('project.variant', {'op': 'activate'})
        functional = json.loads((ROOT/'examples/button-led/project.json').read_text(encoding='utf-8'))
        c.call('project.load', {'json': functional}); reject(c, {}, 'electrical')
        board = copy.deepcopy(source)
        # Board-free functional circuits and actual board projects are distinct refusals.
        board['boards'] = [{'id': 'B', 'board': 'emblink/arduino_uno'}]
        board['policy'] = 'electrical'; c.call('project.load', {'json': board}); reject(c, {}, 'board-free')
        floating = copy.deepcopy(source)
        floating['components'].append({'id': 'TP', 'component': 'emblink/test_point'})
        floating['nets']['FLOAT'] = ['TP.1']
        c.call('project.load', {'json': floating})
        unknown = c.call('analysis.sweep', dict(request, probes=['net:FLOAT'], values=['1meg']))
        check(unknown['ok'] and unknown['points'][0]['measurements'][0]['voltage'] is None and unknown['points'][0]['measurements'][0]['logic'] == 'Z', 'floating digital net fabricated a voltage')
        rc = json.loads((ROOT/'examples/rc-filter/project.json').read_text(encoding='utf-8'))
        c.call('project.load', {'json': rc})
        transient = c.call('analysis.sweep', {'target': 'R1', 'parameter': 'resistance', 'values': ['1k', '10k', '100k', '1k'], 'sample_at': '100us', 'probes': ['net:OUT']})
        for p in transient['points']:
            expected = 3.3*(1-math.exp(-1e-4/(p['resolved']*1e-8)))
            check(abs(p['measurements'][0]['voltage']-expected) < 1e-4, 'RC charge differs from analytic exp response')
        check(transient['points'][0] == transient['points'][-1], 'capacitor memory leaks between independent runs')
        result_path = root / 'result.json'; result_path.write_text(json.dumps(data), encoding='utf-8')
        valid = subprocess.run([sys.executable, str(ROOT/'tools/validate.py'), str(result_path), '--strict'], cwd=ROOT, capture_output=True, text=True, encoding='utf-8')
        check(valid.returncode == 0, valid.stdout + valid.stderr)
        project_path = root/'project.json'; project_path.write_text(json.dumps(source), encoding='utf-8')
        cli = subprocess.run([binary, 'sweep', str(project_path), '--request', str(ROOT/'examples/assembly-variants/loading.sweep.json')], cwd=ROOT, capture_output=True, text=True, encoding='utf-8')
        check(cli.returncode == 0, cli.stderr)
        check(json.loads(cli.stdout)['points'] == data['points'], 'CLI/native graphical API disagree')
        mixed = root/'mixed.json'; mixed.write_text(json.dumps(dict(request, values=[-1, '1meg'])), encoding='utf-8')
        cli = subprocess.run([binary, 'sweep', str(project_path), '--request', str(mixed)], cwd=ROOT, capture_output=True, text=True, encoding='utf-8')
        check(cli.returncode == 1 and len(json.loads(cli.stdout)['points']) == 2, 'CLI failed-point status/artifact lost')
    print(f'{count} native parameter sweep checks, 0 failed')


if __name__ == '__main__': main()
