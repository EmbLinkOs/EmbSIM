#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Native linear AC: analytic RC/RLC, loading, source polarity, isolation and refusal."""
import argparse
import cmath
import copy
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools'))
from embsim_client import StdioClient  # noqa: E402
from embsim.transport import EmbsimError  # noqa: E402
import validate  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--embsim', required=True, type=Path)
    args = ap.parse_args()
    binary = str(args.embsim.resolve())
    count = 0

    def check(value, message):
        nonlocal count
        count += 1
        assert value, message

    def near(actual, expected):
        check(abs(actual-expected) <= 2e-10*max(1, abs(expected)), (actual, expected))

    base = json.loads((ROOT/'examples/spectrum-rc/project.json').read_text(encoding='utf-8'))
    request = {'schema_version': 1, 'kind': 'ac_sweep_request', 'source': 'V1', 'reference': 'net:GND',
               'frequencies_hz': [10**(-3+i*15/48) for i in range(49)], 'probes': ['net:IN', 'net:OUT', 'net:GND']}
    # Set exact endpoints, avoiding exponentiation roundoff outside schema bounds.
    request['frequencies_hz'][0] = .001
    request['frequencies_hz'][-1] = 1e12
    with StdioClient([binary, 'sidecar']) as client, tempfile.TemporaryDirectory(prefix='embsim-ac-') as tmp:
        capability = client.call('session.open', {'client': 'ac', 'protocol': '1.0'})
        check('analysis.ac' in capability['capabilities'], 'AC capability missing')

        def load(project):
            client.call('project.load', {'json': project})

        def sweep(req=None):
            return client.call('analysis.ac', req or request)

        def refuse(req):
            try:
                sweep(req)
            except EmbsimError:
                check(True, '')
            else:
                check(False, 'invalid AC analysis accepted')

        def coefficients(result, expected, channel=1):
            for p in result['points']:
                r = p['responses'][channel]
                value = expected(p['frequency_hz'])
                near(r['real'], value.real)
                near(r['imag'], value.imag)
                near(r['magnitude'], abs(value))
                if abs(value):
                    near(r['db'], 20*math.log10(abs(value)))
                    near(r['phase_deg'], math.degrees(cmath.phase(value)))
                else:
                    check(r['db'] is None and r['phase_deg'] is None, 'zero semantics')

        load(base)
        client.call('control.advance', {'by': '1us'})
        before = [client.call('project.get', {}), client.call('session.info', {})['now_ps'], client.call('probe.list', {})]
        physical = sweep()
        coefficients(physical, lambda f: 1/(1+2j*math.pi*f*1e-4))
        coefficients(physical, lambda f: 1+0j, 0)
        coefficients(physical, lambda f: 0j, 2)
        check(before == [client.call('project.get', {}), client.call('session.info', {})['now_ps'], client.call('probe.list', {})], 'AC mutates live project/time/probes')
        check(sweep() == physical, 'AC is nondeterministic')
        for backend in ('jsonschema', 'builtin'):
            schemas = validate.Schemas()
            if backend == "builtin": schemas._jsonschema = None
            path = Path(tmp)/'ac.json'
            path.write_text(json.dumps(physical), encoding='utf-8')
            report = validate.Report()
            validate.validate_file(path, schemas, validate.Index([ROOT/"catalog"]), report)
            check(report.errors == 0, report.findings)
            for mutation in ('phase', 'grid', 'hash', 'zero', 'source', 'target', 'json'):
                bad = copy.deepcopy(physical)
                if mutation == 'phase': bad['points'][0]['responses'][1]['phase_deg'] += 1
                if mutation == 'grid': bad['points'][0]['frequency_hz'] = .002
                if mutation == 'hash': bad['source']['sha256'] = 'sha256:'+'0'*64
                if mutation == 'zero': bad['points'][0]['responses'][2]['db'] = 0
                if mutation == 'source': bad['request']['source'] = 'R1'
                if mutation == 'target': bad['points'][0]['responses'][0]['target'] = 'net:OUT'
                if mutation == 'json': bad['source']['json'] = '{'
                path.write_text(json.dumps(bad), encoding='utf-8')
                report = validate.Report()
                validate.validate_file(path, schemas, validate.Index([ROOT/"catalog"]), report)
                check(report.errors > 0, mutation+' accepted')
        path = Path(tmp)/'request.json'
        path.write_text(json.dumps(request), encoding='utf-8')
        cli = subprocess.run([binary, 'ac', str(ROOT/'examples/spectrum-rc/project.json'), '--request', str(path)], cwd=ROOT, capture_output=True, text=True, encoding='utf-8')
        # Runtime timestamp differs because the CLI source starts at reset.
        exported = json.loads(cli.stdout)
        check(cli.returncode == 0 and exported['points'] == physical['points'], cli.stderr)
        differential = sweep({**request, 'reference': 'net:OUT'})
        coefficients(differential, lambda f: 1-1/(1+2j*math.pi*f*1e-4), 0)
        coefficients(differential, lambda f: 0j)
        # Physical meter and scope loading; selected source stays internally 1 V.
        loaded = copy.deepcopy(base)
        loaded['components'] += [{'id': 'M', 'component': 'emblink/scope_input', 'params': {'input_resistance': '10k', 'input_capacitance': '10n'}}]
        loaded['nets']['OUT'].append('M.+'); loaded['nets']['GND'].append('M.-')
        load(loaded)
        coefficients(sweep(), lambda f: 1/(2+2j*math.pi*f*2e-4))
        # RLC with physical winding resistance, capacitor ESR and source Rout.
        rlc = copy.deepcopy(base)
        rlc['components'][0] = {'id': 'V1', 'component': 'emblink/function_generator', 'params': {'kind': 'dc', 'dc': 0, 'output_resistance': 50}}
        rlc['components'][2]['params']['esr'] = 20
        rlc['components'].append({'id': 'L1', 'component': 'emblink/inductor', 'params': {'inductance': '.01', 'dcr': 30}})
        rlc['nets']['OUT'].remove('R1.2'); rlc['nets']['OUT'].append('L1.2')
        rlc['nets']['MID'] = ['R1.2', 'L1.1']
        load(rlc)
        coefficients(sweep(), lambda f: (20+1/(2j*math.pi*f*1e-8))/(10000+50+30+2j*math.pi*f*.01+20+1/(2j*math.pi*f*1e-8)))
        # Current source positive flows + -> -, so the passive node is negative.
        current = copy.deepcopy(base)
        current['components'][0] = {'id': 'V1', 'component': 'emblink/isource', 'params': {'kind': 'dc', 'dc': 0}}
        load(current)
        result = sweep()
        check(result['unit'] == 'ohm', 'current source unit')
        coefficients(result, lambda f: -1/(2j*math.pi*f*1e-8))
        # Other independent source excitations are zeroed, not removed.
        multi = copy.deepcopy(base)
        multi['components'].append({'id': 'V2', 'component': 'emblink/vsource', 'params': {'dc': 17}})
        multi['nets']['GND'].remove('C1.2'); multi['nets']['GND'].append('V2.-')
        multi['nets']['BIAS'] = ['V2.+', 'C1.2']
        load(multi)
        coefficients(sweep(), lambda f: 1/(1+2j*math.pi*f*1e-4))
        # Independent committed ngspice coefficients, not generated by EmbSIM.
        referee = json.loads((ROOT/'tests/analog/ac-linear-reference.json').read_text(encoding='utf-8'))
        for name, case in referee['cases'].items():
            circuit = copy.deepcopy(loaded if name == 'loaded_rc' else base)
            if name == 'series_rlc':
                circuit['components'][1]['params']['resistance'] = 10
                circuit['components'][2]['params']['capacitance'] = '1u'
                circuit['components'].append({'id': 'L1', 'component': 'emblink/inductor', 'params': {'inductance': '10m'}})
                circuit['nets']['OUT'].remove('R1.2'); circuit['nets']['OUT'].append('L1.2')
                circuit['nets']['MID'] = ['R1.2', 'L1.1']
            load(circuit)
            frequencies = [p['frequency_hz'] for p in case['points']]
            actual = sweep({**request, 'frequencies_hz': frequencies})
            for a, expected in zip(actual['points'], case['points']):
                near(a['responses'][1]['real'], expected['real'])
                near(a['responses'][1]['imag'], expected['imag'])
            if name == 'series_rlc':
                resonant = 1/(2*math.pi*math.sqrt(.01*1e-6))
                at_resonance = sweep({**request, 'frequencies_hz': [resonant]})['points'][0]['responses'][1]
                near(at_resonance['magnitude'], 10)
                near(at_resonance['phase_deg'], -90)
        # Hierarchical shells are structural; descendants retain native behavior
        # and loading one instance never changes the shared definition's sibling.
        block = {'name': 'RC', 'components': [{'id': 'R', 'component': 'emblink/resistor', 'params': {'resistance': '10k'}}, {'id': 'C', 'component': 'emblink/capacitor', 'params': {'capacitance': '10n'}}], 'nets': {'IN': ['R.1'], 'OUT': ['R.2', 'C.1'], 'GND': ['C.2']}, 'ports': {'IN': 'R.1', 'OUT': 'C.1', 'GND': 'C.2'}}
        hierarchical = {**base, 'layout': {}, 'blocks': {'rc': block}, 'components': [copy.deepcopy(base['components'][0]), copy.deepcopy(base['components'][3]), {'id': 'A', 'component': 'block/rc'}, {'id': 'B', 'component': 'block/rc'}, {'id': 'M', 'component': 'emblink/voltmeter', 'params': {'input_resistance': '10k'}}], 'nets': {'IN': ['V1.+', 'A.IN', 'B.IN'], 'A_OUT': ['A.OUT', 'M.+'], 'B_OUT': ['B.OUT'], 'GND': ['V1.-', 'GND1.GND', 'A.GND', 'B.GND', 'M.-']}}
        load(hierarchical)
        hierarchy_result = sweep({**request, 'probes': ['pin:A.C.1','net:B_OUT']})
        coefficients(hierarchy_result, lambda f: 1/(2+2j*math.pi*f*1e-4), 0)
        coefficients(hierarchy_result, lambda f: 1/(1+2j*math.pi*f*1e-4), 1)
        # Bounds, malformed options, unsupported and singular circuits never emit points.
        load(base)
        for changes in ({'frequencies_hz': []}, {'frequencies_hz': [1, 1]}, {'frequencies_hz': [2, 1]}, {'frequencies_hz': [True]}, {'frequencies_hz': ['1k']}, {'frequencies_hz': [0]}, {'frequencies_hz': [1e13]}, {'frequencies_hz': list(range(1,258))}, {'probes': []}, {'probes': ['net:OUT']*2}, {'probes': ['net:NO']}, {'reference': 'net:NO'}, {'source': 'R1'}, {'extra': True}, {'kind': 'wrong'}, {'schema_version': 2}):
            refuse({**request, **changes})
        functional = {**base, 'policy': 'functional', 'components': [], 'nets': {}, 'layout': {}}
        load(functional); refuse(request)
        load(base)
        transaction = client.call('project.transaction', {'op': 'begin'})
        refuse(request)
        client.call('project.transaction', {'op': 'abort', 'tx': transaction['tx']})
        nonlinear = copy.deepcopy(base)
        nonlinear['components'][2] = {'id': 'C1', 'component': 'emblink/diode'}
        nonlinear['nets']['OUT'] = ['R1.2', 'C1.A']; nonlinear['nets']['GND'][1] = 'C1.K'
        load(nonlinear); refuse(request)
        floating = copy.deepcopy(base)
        floating['components'].append({'id': 'OPEN', 'component': 'emblink/scope_input'})
        floating['nets']['FLOAT'] = ['OPEN.+']
        load(floating); refuse(request)
        load(base)
        maximum = {**request, 'frequencies_hz': [10**(i*6/255) for i in range(256)], 'probes': ['net:OUT']}
        check(len(sweep(maximum)['points']) == 256, 'maximum grid')
        # Largest admitted island and bounded dense work; a 129th unknown fails.
        chain = {**base, 'layout': {}, 'components': [copy.deepcopy(base['components'][0]), copy.deepcopy(base['components'][3])], 'nets': {'N0': ['V1.+'], 'GND': ['V1.-', 'GND1.GND']}}
        for i in range(127):
            chain['components'].append({'id': f'R{i}', 'component': 'emblink/resistor', 'params': {'resistance': '1k'}})
            chain['nets'][f'N{i}'].append(f'R{i}.1')
            if i == 126: chain['nets']['GND'].append(f'R{i}.2')
            else: chain['nets'][f'N{i+1}'] = [f'R{i}.2']
        load(chain)
        large = {**request, 'frequencies_hz': [1000], 'probes': ['net:N126']}
        near(sweep(large)['points'][0]['responses'][0]['magnitude'], 1/127)
        refuse({**large, 'frequencies_hz': maximum['frequencies_hz']})
        chain['components'].append({'id': 'EXTRA', 'component': 'emblink/resistor'})
        chain['nets']['GND'].remove('R126.2');chain['nets']['GND'].append('EXTRA.2')
        chain['nets']['N127'] = ['R126.2','EXTRA.1']
        load(chain);refuse(large)

    print(f'{count} native AC acceptance checks, 0 failed')


if __name__ == '__main__': main()
