#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Retarget native observation probes without changing their identity or loading."""
import argparse
import json
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from embsim_client import StdioClient
from embsim.transport import EmbsimError  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--embsim', required=True)
    args = parser.parse_args()
    project = dict(schema_version=1, kind='project', name='probe-move-contract', policy='functional',
                   components=[dict(id='L', component='emblink/logic_level', params={'level': 0}),
                               dict(id='H', component='emblink/logic_level', params={'level': 1}),
                               dict(id='P', component='emblink/logic_probe'), dict(id='Q', component='emblink/logic_probe')],
                   nets={'LOW': ['L.OUT', 'P.IN'], 'HIGH': ['H.OUT', 'Q.IN']},
                   probes=[dict(id='signal', target='net:LOW', kind='analog', rate_hz=100000),
                           dict(id='logic', target='net:LOW', kind='digital')])
    checked = 0

    def check(ok, message):
        nonlocal checked
        if not ok:
            raise AssertionError(message)
        checked += 1

    with tempfile.TemporaryDirectory(prefix='embsim-probe-move-') as tmp:
        path = Path(tmp) / 'project.json'
        path.write_text(json.dumps(project))
        with StdioClient([str(Path(args.embsim).resolve()), 'sidecar', str(path)]) as c:
            c.call('session.open', {'client': 'move-contract', 'protocol': '1.0'})
            c.call('waveform.subscribe', {'probes': ['signal', 'logic']})
            c.call('control.advance', {'by': '1ms'})
            before = c.call('probe.list', {})['probes']
            c.notifications()
            old = next(p for p in before if p['id'] == 'signal')
            result = c.call('probe.move', {'probe': 'signal', 'target': 'net:HIGH'})
            check(result['now_ps'] == '1000000000', 'moving a probe must not advance time')
            check(result['capture_epoch'] == old['capture_epoch'] + 1, 'source change needs a new epoch')
            moved = next(p for p in c.call('probe.list', {})['probes'] if p['id'] == 'signal')
            check(moved['probe'] == old['probe'] and moved['rate_hz'] == old['rate_hz'] and moved['kind'] == old['kind'], 'move must preserve identity, rate and kind')
            data = c.call('probe.read', {'probe': 'signal', 'max': 1000})['samples']
            check(data and all(int(t) >= 1000000000 and v > 0 for t, v in data), 'new capture contains old-source samples')
            c.call('control.advance', {'by': '1ms'})
            packets = [n.params for n in c.notifications() if n.method == 'waveform.samples' and n.params['probe'] == 'signal']
            check(any(p.get('restart') and p['capture_epoch'] == result['capture_epoch'] for p in packets), 'move lost existing subscription or restart evidence')
            check(packets[-1]['through_time_ps'] == '2000000000', 'moved capture has wrong frontier')
            same = c.call('probe.move', {'probe': 'signal', 'target': 'net:HIGH'})
            check(same['capture_epoch'] == result['capture_epoch'], 'same target should preserve capture')
            retained = c.call('probe.read', {'probe': 'signal', 'max': 1000})
            try:
                c.call('probe.move', {'probe': 'signal', 'target': 'net:MISSING'})
                raise AssertionError('invalid target accepted')
            except EmbsimError:
                pass
            check(c.call('probe.read', {'probe': 'signal', 'max': 1000}) == retained, 'failed move destroyed capture')
            c.call('record.start', {})
            try:
                c.call('probe.move', {'probe': 'signal', 'target': 'net:LOW'})
                raise AssertionError('recording allowed an unrecorded source change')
            except EmbsimError:
                checked += 1
            c.call('record.stop', {})
            c.call('probe.move', {'probe': 'logic', 'target': 'pin:H.OUT'})
            c.call('control.advance', {'by': '1ms'})
            check(all(v == 1 for _, v in c.call('probe.read', {'probe': 'logic'})['samples']), 'digital pin retarget failed')
            # Repeated moves exercise cancellation/freeing of scheduled callbacks.
            for i in range(30):
                c.call('probe.move', {'probe': 'signal', 'target': 'net:LOW' if i % 2 == 0 else 'net:HIGH'})
                c.call('control.advance', {'by': '10us'})
            check(len(c.call('probe.list', {})['probes']) == 2, 'temporary replacement probe leaked')
            epoch = c.call('probe.list', {})['probes'][0]['capture_epoch']
            c.call('control.reset', {})
            check(c.call('probe.list', {})['probes'][0]['capture_epoch'] > epoch, 'reset must change capture epoch')
    print(f'{checked} native probe-move checks, 0 failures')


if __name__ == '__main__':
    main()
