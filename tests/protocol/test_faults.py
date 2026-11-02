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

Then the Uno blinking its L LED on D13 (examples/uno-blink-serial), with D13
shorted to ground by a fault: while it drives high it sources far more than
the ATmega328P's 40 mA absolute maximum, a pin.over_current warning; and 6 V
through 100 ohm into D3 (a cut wire mended), the pin clamped a diode drop
above VCC by its protection diodes, past VCC + 0.5 V: a pin.over_voltage
warning. Intact, neither is raised, the cut's leakage into D3 clamped.

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
        uno_pins(args.embsim, Path(tmp), check)
    print("faults: %s" % ("ok" if not failures else f"{failures} failure(s)"))
    return 1 if failures else 0


def uno_pins(embsim: str, tmp: Path, check) -> None:
    example = ROOT / "examples" / "uno-blink-serial"
    project = json.loads((example / "project.json").read_text(encoding="utf-8"))
    project["boards"][0]["firmware"] = {"image": str(example / "firmware" / "blink-serial.elf"), "format": "elf"}
    project.pop("scenarios", None)
    project["components"] += [
        {"id": "HI", "component": "emblink/vsource", "params": {"kind": "dc", "dc": 6}},
        {"id": "RHI", "component": "emblink/resistor", "params": {"resistance": 100}},
    ]
    project["nets"]["D13"] = ["B1.D13"]
    project["nets"]["SIX"] = ["HI.+", "RHI.1"]
    project["nets"]["D3"] = ["B1.D3", "RHI.2"]
    project["nets"]["GND"].append("HI.-")
    project["faults"] = [
        {"id": "SHORT", "kind": "short", "nets": ["D13", "GND"]},
        {"id": "CUT", "kind": "open", "pin": "RHI.2", "active": True},
    ]
    src = tmp / "uno.json"
    src.write_text(json.dumps(project), encoding="utf-8")
    with StdioClient([embsim, "sidecar", str(src)]) as c:
        def pins() -> str:
            got = c.call("diagnostics.list", {})
            items = got if isinstance(got, list) else next((v for v in got.values() if isinstance(v, list)), [])
            return json.dumps([d for d in items if str(d.get("code", "")).startswith("pin.over")])

        c.call("control.advance", {"until": str(int(1.2e12))})
        quiet = pins()
        check(quiet == "[]", f"the Uno intact: no pin past its ratings ({quiet[:200]})")
        c.call("input.set", {"target": "SHORT", "input": "active", "value": True})
        c.call("control.advance", {"until": str(int(2.4e12))})
        d = pins()
        check("pin.over_current" in d and "40 mA" in d, f"D13 shorted to ground: pin.over_current ({d[:240]})")
        c.call("input.set", {"target": "CUT", "input": "active", "value": False})
        c.call("control.advance", {"until": str(int(2.5e12))})
        d = pins()
        ov = [x for x in json.loads(d) if x["code"] == "pin.over_voltage"]
        check(len(ov) == 1 and "5.5 V" in ov[0]["message"] and "mA" in ov[0]["message"],
              f"6 V through 100 ohm into D3: pin.over_voltage ({ov[0]['message'] if ov else d[:240]})")
        v = float(c.call("observe.snapshot", {"include": ["nets"]})["nets"]["D3"]["voltage"])
        check(5.5 < v < 5.8, f"D3 held a diode drop above VCC by its protection diodes: {v:.2f} V")


if __name__ == "__main__":
    sys.exit(main())
