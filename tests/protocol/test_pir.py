#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""A PIR (HC-SR501) watching the world: a warm body moving in its field of
view and range raises OUT; a warm body standing still, or a moving body at
the room's temperature, does not; a person walking out of range lets OUT fall
a hold time later."""
import argparse
import json
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from embsim_client import StdioClient  # noqa: E402

PROJECT = {
    "schema_version": 1, "kind": "project", "name": "pir", "policy": "functional",
    "components": [
        # facing the back of the desk (its default sense direction), 3 m range, 1 s hold, no warm-up
        {"id": "PIR", "component": "emblink/pir_sensor", "params": {"warmup": 0, "hold": 1, "range": 3}},
    ],
    "nets": {"OUT": ["PIR.OUT"]},
}


def out(client, probe):
    return client.call("probe.read", {"probe": probe})["samples"][-1][1]


def body(client, id_, spec):
    client.call("input.set", {"target": f"world.{id_}", "input": "body", "value": spec})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--embsim", required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="embsim-pir-") as tmp:
        src = Path(tmp) / "project.json"
        src.write_text(json.dumps(PROJECT), encoding="utf-8")
        with StdioClient([args.embsim, "sidecar", str(src)]) as client:
            probe = client.call("probe.add", {"target": "net:OUT", "kind": "digital"})["probe"]
            client.call("control.advance", {"by": "300ms"})
            assert out(client, probe) in (0, "0"), "quiet room: OUT low"

            # a box sliding past, at the room's temperature: nothing in the infrared changes
            body(client, "box", {"shape": "box", "at": [-500, -1500, 300], "size": [300, 300, 300], "velocity_mm_s": [500, 0, 0]})
            client.call("control.advance", {"by": "500ms"})
            assert out(client, probe) in (0, "0"), "a cold moving box is not seen"
            body(client, "box", None)

            # a person standing 2 m away in front of it: warm, but still
            person = {"shape": "box", "at": [0, -2000, 900], "size": [450, 300, 1700], "warm": True}
            body(client, "person", person)
            client.call("control.advance", {"by": "500ms"})
            assert out(client, probe) in (0, "0"), "a warm body standing still is not seen"

            # the person walks across at 1 m/s: seen within one 100 ms look
            body(client, "person", dict(person, velocity_mm_s=[1000, 0, 0]))
            client.call("control.advance", {"by": "150ms"})
            assert out(client, probe) in (1, "1"), "a warm body walking across is seen"
            assert client.call("world.get")["bodies"][0]["warm"] is True

            # walking on, out of the 3 m range (about 2.2 m to the side): OUT falls 1 s after the last sight
            client.call("control.advance", {"by": "2500ms"})
            assert out(client, probe) in (1, "1"), "still within 1 s of the last sight"
            client.call("control.advance", {"by": "1000ms"})
            assert out(client, probe) in (0, "0"), "out of range, and the hold over: OUT low"
    print("PASS: the PIR sees a warm body walking, not a still one nor a cold moving one, and lets go a hold after it leaves")
    return 0


if __name__ == "__main__":
    sys.exit(main())
