#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Writes a schematic of N inverters in a chain for the workbench's
performance check (07 §2, M2: a 500-component schematic at 60 Hz).

Each inverter's output drives the next one's input through a drawn wire;
every VCC and GND pin has a short stub and a net label, as a real
schematic of this size would. A clock source drives the first input.

    python3 tools/perf/large_project.py 500 > /tmp/large/project.json
"""
import json
import sys

COLS, DX, DY = 25, 100, 80


def main() -> None:
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 500
    comps = [
        {"id": "VCC1", "component": "emblink/vcc", "params": {"voltage": 3.3}, "layout": {"x": 0, "y": 0}},
        {"id": "GND1", "component": "emblink/gnd", "layout": {"x": 40, "y": 0}},
        {"id": "CLK", "component": "emblink/clock_source", "layout": {"x": -100, "y": 100}},
    ]
    nets = {"VCC": ["VCC1.VCC"], "GND": ["GND1.GND"]}
    wires, labels = [], []
    for i in range(n):
        x, y = (i % COLS) * DX, 80 + (i // COLS) * DY
        cid = f"U{i + 1}"
        comps.append({"id": cid, "component": "emblink/inverter", "layout": {"x": x, "y": y}})
        nets["VCC"].append(f"{cid}.VCC")
        nets["GND"].append(f"{cid}.GND")
        wires.append({"net": "VCC", "points": [[x + 30, y], [x + 30, y - 10]]})
        wires.append({"net": "GND", "points": [[x + 30, y + 40], [x + 30, y + 50]]})
        labels.append({"net": "VCC", "x": x + 30, "y": y - 10})
        labels.append({"net": "GND", "x": x + 30, "y": y + 50})
        if i:
            px, py = ((i - 1) % COLS) * DX, 80 + ((i - 1) // COLS) * DY
            net = f"S{i}"
            nets[net] = [f"U{i}.Y", f"{cid}.A"]
            if py == y:
                wires.append({"net": net, "points": [[px + 60, py + 20], [x, y + 20]]})
            else:  # the next row: down, back to the left, down
                wires.append({"net": net, "points": [[px + 60, py + 20], [px + 80, py + 20], [px + 80, py + 65], [x - 20, py + 65], [x - 20, y + 20], [x, y + 20]]})
    nets["IN"] = ["CLK.OUT", "U1.A"]
    project = {
        "schema_version": 1,
        "kind": "project",
        "name": f"large-{n}",
        "description": f"{n} inverters in a chain: the workbench's performance check.",
        "policy": "functional",
        "components": comps,
        "nets": nets,
        "layout": {"wires": wires, "labels": labels, "sheet": {"width": COLS * DX + 200, "height": (n // COLS + 2) * DY + 200}},
    }
    json.dump(project, sys.stdout, indent=1)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
