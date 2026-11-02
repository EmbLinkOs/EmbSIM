#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""The fault mode and damage (ADR-034), through the session protocol.

A red LED behind 220 ohm from 5 V, with three faults declared in the project:
F1 cuts the LED's anode, F2 bridges the LED (a solder bridge), F3 shorts the
resistor (a failed part). Each is switched by its `active` input while the
session runs, as a scenario, the workbench or a script would:
- intact, the LED carries about 14 mA;
- F1 active: no current; cleared: lit again;
- F2 active: the LED dark, the resistor alone across the supply;
- F3 active: the resistor gone, the LED across 5 V: it burns out (open),
  and stays dead with F3 cleared, until it is replaced.
The project as the session holds it keeps its `faults` list, not the switches.

    test_faults.py --embsim PATH
"""
import argparse
import json
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from embsim_client import StdioClient  # noqa: E402

PROJECT = {
    "schema_version": 1,
    "kind": "project",
    "name": "faults",
    "policy": "electrical",
    "components": [
        {"id": "SUP", "component": "emblink/vsource", "params": {"kind": "dc", "dc": 5}},
        {"id": "R1", "component": "emblink/resistor", "params": {"resistance": 220, "tolerance": 0}},
        {"id": "D1", "component": "emblink/led"},
        {"id": "G", "component": "emblink/gnd"},
    ],
    "nets": {"P": ["SUP.+", "R1.1"], "A": ["R1.2", "D1.A"], "GND": ["D1.K", "SUP.-", "G.GND"]},
    "faults": [
        {"id": "F1", "kind": "open", "pin": "D1.A"},
        {"id": "F2", "kind": "short", "nets": ["A", "GND"]},
        {"id": "F3", "kind": "fail_short", "part": "R1"},
    ],
}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--embsim", required=True)
    args = ap.parse_args()
    failures = 0

    def check(ok: bool, what: str) -> None:
        nonlocal failures
        print(("ok    " if ok else "FAIL  ") + what)
        failures += 0 if ok else 1

    with tempfile.TemporaryDirectory(prefix="embsim-faults-") as tmp:
        src = Path(tmp) / "project.json"
        src.write_text(json.dumps(PROJECT), encoding="utf-8")
        with StdioClient([args.embsim, "sidecar", str(src)]) as c:
            now = [0]

            def step(ms: float) -> dict:
                now[0] += int(ms * 1e9)
                c.call("control.advance", {"until": str(now[0])})
                return c.call("observe.snapshot", {"include": ["outputs"]})["outputs"]

            def led() -> float:
                return float(step(2)["D1"]["current"])

            i = led()
            check(0.012 < i < 0.016, f"intact: the LED carries {i * 1e3:.2f} mA")
            c.call("input.set", {"target": "F1", "input": "active", "value": True})
            i = led()
            check(abs(i) < 1e-6, f"F1 (the anode cut): {i * 1e3:.3f} mA")
            c.call("input.set", {"target": "F1", "input": "active", "value": False})
            i = led()
            check(0.012 < i < 0.016, f"F1 cleared: lit again, {i * 1e3:.2f} mA")
            c.call("input.set", {"target": "F2", "input": "active", "value": True})
            out = step(2)
            check(abs(float(out["D1"]["current"])) < 1e-6 and abs(float(out["R1"]["i"]) - 5 / 220) < 1e-3,
                  f"F2 (the LED bridged): dark, the resistor passes {float(out['R1']['i']) * 1e3:.1f} mA")
            c.call("input.set", {"target": "F2", "input": "active", "value": False})
            c.call("input.set", {"target": "F3", "input": "active", "value": True})
            out = step(50)
            check(bool(out["D1"]["damaged"]), "F3 (the resistor shorted): the LED across 5 V burns out")
            c.call("input.set", {"target": "F3", "input": "active", "value": False})
            out = step(5)
            check(bool(out["D1"]["damaged"]) and abs(float(out["D1"]["current"])) < 1e-6,
                  "F3 cleared: the LED stays dead")
            c.call("input.set", {"target": "D1", "input": "replace", "value": True})
            i = led()
            check(0.012 < i < 0.016, f"replaced: the new LED lights, {i * 1e3:.2f} mA")
            diags = json.dumps(c.call("diagnostics.list", {}))
            check("part.damaged" in diags, "the burn-out is a diagnostic (part.damaged)")
            proj = c.call("project.get", {})
            proj = proj.get("project", proj)
            check(len(proj.get("faults", [])) == 3 and not any(x["id"].startswith("F") for x in proj["components"]),
                  "the project keeps its faults list, not the switches")
    print("faults: %s" % ("ok" if not failures else f"{failures} failure(s)"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
