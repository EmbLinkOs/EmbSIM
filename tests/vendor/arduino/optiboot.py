#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Programming the Uno the real way (ADR-031): the boot loader every Uno
ships with, Optiboot, as the unmodified image ArduinoCore-avr carries
(bootloaders/optiboot/optiboot_atmega328.hex), in the ATmega328P's boot
section under the Uno's fuses, and avrdude, the program the Arduino IDE's
Upload button runs, writing a sketch through it over the board's serial
port on the host.

As on the bench:
- avrdude opens the port (DTR: the auto-reset), syncs with Optiboot at
  115200 baud, reads the signature, writes the sketch page by page (SPM
  from the boot section, 4.1 ms a page) and reads it back to verify;
- leaving programming mode, Optiboot lets the watchdog reset the part and
  the sketch starts; a terminal opening the port later resets it again,
  Optiboot waits its second for a programmer, then starts the sketch with
  MCUSR cleared ("boot 00", as Optiboot 4.4 leaves it);
- the sketch stays in flash through a power cycle (control.reset).

Optiboot is GPL-2.0: it is not vendored (ADR-029), it is read from the
ArduinoCore-avr checkout the Arduino suite already uses (--core). The sketch
is tests/firmware/uno-echo (Apache-2.0). Without avrdude, avr-objcopy or a
POSIX pseudo-terminal, it skips.

    optiboot.py --core DIR --embsim PATH
"""
import argparse
import json
import os
import select
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools"))
from embsim_client import StdioClient  # noqa: E402

PROJECT = {
    "schema_version": 1,
    "kind": "project",
    "name": "optiboot-upload",
    "description": "An Uno as it ships: Optiboot in its boot section, no sketch yet.",
    "boards": [{"id": "B1", "board": "emblink/arduino_uno",
                "firmware": {"image": "optiboot_atmega328.hex", "format": "hex"}}],
    # the USB cable that carries the serial port powers the board
    "components": [{"id": "USB", "component": "emblink/vsource", "params": {"kind": "dc", "dc": 5}},
                   {"id": "G", "component": "emblink/gnd"}],
    "nets": {"VBUS": ["USB.+", "B1.5V"], "GND": ["USB.-", "G.GND", "B1.GND"]},
}


def read_for(fd: int, seconds: float, until: bytes = b"") -> bytes:
    got = b""
    end = time.monotonic() + seconds
    while time.monotonic() < end and not (until and until in got):
        r, _, _ = select.select([fd], [], [], 0.05)
        if r:
            try:
                got += os.read(fd, 4096)
            except (BlockingIOError, OSError):
                pass
    return got


def open_port(path: str, baud: int) -> int:
    import termios
    import tty
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    tty.setraw(fd)
    attrs = termios.tcgetattr(fd)
    attrs[4] = attrs[5] = getattr(termios, f"B{baud}")
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    return fd


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--core", default=os.environ.get("ARDUINO_CORE_AVR"))
    ap.add_argument("--embsim", required=True)
    args = ap.parse_args()
    hexfile = Path(args.core or "") / "bootloaders" / "optiboot" / "optiboot_atmega328.hex"
    avrdude, objcopy = shutil.which("avrdude"), shutil.which("avr-objcopy")
    if os.name != "posix" or not args.core or not hexfile.is_file() or not avrdude or not objcopy:
        print("SKIP: needs a POSIX host, --core with Optiboot, avrdude and avr-objcopy")
        return 0
    failures = 0

    def check(ok: bool, what: str) -> None:
        nonlocal failures
        print(("ok    " if ok else "FAIL  ") + what)
        failures += 0 if ok else 1

    with tempfile.TemporaryDirectory(prefix="embsim-optiboot-") as tmp:
        t = Path(tmp)
        shutil.copy(hexfile, t / hexfile.name)
        sketch = t / "uno-echo.hex"
        subprocess.run([objcopy, "-O", "ihex", str(ROOT / "tests/firmware/uno-echo/uno-echo.elf"), str(sketch)],
                       check=True)
        (t / "project.json").write_text(json.dumps(PROJECT), encoding="utf-8")
        with StdioClient([args.embsim, "sidecar", str(t / "project.json")]) as c:
            path = c.call("port.open", {"target": "B1"})["path"]
            c.call("control.run", {"speed": 1})
            time.sleep(0.5)
            cmd = [avrdude, "-c", "arduino", "-p", "m328p", "-P", path, "-b", "115200",
                   "-U", f"flash:w:{sketch}:i"]
            r = subprocess.run(cmd, capture_output=True, text=True, timeout=180)
            log = r.stdout + r.stderr
            check(r.returncode == 0, f"avrdude exits 0 ({r.returncode})")
            check("device signature = 0x1e950f" in log, "avrdude reads the ATmega328P's signature through Optiboot")
            check("bytes of flash verified" in log, "the sketch is written and verified")
            check("expects sync byte" not in log and "not responding" not in log,
                  "Optiboot answers every command in sync")
            if r.returncode:
                print("\n".join(line for line in log.splitlines() if "TIOCM" not in line)[-3000:])

            # a terminal opens the port: DTR resets, Optiboot waits for a
            # programmer, then the sketch starts (a moment after avrdude
            # closed it: a pseudo-terminal's close and reopen within one poll
            # of the session look like one open)
            time.sleep(0.5)
            fd = open_port(path, 9600)
            banner = read_for(fd, 20.0, b"\r\n")
            check(banner.endswith(b"boot 00\r\n"), f"the uploaded sketch runs after Optiboot's wait: {banner!r}")
            os.write(fd, b"uploaded\n")
            echo = read_for(fd, 15.0, b"\n")
            check(echo == b"UPLOADED\n", f"and echoes: {echo!r}")
            os.close(fd)

            # a power cycle keeps what Optiboot wrote
            c.call("control.pause")
            c.call("control.reset", {"reload_images": False})
            c.call("control.run", {"speed": 1})
            time.sleep(0.3)
            fd = open_port(path, 9600)
            banner = read_for(fd, 20.0, b"\r\n")
            check(banner.endswith(b"boot 00\r\n"), f"after a power cycle the sketch is still there: {banner!r}")
            os.close(fd)
            c.call("control.pause")
    print("optiboot: %s" % ("ok" if not failures else f"{failures} failure(s)"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
