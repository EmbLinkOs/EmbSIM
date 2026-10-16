#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Execute component conformance fixtures through the real CLI and scenario runner.

Unlike schema validation, this checks circuit behavior. No result is inferred
from file presence, and a zero-fixture selection is an error. The original
catalog is never modified. --named selects components with manufacturer metadata.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def convert(fixture: dict) -> tuple[dict, dict]:
    probes, assertions = [], []
    for i, (target, expectation) in enumerate(fixture['expect'].items()):
        selector = {k: v for k, v in expectation.items() if k in ('at', 'between', 'by')}
        expected = {k: v for k, v in expectation.items() if k not in selector}
        kind = 'digital' if 'logic' in expected or (fixture.get('policy') != 'electrical' and any(k in expected for k in ('edges', 'frequency_hz'))) else 'analog' if any(k in expected for k in ('min_v', 'max_v', 'edges', 'frequency_hz')) else 'value'
        probes.append({'id': f'p{i}', 'target': target, 'kind': kind, **({'rate_hz': 1_000_000} if kind == 'analog' else {})})
        assertions.append({'id': f'assert-{i}', 'probe': f'p{i}', **(selector or {'at': fixture.get('until', '1ms')}), 'expect': expected})
    assertions.append({'id': 'solver-health', 'always': True, 'diagnostics': {'forbid': ['analog.no_convergence', 'analog.timestep_too_small', 'sched.delta_overflow']}})
    project = {'schema_version': 1, 'kind': 'project', 'name': 'catalog-fixture',
               'libraries': [str(ROOT / 'catalog')], 'policy': fixture.get('policy', 'electrical'),
               **fixture['circuit'], 'probes': probes, 'settings': {'max_delta_cycles': 1000}}
    scenario = {'schema_version': 1, 'kind': 'scenario', 'name': fixture.get('name', 'fixture'),
                'until': fixture.get('until', '1ms'), 'inputs': fixture.get('inputs', []), 'assertions': assertions}
    return project, scenario


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--embsim', required=True, type=Path)
    parser.add_argument('--named', action='store_true')
    parser.add_argument('--component', action='append', default=[])
    args = parser.parse_args()
    binary = args.embsim.resolve()
    if not binary.is_file():
        parser.error('embsim executable not found')
    selected = []
    for description in sorted((ROOT / 'catalog/components').glob('*/component.json')):
        doc = json.loads(description.read_text())
        if args.named and not doc.get('part'):
            continue
        if args.component and description.parent.name not in args.component:
            continue
        for pattern in doc.get('tests', []):
            selected.extend(sorted(description.parent.glob(pattern)))
    selected = list(dict.fromkeys(selected))
    if not selected:
        parser.error('no fixtures selected')
    failed, count = 0, 0
    with tempfile.TemporaryDirectory(prefix='embsim-catalog-') as temp:
        work = Path(temp)
        for fixture in selected:
            project, scenario = convert(json.loads(fixture.read_text()))
            (work / 'project.json').write_text(json.dumps(project))
            (work / 'scenario.json').write_text(json.dumps(scenario))
            try:
                result = subprocess.run([str(binary), 'run', str(work / 'project.json'), '--scenario', str(work / 'scenario.json'), '--json'], cwd=ROOT, capture_output=True, text=True, timeout=60)
            except subprocess.TimeoutExpired:
                failed += 1
                print(f'FAIL {fixture.relative_to(ROOT)}: timed out after 60 seconds', flush=True)
                continue
            try:
                report = json.loads(result.stdout)
            except json.JSONDecodeError:
                report = {}
            assertions = report.get('assertions', [])
            ok = result.returncode == 0 and len(assertions) == len(scenario['assertions']) and all(a.get('ok') for a in assertions)
            label = str(fixture.relative_to(ROOT / 'catalog/components'))
            print(('PASS ' if ok else 'FAIL ') + label, flush=True)
            if not ok:
                failed += 1
                print(result.stderr[-3000:], result.stdout[-5000:], flush=True)
            count += len(assertions)
    print(f'{len(selected)} fixtures, {count} assertions executed, {failed} failed')
    return int(failed != 0)


if __name__ == '__main__':
    raise SystemExit(main())
