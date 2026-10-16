#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""The importers end to end, through the engine: the DeviceTree draft
validates (instantiated and its fixture run), the RTOS draft is invalid
only for its todo placeholder, and the KiCad flasher blinks at the 555's
frequency."""
import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
IMP = ROOT / "tools" / "importers"


def run(cmd, ok=True):
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
    if ok and p.returncode != 0:
        print(f"FAIL: {' '.join(map(str, cmd))}\n{p.stdout}\n{p.stderr}")
        sys.exit(1)
    return p


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--embsim", required=True)
    e = ap.parse_args().embsim
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        run([sys.executable, IMP / "dts2board.py", IMP / "testdata/f103_dev.dts", "--library", "imported", "--out", tmp / "dts"])
        rep = json.loads(run([e, "catalog", "validate", tmp / "dts", "--json"]).stdout)
        assert rep["summary"]["ok"] == 1 and rep["summary"]["fixtures"] == 1, rep["summary"]
        try:
            import yaml  # noqa: F401
            run([sys.executable, IMP / "rtos2board.py", IMP / "testdata/rtos_f103_board.yaml", "--library", "imported", "--out", tmp / "rtos"])
            rep = json.loads(run([e, "catalog", "validate", tmp / "rtos", "--json"], ok=False).stdout)
            item = rep["items"][0]
            assert item["status"] == "invalid" and all("catalog.todo" in p for p in item["problems"]), item
        except ImportError:
            print("SKIP: rtos2board needs PyYAML")
        run([sys.executable, IMP / "kicad2project.py", IMP / "testdata/flasher.net", "-o", tmp / "kicad/project.json"])
        proj = json.loads((tmp / "kicad/project.json").read_text())
        proj["probes"] = [{"id": "out", "target": "net:OUT", "kind": "digital"}]
        (tmp / "kicad/project.json").write_text(json.dumps(proj))
        (tmp / "kicad/blink.json").write_text(json.dumps({
            "schema_version": 1, "kind": "scenario", "name": "blink", "until": "1s",
            "assertions": [{"id": "freq", "between": ["0.2s", "1s"], "probe": "out", "expect": {"frequency_hz": 6.86, "tolerance": 0.1}}]}))
        rep = json.loads(run([e, "run", tmp / "kicad/project.json", "--scenario", tmp / "kicad/blink.json", "--json"]).stdout)
        assert all(a["ok"] for a in rep["assertions"]), rep["assertions"]
    print("PASS: DeviceTree, RTOS board YAML and KiCad importers")
    return 0


if __name__ == "__main__":
    sys.exit(main())
