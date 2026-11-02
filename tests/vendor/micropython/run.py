#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""MicroPython on EmbSIM's Pico, put there as a person does it.

MicroPython's rp2 port (https://github.com/micropython/micropython, its
RPI_PICO board), built unmodified from a checkout with its own Makefile
(mpy-cross, then ports/rp2), is copied as its firmware.uf2 onto the Pico's
RPI-RP2 drive: a Pico running examples/pico-blink-serial is plugged into a
computer's USB port with BOOTSEL held, the drive appears, the file is
copied, the Pico reboots into MicroPython, and the computer opens the
board's USB serial port (MicroPython's own TinyUSB CDC device) as a
terminal does. At its prompt:

  - `print(1+1)` answers 2;
  - `machine.Pin("LED", machine.Pin.OUT).value(1)` lights the LED: GPIO25's
    net goes high;
  - a file written to its flash filesystem (littlefs, formatted on the first
    boot through the bootrom's flash functions) is listed by `os.listdir()`.

Then the board's USB serial port is a serial port on this computer
(`port.open` on the USB host, a pseudo-terminal) and the session runs in
real time: a program opening it raises DTR, the friendly prompt answers
over it, and so does MicroPython's raw REPL as mpremote drives it (Ctrl-C,
Ctrl-A, the code, Ctrl-D: `OK`, the output, Ctrl-D twice).

The checkout is not vendored: --mp names it (with its lib/pico-sdk,
lib/tinyusb, lib/micropython-lib and lib/mbedtls submodules), CI's at a
pinned release; or --uf2 names a firmware.uf2 already built.

    run.py --mp DIR | --uf2 FILE [--embsim PATH]

Without --mp or --uf2, or without arm-none-eabi-gcc, cmake and make, it
skips."""
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

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from embsim_client import StdioClient  # noqa: E402


def build(mp: Path) -> Path:
    subprocess.run(["make", "-C", str(mp / "mpy-cross"), "-j8"], check=True)
    subprocess.run(["make", "-C", str(mp / "ports" / "rp2"), "BOARD=RPI_PICO", "-j8"], check=True)
    return mp / "ports" / "rp2" / "build-RPI_PICO" / "firmware.uf2"


def read_for(fd: int, seconds: float, until: bytes) -> bytes:
    got = b""
    end = time.monotonic() + seconds
    while time.monotonic() < end and until not in got:
        r, _, _ = select.select([fd], [], [], 0.05)
        if r:
            try:
                got += os.read(fd, 4096)
            except BlockingIOError:
                pass
    return got


def open_port(path: str) -> int:
    import termios
    import tty
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    tty.setraw(fd)
    attrs = termios.tcgetattr(fd)
    attrs[4] = attrs[5] = termios.B115200
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    return fd


def project(uf2: str) -> dict:
    blink = ROOT / "examples" / "pico-blink-serial" / "firmware" / "blink-serial.elf"
    return {
        "schema_version": 1, "kind": "project", "name": "micropython", "policy": "electrical",
        "boards": [{"id": "B1", "board": "emblink/pico", "firmware": {"image": str(blink), "format": "elf"}}],
        "components": [
            {"id": "PC", "component": "emblink/usb_host", "params": {"copy_file": uf2}},
            {"id": "G", "component": "emblink/gnd"},
        ],
        "nets": {
            "VBUS": ["PC.VBUS", "B1.VBUS"], "DP": ["PC.DP", "B1.USB_DP"], "DM": ["PC.DM", "B1.USB_DM"],
            "GND": ["PC.GND", "B1.GND", "G.GND"],
        },
        "probes": [{"id": "led", "target": "net:B1.GPIO25", "kind": "digital"}],
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--mp", default=os.environ.get("MICROPYTHON_PATH"), help="a micropython checkout")
    ap.add_argument("--uf2", help="a firmware.uf2 already built")
    ap.add_argument("--embsim", default=str(ROOT / "build" / "dev" / "bin" / "embsim"))
    args = ap.parse_args()
    if not args.uf2:
        missing = [t for t in ("arm-none-eabi-gcc", "cmake", "make") if not shutil.which(t)]
        if missing or not args.mp:
            print(f"SKIP: {'needs ' + ', '.join(missing) if missing else 'no --mp (a micropython checkout) or --uf2'}")
            return 0
    uf2 = Path(args.uf2).resolve() if args.uf2 else build(Path(args.mp).resolve())
    failures = 0

    def check(ok: bool, what: str) -> None:
        nonlocal failures
        print(("  ok   " if ok else "  FAIL ") + what)
        failures += 0 if ok else 1

    with tempfile.TemporaryDirectory(prefix="embsim-micropython-") as tmp:
        t = Path(tmp)
        shutil.copy(uf2, t / "firmware.uf2")
        (t / "project.json").write_text(json.dumps(project("firmware.uf2")), encoding="utf-8")
        with StdioClient([args.embsim, "sidecar", str(t / "project.json")]) as c:
            now = [0]

            def step(ms: float) -> dict:
                now[0] += int(ms * 1e9)
                c.call("control.advance", {"until": str(now[0])})
                return c.call("observe.snapshot", {"include": ["outputs"]})["outputs"]["PC.HOST"]

            def until(cond, ms: float, limit_s: float) -> dict:
                h = step(ms)
                while not cond(h) and now[0] < int(limit_s * 1e12):
                    h = step(ms)
                return h

            # plugged in with BOOTSEL held: the drive; the file dropped on it
            c.call("input.set", {"target": "B1.SW_BOOTSEL", "input": "pressed", "value": True})
            h = step(500)
            check(h["vid"] == 0x2E8A and h["pid"] == 0x0003, f"BOOTSEL held: the RPI-RP2 drive ({h['product']!r})")
            c.call("input.set", {"target": "B1.SW_BOOTSEL", "input": "pressed", "value": False})
            c.call("input.set", {"target": "PC.HOST", "input": "copy", "value": True})
            h = until(lambda h: h["copy_result"], 100, 20)
            check(h["copy_result"] == "copied", f"firmware.uf2 copied: {h['copy_bytes']} bytes {h['copy_error']}")
            # the Pico reboots into MicroPython, whose serial port the terminal opens
            h = until(lambda h: h["cdc_open"] and ">>>" in h["cdc_text"], 100, 30)
            check(h["pid"] == 0x0005 and h["manufacturer"] == "MicroPython" and "MicroPython" in h["cdc_text"],
                  f"MicroPython's prompt on its USB serial port: {h['cdc_text'].splitlines()[0] if h['cdc_text'] else ''!r}")

            def repl(line: str, want: str) -> str:
                seen = len(step(1)["cdc_text"])  # what the terminal had before the line
                c.call("input.set", {"target": "PC.HOST", "input": "cdc_send", "value": line + "\r\n"})
                out = until(lambda x: x["cdc_text"].count(">>> ") > 0 and want in x["cdc_text"][seen:], 50,
                            now[0] / 1e12 + 10)["cdc_text"][seen:]
                return out

            out = repl("print(1+1)", "2\r\n")
            check("2\r\n" in out, "print(1+1) answers 2")
            out = repl("import machine; machine.Pin('LED', machine.Pin.OUT).value(1); print('lit')", "lit\r\n")
            samples = c.call("probe.read", {"probe": "led"}).get("samples", [])
            check("lit" in out and samples and samples[-1][1] in (1, "1", True),
                  f"the LED on GPIO25 lit from machine.Pin: {samples[-1] if samples else None}")
            out = repl("import os; open('hello.txt','w').write('hi'); print(os.listdir())", "]\r\n")
            check("'hello.txt'" in out, f"a file in its flash filesystem: {out.strip().splitlines()[-2:] if out else ''}")
            # rp2.asm_pio: a state machine returns each word it is given, inverted
            out = repl("import rp2; f=rp2.asm_pio()(lambda: (pull(), mov(isr, invert(osr)), push())); "
                       "sm=rp2.StateMachine(0, f); sm.active(1); sm.put(0x1234); print('pio', hex(sm.get()))",
                       "pio ")
            check("pio 0xffffedcb" in out, f"rp2.StateMachine runs a PIO program: {out.strip().splitlines()[-2:] if out else ''}")
            # rp2.DMA: a copy of a bytearray
            out = repl("import rp2; d=rp2.DMA(); a=bytearray(b'dma copies this!'); b=bytearray(len(a)); "
                       "d.config(read=a, write=b, count=len(a)//4, ctrl=d.pack_ctrl(), trigger=True); "
                       "import time; time.sleep_ms(1); print('dma', bytes(b))", "dma ")
            check("dma b'dma copies this!'" in out, f"rp2.DMA copies memory: {out.strip().splitlines()[-2:] if out else ''}")

            # its serial port on this computer, the session in real time
            if os.name == "posix":
                port = c.call("port.open", {"target": "PC"})
                path = port["path"]
                check(os.path.exists(path) and not port["connected"], f"the Pico's USB serial port on the host: {path}")
                c.call("control.run", {"speed": 1})
                time.sleep(0.3)
                fd = open_port(path)
                time.sleep(0.5)
                os.write(fd, b"\r\nprint(6*7)\r\n")
                got = read_for(fd, 30.0, b"42\r\n")
                check(b"42\r\n" in got, f"a terminal on {path}: print(6*7) answers {got[-24:]!r}")
                # MicroPython's raw REPL, as mpremote enters it and runs code
                os.write(fd, b"\r\x03\x03")
                read_for(fd, 2.0, b">>> ")
                os.write(fd, b"\r\x01")
                got = read_for(fd, 30.0, b"raw REPL; CTRL-B to exit\r\n>")
                ok = b"raw REPL; CTRL-B to exit\r\n>" in got
                os.write(fd, b"print('raw', 2**10)\x04")
                got = read_for(fd, 30.0, b"\x04>")
                ok = ok and got.startswith(b"OK") and b"raw 1024" in got
                os.write(fd, b"\x02")
                check(ok, f"mpremote's raw REPL: {got[:40]!r}")
                os.close(fd)
                time.sleep(0.5)
                c.call("control.pause")
                log = c.call("observe.snapshot", {"include": ["outputs"]})["outputs"]["PC.HOST"]["log"]
                check("DTR and RTS raised" in log and "DTR and RTS dropped" in log,
                      "DTR rises as a program opens the port and drops as it closes it")
                c.call("port.close", {"target": "PC"})
    print("micropython: %s" % ("ok" if not failures else f"{failures} failure(s)"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
