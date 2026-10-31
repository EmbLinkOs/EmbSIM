#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""The world (ADR-026): an LDR in a divider follows the room's light; a
project without the field keeps the part's default; set_world changes the
room without a restart, as an input that is recorded; a part's own input
overrides the world; bodies come and go; world.get reports it all."""
import argparse
import json
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from embsim_client import StdioClient, EmbsimError  # noqa: E402

PROJECT = {
    "schema_version": 1, "kind": "project", "name": "world", "policy": "electrical",
    "components": [
        {"id": "V1", "component": "emblink/vsource", "params": {"kind": "dc", "dc": 5}},
        {"id": "LDR1", "component": "emblink/gl5528"},
        {"id": "R1", "component": "emblink/resistor", "params": {"resistance": "10k"}},
        {"id": "G", "component": "emblink/gnd"},
    ],
    # the LDR from 5 V to OUT, 10 kOhm from OUT to ground: brighter, higher
    "nets": {"VIN": ["V1.+", "LDR1.1"], "OUT": ["LDR1.2", "R1.1"], "GND": ["V1.-", "R1.2", "G.GND"]},
}


def edit(client, edits):
    tx = client.call("project.transaction", {"op": "begin"})["tx"]
    try:
        client.call("project.transaction", {"op": "edit", "tx": tx, "edits": edits})
    except EmbsimError:
        client.call("project.transaction", {"op": "abort", "tx": tx})
        raise
    return client.call("project.transaction", {"op": "commit", "tx": tx})


def out_v(client, probe):
    return client.call("probe.read", {"probe": probe})["samples"][-1][1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--embsim", required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="embsim-world-") as tmp:
        src = Path(tmp) / "project.json"
        src.write_text(json.dumps(PROJECT), encoding="utf-8")
        with StdioClient([args.embsim, "sidecar", str(src)]) as client:
            probe = client.call("probe.add", {"target": "net:OUT", "kind": "analog"})["probe"]
            client.call("control.advance", {"by": "1ms"})
            # no world declared: the LDR at its own default, 10 lx
            dark = out_v(client, probe)
            world = client.call("world.get")
            assert world["fields"]["illuminance"]["set"] is False, world
            ldr = next(i for i in client.call("input.list")["inputs"] if i["target"] == "LDR1")
            assert ldr["world"] == "illuminance" and ldr["value"] == 10, ldr

            # the room lit to 1000 lx: the LDR's resistance falls, OUT rises
            client.call("input.set", {"target": "world", "input": "illuminance", "value": 1000})
            client.call("control.advance", {"by": "1ms"})
            lit = out_v(client, probe)
            assert lit > dark + 1.0, (dark, lit)
            ldr = next(i for i in client.call("input.list")["inputs"] if i["target"] == "LDR1")
            assert ldr["value"] == 1000 and ldr.get("source") == "world", ldr

            # the part's own value overrides the world
            client.call("input.set", {"target": "LDR1", "input": "illuminance", "value": 10})
            client.call("control.advance", {"by": "1ms"})
            assert abs(out_v(client, probe) - dark) < 1e-6
            client.call("input.set", {"target": "world", "input": "illuminance", "value": 5000})
            client.call("control.advance", {"by": "1ms"})
            assert abs(out_v(client, probe) - dark) < 1e-6

            # bad world inputs are refused
            for target, name, value in (("world", "colour", 1), ("world", "temperature", "warm"), ("world.box", "at", [0, 0, 0])):
                try:
                    client.call("input.set", {"target": target, "input": name, "value": value})
                    raise AssertionError(f"{target}.{name} was accepted")
                except EmbsimError:
                    pass

            # bodies
            client.call("input.set", {"target": "world.box", "input": "body", "value": {"shape": "box", "at": [0, -300, 50], "size": [100, 100, 100]}})
            assert [b["id"] for b in client.call("world.get")["bodies"]] == ["box"]
            client.call("input.set", {"target": "world.box", "input": "body", "value": None})
            assert client.call("world.get")["bodies"] == []

            # a moving body: where it is follows virtual time; with a travel it goes back and forth
            client.call("input.set", {"target": "world.cart", "input": "body", "value": {"shape": "box", "at": [0, -300, 50], "size": [100, 100, 100], "velocity_mm_s": [0, 100, 0], "travel_mm": 200}})
            client.call("control.advance", {"by": "1s"})
            cart = client.call("world.get")["bodies"][0]
            assert abs(cart["at"][1] - (-200)) < 1e-6, cart  # 100 mm/s for 1 s
            assert cart["velocity_mm_s"] == [0, 100, 0] and cart["travel_mm"] == 200, cart
            client.call("control.advance", {"by": "2s"})
            cart = client.call("world.get")["bodies"][0]
            assert abs(cart["at"][1] - (-200)) < 1e-6, cart  # out 200 mm, then 100 back
            client.call("input.set", {"target": "world.cart", "input": "body", "value": None})

            # a project's moving body runs on the project's timeline (from 0), even when added later,
            # as the bench draws it: 100 mm/s since 0 has carried it 100 mm a second
            edit(client, [{"set_world": {"bodies": [{"id": "belt", "at": [0, -600, 50], "size": [50, 50, 50], "velocity_mm_s": [0, 100, 0]}]}}])
            belt = [b for b in client.call("world.get")["bodies"] if b["id"] == "belt"][0]
            elapsed = int(client.call("session.info")["now_ps"]) / 1e12
            assert abs(belt["at"][1] - (-600 + 100 * elapsed)) < 1e-6, (belt, elapsed)
            edit(client, [{"set_world": {"bodies": []}}])

            # set_world: the project's room changed without a restart, the change recorded as an input
            now = client.call("session.info")["now_ps"]
            result = edit(client, [{"set_world": {"fields": {"temperature": 30, "humidity": 40}, "bodies": [{"id": "wall", "at": [0, -500, 100], "size": [1000, 10, 200]}]}}])
            assert result["applied"] and not result["restarted"], result
            assert client.call("session.info")["now_ps"] == now
            world = client.call("world.get")
            assert world["fields"]["temperature"] == {"value": 30, "unit": "degC", "set": True}, world["fields"]["temperature"]
            assert world["bodies"][0]["id"] == "wall"
            saved = Path(tmp) / "saved.json"
            client.call("project.save", {"path": str(saved)})
            assert json.loads(saved.read_text(encoding="utf-8"))["world"]["fields"] == {"temperature": 30, "humidity": 40}

            # the room and the sun (the 3D bench's presentation) are kept with the project, nothing restarts
            result = edit(client, [{"set_world": {"room": {"size_mm": [6000, 5000, 3000], "window_azimuth_deg": 180}, "sun": {"latitude_deg": 48.1, "date": "06-21", "solar_time_h": 9.5}}}])
            assert result["applied"] and not result["restarted"], result
            world_spec = client.call("project.get")["project"]["world"]
            assert world_spec["room"] == {"size_mm": [6000, 5000, 3000], "window_azimuth_deg": 180}, world_spec
            assert world_spec["sun"]["solar_time_h"] == 9.5 and world_spec["fields"]["temperature"] == 30
            edit(client, [{"set_world": {"sun": {}}}])
            assert "sun" not in client.call("project.get")["project"]["world"]
            try:
                edit(client, [{"set_world": {"room": [6000, 5000, 3000]}}])
                raise AssertionError("set_world took a room that is not an object")
            except EmbsimError as e:
                assert "room is an object" in str(e), e

            # a reset starts the room again as the project declares it
            client.call("input.set", {"target": "world", "input": "temperature", "value": -10})
            client.call("control.reset")
            assert client.call("world.get")["fields"]["temperature"]["value"] == 30
        print("PASS: the world drives an LDR, a part's own value overrides it, set_world applies live (room and sun kept), reset restores the project's room")


if __name__ == "__main__":
    main()
