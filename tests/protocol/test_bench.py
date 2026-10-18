#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""The bench (SPEC-007 §2): places, jumpers and notes persist through
transactions, save and reload, without restarting the simulation; removing
a part removes its place and the jumpers to it; a strip joins what is
plugged into it."""
import argparse
import json
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from embsim_client import StdioClient, EmbsimError  # noqa: E402

PROJECT = {
    "schema_version": 1, "kind": "project", "name": "bench", "policy": "electrical",
    "components": [
        {"id": "V1", "component": "emblink/vsource", "params": {"kind": "dc", "dc": 5}},
        {"id": "BB1", "component": "emblink/breadboard_830"},
        {"id": "R1", "component": "emblink/resistor", "params": {"resistance": "1k"}},
        {"id": "R2", "component": "emblink/resistor", "params": {"resistance": "1k"}},
        {"id": "G", "component": "emblink/gnd"},
    ],
    # R1 from a3 to a8, R2 from c8 to the ground rail: a divider through strip 8
    "nets": {
        "IN": ["V1.+", "BB1.TP1"],
        "S3": ["BB1.a3", "R1.1"],
        "MID": ["BB1.a8", "R1.2"],
        "LOW": ["BB1.c8", "R2.1"],
        "RET": ["BB1.TN12", "R2.2"],
        "GND": ["V1.-", "G.GND", "BB1.TN1"],
        "J1": ["BB1.TP2", "BB1.e3"],
    },
}

BENCH = {
    "parts": {"BB1": {"x": 100, "y": 60}, "R1": {"x": 51.2, "y": 30.7, "rotation": 0}, "R2": {"x": 60, "y": 40, "rotation": 90}},
    "wires": [{"from": "BB1.TP2", "to": "BB1.e3", "color": "#d63031"}, {"from": "R2.2", "to": "BB1.TN12"}],
    "notes": [{"x": 10, "y": 5, "text": "A divider on strip 8", "to": [40, 30]}],
}


def edit(client, edits):
    tx = client.call("project.transaction", {"op": "begin"})["tx"]
    try:
        client.call("project.transaction", {"op": "edit", "tx": tx, "edits": edits})
    except EmbsimError:
        client.call("project.transaction", {"op": "abort", "tx": tx})
        raise
    return client.call("project.transaction", {"op": "commit", "tx": tx})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--embsim", required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="embsim-bench-") as tmp:
        src = Path(tmp) / "project.json"
        src.write_text(json.dumps(PROJECT), encoding="utf-8")
        saved = Path(tmp) / "saved.json"
        with StdioClient([args.embsim, "sidecar", str(src)]) as client:
            probe = client.call("probe.add", {"target": "net:MID", "kind": "analog"})["probe"]
            client.call("control.advance", {"by": "1ms"})
            # 5 V through the + rail, a jumper, strip 3, R1, strip 8 and R2 to the - rail
            mid = client.call("probe.read", {"probe": probe})["samples"][-1][1]
            assert abs(mid - 2.5) < 1e-6, mid
            now = client.call("session.info")["now_ps"]
            result = edit(client, [{"set_layout": {"bench": BENCH}}])
            assert result["applied"] and not result["restarted"], result
            assert client.call("session.info")["now_ps"] == now
            try:
                edit(client, [{"set_layout": {"bench": []}}])
                raise AssertionError("a bench that is not an object was accepted")
            except EmbsimError:
                pass
            client.call("project.save", {"path": str(saved)})
            persisted = json.loads(saved.read_text(encoding="utf-8"))
            assert persisted["layout"]["bench"] == BENCH, persisted["layout"]
            edit(client, [{"remove_component": {"id": "R2"}}])
            bench = client.call("project.get")["project"]["layout"]["bench"]
            assert "R2" not in bench["parts"] and bench["parts"]["R1"] == BENCH["parts"]["R1"]
            assert bench["wires"] == [BENCH["wires"][0]], bench["wires"]
            assert bench["notes"] == BENCH["notes"]
            client.call("project.load", {"json": persisted, "base_dir": tmp})
            assert client.call("project.get")["project"]["layout"]["bench"] == BENCH
        print("PASS: the bench persists without a restart; removing a part prunes its place and jumpers")


if __name__ == "__main__":
    main()
