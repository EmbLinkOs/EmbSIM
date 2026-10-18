#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""serial.data (SPEC-008 §6): a client hears what a serial probe receives as
the run goes, not only by reading the probe. examples/bluepill-button-led
over `embsim sidecar`: its banner arrives on both serial probes (the MCU's
console and the virtual terminal) as serial.data notifications whose bytes,
joined, are the probe's text; a later advance sends only what is new, and a
reset starts the stream again."""
import argparse
import base64
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from embsim_client import StdioClient  # noqa: E402

BANNER = b"EmbSIM Blue Pill demo\r\n"


def received(c: StdioClient) -> dict[str, bytes]:
    c.poll(0.5)
    got: dict[str, bytes] = {}
    for n in c.notifications():
        if n.method == "serial.data":
            got[n.params["probe"]] = got.get(n.params["probe"], b"") + base64.b64decode(n.params["base64"])
    return got


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--embsim", required=True)
    args = ap.parse_args()
    failures = 0

    def check(ok: bool, what: str) -> None:
        nonlocal failures
        print(("ok    " if ok else "FAIL  ") + what)
        failures += 0 if ok else 1

    with StdioClient([args.embsim, "sidecar", str(ROOT / "examples" / "bluepill-button-led" / "project.json")]) as c:
        c.call("session.open", {"client": "test_serial_stream", "protocol": "1.0"})
        c.call("control.advance", {"by": "20ms"})
        got = received(c)
        check(got.get("console") == BANNER, f"the MCU's console streams its banner ({got.get('console')!r})")
        check(got.get("terminal") == BANNER, f"the virtual terminal streams it too ({got.get('terminal')!r})")
        text = c.call("probe.read", {"probe": "terminal"}).get("text", "")
        check(text.encode() == got.get("terminal"), "the streamed bytes are the probe's text")
        c.call("control.advance", {"by": "20ms"})
        check("terminal" not in received(c), "nothing new, nothing sent")
        c.call("control.reset", {})
        c.call("control.advance", {"by": "20ms"})
        check(received(c).get("terminal") == BANNER, "after a reset the stream starts again")
    print(f"{failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
