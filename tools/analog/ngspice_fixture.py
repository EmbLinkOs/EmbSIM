#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""ngspice references for the analog fixtures (SPEC-004 §10).

Each fixture's circuit.json (tests/analog/fixtures/<name>/) becomes a SPICE
netlist with the same devices and parameters, ngspice runs its operating
point and a transient through the fixture's sample times, and the node
voltages at time 0 (the transient's start, which honours a capacitor's
`ic` as EmbSIM's operating point does; ngspice's `op` does not) and at each
sample time are written to ngspice.json beside it, with the netlist and
ngspice's version. A node a source drives against ground is an input, not a
solution, and is left out. test_analog compares EmbSIM's solution against
those references.

The translation is exact where the models agree, and says so where they
are fitted:
  - resistor, capacitor (with `ic`: a .ic on the node, the other pin at
    ground), inductor, a potentiometer as its two resistors at its position;
  - vsource dc and pulse (v1, v2, td, tr, tf, pw, per); the fixture's
    supplies as DC sources;
  - diode (is, n, rs), an LED as a diode whose IS is fitted, as EmbSIM fits
    it, so the junction drops vf at if_nom (default colour values);
  - bjt_npn/bjt_pnp: a Gummel-Poon model with only IS, BF, BR, NF, NR, VAF
    (and RB, RC, RE), which is Ebers-Moll with the Early effect, EmbSIM's.
ngspice runs at 26.85 °C (TNOM too): EmbSIM's thermal voltage is kT/q at
300.00 K. Tolerances are tight (RELTOL 1e-6, a step a thousandth of the
span) so the reference is ngspice's model, not its step control.

    ngspice_fixture.py            regenerate every fixture's ngspice.json
    ngspice_fixture.py --check    fail if a committed reference differs
                                  (by more than 1e-6 relative) from a new run
"""
import argparse
import json
import math
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests" / "analog" / "fixtures"
VT = 0.025852  # EmbSIM's kT/q (core/analog/devices.c VT_27C)
SI = {"f": 1e-15, "p": 1e-12, "n": 1e-9, "u": 1e-6, "µ": 1e-6, "m": 1e-3, "k": 1e3, "M": 1e6, "G": 1e9}
LED = {"red": (1.8, 2.0), "green": (2.1, 2.0), "yellow": (2.0, 2.0), "blue": (2.9, 3.0), "white": (2.9, 3.0)}


def num(v) -> float:
    if isinstance(v, (int, float)):
        return float(v)
    m = re.fullmatch(r"\s*([-+0-9.eE]+)\s*([a-zA-Zµ]*)\s*", str(v))
    if not m:
        raise ValueError(f"not a number: {v!r}")
    unit = m.group(2)
    scale = SI.get(unit[:1], 1.0) if unit and unit not in ("s", "V", "A", "ohm", "Hz") else 1.0
    return float(m.group(1)) * scale


def time_s(t: str) -> float:
    m = re.fullmatch(r"\s*([0-9.eE+-]+)\s*(ps|ns|us|µs|ms|s)\s*", t)
    if not m:
        raise ValueError(f"not a time: {t!r}")
    return float(m.group(1)) * {"ps": 1e-12, "ns": 1e-9, "us": 1e-6, "µs": 1e-6, "ms": 1e-3, "s": 1.0}[m.group(2)]


class Unsupported(Exception):
    pass


def netlist(circuit: dict, times: list) -> tuple:
    gnd = circuit.get("ground")
    nodes = set()

    def node(n: str) -> str:
        if n == gnd:
            return "0"
        nodes.add(n)
        return n

    lines, ics, models = [f"* {circuit['name']} (EmbSIM analog fixture)"], [], []
    driven = set()  # the nodes a source holds against ground: inputs
    for net, volts in (circuit.get("supplies") or {}).items():
        lines.append(f"VSUP_{net} {node(net)} 0 DC {num(volts)!r}")
        driven.add(net)
    for c in circuit["components"]:
        dev, p, pins, cid = c["device"], c.get("params") or {}, c["pins"], c["id"]
        n = [node(x) for x in pins]
        if dev == "resistor":
            lines.append(f"R{cid} {n[0]} {n[1]} {num(p.get('r', 1e3))!r}")
        elif dev == "capacitor":
            lines.append(f"C{cid} {n[0]} {n[1]} {num(p.get('c', 1e-6))!r}")
            if "ic" in p:
                if n[1] != "0":
                    raise Unsupported(f"{cid}: an initial condition across two nodes")
                ics.append(f"v({n[0]})={num(p['ic'])!r}")
        elif dev == "inductor":
            lines.append(f"L{cid} {n[0]} {n[1]} {num(p.get('l', 1e-3))!r}")
        elif dev == "potentiometer":
            r, pos = num(p.get("r", 10e3)), num(p.get("position", 0.5))
            lines.append(f"R{cid}a {n[0]} {n[1]} {max(r * pos, 1e-3)!r}")
            lines.append(f"R{cid}b {n[1]} {n[2]} {max(r * (1 - pos), 1e-3)!r}")
        elif dev == "vsource":
            kind = p.get("kind", "dc")
            if n[1] == "0":
                driven.add(pins[0])
            elif n[0] == "0":
                driven.add(pins[1])
            if kind == "dc":
                lines.append(f"V{cid} {n[0]} {n[1]} DC {num(p.get('dc', 0))!r}")
            elif kind == "pulse":
                tr, pw, tf = num(p.get("tr", 1e-9)), num(p.get("pw", 1e-3)), num(p.get("tf", 1e-9))
                per = num(p["per"]) if "per" in p else 2 * (tr + pw + tf)
                v2 = num(p["v2"]) if "v2" in p else num(p.get("dc", 0))
                lines.append(f"V{cid} {n[0]} {n[1]} PULSE({num(p.get('v1', 0))!r} {v2!r} {num(p.get('td', 0))!r} "
                             f"{tr!r} {tf!r} {pw!r} {per!r})")
            else:
                raise Unsupported(f"{cid}: a {kind} source")
        elif dev in ("diode", "led"):
            if dev == "led":
                vf, n_def = LED.get(p.get("color", "red"), LED["red"])
                vf, nn, if_nom = num(p.get("vf", vf)), num(p.get("n", n_def)), num(p.get("if_nom", 0.02))
                is_ = num(p["is"]) if "is" in p else if_nom / (math.exp(vf / (nn * VT)) - 1.0)
            else:
                nn, is_ = num(p.get("n", 1.0)), num(p.get("is", 1e-14))
            rs = num(p.get("rs", 0))
            models.append(f".model DM_{cid} D(IS={is_!r} N={nn!r} RS={rs!r})")
            lines.append(f"D{cid} {n[0]} {n[1]} DM_{cid}")
        elif dev in ("bjt_npn", "bjt_pnp"):
            kind = "NPN" if dev == "bjt_npn" else "PNP"
            fields = [f"IS={num(p.get('is', 1e-15))!r}", f"BF={num(p.get('bf', 100))!r}",
                      f"BR={num(p.get('br', 1))!r}", f"NF={num(p.get('nf', 1))!r}", f"NR={num(p.get('nr', 1))!r}"]
            for k in ("vaf", "rb", "rc", "re"):
                if k in p:
                    fields.append(f"{k.upper()}={num(p[k])!r}")
            models.append(f".model QM_{cid} {kind}({' '.join(fields)})")
            lines.append(f"Q{cid} {n[0]} {n[1]} {n[2]} QM_{cid}")
        else:
            raise Unsupported(f"{cid}: device {dev}")
    span = max(times) if times else 1e-3
    names = sorted(nodes - driven)
    lines += models
    if ics:
        lines.append(".ic " + " ".join(ics))
    lines.append(".options reltol=1e-6 abstol=1e-15 vntol=1e-9 chgtol=1e-18 temp=26.85 tnom=26.85")
    lines += [".control"]
    lines.append(f"tran {span / 1000!r} {span * 1.0001!r} 0 {span / 1000!r}")
    for x in names:
        lines.append(f"meas tran OP_{re.sub('[^A-Za-z0-9]', '_', x)} find v({x}) at=0")
    for k, t in enumerate(times):
        for x in names:
            lines.append(f"meas tran S{k}_{re.sub('[^A-Za-z0-9]', '_', x)} find v({x}) at={t!r}")
    lines += ["quit", ".endc", ".end"]
    return "\n".join(lines) + "\n", names


def run(circuit: dict, expected: dict) -> dict:
    times = sorted({time_s(s["t"]) for s in expected.get("samples", [])})
    text, names = netlist(circuit, times)
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "fixture.cir"
        path.write_text(text)
        p = subprocess.run(["ngspice", "-b", str(path)], capture_output=True, text=True, timeout=300)
    out = p.stdout + p.stderr
    op, samples = {}, [{"t": f"{t * 1e6:.9g}us", "nets": {}} for t in times]
    safe = {re.sub("[^A-Za-z0-9]", "_", x).lower(): x for x in names}
    for m in re.finditer(r"^op_(\S+)\s*=\s*([-+0-9.eE]+)", out, re.M | re.I):
        op[safe[m.group(1).lower()]] = float(m.group(2))
    for m in re.finditer(r"^s(\d+)_(\S+)\s*=\s*([-+0-9.eE]+)", out, re.M | re.I):
        samples[int(m.group(1))]["nets"][safe[m.group(2).lower()]] = float(m.group(3))
    if len(op) != len(names) or any(len(s["nets"]) != len(names) for s in samples):
        raise RuntimeError(f"ngspice did not report every node:\n{out[-3000:]}")
    version = re.search(r"ngspice-(\d+)", subprocess.run(["ngspice", "-v"], capture_output=True, text=True).stdout)
    return {"generator": f"ngspice-{version.group(1) if version else '?'} (tools/analog/ngspice_fixture.py)",
            "netlist": text.splitlines(), "op": op, "samples": samples}


def close(a, b) -> bool:
    if isinstance(a, dict):
        return isinstance(b, dict) and a.keys() == b.keys() and all(close(a[k], b[k]) for k in a)
    if isinstance(a, list):
        return isinstance(b, list) and len(a) == len(b) and all(close(x, y) for x, y in zip(a, b))
    if isinstance(a, float):
        return isinstance(b, (int, float)) and abs(a - b) <= 1e-9 + 1e-6 * abs(a)
    return a == b


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()
    if not shutil.which("ngspice"):
        print("SKIP: ngspice is not installed")
        return 0
    bad = 0
    for d in sorted(FIXTURES.iterdir()):
        if not (d / "circuit.json").is_file():
            continue
        circuit = json.loads((d / "circuit.json").read_text())
        expected = json.loads((d / "expected.json").read_text())
        try:
            ref = run(circuit, expected)
        except Unsupported as e:
            print(f"skip  {d.name}: {e}")
            continue
        target = d / "ngspice.json"
        if args.check:
            old = json.loads(target.read_text()) if target.exists() else None
            same = old is not None and close(old.get("op"), ref["op"]) and close(old.get("samples"), ref["samples"])
            print(("ok    " if same else "STALE ") + d.name)
            bad += not same
        else:
            target.write_text(json.dumps(ref, indent=1) + "\n")
            print(f"wrote {target.relative_to(ROOT)}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
