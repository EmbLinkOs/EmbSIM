#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""A Raspberry Pi Debug Probe on a Pico, driven over USB/IP as OpenOCD drives it.

The probe's D port is on the Pico's SWD pads, its U port on GP0/GP1; its
USB device is served by the session's USB/IP server and opened here as a
computer's USB stack opens it (test_usbip.py's client), then driven with
CMSIS-DAP v2 packets in the order and form OpenOCD 0.12's cmsis_dap.c
sends them for `target/rp2040.cfg`:

  - the enumeration: 2e8a:000c, USB 2.1, "CMSIS-DAP v2 Interface", the BOS
    and the Microsoft OS 2.0 descriptor set;
  - DAP_Info (firmware 2.0.0, capabilities, 64-byte packets, 6 of them),
    Connect, SWJ_Clock, TransferConfigure, SWD_Configure;
  - the RP2040's multi-drop wire: JTAG-to-dormant and dormant-to-SWD by
    SWJ_Sequence, TARGETSEL by SWD_Sequence, DPIDR 0x0BC12477, the debug
    power-up waited for with a value match;
  - core 0 halted and its PC read through its AHB-AP, its vector table
    read by TransferBlock, the posted reads and the timestamps;
  - core 1's port, its own debug (not halted);
  - QueueCommands held until the next packet, an unknown command, JTAG;
  - the UART: the line coding, DTR, a line sent on TX and its echo back;
  - the Rescue DP: the chip held and released, the bootrom halted in its
    dead loop.

    test_debug_probe.py --embsim PATH
"""
import argparse
import json
import struct
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from embsim_client import StdioClient  # noqa: E402
from test_usbip import Usbip  # noqa: E402

EXAMPLE = ROOT / "examples" / "pico-debug-probe"
failures = 0

# OpenOCD's sequences (src/jtag/swd.h)
JTAG_TO_DORMANT = bytes([0xff, 0xba, 0xbb, 0xbb, 0x33])
DORMANT_TO_SWD = bytes([0xff, 0x92, 0xf3, 0x09, 0x62, 0x95, 0x2d, 0x85, 0x86, 0xe9, 0xaf, 0xdd, 0xe3, 0xa2,
                        0x0e, 0xbc, 0x19, 0xa0, 0xf1, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00])
LINE_RESET = bytes([0xff] * 7 + [0x00])

DP_RD = lambda a: 0x02 | (a & 0xC)  # noqa: E731
DP_WR = lambda a: (a & 0xC)  # noqa: E731
AP_RD = lambda a: 0x03 | (a & 0xC)  # noqa: E731
AP_WR = lambda a: 0x01 | (a & 0xC)  # noqa: E731


def check(ok: bool, what: str) -> None:
    global failures
    print(("  ok   " if ok else "  FAIL ") + what)
    if not ok:
        failures += 1


def parity(v: int) -> int:
    return bin(v).count("1") & 1


class Dap:
    def __init__(self, u: Usbip):
        self.u = u

    def send(self, pkt: bytes) -> None:
        cmd, _, status, _ = self.u.bulk_out(4, pkt)
        assert status == 0, status

    def recv(self) -> bytes:
        cmd, _, status, body = self.u.bulk_in(5, 64)
        assert status == 0, status
        return body

    def cmd(self, pkt: bytes) -> bytes:
        self.send(pkt)
        return self.recv()

    def swj(self, bits: bytes, n: int) -> bytes:
        return self.cmd(bytes([0x12, n & 0xFF]) + bits)

    def targetsel(self, inst: int) -> bytes:
        v = 0x01002927 | inst << 28
        # OpenOCD's cmsis_dap_metacmd_targetsel: the request, Trn ACK Trn read, the data and parity
        return self.cmd(bytes([0x1D, 3, 8, 0x99, 0x80 | 5, 33]) + struct.pack("<I", v) + bytes([parity(v)]))

    def transfer(self, reqs: list) -> tuple:
        """reqs: (request, value-or-None); the count done, the ACK, the words read"""
        body = bytes([0x05, 0, len(reqs)])
        for r, v in reqs:
            body += bytes([r]) + (struct.pack("<I", v) if v is not None else b"")
        rs = self.cmd(body)
        assert rs[0] == 0x05, rs
        n = (len(rs) - 3) // 4
        return rs[1], rs[2], list(struct.unpack("<%dI" % n, rs[3:3 + 4 * n]))

    def rd(self, req: int) -> int:
        done, ack, words = self.transfer([(req, None)])
        assert ack == 1, (done, ack)
        return words[-1]

    def wr(self, req: int, v: int) -> int:
        return self.transfer([(req, v)])[1]

    def mem_rd(self, a: int) -> int:
        self.wr(AP_WR(0x4), a)
        return self.rd(AP_RD(0xC))

    def mem_wr(self, a: int, v: int) -> None:
        self.transfer([(AP_WR(0x4), a), (AP_WR(0xC), v)])


def project(tmp: Path) -> Path:
    p = json.loads((EXAMPLE / "project.json").read_text(encoding="utf-8"))
    p["boards"][0]["firmware"]["image"] = str(ROOT / "examples" / "pico-blink-serial" / "firmware" / "blink-serial.elf")
    src = tmp / "probe.json"
    src.write_text(json.dumps(p), encoding="utf-8")
    return src


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--embsim", default=str(ROOT / "build" / "dev" / "bin" / "embsim"))
    args = ap.parse_args()
    with tempfile.TemporaryDirectory(prefix="embsim-probe-") as tmp:
        src = project(Path(tmp))
        with StdioClient([args.embsim, "sidecar", str(src)]) as c:
            r = c.call("usbip.serve", {"port": 0})
            check([d["id"] for d in r["devices"]] == ["P1"], f"usbip.serve: devices {r['devices']}")
            c.call("control.run", {"speed": 1})
            time.sleep(0.3)
            u = Usbip(r["port"])
            devs = [d for d in u.devlist() if d["vid"] == 0x2E8A and d["pid"] == 0x000C]
            check(len(devs) == 1 and devs[0]["interfaces"][0] == (0xFF, 0, 0) and (2, 2, 0) in devs[0]["interfaces"],
                  f"OP_REQ_DEVLIST: the Debug Probe, {devs[0]['interfaces'] if devs else None}")
            if not devs:
                return 1
            check(u.attach(devs[0]["busid"]) == {"vid": 0x2E8A, "pid": 0x000C}, "OP_REQ_IMPORT")
            _, _, st, dd = u.control_in(bytes([0x80, 6, 0, 1, 0, 0, 18, 0]), 18)
            check(st == 0 and dd[2:4] == b"\x10\x02" and dd[8:14] == bytes.fromhex("8a2e0c003102"),
                  f"the device descriptor: {dd.hex()}")
            _, _, st, cd = u.control_in(bytes([0x80, 6, 0, 2, 0, 0, 255, 0]), 255)
            check(st == 0 and len(cd) == 107 and cd[9:18] == bytes([9, 4, 0, 0, 2, 0xFF, 0, 0, 5]),
                  f"the configuration descriptor: {len(cd)} bytes, interface 0 {cd[9:18].hex()}")
            _, _, st, s5 = u.control_in(bytes([0x80, 6, 5, 3, 0x09, 0x04, 255, 0]), 255)
            check(s5[2:].decode("utf-16-le") == "CMSIS-DAP v2 Interface", f"string 5: {s5[2:].decode('utf-16-le')!r}")
            _, _, st, s2 = u.control_in(bytes([0x80, 6, 2, 3, 0x09, 0x04, 255, 0]), 255)
            check(s2[2:].decode("utf-16-le") == "Debug Probe (CMSIS-DAP)", f"the product: {s2[2:].decode('utf-16-le')!r}")
            _, _, st, bos = u.control_in(bytes([0x80, 6, 0, 0x0F, 0, 0, 255, 0]), 255)
            ms_len = struct.unpack("<H", bos[29:31])[0] if len(bos) >= 33 else 0
            check(st == 0 and len(bos) == 33 and bos[9:25] == bytes.fromhex("df60ddd88945c74c9cd2659d9e648a9f"),
                  f"the BOS: {len(bos)} bytes, the MS OS 2.0 platform capability, a set of {ms_len} bytes")
            _, _, st, ms = u.control_in(bytes([0xC0, 1, 0, 0, 7, 0, ms_len & 0xFF, ms_len >> 8]), ms_len)
            check(st == 0 and len(ms) == ms_len == 0x14E and b"W\0" not in ms[:0] and b"WINUSB" in ms and
                  "{CDB3B5AD-293B-4663-AA36-1AAE46463776}".encode("utf-16-le") in ms,
                  f"the Microsoft OS 2.0 descriptor set: {len(ms)} bytes, WinUSB and the interface GUID")
            _, _, st, _ = u.control_out(bytes([0, 9, 1, 0, 0, 0, 0, 0]))
            check(st == 0, "SET_CONFIGURATION 1")
            d = Dap(u)

            # DAP_Info, as OpenOCD's cmsis_dap_get_caps_info and friends ask
            fw = d.cmd(bytes([0x00, 0x04]))
            caps = d.cmd(bytes([0x00, 0xF0]))
            size = d.cmd(bytes([0x00, 0xFF]))
            count = d.cmd(bytes([0x00, 0xFE]))
            ts = d.cmd(bytes([0x00, 0xF1]))
            vendor = d.cmd(bytes([0x00, 0x01]))
            check(fw[:2] == b"\x00\x06" and fw[2:8] == b"2.0.0\0" and caps[1:3] == b"\x01\x31" and
                  size[1:4] == b"\x02\x40\x00" and count[1:3] == b"\x01\x06" and
                  ts[1:6] == b"\x04\x40\x42\x0f\x00" and vendor[:2] == b"\x00\x00",
                  f"DAP_Info: firmware {fw[2:7].decode()}, capabilities {caps[2]:#x}, packets {size[2]} x {count[2]}, "
                  "a 1 MHz timestamp, no vendor string")
            check(d.cmd(bytes([0x02, 0x01]))[:2] == b"\x02\x01", "DAP_Connect: SWD")
            check(d.cmd(bytes([0x02, 0x02]))[:2] == b"\x02\x00", "DAP_Connect: JTAG refused")
            d.cmd(bytes([0x02, 0x01]))
            check(d.cmd(bytes([0x11]) + struct.pack("<I", 1000000))[:2] == b"\x11\x00", "DAP_SWJ_Clock: 1 MHz")
            check(d.cmd(bytes([0x04, 0, 64, 0, 0, 0]))[:2] == b"\x04\x00", "DAP_TransferConfigure")
            check(d.cmd(bytes([0x13, 0x00]))[:2] == b"\x13\x00", "DAP_SWD_Configure")
            d.cmd(bytes([0x01, 0, 1]))
            outs = c.call("observe.snapshot", {"include": ["outputs"]})["outputs"]["P1"]
            check(outs.get("led_connected") is True and outs.get("dap_connected") is True and
                  outs.get("target") == "B1.U1", f"the probe: target {outs.get('target')}, connected LED on")

            # the multi-drop wire, as swd_connect_multidrop does it
            check(d.swj(JTAG_TO_DORMANT, 40)[:2] == b"\x12\x00", "SWJ_Sequence: JTAG-to-dormant")
            check(d.swj(DORMANT_TO_SWD, 224)[:2] == b"\x12\x00", "SWJ_Sequence: dormant-to-SWD")
            d.swj(LINE_RESET, 64)
            check(d.targetsel(0)[:2] == b"\x1d\x00", "SWD_Sequence: TARGETSEL core 0 (0x01002927)")
            idr = d.rd(DP_RD(0))
            check(idr == 0x0BC12477, f"DPIDR {idr:#010x}")
            d.wr(DP_WR(0x8), 3)
            dl = d.rd(DP_RD(0x4))
            check(dl == 0x00000001, f"DLPIDR {dl:#010x}: instance 0")
            check(d.wr(DP_WR(0), 0x1E) == 1 and d.wr(DP_WR(0x8), 0) == 1, "ABORT, SELECT")
            d.wr(DP_WR(0x4), 0x50000000)
            done, ack, _ = d.transfer([(0x20, 0xA0000000), (DP_RD(0x4) | 0x10, 0xA0000000)])
            check(ack == 1 and done == 2, f"the debug power-up waited for by a value match: ack {ack}")
            done, ack, _ = d.transfer([(0x20, 0xFFFFFFFF), (DP_RD(0) | 0x10, 0x12345678)])
            check(ack == 0x11 and done == 1, f"a value that never matches: ack {ack:#x} (OK | MISMATCH)")

            # core 0 through its AHB-AP
            d.wr(DP_WR(0x8), 0xF0)
            ap_idr = d.rd(AP_RD(0xC))
            check(ap_idr == 0x04770031, f"the AHB-AP's IDR {ap_idr:#010x}")
            d.wr(DP_WR(0x8), 0x00)
            d.wr(AP_WR(0x0), 0x23000002)
            # the ROM table OpenOCD's examine walks: its SCS entry, its component ID, the CPUID
            rom0, cidr1, cpuid = d.mem_rd(0xE00FF000), d.mem_rd(0xE00FFFF4), d.mem_rd(0xE000ED00)
            check(rom0 & 1 and (0xE00FF000 + (rom0 & 0xFFFFF000)) & 0xFFFFFFFF == 0xE000E000 and
                  cidr1 == 0x10 and cpuid >> 4 & 0xFFF == 0xC60,
                  f"the ROM table: SCS entry {rom0:#010x}, CIDR1 {cidr1:#x}; CPUID {cpuid:#010x} (Cortex-M0+)")
            d.mem_wr(0xE000EDF0, 0xA05F0003)
            dh = d.mem_rd(0xE000EDF0)
            check(dh & 0x20003 == 0x20003, f"core 0 halted: DHCSR {dh:#010x}")
            d.mem_wr(0xE000EDF4, 15)
            pc = d.mem_rd(0xE000EDF8)
            check(0x10000000 <= pc < 0x10200000 or 0x20000000 <= pc < 0x20042000, f"core 0's PC {pc:#010x}")
            # its vector table, four words by TransferBlock with the auto-increment
            d.wr(AP_WR(0x0), 0x23000012)
            d.wr(AP_WR(0x4), 0x10000100)
            rs = d.cmd(bytes([0x06, 0, 4, 0, AP_RD(0xC)]))
            vt = struct.unpack("<4I", rs[4:20]) if len(rs) >= 20 else ()
            check(rs[:4] == b"\x06\x04\x00\x01" and vt and 0x20000000 < vt[0] <= 0x20042000 and vt[1] & 1,
                  f"TransferBlock: the vector table {[hex(v) for v in vt]}")
            # posted reads in one Transfer, with timestamps
            d.wr(AP_WR(0x4), 0x10000100)
            done, ack, words = d.transfer([(AP_RD(0xC), None), (AP_RD(0xC), None), (DP_RD(0) | 0x80, None)])
            check(ack == 1 and done == 3 and len(words) == 4 and tuple(words[:2]) == vt[:2] and
                  words[3] == 0x0BC12477, f"posted AP reads and a timestamped read: {[hex(w) for w in words]}")
            d.wr(AP_WR(0x0), 0x23000002)

            # core 1's own port
            d.swj(LINE_RESET, 64)
            d.targetsel(1)
            check(d.rd(DP_RD(0)) == 0x0BC12477, "TARGETSEL core 1 (0x11002927): DPIDR")
            d.wr(DP_WR(0x4), 0x50000000)
            d.wr(AP_WR(0x0), 0x23000002)
            dh1 = d.mem_rd(0xE000EDF0)
            check(dh1 & 0x20000 == 0, f"core 1's own DHCSR {dh1:#010x}: not halted")
            # core 0 runs again
            d.swj(LINE_RESET, 64)
            d.targetsel(0)
            d.rd(DP_RD(0))
            d.mem_wr(0xE000EDF0, 0xA05F0000)
            check(d.mem_rd(0xE000EDF0) & 0x20000 == 0, "core 0 restarted")

            # the firmware's packet handling
            d.send(bytes([0x7E, 1, 0x00, 0x04]))  # QueueCommands: held
            d.send(bytes([0x00, 0xFE]))
            q1, q2 = d.recv(), d.recv()
            check(q1[:3] == b"\x7f\x01\x00" and q1[3:10] == b"\x062.0.0\0" and q2[:3] == b"\x00\x01\x06",
                  f"QueueCommands held, then run before the next packet: {q1[:10].hex()} {q2[:3].hex()}")
            check(d.cmd(bytes([0x7F, 2, 0x00, 0x04, 0x11, 0x40, 0x42, 0x0F, 0x00]))[:4] == b"\x7f\x02\x00\x06",
                  "ExecuteCommands")
            check(d.cmd(bytes([0x14, 1, 8, 0xFF]))[:2] == b"\x14\xff", "JTAG_Sequence: DAP_ERROR (no JTAG)")
            check(d.cmd(bytes([0x1C, 0, 0]))[:1] == b"\xff", "SWO_Data: ID_DAP_Invalid (no SWO)")
            check(d.cmd(bytes([0x10, 0x00, 0x80, 0, 0, 0, 0]))[:2] == b"\x10\x00", "SWJ_Pins: the inputs read 0")

            # the UART: line coding, DTR, a line out and its echo
            u.control_out(bytes([0x21, 0x20, 0, 0, 1, 0, 7, 0]), struct.pack("<IBBB", 115200, 0, 0, 8))
            u.control_out(bytes([0x21, 0x22, 3, 0, 1, 0, 0, 0]))
            u.bulk_out(2, b"probe\r")
            got = b""
            t0 = time.time()
            while b"PROBE" not in got and time.time() - t0 < 20:
                _, _, st, body = u.bulk_in(3, 64)
                got += body
            check(b"PROBE" in got, f"the UART through the probe: {got[-60:]!r}")

            # the Rescue DP
            d.swj(LINE_RESET, 64)
            d.targetsel(0xF)
            check(d.rd(DP_RD(0)) == 0x0BC12477, "TARGETSEL the Rescue DP (0xf1002927): DPIDR")
            d.wr(DP_WR(0x4), 0x50000000)
            cs = d.rd(DP_RD(0x4))
            check(cs & 0xF0000000 == 0x70000000, f"its CTRL/STAT {cs:#010x}: CDBGPWRUPACK, no CSYSPWRUPACK")
            d.wr(DP_WR(0x4), 0)
            time.sleep(0.2)
            d.swj(LINE_RESET, 64)
            d.targetsel(0)
            d.rd(DP_RD(0))
            d.wr(DP_WR(0x4), 0x50000000)
            d.wr(AP_WR(0x0), 0x23000002)
            cr = d.mem_rd(0x40064008)
            d.mem_wr(0xE000EDF0, 0xA05F0003)
            d.mem_wr(0xE000EDF4, 15)
            pc = d.mem_rd(0xE000EDF8)
            check(cr & 0x01110100 == 0x00100000 and pc & ~3 == 0x28,
                  f"after the rescue: CHIP_RESET {cr:#010x} (HAD_PSM_RESTART), the bootrom's dead loop at {pc:#x}")
            outs = c.call("observe.snapshot", {"include": ["outputs"]})["outputs"]["P1"]
            check(outs.get("commands", 0) > 40 and outs.get("uart_tx_bytes") == 6,
                  f"the probe: {outs.get('commands')} packets, {outs.get('uart_tx_bytes')} bytes sent")
    print("debug probe: " + ("ok" if not failures else f"{failures} failure(s)"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
