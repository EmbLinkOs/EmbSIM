#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""The NUCLEO-F446RE's ST-LINK/V2-1 from the computer, as stlink-server shares it.

The session serves its probes as ST's stlink-server does (stlink.serve, TCP).
A client speaking that protocol, as STM32CubeIDE and OpenOCD's
`hla_stlink_backend tcp` do, finds the board's probe, opens it and sends it
ST-LINK commands:

  - the server's version and the probe's identity: V2J37M26, 0483:374B, its
    serial number; the target's voltage (the board's 3.3 V on VDD);
  - SWD entered, the STM32F446's SW-DP IDCODE (0x2BA01477);
  - the core halted through DHCSR (S_HALT), its PC read through DCRSR in
    the flash, the program's first words read back from 0x08000000,
    DBGMCU_IDCODE 0x10006421, a word written to SRAM and read back, an
    access nothing answers reported by GETLASTRWSTATUS2 with its address;
  - DBGMCU's DBG_TIM2_STOP: TIM2's counter holds while the core is halted,
    and counts once the bit is cleared;
  - DBGMCU's DBG_IWDG_STOP: the independent watchdog, started (its 512 ms
    time-out) while the core is halted, does not reset the part until the
    bit is cleared;
  - a single step moves the PC on by one instruction; RUNCORE restarts the
    core, which runs on; DRIVE_NRST holds the board's NRST low, and the MCU
    in reset, until it is released.

Then, when OpenOCD is installed (Debian's openocd 0.12), a blank board (its
image not built yet) is programmed as a person does it:

    openocd -f interface/stlink.cfg -c "hla_stlink_backend tcp PORT" \\
            -f target/stm32f4x.cfg -c "program blink-serial.elf verify reset exit"

OpenOCD halts the locked-up blank part, runs its flash loader in the
target's SRAM, verifies the flash and resets it; the program then runs:
its banner on the ST-LINK's virtual COM port and LD2 blinking. With an
Arm GDB too, GDB debugs the running program through OpenOCD's GDB server:
a breakpoint at main, a line stepped over, a watchpoint on a variable the
SysTick handler writes. Last, printf over SWO: tests/firmware/nucleo-swo
writes lines to the ITM's port 0, OpenOCD's `tpiu configure ... enable` and
`itm port 0 on` set the trace up at 2 MHz from the 16 MHz HSI, and the lines
arrive in OpenOCD's file through the probe's trace receiver; with the TPIU's
prescaler changed behind OpenOCD's back (1 MHz), the probe gets garbage.

    test_stlink.py --embsim PATH
"""
import argparse
import json
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from embsim_client import StdioClient  # noqa: E402

EXAMPLE = ROOT / "examples" / "nucleo-f446re-blink-serial"
ELF = EXAMPLE / "firmware" / "blink-serial.elf"

failures = 0


def check(ok: bool, what: str) -> None:
    global failures
    print(("  ok   " if ok else "  FAIL ") + what)
    if not ok:
        failures += 1


class StlinkServer:
    """A client of stlink-server's protocol (TCP, little-endian words)."""

    def __init__(self, port: int):
        self.s = socket.create_connection(("127.0.0.1", port), timeout=10)
        self.cid = 0

    def req(self, data: bytes, n: int) -> bytes:
        self.s.sendall(data)
        got = b""
        while len(got) < n:
            part = self.s.recv(n - len(got))
            if not part:
                raise RuntimeError("stlink-server closed the connection")
            got += part
        return got

    def usb(self, cmd: bytes, rx: int = 0, tx: bytes = b"") -> bytes:
        cmd = cmd.ljust(16, b"\0")
        direction = 0 if tx else 1
        size = len(tx) if tx else rx
        head = struct.pack("<B3xI16sB3xI", 5, self.cid, cmd, direction, size)
        r = self.req(head + tx, 4 + (0 if tx else rx))
        status, = struct.unpack_from("<I", r)
        assert status == 1, f"stlink-server status {status:#x}"
        return r[4:]

    def close(self) -> None:
        self.s.close()


def until(cond, seconds: float) -> bool:
    """cond() within `seconds` of wall time: the session runs in real time,
    and a loaded CI runner may run it slower"""
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if cond():
            return True
        time.sleep(0.05)
    return cond()


def dbg(*b: int) -> bytes:
    return bytes((0xF2,) + b)


def u32(v: int) -> bytes:
    return struct.pack("<I", v)


def project(tmp: Path, blank: bool) -> Path:
    p = json.loads((EXAMPLE / "project.json").read_text(encoding="utf-8"))
    for b in p["boards"]:
        b["firmware"]["image"] = "not-built.elf" if blank else str(ELF)
    # a blank part has no symbols or registers to probe by name
    p["probes"] = [q for q in p.get("probes", []) if q["target"].split(":")[0] not in ("var", "reg", "mem")]
    out = tmp / ("blank.json" if blank else "project.json")
    out.write_text(json.dumps(p), encoding="utf-8")
    return out


def protocol(embsim: str, tmp: Path) -> None:
    with StdioClient([embsim, "sidecar", str(project(tmp, False))]) as c:
        c.call("control.advance", {"until": "50ms"})
        r = c.call("stlink.serve", {"port": 0})
        check(r["probes"] and r["probes"][0]["id"] == "B1.U2", f"stlink.serve shares the board's probe: {r['probes']}")
        c.call("control.run", {"speed": 1})
        v = StlinkServer(r["port"])
        ver = struct.unpack("<4I", v.req(bytes([6, 2, 0, 0]), 16))
        check(ver[0] == 2, f"the server's version: API {ver[0]}, {ver[1]}.{ver[2]}.{ver[3]}")
        v.req(bytes([0, 0]), 4)
        n, = struct.unpack("<I", v.req(bytes([1]), 4))
        info = v.req(bytes([2, 0, 0, 0]) + u32(41), 45)
        dev_id, = struct.unpack_from("<I", info, 4)
        serial = info[8:40].rstrip(b"\0").decode()
        vid, pid = struct.unpack_from("<HH", info, 40)
        check(n == 1 and (vid, pid) == (0x0483, 0x374B) and len(serial) == 24,
              f"one probe: {vid:04x}:{pid:04x}, serial {serial}")
        opened = v.req(bytes([3, 0, 0, 0]) + u32(dev_id), 8)
        v.cid, = struct.unpack_from("<I", opened, 4)
        check(struct.unpack_from("<I", opened)[0] == 1 and v.cid != 0, "the probe opened")

        raw = v.usb(b"\xF1", 6)
        version = raw[0] << 8 | raw[1]
        check((version >> 12, version >> 6 & 63, version & 63) == (2, 37, 26), "GET_VERSION: V2J37M26")
        adc = struct.unpack("<II", v.usb(b"\xF7", 8))
        volts = 2 * adc[1] * 1.2 / adc[0]
        check(3.2 < volts < 3.4, f"GET_TARGET_VOLTAGE: {volts:.3f} V")
        check(v.usb(dbg(0x30, 0xA3), 2)[0] == 0x80, "SWD entered")
        idc = v.usb(dbg(0x31), 12)
        check(idc[0] == 0x80 and struct.unpack_from("<I", idc, 4)[0] == 0x2BA01477, "READ_IDCODES: 0x2BA01477")

        def rdreg(addr: int) -> int:
            b = v.usb(dbg(0x36) + u32(addr), 8)
            assert b[0] == 0x80
            return struct.unpack_from("<I", b, 4)[0]

        def core(sel: int) -> int:
            b = v.usb(dbg(0x33, sel), 8)
            assert b[0] == 0x80
            return struct.unpack_from("<I", b, 4)[0]

        v.usb(dbg(0x35) + u32(0xE000EDF0) + u32(0xA05F0003), 2)
        check(rdreg(0xE000EDF0) & (1 << 17) != 0, "the core halted (DHCSR.S_HALT)")
        pc = core(15)
        check(0x08000000 <= pc < 0x08080000, f"its PC in the flash: {pc:#010x}")
        words = v.usb(dbg(0x07) + u32(0x08000000) + struct.pack("<H", 8), 8)
        with open(ELF, "rb") as f:
            elf = f.read()
        phoff, = struct.unpack_from("<I", elf, 28)
        p_offset, = struct.unpack_from("<I", elf, phoff + 4)
        check(words == elf[p_offset:p_offset + 8], f"the vector table read back: {words.hex()}")
        check(struct.unpack("<I", v.usb(dbg(0x07) + u32(0xE0042000) + struct.pack("<H", 4), 4))[0] == 0x10006421,
              "DBGMCU_IDCODE: 0x10006421")
        v.usb(dbg(0x08) + u32(0x2001F000) + struct.pack("<H", 4), tx=u32(0xC0FFEE42))
        back = struct.unpack("<I", v.usb(dbg(0x07) + u32(0x2001F000) + struct.pack("<H", 4), 4))[0]
        check(back == 0xC0FFEE42, f"SRAM written and read back: {back:#x}")
        v.usb(dbg(0x07) + u32(0x60000000) + struct.pack("<H", 4), 4)
        st = v.usb(dbg(0x3E), 12)
        check(st[0] == 0x11 and struct.unpack_from("<I", st, 4)[0] == 0x60000000,
              f"an access nothing answers: status {st[0]:#x} at {struct.unpack_from('<I', st, 4)[0]:#x}")
        # the IWDG started while the core is halted:
        # DBGMCU's DBG_IWDG_STOP freezes it, as OpenOCD's stm32f4x.cfg sets it
        def wr(addr: int, val: int) -> None:
            v.usb(dbg(0x35) + u32(addr) + u32(val), 2)

        # TIM2 counting while the core is halted: DBG_TIM2_STOP holds its counter
        wr(0x40023840, rdreg(0x40023840) | 1)    # RCC_APB1ENR: TIM2EN
        wr(0x40000028, 0)                        # PSC: the timer clock itself
        wr(0x4000002C, 0xFFFFFFFF)               # ARR
        wr(0x40000000, 1)                        # CR1: CEN, counting
        check(until(lambda: rdreg(0x40000024) != 0, 5.0), "TIM2 counts while the core is halted")
        wr(0xE0042008, 1 << 0)                   # DBGMCU_APB1_FZ: DBG_TIM2_STOP
        c1 = rdreg(0x40000024)
        time.sleep(0.3)
        c2 = rdreg(0x40000024)
        check(c1 == c2 and c1 != 0, f"DBG_TIM2_STOP set: CNT holds at {c2}")
        wr(0xE0042008, 0)
        check(until(lambda: rdreg(0x40000024) != c2, 5.0), "the freeze bit cleared: TIM2 counts on while the core is halted")
        wr(0x40000000, 0)
        wr(0xE0042008, 1 << 12)                  # DBGMCU_APB1_FZ: DBG_IWDG_STOP
        rdreg(0xE000EDF0)                        # S_RESET_ST read away
        wr(0x40003000, 0xCCCC)                   # IWDG started: 4096 counts of 32 kHz / 4, 512 ms
        time.sleep(0.8)
        check(rdreg(0xE000EDF0) & (1 << 25) == 0, "halted, the IWDG frozen by DBG_IWDG_STOP: no reset in 800 ms")
        wr(0xE0042008, 0)
        check(until(lambda: rdreg(0xE000EDF0) & (1 << 25) != 0, 10.0),
              "the freeze bit cleared: the IWDG resets the part (S_RESET_ST)")
        v.usb(dbg(0x35) + u32(0xE000EDF0) + u32(0xA05F0003), 2)
        pc = core(15)
        v.usb(dbg(0x0A), 2)  # STEPCORE
        time.sleep(0.05)
        pc2 = core(15)
        check(pc2 != pc and 0 < pc2 - pc <= 4 or pc2 < pc, f"a step: PC {pc:#x} to {pc2:#x}")
        v.usb(dbg(0x09), 2)  # RUNCORE
        check(until(lambda: rdreg(0xE000EDF0) & (1 << 17) == 0, 5.0), "RUNCORE: the core runs again")
        def state() -> str:
            c.call("control.pause")
            st = c.call("observe.snapshot", {"include": ["outputs"]})["outputs"]["B1.U5"]["state"]
            c.call("control.run", {"speed": 1})
            return st

        v.usb(dbg(0x3C, 0), 2)  # NRST low
        held = "reset" if until(lambda: state() == "reset", 5.0) else state()
        v.usb(dbg(0x3C, 1), 2)
        until(lambda: state() in ("running", "sleeping"), 5.0)
        c.call("control.pause")
        out = c.call("observe.snapshot", {"include": ["outputs"]})["outputs"]
        check(held == "reset" and out["B1.U5"]["state"] in ("running", "sleeping"),
              f"DRIVE_NRST: the MCU {held} while NRST is low, then {out['B1.U5']['state']}")
        c.call("control.run", {"speed": 1})  # a paused session holds the requests
        v.usb(dbg(0x21))
        v.req(bytes([4, 0, 0, 0]) + u32(v.cid), 4)
        v.close()
        c.call("control.pause")
        check(out["B1.U2"]["commands"] > 10 and "SWD entered" in out["B1.U2"]["log"],
              f"the probe's outputs: {out['B1.U2']['commands']} commands, log {out['B1.U2']['log']!r}")


def openocd(embsim: str, tmp: Path) -> None:
    tool = shutil.which("openocd")
    if not tool:
        print("  SKIP OpenOCD is not installed")
        return
    with StdioClient([embsim, "sidecar", str(project(tmp, True))]) as c:
        r = c.call("stlink.serve", {"port": 0})
        c.call("control.run", {"speed": 1})
        time.sleep(0.3)
        p = subprocess.run([tool, "-f", "interface/stlink.cfg", "-c", f"hla_stlink_backend tcp {r['port']}",
                            "-f", "target/stm32f4x.cfg", "-c", "gdb_port disabled; telnet_port disabled; tcl_port disabled",
                            "-c", f"program {ELF.as_posix()} verify reset exit"],
                           capture_output=True, text=True, timeout=240)
        log = p.stdout + p.stderr
        check(p.returncode == 0 and "Verified OK" in log and "device id = 0x10006421" in log,
              "OpenOCD programmed the blank board's flash and verified it" +
              ("" if p.returncode == 0 else ": " + log[-800:]))
        def banner() -> dict:
            c.call("control.pause")
            u = c.call("observe.snapshot", {"include": ["outputs"]})["outputs"]["B1.U5"]
            c.call("control.run", {"speed": 1})
            return u

        until(lambda: "nucleo-f446re" in banner()["serial.USART2"], 15.0)
        time.sleep(2.5)  # LD2's 1 Hz: a blink or two
        c.call("control.pause")
        u5 = c.call("observe.snapshot", {"include": ["outputs"]})["outputs"]["B1.U5"]
        check("nucleo-f446re: STM32F446RE" in u5["serial.USART2"],
              f"the program runs: {u5['serial.USART2'].strip()!r}")
        led = [s[1] for s in c.call("probe.read", {"probe": "led"}).get("samples", [])]
        check(0 in led and 1 in led, "LD2 blinks")


def gdb(embsim: str, tmp: Path) -> None:
    tool, gdbx = shutil.which("openocd"), shutil.which("gdb-multiarch") or shutil.which("arm-none-eabi-gdb")
    if not tool or not gdbx:
        print("  SKIP OpenOCD or an Arm GDB is not installed")
        return
    with StdioClient([embsim, "sidecar", str(project(tmp, False))]) as c:
        r = c.call("stlink.serve", {"port": 0})
        c.call("control.run", {"speed": 1})
        with socket.socket() as probe:
            probe.bind(("127.0.0.1", 0))
            gport = probe.getsockname()[1]
        oc = subprocess.Popen([tool, "-f", "interface/stlink.cfg", "-c", f"hla_stlink_backend tcp {r['port']}",
                               "-f", "target/stm32f4x.cfg",
                               "-c", f"gdb_port {gport}; telnet_port disabled; tcl_port disabled"],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            time.sleep(2.0)
            g = subprocess.run([gdbx, "-q", "-batch", "-nx", str(ELF), "-ex", f"target extended-remote :{gport}",
                                "-ex", "monitor reset halt", "-ex", "break main", "-ex", "continue",
                                "-ex", "next", "-ex", "delete", "-ex", "watch ticks", "-ex", "continue",
                                "-ex", "print ticks", "-ex", "bt 1", "-ex", "detach"],
                               capture_output=True, text=True, timeout=180)
        finally:
            oc.terminate()
            oc.wait(timeout=10)
        out = g.stdout
        check("Breakpoint 1, main ()" in out, "GDB through OpenOCD: the breakpoint at main (the FPB) is hit")
        check("main.c:135" in out or "\n135\t" in out, "a source line stepped over (next)")
        check("SysTick_Handler" in out and "$1 = 1" in out,
              "a watchpoint on ticks (a DWT comparator) stops SysTick_Handler's write: ticks = 1")


SWO_ELF = ROOT / "tests" / "firmware" / "nucleo-swo" / "swo.elf"


def itm_text(raw: bytes) -> str:
    """the ITM's software packets on port 0, as text"""
    out, i = bytearray(), 0
    while i < len(raw):
        h = raw[i]
        size = {1: 1, 2: 2, 3: 4}.get(h & 3, 0)
        if h & 4 or not size:  # not a software source packet: skip its byte
            i += 1
            continue
        if h >> 3 == 0:
            out += raw[i + 1:i + 1 + size]
        i += 1 + size
    return out.decode("latin-1")


def swo(embsim: str, tmp: Path) -> None:
    tool = shutil.which("openocd")
    if not tool:
        print("  SKIP OpenOCD is not installed")
        return
    p = json.loads((EXAMPLE / "project.json").read_text(encoding="utf-8"))
    for b in p["boards"]:
        b["firmware"]["image"] = str(SWO_ELF)
    p["probes"] = []
    src = tmp / "swo.json"
    src.write_text(json.dumps(p), encoding="utf-8")
    for acpr, want in ((None, True), (15, False)):
        out = tmp / f"swo-{acpr}.bin"
        with StdioClient([embsim, "sidecar", str(src)]) as c:
            r = c.call("stlink.serve", {"port": 0})
            c.call("control.run", {"speed": 1})
            time.sleep(0.3)
            # the TPIU's prescaler for 2 MHz from the 16 MHz HSI; the probe's
            # UART at `rate`
            oc = subprocess.Popen([tool, "-f", "interface/stlink.cfg", "-c", f"hla_stlink_backend tcp {r['port']}",
                                   "-f", "target/stm32f4x.cfg",
                                   "-c", "gdb_port disabled; telnet_port disabled; tcl_port disabled", "-c", "init",
                                   "-c", f"stm32f4x.tpiu configure -protocol uart -output {out.as_posix()} "
                                         "-traceclk 16000000 -pin-freq 2000000",
                                   "-c", "stm32f4x.tpiu enable", "-c", "itm port 0 on",
                                   # the TPIU's prescaler changed behind OpenOCD: 16 MHz / 16, 1 MHz
                                   *(["-c", f"mww 0xE0040010 {acpr}"] if acpr is not None else [])],
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            def garbled() -> int:
                c.call("control.pause")
                n = c.call("observe.snapshot", {"include": ["outputs"]})["outputs"]["B1.U2"]["trace_garbled"]
                c.call("control.run", {"speed": 1})
                return n

            try:
                if want:
                    until(lambda: out.exists() and "swo line 3" in itm_text(out.read_bytes()), 25.0)
                else:  # the garbage of a second or more of lines
                    until(lambda: garbled() > 100, 25.0)
            finally:
                # OpenOCD stops the trace through the probe as it shuts down: on a
                # slow (sanitized) build that takes the session a while
                oc.terminate()
                try:
                    log = oc.communicate(timeout=90)[0]
                except subprocess.TimeoutExpired:
                    oc.kill()
                    log = oc.communicate()[0]
            c.call("control.pause")
            u2 = c.call("observe.snapshot", {"include": ["outputs"]})["outputs"]["B1.U2"]
        text = itm_text(out.read_bytes()) if out.exists() else ""
        if want:
            lines = [ln for ln in text.splitlines() if ln.startswith("swo line ")]
            nums = [int(ln.split()[2]) for ln in lines]
            check(len(lines) >= 3 and nums == list(range(nums[0], nums[0] + len(nums))),
                  f"OpenOCD's tpiu and itm: printf over SWO reaches its file ({lines[:3]})" +
                  ("" if lines else ": " + log[-600:]))
            check(u2["trace_hz"] == 2000000 and u2["trace_bytes"] > 0 and u2["trace_garbled"] == 0,
                  f"the probe received {u2['trace_bytes']} bytes at {u2['trace_hz']} Hz")
        else:
            # the target's SWO at 1 MHz read at 2 MHz: the probe's UART sees garbage
            # (the lines of the second or so OpenOCD takes to send its commands get
            # through before the prescaler changes; the 6 s would hold some 60)
            check(text.count("swo line") <= 20 and u2["trace_garbled"] > 100,
                  f"SWO at 1 MHz read at 2 MHz: {text.count('swo line')} line(s) before the change, "
                  f"then {u2['trace_garbled']} bytes garbled")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--embsim", default=str(ROOT / "build" / "dev" / "bin" / "embsim"))
    args = ap.parse_args()
    with tempfile.TemporaryDirectory(prefix="embsim-stlink-") as tmp:
        protocol(args.embsim, Path(tmp))
        openocd(args.embsim, Path(tmp))
        gdb(args.embsim, Path(tmp))
        swo(args.embsim, Path(tmp))
    print("stlink: %s" % ("ok" if not failures else f"{failures} failure(s)"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
