#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Exercise measured scenario assertions through the actual CLI, including failures."""
import argparse
import copy
import json
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--embsim', required=True)
    args = parser.parse_args()
    cli = str(Path(args.embsim).resolve())
    project = dict(schema_version=1, kind='project', name='waveform-contract', policy='functional',
                   components=[dict(id='CLK', component='emblink/clock_source', params={'freq': 1000, 'duty': .25}),
                               dict(id='P', component='emblink/logic_probe')],
                   nets={'PWM': ['CLK.OUT', 'P.IN']},
                   probes=[dict(id='pwm', target='net:PWM', kind='digital'),
                           dict(id='analog', target='net:PWM', kind='analog', rate_hz=100000)])
    base = dict(id='measured', between=['1ms', '5ms'], probe='pwm', measure='mean', expect={'equals': .25, 'tolerance': 1e-9})
    checked = 0
    with tempfile.TemporaryDirectory(prefix='embsim-waveform-') as tmp:
        root = Path(tmp)
        (root / 'project.json').write_text(json.dumps(project))

        def run(assertions, succeeds):
            nonlocal checked
            scenario = dict(schema_version=1, kind='scenario', name='contract', until='10ms', assertions=assertions)
            (root / 'scenario.json').write_text(json.dumps(scenario))
            p = subprocess.run([cli, 'run', str(root / 'project.json'), '--scenario', str(root / 'scenario.json'), '--json'], capture_output=True, text=True, timeout=30)
            report = json.loads(p.stdout)
            if p.returncode != (0 if succeeds else 1) or report['ok'] != succeeds:
                raise AssertionError(f'exit {p.returncode}, {report}: {p.stderr}')
            if succeeds and not all(a['ok'] for a in report['assertions']):
                raise AssertionError(report)
            if succeeds and not all(a.get('measure') and set(a.get('window_ps', {})) == {'from', 'to'} for a in report['assertions']):
                raise AssertionError('report lost the measurement/window identity')
            if not succeeds and not all(not a['ok'] and a.get('reason') for a in report['assertions']):
                raise AssertionError('an invalid measurement silently passed')
            checked += len(assertions)

        valid = []
        for measure, expected in [('mean', .25), ('rms', .5), ('duty_cycle', .25)]:
            for index, window in enumerate([['1ms', '5ms'], ['125us', '1125us']]):
                valid.append({**base, 'id': f'{measure}_{index}', 'measure': measure, 'between': window,
                              'expect': {'equals': expected, 'tolerance': 1e-9}})
        run(valid, True)
        invalid = []
        for mutation in [{'measure': 'median'}, {'between': ['1ms', '1ms']},
                         {'between': ['1ms', '12ms']}, {'expect': {'logic': '1'}},
                         {'expect': {'equals': True}}, {'expect': {'tolerance': .1}},
                         {'expect': {'equals': .25, 'tolerance': -1}},
                         {'probe': 'analog', 'measure': 'duty_cycle'}, {'probe': 'missing'}]:
            assertion = {**copy.deepcopy(base), **mutation, 'id': f'invalid_{len(invalid)}'}
            invalid.append(assertion)
        for timing in ('at', 'by', 'always'):
            assertion = {**copy.deepcopy(base), 'id': timing}
            del assertion['between']
            assertion[timing] = True if timing == 'always' else '5ms'
            if timing == 'always': assertion['diagnostics'] = {'forbid': ['net.contention']}
            invalid.append(assertion)
        run(invalid, False)
    print(f'{checked} CLI waveform assertions checked, 0 failures')


if __name__ == '__main__':
    main()
