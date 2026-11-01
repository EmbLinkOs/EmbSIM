#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""The Nordic Power Profiler Kit II following a board that sleeps and wakes,
read through instrument.read as the Power Profiler app reads it: the PPK2
powers the load at 3.3 V; 1 mA flows always (3.3 kOhm, the board asleep)
and 100 mA more for 1 ms every 10 ms (33 Ohm through a MOSFET, the board
awake), the MOSFET's gate on a 100 Hz, 10 % clock also wired to the logic
port's D0. At 100 kS/s the samples show both levels, each at its range's
resolution, 1 ms bursts every 10 ms, an average of 11 mA, and D0's edges
where the current steps."""
import argparse
import json
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from embsim_client import StdioClient  # noqa: E402

PROJECT = {
    "schema_version": 1, "kind": "project", "name": "ppk2", "policy": "electrical",
    # exact parts: the check is the engine's and the instrument's numbers, not a resistor's spread (ADR-034)
    "settings": {"imperfections": "ideal"},
    "components": [
        {"id": "PPK", "component": "emblink/nordic_ppk2", "params": {"mode": "source", "voltage": 3.3}},
        {"id": "SLEEP", "component": "emblink/resistor", "params": {"resistance": 3300}},
        {"id": "AWAKE", "component": "emblink/resistor", "params": {"resistance": 33}},
        {"id": "Q", "component": "emblink/nmos", "params": {"vto": 1.0, "kp": "10"}},
        {"id": "CLK", "component": "emblink/clock_source", "params": {"freq": "100", "duty": 0.1}},
        {"id": "G", "component": "emblink/gnd"},
    ],
    "nets": {
        "VDD": ["PPK.VOUT", "SLEEP.1", "AWAKE.1"],
        "SW": ["AWAKE.2", "Q.D"],
        "GATE": ["CLK.OUT", "Q.G", "PPK.D0"],
        "GND": ["PPK.GND", "SLEEP.2", "Q.S", "G.GND"],
    },
}


def read_all(client, part, since=0):
    out = []
    while True:
        r = client.call("instrument.read", {"part": part, "since": since})
        out += r.get("samples", r.get("transitions", []))
        if r["next"] == since:
            return r, out
        since = r["next"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--embsim", required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="embsim-ppk2-") as tmp:
        src = Path(tmp) / "project.json"
        src.write_text(json.dumps(PROJECT), encoding="utf-8")
        with StdioClient([args.embsim, "sidecar", str(src)]) as client:
            for member in ("PPK.METER", "PPK.LOGIC"):
                client.call("input.set", {"target": member, "input": "run", "value": True})
            client.call("control.advance", {"by": "50ms"})
            meter, samples = read_all(client, "PPK.METER")
            assert meter["kind"] == "power" and meter["sample_rate"] == 100000 and meter["period_ps"] == "10000000", meter
            assert 4999 <= len(samples) <= 5000, len(samples)  # 50 ms at 100 kS/s
            sleep = [s for s in samples if s < 0.05]
            awake = [s for s in samples if s > 0.05]
            # asleep: 1 mA on the 0.5-5 mA range, at its 5 uA resolution; awake: ~101 mA on the
            # 50-1000 mA range, at its 1 mA resolution. A sample whose 10 us takes in a switching
            # edge averages the two (as the real PPK2's do): at most two such a burst
            asleep = [s for s in sleep if abs(s - 0.001) < 2.6e-6]
            woke = [s for s in awake if abs(s - 0.101) <= 0.0015]
            assert all(abs(x * 200000 - round(x * 200000)) < 1e-3 for x in sleep), "5 uA steps"
            assert all(abs(x * 1000 - round(x * 1000)) < 1e-4 for x in awake), "1 mA steps"  # samples travel as 32-bit floats
            edges_seen = len(samples) - len(asleep) - len(woke)
            assert edges_seen <= 2 * 2 * 5, (edges_seen, sorted(set(samples) - set(asleep) - set(woke))[:6])
            duty = len(awake) / len(samples)
            assert 0.09 <= duty <= 0.11, duty  # 1 ms in each 10 ms
            avg = sum(samples) / len(samples)
            assert 0.0105 <= avg <= 0.0115, avg  # 1 mA + 10 % of 100 mA
            logic, edges = read_all(client, "PPK.LOGIC")
            assert logic["kind"] == "logic" and logic["sample_rate"] == 100000, logic
            rises = [k for (k, b), (_, p) in zip(edges[1:], edges) if b & 1 and not p & 1]
            assert 4 <= len(rises) <= 5, edges[:8]
            # each rise of D0 is where the current steps up, on the same 10 us grid: D0's sample k is
            # the first at or after the edge, so the current's window holding the edge is the one
            # just before it; the one before that is still asleep
            start = int(logic["start_ps"]) // 10_000_000 - int(meter["start_ps"]) // 10_000_000
            for k in rises:
                i = k + start
                assert samples[i - 2] < 0.05 < samples[i], (k, samples[i - 2:i + 3])
            print(f"{len(samples)} samples: asleep {asleep[0]*1e3:.3f} mA, awake {woke[0]*1e3:.0f} mA, {edges_seen} on edges, duty {duty:.1%}, average {avg*1e3:.2f} mA; D0 rose {len(rises)} times with the current")
    print("PASS: the PPK2 followed a board sleeping and waking at 100 kS/s, each sample at its range's resolution, D0 in step")
    return 0


if __name__ == "__main__":
    sys.exit(main())
