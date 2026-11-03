#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""The Rigol DS1054Z on an RC network, read through instrument.read as its
screen shows it: a 0-to-3.3 V step through 10 kOhm into 10 nF (a 100 us
time constant), channel 1 on the step, channel 2 on the capacitor, the edge
trigger on channel 1 rising at 1.65 V, 50 us/div. The capacitor's trace
reaches 63.2 % of 3.3 V one time constant after the trigger, its points are
8-bit codes (multiples of a 25th of a division), and the trigger sits at the
screen's centre."""
import argparse
import json
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from embsim_client import StdioClient  # noqa: E402

PROJECT = {
    "schema_version": 1, "kind": "project", "name": "scope-rc", "policy": "electrical",
    "components": [
        {"id": "STEP", "component": "emblink/pulse_source", "params": {"v1": 0, "v2": 3.3, "td": "1m", "tr": "10n", "tf": "10n", "pw": "5m", "per": "10m"}},
        {"id": "R", "component": "emblink/resistor", "params": {"resistance": 10000}},
        {"id": "C", "component": "emblink/capacitor", "params": {"capacitance": "10n"}},
        {"id": "SCOPE", "component": "emblink/rigol_ds1054z", "params": {
            "timebase": "50u", "trig_source": 1, "trig_level": 1.65, "sweep": "single",
            "ch1_scale": 1, "ch2_on": True, "ch2_scale": 0.5, "ch2_offset": -1.5}},
        {"id": "G", "component": "emblink/gnd"},
    ],
    "nets": {
        "IN": ["STEP.+", "R.1", "SCOPE.CH1"],
        "OUT": ["R.2", "C.1", "SCOPE.CH2"],
        "GND": ["STEP.-", "C.2", "SCOPE.GND", "G.GND"],
    },
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--embsim", required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="embsim-scope-") as tmp:
        src = Path(tmp) / "project.json"
        src.write_text(json.dumps(PROJECT), encoding="utf-8")
        with StdioClient([args.embsim, "sidecar", str(src)]) as client:
            client.call("control.advance", {"by": "3ms"})
            acq = client.call("instrument.read", {"part": "SCOPE"})
            assert acq["kind"] == "scope" and acq["seq"] == 1 and acq["status"] == "STOP", {k: acq[k] for k in ("kind", "seq", "status")}
            trig, x0, dx = int(acq["trigger_ps"]), int(acq["x0_ps"]), int(acq["dx_ps"])
            # the step's edge at 1 ms, crossing 1.65 V halfway up its 10 ns rise
            assert abs(trig - 1_000_005_000) < 1000, trig
            # the trigger at the centre of 12 divisions of 50 us
            assert x0 == trig - 300_000_000 and dx == 600_000_000 // 1200, (x0, dx)
            assert acq["sample_rate"] == 1e9 / 2, acq["sample_rate"]  # two channels on
            ch1, ch2 = acq["channels"][0]["points"], acq["channels"][1]["points"]
            # 8-bit codes: channel 2 at 0.5 V/div is in steps of 20 mV about its -1.5 V offset
            assert all(abs(((v + -1.5) / 0.02) - round((v + -1.5) / 0.02)) < 1e-3 for v in ch2), ch2[:5]
            assert ch1[0] == 0 and abs(ch1[-1] - 3.28) < 0.05, (ch1[0], ch1[-1])  # 1 V/div: 40 mV codes
            # one time constant (100 us) after the trigger the capacitor is at 63.2 % of 3.3 V
            def at(t_ps):
                return ch2[round((t_ps - x0) / dx)]
            v_tau = at(trig + 100_000_000)
            assert abs(v_tau - 3.3 * 0.632) < 0.03, v_tau
            v_2tau = at(trig + 200_000_000)
            assert abs(v_2tau - 3.3 * 0.8647) < 0.03, v_2tau
            # the time to 63.2 %, from the trace alone
            t63 = next(x0 + i * dx for i, v in enumerate(ch2) if x0 + i * dx >= trig and v >= 3.3 * 0.632)
            tau_us = (t63 - trig) / 1e6
            assert 95 <= tau_us <= 106, tau_us
            # a second Single after re-arming acquires again
            client.call("input.set", {"target": "SCOPE", "input": "run", "value": False})
            client.call("input.set", {"target": "SCOPE", "input": "run", "value": True})
            client.call("control.advance", {"by": "10ms"})
            again = client.call("instrument.read", {"part": "SCOPE", "since": 1})
            assert again["seq"] == 2 and abs(int(again["trigger_ps"]) - 11_000_005_000) < 1000, {k: again.get(k) for k in ("seq", "trigger_ps", "status")}
            print(f"tau measured {tau_us:.1f} us (100 us expected); at tau {v_tau:.3f} V, at 2 tau {v_2tau:.3f} V; 500 MSa/s, 8-bit codes")
    print("PASS: the DS1054Z triggered on the step and measured the RC's time constant from its trace")
    return 0


if __name__ == "__main__":
    sys.exit(main())
