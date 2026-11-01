#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""The Uno's serial port on the host (ADR-031, ADR-032): `port.open` makes
the board's ATmega16U2 bridge a pseudo-terminal, and a program on the host
talks to the running sketch through it, paced at real time.

tests/firmware/uno-echo prints "boot" and MCUSR at reset, then echoes what it
receives in upper case, at 9600 baud. As on a real Uno:
- what the sketch sends while no program has the port open is lost;
- opening the port (DTR raised) resets the ATmega328P: "boot 02", the
  external reset flag;
- at the sketch's baud rate the host reads its echo; at another rate both
  directions are garbage;
- closing the port does not reset; reopening it does;
- the host's bytes are recorded inputs (record.input), so a replay needs no
  host.
The NUCLEO-F446RE's ST-LINK/V2-1 is a bridge too, without the auto-reset:
opening its virtual COM port leaves the STM32 running, and the example's
firmware echoes at 115200 baud.
POSIX only: Windows has no pseudo-terminals (Q16)."""
import argparse
import json
import os
import select
import shutil
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from embsim_client import StdioClient  # noqa: E402

PROJECT = {
    "schema_version": 1,
    "kind": "project",
    "name": "host-serial",
    "boards": [{"id": "B1", "board": "emblink/arduino_uno",
                "firmware": {"image": "uno-echo.elf", "format": "elf"}}],
    "components": [],
    "nets": [],
}


def read_for(fd: int, seconds: float, until: bytes = b"") -> bytes:
    """What the port gives within `seconds` (sooner once `until` is seen)."""
    got = b""
    end = time.monotonic() + seconds
    while time.monotonic() < end and not (until and until in got):
        r, _, _ = select.select([fd], [], [], 0.05)
        if r:
            try:
                got += os.read(fd, 4096)
            except BlockingIOError:
                pass
    return got


def wait_for(cond, seconds: float = 15.0) -> bool:
    """`cond()` true within `seconds` (a loaded machine runs the session slowly)."""
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if cond():
            return True
        time.sleep(0.05)
    return cond()


def open_port(path: str, baud: int) -> int:
    import termios
    import tty
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    tty.setraw(fd)
    attrs = termios.tcgetattr(fd)
    speed = getattr(termios, f"B{baud}")
    attrs[4] = attrs[5] = speed
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    return fd


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--embsim", required=True)
    args = ap.parse_args()
    if os.name != "posix":
        print("SKIP: no pseudo-terminals on this host (Q16)")
        return 0
    failures = 0

    def check(ok: bool, what: str) -> None:
        nonlocal failures
        print(("ok    " if ok else "FAIL  ") + what)
        failures += 0 if ok else 1

    with tempfile.TemporaryDirectory(prefix="embsim-hostserial-") as tmp:
        shutil.copy(ROOT / "tests/firmware/uno-echo/uno-echo.elf", Path(tmp) / "uno-echo.elf")
        src = Path(tmp) / "project.json"
        src.write_text(json.dumps(PROJECT), encoding="utf-8")
        with StdioClient([args.embsim, "sidecar", str(src)]) as c:
            port = c.call("port.open", {"target": "B1"})
            path = port["path"]
            check(os.path.exists(path) and not port["connected"], f"B1's port is {path}, no program on it")
            check(c.call("port.open", {"target": "B1"})["path"] == path, "opening it again gives the same port")
            c.call("control.run", {"speed": 1})
            # the power-on banner goes out to no one
            wait_for(lambda: int(c.call("session.info")["now_ps"]) > 50_000_000_000)

            fd = open_port(path, 9600)
            banner = read_for(fd, 15.0, b"\r\n")
            check(banner == b"boot 02\r\n", f"opening the port resets the sketch: {banner!r} (EXTRF, not power-on)")

            os.write(fd, b"hello, uno\n")
            echo = read_for(fd, 15.0, b"\n")
            check(echo == b"HELLO, UNO\n", f"the sketch's echo comes back: {echo!r}")

            wait_for(lambda: c.call("port.list")["ports"][0]["connected"])
            c.poll(0.3)
            ports = c.call("port.list")["ports"]
            check(len(ports) == 1 and ports[0]["connected"] and ports[0]["baud"] == 9600
                  and abs(ports[0]["sketch_baud"] - 9600) < 100, f"port.list: {ports}")
            heard = c.notifications()
            states = [n.params for n in heard if n.method == "port.state"]
            check(any(s["connected"] for s in states), "port.state told of the connection")
            inputs = [n.params for n in heard if n.method == "record.input"]
            check(any(i["target"] == "B1.DTR_RESET" and i["input"] == "pressed" for i in inputs)
                  and any(i["target"] == "B1" and i["input"] == "serial" and i["value"] == "hello, uno\n" for i in inputs),
                  "the DTR reset and the host's bytes are recorded inputs")

            # the program sets 115200 on the line; the sketch stays at 9600
            import termios
            attrs = termios.tcgetattr(fd)
            attrs[4] = attrs[5] = termios.B115200
            termios.tcsetattr(fd, termios.TCSANOW, attrs)
            wait_for(lambda: c.call("port.list")["ports"][0]["baud"] == 115200)
            os.write(fd, b"hello, uno\n")
            garbage = read_for(fd, 1.5)
            check(b"HELLO" not in garbage, f"at 115200 against the sketch's 9600, no echo makes sense: {garbage!r}")

            os.close(fd)
            check(wait_for(lambda: not c.call("port.list")["ports"][0]["connected"]), "closing the port: no program on it")

            fd = open_port(path, 9600)
            banner = read_for(fd, 15.0, b"\r\n")
            check(banner == b"boot 02\r\n", f"opening it again resets again: {banner!r}")
            os.close(fd)

            c.call("control.pause")
            c.call("port.close", {"target": "B1"})
            check(c.call("port.list")["ports"] == [], "port.close removes it")
            try:
                c.call("port.open", {"target": "U9"})
                check(False, "a port on a target that is neither a board nor a terminal is refused")
            except Exception:  # noqa: BLE001 (EmbsimError)
                check(True, "a port on a target that is neither a board nor a terminal is refused")
    # the NUCLEO-F446RE's ST-LINK virtual COM port: no DTR reset, 115200 baud
    with tempfile.TemporaryDirectory(prefix="embsim-hostserial-") as tmp:
        ex = Path(tmp) / "nucleo"
        shutil.copytree(ROOT / "examples/nucleo-f446re-blink-serial", ex)
        with StdioClient([args.embsim, "sidecar", str(ex / "project.json")]) as c:
            path = c.call("port.open", {"target": "B1"})["path"]
            c.call("control.run", {"speed": 1})
            wait_for(lambda: int(c.call("session.info")["now_ps"]) > 50_000_000_000)
            fd = open_port(path, 115200)
            quiet = read_for(fd, 1.0)
            check(b"nucleo-f446re" not in quiet, f"opening the VCP does not reset the STM32: {quiet!r}")
            os.write(fd, b"hello, nucleo")
            echo = read_for(fd, 15.0, b"O, NUCLEO")
            check(echo == b"HELLO, NUCLEO", f"the firmware's echo through the ST-LINK: {echo!r}")
            os.close(fd)
            c.call("control.pause")
    print("PASS" if not failures else f"FAIL: {failures} check(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
