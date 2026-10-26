#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Observe current digital contention and mixed receiver states over native stdio."""
import argparse
import copy
import json
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from embsim_client import StdioClient  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--embsim', required=True)
    args = parser.parse_args()
    checked = 0

    def check(ok, message):
        nonlocal checked
        if not ok:
            raise AssertionError(message)
        checked += 1

    base = dict(schema_version=1, kind='project', name='electrical-feedback', policy='functional',
                components=[dict(id='H', component='emblink/logic_level', params={'level': 1}),
                            dict(id='L', component='emblink/logic_level', params={'level': 0}),
                            dict(id='P', component='emblink/logic_probe'),
                            dict(id='FLOAT', component='emblink/logic_probe')],
                nets={'CONFLICT': ['H.OUT', 'L.OUT', 'P.IN'], 'FLOATING': ['FLOAT.IN']})
    with tempfile.TemporaryDirectory(prefix='embsim-electrical-feedback-') as tmp:
        path = Path(tmp) / 'project.json'
        for policy in ('functional', 'electrical'):
            project = copy.deepcopy(base)
            project['policy'] = policy
            if policy == 'electrical':
                project['components'] += [dict(id='S', component='emblink/scope_input'), dict(id='G', component='emblink/gnd')]
                project['nets']['CONFLICT'].append('S.+')
                project['nets']['GND'] = ['S.-', 'G.GND']
            path.write_text(json.dumps(project))
            with StdioClient([str(Path(args.embsim).resolve()), 'sidecar', str(path)]) as client:
                client.call('session.open', {'client': 'electrical-feedback', 'protocol': '1.0'})
                client.call('control.advance', {'by': '1us'})
                states = client.call('observe.snapshot', {'include': ['nets']})['nets']
                conflict, floating = states['CONFLICT'], states['FLOATING']
                check(conflict['contention'] is True, f'{policy}: opposing outputs were not reported')
                check(conflict['receivers'] == ['P.IN'], 'output terminals must not be listed as active receivers')
                check(floating['logic'] == 'Z' and floating['contention'] is False, 'high impedance is not contention')
                check(floating['receivers'] == ['FLOAT.IN'], 'a floating receiver lost its identity')
                if policy == 'functional':
                    check(conflict['logic'] == 'X' and conflict['kind'] == 'digital', 'digital contention must resolve to X')
                else:
                    check(conflict['kind'] == 'mixed' and 1.64 < conflict['voltage'] < 1.66, 'finite opposing drivers must retain the solved voltage')
                    check(conflict['logic'] in ['0', '1', 'X', 'Z'], 'mixed receiver level must be present')
        # Same-valued outputs are not a conflict, including when loaded.
        project['components'][1]['params']['level'] = 1
        path.write_text(json.dumps(project))
        with StdioClient([str(Path(args.embsim).resolve()), 'sidecar', str(path)]) as client:
            client.call('session.open', {'client': 'electrical-feedback', 'protocol': '1.0'})
            client.call('control.advance', {'by': '1us'})
            state = client.call('observe.snapshot', {'include': ['nets']})['nets']['CONFLICT']
            check(state['contention'] is False and state['logic'] == '1', 'agreeing outputs must clear contention and report the loaded receiver level')
    print(f'{checked} native electrical feedback checks, 0 failures')


if __name__ == '__main__':
    main()
