#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Generate/check independent ngspice linear AC references; ngspice is referee only."""
import argparse
import json
import math
import re
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT/'tests/analog/ac-linear-reference.json'
CIRCUITS = {
    'rc': ['V1 IN 0 AC 1', 'R1 IN OUT 10k', 'C1 OUT 0 10n'],
    'loaded_rc': ['V1 IN 0 AC 1', 'R1 IN OUT 10k', 'C1 OUT 0 10n', 'RM OUT 0 10k', 'CM OUT 0 10n'],
    'series_rlc': ['V1 IN 0 AC 1', 'R1 IN MID 10', 'L1 MID OUT 10m', 'C1 OUT 0 1u'],
}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--ngspice', default='ngspice')
    ap.add_argument('--check', action='store_true')
    args = ap.parse_args()
    version = subprocess.run([args.ngspice, "--version"], check=True, capture_output=True, text=True, encoding="utf-8")
    match = re.search(r"ngspice-([0-9.]+)", version.stdout)
    if not match: raise RuntimeError("Cannot identify ngspice referee version")
    referee = "ngspice-"+match.group(1)
    cases = {}
    with tempfile.TemporaryDirectory(prefix='embsim-ac-ngspice-') as tmp:
        temp = Path(tmp)
        for name, lines in CIRCUITS.items():
            target = temp/(name+'.dat')
            # Use a relative file name in ngspice control syntax; cwd may contain spaces.
            deck = '\n'.join(['Linear AC independent reference', *lines, '.control', 'set numdgt=17', 'ac dec 8 10 1meg', f'wrdata {target.name} real(v(out)) imag(v(out))', 'quit', '.endc', '.end', ''])
            path = temp/(name+'.cir');path.write_text(deck, encoding='utf-8')
            run = subprocess.run([args.ngspice, '-b', str(path)], cwd=temp, capture_output=True, text=True, encoding='utf-8')
            if run.returncode or not target.exists(): raise RuntimeError(run.stdout+run.stderr)
            points = []
            for line in target.read_text(encoding='utf-8').splitlines():
                f, re, f2, im = map(float, line.split())
                assert f == f2
                points.append({'frequency_hz': f, 'real': re, 'imag': im})
            cases[name] = {'netlist': lines, 'points': points}
    if args.check:
        saved = json.loads(OUTPUT.read_text(encoding='utf-8'))['cases']
        assert saved.keys() == cases.keys()
        for name, data in cases.items():
            assert saved[name]['netlist'] == data['netlist']
            assert len(saved[name]['points']) == len(data['points'])
            for old, new in zip(saved[name]['points'], data['points']):
                for key in old: assert math.isclose(old[key], new[key], rel_tol=1e-10, abs_tol=1e-12), (name,key,old,new)
        print('ngspice linear AC reference agrees: 3 circuits, 123 complex points')
    else:
        OUTPUT.write_text(json.dumps({'referee':referee,'method':'ac dec 8 10 1meg','cases':cases}, indent=2)+'\n', encoding='utf-8')
        print(OUTPUT)


if __name__ == '__main__': main()
