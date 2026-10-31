#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""The Saleae Logic 8 on a UART line, read through instrument.read: a
terminal sends "Hi" at 9600 baud; the analyzer, sampling at 1 MS/s, holds
the frames' edges on its 1 us grid, and decoding them from the capture
alone gives the bytes back. A capture triggered on the line's first
falling edge starts with the start bit, and one at 100 MS/s with eight
channels on runs at 50 MS/s."""
import argparse
import json
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from embsim_client import StdioClient  # noqa: E402


def project(la_params):
    return {
        "schema_version": 1, "kind": "project", "name": "la", "policy": "functional",
        "components": [
            {"id": "TERM", "component": "emblink/virtual_terminal", "params": {"baud": 9600}},
            {"id": "LA", "component": "emblink/saleae_logic8", "params": la_params},
        ],
        "nets": {"TX": ["TERM.TX", "LA.D0"]},
    }


def read_all(client):
    since, edges, cap = 0, [], None
    while True:
        cap = client.call("instrument.read", {"part": "LA", "since": since})
        edges += cap["transitions"]
        if cap["next"] == since or not cap["transitions"]:
            break
        since = cap["next"]
    return cap, edges


def decode_uart(edges, rate, baud, channel=0):
    """8N1 from the transitions alone: each byte's start bit, then each bit sampled at its middle."""
    def level(sample):
        v = None
        for k, bits in edges:
            if k > sample:
                break
            v = (bits >> channel) & 1
        return v
    bit = rate / baud
    out, i = [], 0
    falls = [k for (k, b), (_, prev) in zip(edges[1:], edges) if not (b >> channel) & 1 and (prev >> channel) & 1]
    last_end = -1
    for f in falls:
        if f < last_end:
            continue
        byte = 0
        for n in range(8):
            byte |= level(f + bit * (n + 1.5)) << n
        assert level(f + bit * 9.5) == 1, "stop bit"
        out.append(byte)
        last_end = f + bit * 9.5
    return bytes(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--embsim", required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="embsim-la-") as tmp:
        src = Path(tmp) / "project.json"
        src.write_text(json.dumps(project({"sample_rate": "1meg", "duration": "5m"})), encoding="utf-8")
        with StdioClient([args.embsim, "sidecar", str(src)]) as client:
            client.call("control.advance", {"by": "1ms"})
            client.call("input.set", {"target": "LA", "input": "run", "value": True})
            client.call("input.set", {"target": "TERM", "input": "tx_text", "value": "Hi"})
            client.call("control.advance", {"by": "6ms"})
            cap, edges = read_all(client)
            assert cap["state"] == "done" and cap["sample_rate"] == 1e6, cap
            assert edges[0][0] == 0 and edges[0][1] & 1 == 1, edges[:2]  # the idle line, high, at the start
            # every edge on the 1 us grid, a bit (104.17 us) apart within a sample
            got = decode_uart(edges, 1e6, 9600)
            assert got == b"Hi", (got, edges[:12])
            # the start bit's fall: 1 ms in plus the terminal's first bit, on the grid
            start_ps = int(cap["start_ps"])
            assert start_ps == 1_000_000_000, cap["start_ps"]
            print(f"read {got!r} from {len(edges)} transitions at 1 MS/s")

        # triggered on the line's first fall, 10% before: the capture opens 0.5 ms before the start bit
        src.write_text(json.dumps(project({"sample_rate": "1meg", "duration": "5m", "trigger_channel": 0, "trigger_edge": "falling", "pretrigger": 0.1})), encoding="utf-8")
        with StdioClient([args.embsim, "sidecar", str(src)]) as client:
            client.call("input.set", {"target": "LA", "input": "run", "value": True})
            client.call("control.advance", {"by": "2ms"})
            assert client.call("instrument.read", {"part": "LA"})["state"] == "armed"
            client.call("input.set", {"target": "TERM", "input": "tx_text", "value": "Hi"})
            client.call("control.advance", {"by": "6ms"})
            cap, edges = read_all(client)
            assert cap["state"] == "done", cap
            # the capture starts 10% of its 5 ms before the trigger: the trigger is 500 samples in
            assert cap["trigger"] == 500 and edges[0][0] == 0, (edges[:3], cap["trigger"])
            assert decode_uart(edges, 1e6, 9600) == b"Hi"

        # 100 MS/s is for three channels at most
        src.write_text(json.dumps(project({"sample_rate": "100meg", "duration": "1m"})), encoding="utf-8")
        with StdioClient([args.embsim, "sidecar", str(src)]) as client:
            client.call("input.set", {"target": "LA", "input": "run", "value": True})
            client.call("control.advance", {"by": "10us"})
            assert client.call("instrument.read", {"part": "LA"})["sample_rate"] == 50e6
    print("PASS: the Logic 8 captured a UART line at 1 MS/s, decoded from its transitions; the trigger and the rate limit hold")
    return 0


if __name__ == "__main__":
    sys.exit(main())
