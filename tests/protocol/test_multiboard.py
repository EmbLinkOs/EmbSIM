#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Two boards in one session (M4's exit criterion): examples/two-boards-uart,
an Arduino Uno's AVR and a Blue Pill's Cortex-M3 talking over one serial
line, run twice. Both runs must pass their scenario and agree exactly: the
same events and analog steps, the same text each MCU sent, the same nets
and outputs at the end; what wall time a run took is the only difference
allowed."""
import argparse
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EX = ROOT / "examples" / "two-boards-uart"


def run(embsim: str) -> dict:
    r = subprocess.run([embsim, "run", str(EX / "project.json"), "--scenario", str(EX / "scenarios" / "exchange.json"),
                        "--json"], capture_output=True, text=True, timeout=600)
    try:
        out = json.loads(r.stdout)
    except json.JSONDecodeError:
        print(r.stdout[-2000:], r.stderr[-2000:])
        raise
    out["statistics"].pop("wall_time_s", None)
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--embsim", required=True)
    args = ap.parse_args()
    a, b = run(args.embsim), run(args.embsim)
    failures = 0
    for name, r in (("first", a), ("second", b)):
        if not r.get("ok"):
            print(f"FAIL  the {name} run: {r.get('assertions')} {r.get('ended')}")
            failures += 1
    outs = a["snapshot"]["outputs"]
    for board, want in (("B1", "ok 3"), ("B2", "PING 3")):
        text = json.dumps({k: v for k, v in outs.items() if k.startswith(board + ".")})
        if want not in text:
            print(f"FAIL  {board} shows no third round trip ({want!r})")
            failures += 1
    for key in ("statistics", "assertions"):
        if a[key] != b[key]:
            print(f"FAIL  the runs differ in {key}:\n{a[key]}\n{b[key]}")
            failures += 1
    if a["snapshot"]["nets"] != b["snapshot"]["nets"] or a["snapshot"]["outputs"] != b["snapshot"]["outputs"]:
        print("FAIL  the runs end in different states")
        failures += 1
    if not failures:
        print(f"ok    two runs agree: {a['statistics']['events']} events, {a['statistics']['analog_steps']} analog steps")
    print(f"{failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
