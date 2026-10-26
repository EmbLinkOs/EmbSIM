#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Native waveform packets preserve large tails, gap evidence and constant frontiers."""
import argparse
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
    project = dict(schema_version=1, kind='project', name='stream-contract', policy='functional',
                   components=[dict(id='CLK', component='emblink/clock_source', params={'freq': 1000, 'duty': .25}),
                               dict(id='H', component='emblink/logic_level', params={'level': 1}),
                               dict(id='P', component='emblink/logic_probe'), dict(id='Q', component='emblink/logic_probe')],
                   nets={'PWM': ['CLK.OUT', 'P.IN'], 'HIGH': ['H.OUT', 'Q.IN']},
                   probes=[dict(id='signal', target='net:PWM', kind='analog', rate_hz=200000),
                           dict(id='constant', target='net:HIGH', kind='digital')])
    checked = 0

    def check(ok, message):
        nonlocal checked
        if not ok: raise AssertionError(message)
        checked += 1

    with tempfile.TemporaryDirectory(prefix='embsim-waveform-stream-') as tmp:
        path = Path(tmp) / 'project.json'
        path.write_text(json.dumps(project))
        with StdioClient([str(Path(args.embsim).resolve()), 'sidecar', str(path)]) as client:
            client.call('session.open', {'client': 'waveform-contract', 'protocol': '1.0'})
            client.call('waveform.subscribe', {'probes': ['signal', 'constant']})

            def frames():
                return [n.params for n in client.notifications() if n.method == 'waveform.samples']

            initial = frames()
            check(all(f.get('restart') for f in initial), 'new subscriptions must replace old captures')
            client.call('control.advance', {'by': '150ms'})
            first = frames()
            signal = [f for f in first if f['probe'] == 'signal']
            streamed = [sample for f in initial + first if f['probe'] == 'signal' for sample in f['samples']]
            retained = client.call('probe.read', {'probe': 'signal', 'max': 100000})['samples']
            check(streamed == retained and len(streamed) == 30001, 'a >20,000-sample advance lost or duplicated its tail')
            check(len(signal) == 2 and all(len(f['samples']) <= 20000 for f in signal), 'packets must be bounded without truncation')
            check(signal[-1]['through_time_ps'] == '150000000000', 'final packet must prove completed capture time')
            check(not any(f.get('gap') for f in first), 'normal packet boundaries are not data gaps')
            constants = [f for f in first if f['probe'] == 'constant']
            check(constants[-1]['through_time_ps'] == '150000000000', 'a constant signal needs a capture frontier too')
            client.call('control.advance', {'by': '400ms'})
            second = frames()
            signal = [f for f in second if f['probe'] == 'signal']
            retained = client.call('probe.read', {'probe': 'signal', 'max': 100000})['samples']
            check([p for f in signal for p in f['samples']] == retained and len(retained) == 65536, 'eviction must deliver every remaining sample')
            check(signal[0].get('gap') is True and not any(f.get('gap') for f in signal[1:]), 'only genuinely evicted unsent history starts a gap')
            check(signal[-1]['through_time_ps'] == '550000000000', 'retained tail has the wrong frontier')
            constant = [f for f in second if f['probe'] == 'constant'][-1]
            check(constant['samples'] == [] and constant['through_time_ps'] == '550000000000', 'empty watermark must extend a constant capture')
            client.call('control.advance', {'by': '100ms'})
            third = [f for f in frames() if f['probe'] == 'signal']
            check(not any(f.get('gap') for f in third) and sum(len(f['samples']) for f in third) == 20000,
                  'evicting previously delivered samples must not invent a capture gap')
            client.call('waveform.subscribe', {'probes': ['signal']})
            restarted = [f for f in frames() if f['probe'] == 'signal']
            check(restarted[0].get('restart') is True, 'resubscription must explicitly replace the previous record')
            client.call('control.reset', {})
            reset = frames()
            check(any(f.get('restart') and f.get('through_time_ps') == '0' for f in reset), 'reset must replace both data and frontier')
    print(f'{checked} native streaming checks, 0 failures')


if __name__ == '__main__':
    main()
