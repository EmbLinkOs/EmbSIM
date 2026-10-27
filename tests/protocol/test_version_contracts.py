#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Requested versions at project, block, composite, board, MCU and library boundaries."""
import argparse
import copy
import json
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
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument('--embsim', required=True, type=Path)
    binary = str(parser.parse_args().embsim.resolve()); count = 0
    def check(ok, why):
        nonlocal count
        if not ok: raise AssertionError(why)
        count += 1
    source = {'schema_version': 1, 'kind': 'project', 'name': 'Version contract', 'policy': 'electrical', 'components': [{'id': 'R', 'component': 'emblink/resistor', 'version': '^1.0.0'}], 'nets': {}}
    def validate(c, project, code=None, base=None):
        report = c.call('project.validate', {'json': project, **({'base_dir': str(base)} if base else {})})
        check(report['ok'] if code is None else not report['ok'] and any(d['code'] == code for d in report['diagnostics']), str(report))
    with StdioClient([binary, 'sidecar']) as c:
        c.call('session.open', {'client': 'version-contract', 'protocol': '1.0'})
        c.call('project.load', {'json': source})
        validate(c, source)
        c.call('control.advance', {'by': '1us'})
        now = c.call('session.info', {})['now_ps']; before = c.call('project.get', {})
        candidate = copy.deepcopy(source); candidate['components'][0]['version'] = '^2.0.0'
        validate(c, candidate, 'catalog.version.mismatch')
        check(c.call('session.info', {})['now_ps'] == now and c.call('project.get', {}) == before, 'validation changed live project/time')
        tx = c.call('project.transaction', {'op': 'begin'})['tx']
        c.call('project.transaction', {'op': 'edit', 'tx': tx, 'edits': [{'set_version': {'id': 'R', 'version': '^2.0.0'}}]})
        try: c.call('project.transaction', {'op': 'commit', 'tx': tx})
        except EmbsimError as e: check(e.data['code'] == 'catalog.version.mismatch', str(e))
        else: check(False, 'incompatible version edit committed')
        check(c.call('project.get', {}) == before and c.call('session.info', {})['now_ps'] == now, 'failed version edit changed live circuit')
        tx = c.call('project.transaction', {'op': 'begin'})['tx']
        c.call('project.transaction', {'op': 'edit', 'tx': tx, 'edits': [{'set_version': {'id': 'R', 'version': '>=1.0 <2'}}]})
        result = c.call('project.transaction', {'op': 'commit', 'tx': tx})
        check(result['applied'] and result['restarted'], 'compatible request not applied as declared structural edit')
        check(c.call('session.info', {})['now_ps'] == '0', 'version edit restart was not explicit')
        c.call('control.advance', {'by': '1us'})
        tx = c.call('project.transaction', {'op': 'begin'})['tx']
        c.call('project.transaction', {'op': 'edit', 'tx': tx, 'edits': [{'set_version': {'id': 'R', 'version': '>=1.0 <2'}}]})
        no_op = c.call('project.transaction', {'op': 'commit', 'tx': tx})
        check(no_op['applied'] and not no_op['restarted'] and c.call('session.info', {})['now_ps'] == now, 'identical request restarts circuit')
        manifest = json.loads(c.call('project.manifest', {})['json'])
        check(manifest['instances'][0]['requested_version'] == '>=1.0 <2' and manifest['instances'][0]['version_satisfied'], 'manifest does not retain native version satisfaction')
        for bad in ['^^1.0', '*', '1.0.0 || 2', '>=1,', '99999999999999.0.0']:
            candidate['components'][0]['version'] = bad; validate(c, candidate, 'catalog.version.invalid')
        hierarchy = json.loads((ROOT / 'examples/hierarchical-instruments/project.json').read_text(encoding='utf-8'))
        definition = next(iter(hierarchy['blocks'].values()))
        definition['components'][0]['version'] = '^2'
        validate(c, hierarchy, 'catalog.version.mismatch')
        # Definitions are checked even without any concrete instance using them.
        hierarchy['components'] = []; hierarchy['nets'] = {}; hierarchy.pop('layout', None); hierarchy.pop('probes', None)
        validate(c, hierarchy, 'catalog.version.mismatch')
        board = {'schema_version': 1, 'kind': 'project', 'name': 'Board range', 'policy': 'functional', 'boards': [{'id': 'B', 'board': 'emblink/arduino_uno', 'version': '^1'}], 'components': [], 'nets': {}}
        validate(c, board)
        board['boards'][0]['version'] = '^2'; validate(c, board, 'catalog.version.mismatch')
    with tempfile.TemporaryDirectory(prefix='embsim-version-') as temp:
        root = Path(temp); lib = root / 'versiontest'; lib.mkdir()
        def write(path, doc): path.write_text(json.dumps(doc), encoding='utf-8')
        library = {'schema_version': 1, 'kind': 'library', 'name': 'versiontest', 'version': '1.2.3', 'license': 'Apache-2.0', 'depends': [{'name': 'emblink', 'version': '^0.1.0'}]}
        write(lib / 'library.json', library)
        amp_dir = lib / 'components' / 'amp'; amp_dir.mkdir(parents=True)
        amp = json.loads((ROOT / 'catalog/components/lm358p/component.json').read_text(encoding='utf-8'))
        amp['id'] = 'versiontest/amp'; amp['model']['composite']['components'][0]['version'] = '^2'
        write(amp_dir / 'component.json', amp)
        shutil.copy(ROOT / 'catalog/components/lm358p/symbol.svg', amp_dir)
        board_dir = lib / 'boards' / 'uno'; board_dir.mkdir(parents=True)
        uno = json.loads((ROOT / 'catalog/boards/arduino_uno/board.json').read_text(encoding='utf-8'))
        uno['id'] = 'versiontest/uno'; uno['mcu']['version'] = '^2'
        write(board_dir / 'board.json', uno)
        project = copy.deepcopy(source); project['libraries'] = ['versiontest']
        path = root / 'project.json'; write(path, project)
        def run(expected, doc=project):
            write(path, doc)
            result = subprocess.run([binary, 'validate', str(path)], cwd=ROOT, capture_output=True, text=True, encoding='utf-8')
            check(result.returncode == (0 if expected is None else 2), result.stdout + result.stderr)
            if expected: check(expected in result.stderr or expected in result.stdout, result.stdout + result.stderr)
        run(None)
        aliases = copy.deepcopy(project); aliases['libraries'].append('./versiontest')
        run(None, aliases)
        composite = copy.deepcopy(project); composite['components'] = [{'id': 'U', 'component': 'versiontest/amp'}]
        run('catalog.version.mismatch', composite)
        mcu = copy.deepcopy(project); mcu['boards'] = [{'id': 'B', 'board': 'versiontest/uno'}]
        run('catalog.version.mismatch', mcu)
        # The MCU is now compatible but an on-board part is not.
        uno['mcu']['version'] = '^1'; uno['components'][0]['version'] = '^99'; write(board_dir / 'board.json', uno)
        run('catalog.version.mismatch', mcu)
        for dep, expected in [({'name': 'emblink', 'version': '^1'}, 'library.dependency.mismatch'),
                              ({'name': 'not_installed'}, 'library.dependency.missing'),
                              ({'name': 'emblink', 'version': '^^1'}, 'library.dependency.invalid')]:
            library['depends'] = [dep]; write(lib / 'library.json', library); run(expected)
        library['depends'] = [{'name': 'emblink'}]; write(lib / 'library.json', library); run(None)
        # A transitive requirement matters even when no part uses its declaring library.
        dep_dir = root / 'dependency'; dep_dir.mkdir()
        write(dep_dir / 'library.json', {'schema_version': 1, 'kind': 'library', 'name': 'dependency', 'version': '1.0.0', 'license': 'Apache-2.0', 'depends': [{'name': 'emblink', 'version': '^9'}]})
        library['depends'] = [{'name': 'dependency', 'version': '^1'}]; write(lib / 'library.json', library)
        project['libraries'].append('dependency'); run('library.dependency.mismatch')
        # Two explicit revisions of one dependency name cannot silently satisfy a request.
        second = root / 'dependency2'; shutil.copytree(dep_dir, second)
        project['libraries'].append('dependency2'); run('library.dependency.ambiguous')
    print(f'{count} native version contract checks, 0 failed')


if __name__ == '__main__': main()
