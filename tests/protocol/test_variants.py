#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Real open footprints, independent sheet alternatives, staged activation and retained bases."""
import argparse
import copy
import hashlib
import json
import shutil
from pathlib import Path
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
    source = json.loads((ROOT / 'examples/assembly-variants/project.json').read_text(encoding='utf-8'))
    def check(ok, why):
        nonlocal count
        if not ok: raise AssertionError(why)
        count += 1
    def close(actual, expected, why, tolerance=1e-4): check(abs(actual - expected) <= tolerance, f'{why}: {actual} != {expected}')
    def cli(*args): return subprocess.run([binary, *map(str, args)], cwd=ROOT, capture_output=True, text=True, encoding='utf-8')
    def outputs(c): return c.call('observe.snapshot', {'include': ['outputs']})['outputs']
    def advance(c): c.call('control.advance', {'by': '1us'})
    with tempfile.TemporaryDirectory(prefix='embsim-variants-') as temp, StdioClient([binary, 'sidecar']) as c:
        root = Path(temp); path = root / 'project.json'; path.write_text(json.dumps(source), encoding='utf-8')
        opened = c.call('session.open', {'client': 'variants', 'protocol': '1.0'})
        check('assembly.variants' in opened['capabilities'], 'missing explicit population capability')
        c.call('project.load', {'path': str(path)}); advance(c)
        before = c.call('project.get', {}); now = c.call('session.info', {})['now_ps']; original = outputs(c); probes = c.call('probe.list', {})
        close(original['A.METER']['voltage'], 5, 'base A'); close(original['B.METER']['voltage'], 100/11, 'base B')
        preview = c.call('project.variant', {'op': 'preview', 'name': 'production'})
        check(preview['valid'] and not preview['diagnostics'], str(preview))
        check(c.call('project.get', {}) == before and c.call('session.info', {})['now_ps'] == now and outputs(c) == original and c.call('probe.list', {}) == probes, 'preview changes live source/state/probes/time')
        projected = preview['project']; origin = projected['variant_origin']
        check(origin['sha256'] == 'sha256:' + hashlib.sha256(origin['json'].encode()).hexdigest() == preview['base_sha256'], 'base identity mismatch')
        check(json.loads(origin['json']) == source and 'variant_origin' not in json.loads(origin['json']), 'base is lost or recursively wrapped')
        data = json.loads(preview['manifest']['json']); rows = {r['path']: r for r in data['instances']}
        check(not rows['A.R']['populated'] and rows['A.R']['model'] == 'unpopulated', 'DNP is only presentation metadata')
        check(rows['B.R']['populated'] and rows['B.R']['params']['resistance'] == 2000000, 'independent instance override lost')
        check(len(rows) == 6 and 'A.R' in rows and projected['nets'] == source['nets'], 'DNP removed package/pads or changed root net membership')
        a, b = projected['components'][2:4]
        check(a['component'] != b['component'] and a['component'] != source['components'][2]['component'], 'concrete sheet overrides still mutate shared definition')
        try: c.call('project.variant', {'op': 'activate', 'name': 'production', 'expected_sha256': 'sha256:' + '0'*64})
        except EmbsimError: check(True, '')
        else: check(False, 'stale reviewed base activated')
        check(c.call('session.info', {})['now_ps'] == now, 'failed activation changes clock')
        open_tx = c.call('project.transaction', {'op': 'begin'})['tx']
        try: c.call('project.variant', {'op': 'activate', 'name': 'production'})
        except EmbsimError as e: check('transaction' in str(e), str(e))
        else: check(False, 'activation bypasses an open edit transaction')
        c.call('project.transaction', {'op': 'abort', 'tx': open_tx})
        c.call('project.variant', {'op': 'activate', 'name': 'production', 'expected_sha256': preview['base_sha256']})
        check(c.call('session.info', {})['now_ps'] == '0', 'activation did not restart')
        check(not c.call('session.info', {}).get('project_path'), 'derived project can overwrite saved base by default')
        advance(c); actual = outputs(c)
        close(actual['A.METER']['voltage'], 0, 'unfitted A resistor still conducts', 1e-8)
        close(actual['B.METER']['voltage'], 25/3, 'B independent 2M load')
        check('A.R' not in actual, 'DNP reports invented fitted-device outputs')
        try: c.call('project.transaction', {'op': 'begin'})
        except EmbsimError as e: check('base project' in str(e), str(e))
        else: check(False, 'derived circuit is editable without returning to base')
        check(c.call('project.variant', {'op': 'base'}) == source, 'returnable base modified by derived circuit')
        c.call('project.variant', {'op': 'activate'})
        check(c.call('project.get', {})['project'] == source, 'base return loses definitions or original shared bindings')
        advance(c); close(outputs(c)['A.METER']['voltage'], 5, 'restored A'); close(outputs(c)['B.METER']['voltage'], 100/11, 'restored B')
        # Metadata edits preserve time and their inverse restores the old declarations.
        after = copy.deepcopy(source['variants']); after['invalid'] = {'overrides': {'A.R': {'params': {'resistance': '-1'}}}}
        now = c.call('session.info', {})['now_ps']
        tx = c.call('project.transaction', {'op': 'begin'})['tx']; c.call('project.transaction', {'op': 'edit', 'tx': tx, 'edits': [{'set_variants': after}]})
        changed = c.call('project.transaction', {'op': 'commit', 'tx': tx})
        check(changed['applied'] and not changed['restarted'] and c.call('session.info', {})['now_ps'] == now, 'variant definition edits reset the base circuit')
        bad = c.call('project.variant', {'op': 'preview', 'name': 'invalid'})
        check(not bad['valid'] and any(d['code'] == 'analog.param_out_of_range' for d in bad['diagnostics']), 'invalid electrical parameters pass preview')
        try: c.call('project.variant', {'op': 'activate', 'name': 'invalid'})
        except EmbsimError: check(True, '')
        else: check(False, 'invalid variant activated')
        check(c.call('session.info', {})['now_ps'] == now and 'variant_origin' not in c.call('project.get', {})['project'], 'invalid activation destroys current base')
        tx = c.call('project.transaction', {'op': 'begin'})['tx']; c.call('project.transaction', {'op': 'edit', 'tx': tx, 'edits': [{'set_variants': source['variants']}]}); c.call('project.transaction', {'op': 'commit', 'tx': tx})
        check(c.call('project.get', {})['project'] == source, 'metadata inverse is not exact')
        # Catalog replacements use their own defaults, and reject wire-geometry changes.
        alternatives = copy.deepcopy(source)
        alternatives['variants']['same_part'] = {'overrides': {'B.R': {'component': 'emblink/resistor'}}}
        alternatives['variants']['bad_geometry'] = {'overrides': {'A.R': {'component': 'emblink/led'}}}
        alternatives['variants']['capacitor'] = {'overrides': {'A.R': {'component': 'emblink/capacitor', 'params': {'capacitance': '1n'}}}}
        alternatives['variants']['bad_version'] = {'overrides': {'B.R': {'version': '^99'}}}
        c.call('project.load', {'json': alternatives})
        replaced = c.call('project.variant', {'op': 'preview', 'name': 'same_part'})
        check(replaced['valid'], str(replaced['diagnostics']))
        replacement_rows = json.loads(replaced['manifest']['json'])['instances']
        check(next(row for row in replacement_rows if row['path'] == 'B.R')['params']['resistance'] == 1000, 'replacement inherited old component parameter bindings')
        compatible = c.call('project.variant', {'op': 'preview', 'name': 'capacitor'})
        check(compatible['valid'], str(compatible['diagnostics']))
        caprow = next(row for row in json.loads(compatible['manifest']['json'])['instances'] if row['path'] == 'A.R')
        check(caprow['reference'] == 'emblink/capacitor' and caprow['params']['capacitance'] == 1e-9 and 'resistance' not in caprow['params'], 'compatible catalog substitution retains old parameters')
        try: c.call('project.variant', {'op': 'preview', 'name': 'bad_geometry'})
        except EmbsimError as e: check('terminal' in str(e), str(e))
        else: check(False, 'replacement silently breaks visible wire endpoints')
        rejected_version = c.call('project.variant', {'op': 'preview', 'name': 'bad_version'})
        check(not rejected_version['valid'] and any(d['code'] == 'catalog.version.mismatch' for d in rejected_version['diagnostics']), 'variant bypasses requested version contracts')
        c.call('project.load', {'json': source})
        advance(c); now = c.call('session.info', {})['now_ps']
        # Explicit fitted flags are metadata until the actual population changes.
        for flag, restart in [(True, False), (False, True), (None, True)]:
            tx = c.call('project.transaction', {'op': 'begin'})['tx']
            c.call('project.transaction', {'op': 'edit', 'tx': tx, 'edits': [{'set_population': {'id': 'V', 'populated': flag}}]})
            result = c.call('project.transaction', {'op': 'commit', 'tx': tx})
            check(result['restarted'] == restart, 'population edit restart contract violated')
            if not restart: check(c.call('session.info', {})['now_ps'] == now, 'explicit fitted metadata resets time')
            advance(c)
            close(outputs(c)['A.METER']['voltage'], 0 if flag is False else 5, 'physical source population')
        # Unfitted actuators expose no usable inputs, including scheduled inputs.
        buttons = {'schema_version': 1, 'kind': 'project', 'name': 'Unfitted switch', 'policy': 'electrical', 'components': [{'id': 'SW', 'component': 'emblink/push_button', 'populated': False}], 'nets': {}}
        c.call('project.load', {'json': buttons})
        check(not any(row['target'] == 'SW' for row in c.call('input.list', {})['inputs']), 'DNP actuator exposes ghost controls')
        for at in [None, '1000000']:
            try: c.call('input.set', {'target': 'SW', 'input': 'pressed', 'value': True, **({'at': at} if at else {})})
            except EmbsimError as e: check(e.data['code'] == 'assembly.not_populated', str(e))
            else: check(False, 'DNP actuator accepts input')
        c.call('project.load', {'json': source})
        # MCU-board internals, wrappers and unknown component paths are explicit boundaries.
        for target in ['A', 'A.R.missing', 'missing', 'B1.LED']:
            invalid = copy.deepcopy(source); invalid['variants'] = {'bad': {'overrides': {target: {'populated': False}}}}
            result = c.call('project.validate', {'json': invalid})
            check(not result['ok'], f'unsupported target {target} accepted')
        for flag in [False, 'false']:
            invalid = copy.deepcopy(source); invalid['components'][2]['populated'] = flag
            result = c.call('project.validate', {'json': invalid}); check(not result['ok'], 'DNP wrapper or non-boolean population accepted')
        tampered = copy.deepcopy(projected); tampered['variant_origin']['json'] += ' '
        check(not c.call('project.validate', {'json': tampered})['ok'], 'tampered retained base accepted')
        listed = cli('variant', 'list', path, '--json'); check(listed.returncode == 0 and set(json.loads(listed.stdout)) == set(source['variants']), listed.stderr)
        render = cli('variant', 'render', path, '--name', 'production'); check(render.returncode == 0, render.stderr)
        exported = json.loads(render.stdout); check(exported == projected, 'CLI and RPC materialize different circuits')
        derived = root / 'production.json'; derived.write_text(render.stdout, encoding='utf-8')
        run = cli('run', derived, '--scenario', ROOT / 'examples/assembly-variants/scenario.production.json', '--json')
        check(run.returncode == 0, run.stderr + run.stdout)
        check(cli('variant', 'render', path, '--name', 'not_found').returncode == 2, 'missing variant emits a valid artifact')
        check(cli('variant', 'list', path).returncode == 0, 'human variant list fails')
        # IC wrappers become passive external footprints with no internal channels or power paths.
        chip = json.loads((ROOT / 'examples/lm358p-dynamics/project.json').read_text(encoding='utf-8'))
        chip['variants'] = {'dnp': {'overrides': {'U1': {'populated': False}}}}
        c.call('project.load', {'json': chip}); ic = c.call('project.variant', {'op': 'preview', 'name': 'dnp'})
        check(ic['valid'], str(ic['diagnostics']))
        manifest = json.loads(ic['manifest']['json'])
        check(any(row['path'] == 'U1' and not row['populated'] for row in manifest['instances']), 'unfitted IC missing from assembly')
        check(any(row['path'] == 'U1' and row['model'] == 'unpopulated' for row in manifest['models']) and not any(row['path'].startswith('U1.') for row in manifest['models']), 'unfitted IC retains compiled active channels')
        check(c.call('project.variant', {'op': 'preview', 'name': 'dnp'})['project'] == ic['project'], 'materialization is not deterministic')
        # Functional wire shortcuts must disappear too: the real downstream LED turns off.
        functional = {'schema_version': 1, 'kind': 'project', 'name': 'Functional footprint', 'policy': 'functional',
                      'components': [{'id': 'V', 'component': 'emblink/vcc'}, {'id': 'R', 'component': 'emblink/resistor'}, {'id': 'L', 'component': 'emblink/led'}, {'id': 'G', 'component': 'emblink/gnd'}],
                      'nets': {'V': ['V.+', 'R.1'], 'OUT': ['R.2', 'L.A'], 'G': ['L.K', 'G.GND']}, 'variants': {'dnp': {'overrides': {'R': {'populated': False}}}}}
        c.call('project.load', {'json': functional}); advance(c)
        check(outputs(c)['L']['lit'] is True, 'functional reference LED is not lit')
        c.call('project.variant', {'op': 'activate', 'name': 'dnp'}); advance(c)
        check(outputs(c)['L']['lit'] is False, 'DNP retains a functional wire shortcut')
        check(c.call('observe.snapshot', {'include': ['nets']})['nets']['OUT']['logic'] == 'Z', 'DNP terminal is not electrically open under functional policy')
        # Internal metal ties are device behavior too, independent of a primitive model.
        library = root / 'variantparts'; partdir = library / 'components' / 'strap'; partdir.mkdir(parents=True)
        (library / 'library.json').write_text(json.dumps({'schema_version': 1, 'kind': 'library', 'name': 'variantparts', 'version': '1.0.0', 'license': 'Apache-2.0'}), encoding='utf-8')
        strap = json.loads((ROOT / 'catalog/components/resistor/component.json').read_text(encoding='utf-8'))
        strap.update(id='variantparts/strap', name='Tied strap fixture', params={}, model={'primitive': 'none'}, functional={'primitive': 'none'}, ties=[['1', '2']])
        (partdir / 'component.json').write_text(json.dumps(strap), encoding='utf-8'); shutil.copy(ROOT / 'catalog/components/resistor/symbol.svg', partdir)
        functional['libraries'] = [str(library)]; functional['components'][1]['component'] = 'variantparts/strap'
        c.call('project.load', {'json': functional}); advance(c)
        check(outputs(c)['L']['lit'] is True, 'fitted internal strap does not conduct')
        c.call('project.variant', {'op': 'activate', 'name': 'dnp'}); advance(c)
        check(outputs(c)['L']['lit'] is False, 'unfitted internal metal ties still short the pads')
        aid = {'schema_version': 1, 'kind': 'project', 'name': 'Unsupported bench aid', 'policy': 'functional', 'components': [{'id': 'BB', 'component': 'emblink/breadboard_170', 'populated': False}], 'nets': {}}
        check(not c.call('project.validate', {'json': aid})['ok'], 'unsupported 3D wiring-aid population silently accepted')
    print(f'{count} native assembly variant checks, 0 failed')


if __name__ == '__main__': main()
