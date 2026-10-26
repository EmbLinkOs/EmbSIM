#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Check nominal DC indicator evidence; optionally reproduce points from source PDFs.

No network access or vendor PDF redistribution. --pdf verifies an explicitly
supplied source snapshot with PyMuPDF; ordinary CI checks the checked-in evidence
and fixtures without that optional dependency. Native fixtures execute separately.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
VT = 0.025852  # Same fixed reference as native junctions; not a temperature model.


def fit(points: list[dict], reference: float) -> dict:
    """Fit terminal V = a + b*ln(I/Iref) + rs*(I-Iref), calibration only."""
    rows = [[1, math.log(p['current_a'] / reference), p['current_a'] - reference] for p in points]
    matrix = [[sum(r[j] * r[k] for r in rows) for k in range(3)] +
              [sum(r[j] * p['voltage_v'] for r, p in zip(rows, points))] for j in range(3)]
    for col in range(3):
        pivot = max(range(col, 3), key=lambda row: abs(matrix[row][col]))
        matrix[col], matrix[pivot] = matrix[pivot], matrix[col]
        scale = matrix[col][col]
        if abs(scale) < 1e-18:
            raise ValueError('calibration points are singular')
        matrix[col] = [v / scale for v in matrix[col]]
        for row in range(3):
            if row != col:
                scale = matrix[row][col]
                matrix[row] = [a - scale * b for a, b in zip(matrix[row], matrix[col])]
    a, b, rs = [row[-1] for row in matrix]
    if b <= 0 or rs < 0:
        raise ValueError('source fit needs positive ideality and nonnegative series resistance')
    return {'vf': a - reference * rs, 'n': b / VT, 'rs': rs, 'if_nom': reference}


def check(doc: dict, base: Path) -> list[str]:
    """Semantic checks used by validate.py as well as this command."""
    q = doc.get('qualification')
    if q is None:
        return []
    errors = []
    try:
        source = q['source']
        if q['temperature_c'] != 25:
            errors.append('reviewed indicator curves apply only at 25 C')
        if source['url'] != doc['part']['datasheet'] or not any(
                p.get('url') == source['url'] and p.get('hash') == 'sha256:' + source['sha256']
                for p in doc['provenance']['sources']):
            errors.append('qualification source must match the manufacturer datasheet and pinned provenance hash')
        model = doc['model']
        if model['primitive'] not in ('led', 'rgb_led'):
            errors.append('nominal DC indicator evidence requires a led or rgb_led primitive')
        constants = model.get('params', {})
        pins = {p['name'] for p in doc['pins']}
        expected = {'A'} if model['primitive'] == 'led' else {'R', 'G', 'B'}
        if {c['pin'] for c in q['curves']} != expected or len(q['curves']) != len(expected):
            errors.append('qualification must cover each emitting channel exactly once')
        lo, hi = q['current_a']
        if lo >= hi:
            errors.append('qualification current envelope must be increasing')
        for curve in q['curves']:
            pin, points = curve['pin'], curve['points']
            if curve['page'] not in source['pages']:
                errors.append(f'{pin}: curve page must be listed in the reviewed source pages')
            calibration = [p for p in points if p['role'] == 'calibration']
            holdout = [p for p in points if p['role'] == 'holdout']
            currents = [p['current_a'] for p in points]
            if not all(math.isfinite(p['current_a']) and math.isfinite(p['voltage_v']) for p in points):
                errors.append(f'{pin}: curve samples must be finite')
                continue
            if pin not in pins or len(calibration) < 3 or len(holdout) < 3 or len(set(currents)) != len(currents):
                errors.append(f'{pin}: distinct calibration and holdout currents and a real terminal are required')
                continue
            if min(currents) != lo or max(currents) != hi:
                errors.append(f'{pin}: samples must span the exact declared current envelope')
            fitted = fit(calibration, curve['reference_current_a'])
            prefix = '' if pin == 'A' else {'R': 'red_', 'G': 'green_', 'B': 'blue_'}[pin]
            for key, value in fitted.items():
                if not math.isclose(constants[prefix + key], value, rel_tol=1e-4, abs_tol=1e-7):
                    errors.append(f'{pin}: model {key} differs from calibration-only fit')
            for point in points:
                amps, volts = point['current_a'], point['voltage_v']
                predicted = fitted['vf'] + fitted['n'] * VT * math.log(amps / fitted['if_nom']) + fitted['rs'] * amps
                tolerance = q['voltage_tolerance_v']
                if abs(predicted - volts) > tolerance:
                    errors.append(f'{pin}: {point["role"]} point exceeds the declared curve tolerance')
                if not re.fullmatch(r'tests/[a-z0-9_-]+\.json', point['fixture']):
                    errors.append(f'{pin}: fixture must be a local tests/*.json file')
                    continue
                fixture = json.loads((base / point['fixture']).read_text())
                if fixture['policy'] != 'electrical':
                    errors.append(f'{pin}: curve fixtures must execute electrical policy')
                instances = {p['id']: p for p in fixture['circuit']['components']}
                driver = instances['I' + pin]
                if instances['DUT']['component'] != doc['id'] or driver['component'] != 'emblink/isource' or driver['params'].get('kind', 'dc') != 'dc' or driver['params']['dc'] != amps:
                    errors.append(f'{pin}: fixture must force the source current through the named DUT')
                net = fixture['circuit']['nets']['D' + pin]
                if set(net) != {f'DUT.{pin}', f'I{pin}.-'}:
                    errors.append(f'{pin}: source fixture terminal topology disagrees')
                ground = fixture['circuit']['nets']['GND']
                common = 'K' if model['primitive'] == 'led' else 'COM'
                if not {f'DUT.{common}', f'I{pin}.+', 'G.GND'}.issubset(ground):
                    errors.append(f'{pin}: curve fixture requires the documented grounded return')
                bounds = fixture['expect']['net:D' + pin]
                if not math.isclose(bounds['min_v'], volts - tolerance, abs_tol=1e-6) or not math.isclose(bounds['max_v'], volts + tolerance, abs_tol=1e-6):
                    errors.append(f'{pin}: native acceptance bounds must come from source points, not model predictions')
                if not any((base / point['fixture']).match(pattern) for pattern in doc['tests']):
                    errors.append(f'{pin}: source fixture is not selected by component tests')
    except (KeyError, TypeError, ValueError, ZeroDivisionError, OSError) as error:
        errors.append(f'invalid indicator evidence: {error}')
    return errors


def source_points(doc: dict, path: Path) -> list[str]:
    """Re-extract factual samples from the pinned vector plots, without refitting."""
    q = doc['qualification']
    if hashlib.sha256(path.read_bytes()).hexdigest() != q['source']['sha256']:
        return ['PDF hash mismatch: use the exact reviewed source revision']
    import fitz  # Optional, only used for an exact, supplied source snapshot.
    errors = []
    with fitz.open(path) as pdf:
        for curve in q['curves']:
            drawings = pdf[curve['page'] - 1].get_drawings()
            segments = []
            for index in curve['pdf_path_indices']:
                for item in drawings[index]['items']:
                    if item[0] != 'l':
                        raise ValueError('reviewed curve must be a vector polyline')
                    segments.append(item[1:])
            xmin, ymin, xmax, ymax = curve['axes_pdf_pt']
            vlo, vhi = curve['axis_voltage_v']
            for point in curve['points']:
                y = ymax - point['current_a'] / curve['axis_max_current_a'] * (ymax - ymin)
                match = next(((a, b) for a, b in segments if b.y <= y <= a.y and b.y != a.y), None)
                if not match:
                    errors.append(f'{curve["pin"]}: source plot does not span sample current')
                    continue
                a, b = match
                x = a.x + (y - a.y) / (b.y - a.y) * (b.x - a.x)
                volts = vlo + (x - xmin) / (xmax - xmin) * (vhi - vlo)
                if abs(volts - point['voltage_v']) > 1e-6:
                    errors.append(f'{curve["pin"]}: stored sample disagrees with pinned vector plot')
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pdf', type=Path, action='append', default=[])
    args = parser.parse_args()
    descriptions = [p for p in (ROOT / 'catalog/components').glob('*/component.json')
                    if 'qualification' in json.loads(p.read_text())]
    failures = 0
    pdf_hashes = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in args.pdf}
    recognized = set()
    for path in sorted(descriptions):
        doc = json.loads(path.read_text())
        errors = check(doc, path.parent)
        for pdf, digest in pdf_hashes.items():
            if digest == doc['qualification']['source']['sha256']:
                errors += source_points(doc, pdf)
                recognized.add(digest)
        print(('FAIL ' if errors else 'PASS ') + doc['id'])
        for error in errors:
            print('  ' + error)
        failures += bool(errors)
    for pdf, digest in pdf_hashes.items():
        if digest not in recognized:
            print(f'FAIL {pdf.name}: source hash does not match a reviewed indicator snapshot')
            failures += 1
    print(f'{len(descriptions)} indicator evidence records, {failures} failed; {len(recognized)} source snapshots reproduced')
    return int(not descriptions or failures > 0)


if __name__ == '__main__':
    raise SystemExit(main())
