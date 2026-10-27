#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Exact library input locks, drift review, relocation and atomic metadata edits."""
import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from embsim_client import StdioClient  # noqa: E402
from embsim.transport import EmbsimError  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--embsim', required=True, type=Path)
    binary = str(parser.parse_args().embsim.resolve())
    count = 0
    def check(ok, why):
        nonlocal count
        if not ok: raise AssertionError(why)
        count += 1
    def cli(*args):
        return subprocess.run([binary, *map(str, args)], cwd=ROOT, capture_output=True, text=True, encoding="utf-8")
    def write(path, value): path.write_text(json.dumps(value), encoding='utf-8')
    with tempfile.TemporaryDirectory(prefix='embsim-lock-') as temp:
        root = Path(temp); lib = root / 'parts'; part = lib / 'components' / 'test_resistor'
        part.mkdir(parents=True)
        write(lib / 'library.json', {'schema_version': 1, 'kind': 'library', 'name': 'locktest', 'version': '1.2.3', 'license': 'Apache-2.0'})
        description = json.loads((ROOT / 'catalog/components/resistor/component.json').read_text(encoding="utf-8"))
        description['id'] = 'locktest/test_resistor'
        write(part / 'component.json', description)
        asset = part / 'model.lua'; asset.write_bytes(b'-- binary asset identity\r\n\x00')
        shutil.copy(ROOT / 'catalog/components/resistor/symbol.svg', part)
        project = {'schema_version': 1, 'kind': 'project', 'name': 'Locked inputs', 'policy': 'electrical', 'libraries': ['parts'], 'components': [
            {'id': 'R', 'component': 'locktest/test_resistor', 'params': {'resistance': 1000}}, {'id': 'V', 'component': 'emblink/vsource', 'params': {'dc': 5}}, {'id': 'G', 'component': 'emblink/gnd'}],
            'nets': {'power': ['V.+', 'R.1'], 'ground': ['V.-', 'R.2', 'G.GND']}}
        path = root / 'project.json'; write(path, project)
        exported = cli('lock', 'capture', path)
        check(exported.returncode == 0, exported.stderr)
        lock = json.loads(exported.stdout)
        check(lock['libraries']['locktest']['version'] == '1.2.3', 'library version absent')
        check(lock['libraries']['locktest']['files']['components/test_resistor/model.lua'] == 'sha256:' + hashlib.sha256(asset.read_bytes()).hexdigest(), 'binary resource bytes not hashed exactly')
        check('component:locktest/test_resistor' in lock['catalog'], 'effective catalog selection absent')
        check(str(root) not in exported.stdout, 'machine paths leaked into portable lock')
        absent = cli('lock', 'check', path)
        check(absent.returncode == 1 and not json.loads(absent.stdout)['ok'], 'missing lock silently passes')
        project['library_lock'] = lock; write(path, project)
        checked = cli('lock', 'check', path)
        check(checked.returncode == 0 and json.loads(checked.stdout)['ok'], checked.stderr)
        run = cli('run', path, '--until', '1us', '--json')
        check(run.returncode == 0, run.stderr)
        relocated = root / 'moved'; relocated.mkdir(); shutil.copytree(lib, relocated / 'parts'); write(relocated / 'project.json', project)
        check(cli('lock', 'check', relocated / 'project.json').returncode == 0, 'absolute directory affects identity')
        with StdioClient([binary, 'sidecar', str(path)]) as c:
            c.call('session.open', {'client': 'lock-test', 'protocol': '1.0'})
            c.call('control.advance', {'by': '1us'})
            now = c.call('session.info', {})['now_ps']; before = c.call('observe.snapshot', {})
            captured = c.call('project.library_lock', {'op': 'capture'})
            check(captured == lock, 'CLI and RPC lock disagree')
            check(c.call('project.library_lock', {'op': 'check'}) == {'ok': True, 'changes': []}, 'RPC baseline mismatch')
            check(c.call('session.info', {})['now_ps'] == now and c.call('observe.snapshot', {}) == before, 'review mutates native circuit or clock')
            tx = c.call('project.transaction', {'op': 'begin'})['tx']
            c.call('project.transaction', {'op': 'edit', 'tx': tx, 'edits': [{'set_library_lock': None}]})
            result = c.call('project.transaction', {'op': 'commit', 'tx': tx})
            check(result['applied'] and not result['restarted'], 'metadata removal rebuilds circuit')
            check(c.call('session.info', {})['now_ps'] == now, 'metadata removal resets clock')
            tx = c.call('project.transaction', {'op': 'begin'})['tx']
            c.call('project.transaction', {'op': 'edit', 'tx': tx, 'edits': [{'set_library_lock': lock}]})
            result = c.call('project.transaction', {'op': 'commit', 'tx': tx})
            check(result['applied'] and not result['restarted'], 'pin operation rebuilds circuit')
            bad = copy.deepcopy(lock); bad['catalog']['component:locktest/test_resistor']['version'] = '99.0.0'
            tx = c.call('project.transaction', {'op': 'begin'})['tx']
            c.call('project.transaction', {'op': 'edit', 'tx': tx, 'edits': [{'set_library_lock': bad}]})
            try: c.call('project.transaction', {'op': 'commit', 'tx': tx})
            except EmbsimError: check(True, '')
            else: check(False, 'invalid baseline committed')
            check(c.call('project.get', {})['project']['library_lock'] == lock and c.call('session.info', {})['now_ps'] == now, 'failed pin did not preserve project/time')
            asset.write_bytes(b'changed Lua bytes\n')
            report = c.call('project.library_lock', {'op': 'check'})
            check(not report['ok'] and any(x['path'].endswith('model.lua') and x['change'] == 'changed' for x in report['changes']), 'asset drift not reported by filename')
            desc_path = part / 'component.json'; changed = copy.deepcopy(description); changed['version'] = '2.0.0'; write(desc_path, changed)
            try: c.call('project.library_lock', {'op': 'capture'})
            except EmbsimError: check(True, '')
            else: check(False, 'stale loaded description used to bless new disk inputs')
            write(desc_path, description)
        drift = cli('lock', 'check', path)
        check(drift.returncode == 1 and any(x['path'].endswith('model.lua') for x in json.loads(drift.stdout)['changes']), 'CLI drift cannot be reviewed')
        failed = cli('run', path, '--until', '1us', '--json')
        check(failed.returncode == 2, 'locked run accepted changed model input')
        fresh = cli('lock', 'capture', path)
        check(fresh.returncode == 0, 'explicit review cannot capture a replacement baseline')
        asset.unlink()
        report = json.loads(cli('lock', 'check', path).stdout)
        check(any(x['path'].endswith('model.lua') and x['change'] == 'removed' for x in report['changes']), 'removed asset not reported')
        new = part / 'added.bin'; new.write_bytes(b'new')
        report = json.loads(cli('lock', 'check', path).stdout)
        check(any(x['path'].endswith('added.bin') and x['change'] == 'added' for x in report['changes']), 'new asset not reported')
        (part / '.private').write_text('excluded distribution metadata', encoding='utf-8')
        first = cli('lock', 'capture', path); (part / '.private').write_text('changed', encoding='utf-8')
        check(json.loads(first.stdout) == json.loads(cli('lock', 'capture', path).stdout), 'hidden metadata changes distribution identity')
        if os.name != 'nt':
            link = part / 'unsafe-link'; link.symlink_to(root)
            check(cli('lock', 'capture', path).returncode == 2, 'link traversal accepted')
            link.unlink()
            fifo = part / 'pipe'; os.mkfifo(fifo)
            check(cli('lock', 'capture', path).returncode == 2, 'special file traversal accepted')
            fifo.unlink()
        check(cli('lock', 'invalid', path).returncode == 2, 'unknown command accepted')
        print(f'{count} library lock acceptance checks, 0 failed')


if __name__ == '__main__': main()
