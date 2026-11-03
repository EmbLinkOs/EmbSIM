#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""The NUCLEO's ST-LINK on the computer's own USB stack, through USB/IP.

The session serves the bench's free USB devices over USB/IP (usbip.serve),
as Linux's usbip-host serves a real device to another computer's vhci-hcd.
This client speaks the protocol as vhci-hcd and the usbip tool do
(Documentation/usb/usbip_protocol.rst):

  - OP_REQ_DEVLIST lists the NUCLEO's ST-LINK: bus id 1-N, 0483:374B, full
    speed, its four interfaces (vendor, mass storage, CDC, CDC data);
  - OP_REQ_IMPORT takes it; URBs then carry the enumeration a computer
    makes (the device and configuration descriptors, the product string,
    SET_CONFIGURATION) and the ST-LINK's commands on its bulk endpoints, as
    st-flash and STM32CubeProgrammer send them: GET_VERSION (V2J37M26),
    GET_TARGET_VOLTAGE, SWD entered, READ_IDCODES (the F446's 0x2BA01477),
    a word of the target's flash read;
  - a URB waiting on a NAK (the empty trace endpoint) is unlinked;
  - the drive answers INQUIRY through the same import.

    test_usbip.py --embsim PATH
"""
import argparse
import json
import socket
import struct
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from embsim_client import StdioClient  # noqa: E402

EXAMPLE = ROOT / "examples" / "nucleo-f446re-blink-serial"
failures = 0


def check(ok: bool, what: str) -> None:
    global failures
    print(("  ok   " if ok else "  FAIL ") + what)
    if not ok:
        failures += 1


class Usbip:
    def __init__(self, port: int):
        self.s = socket.create_connection(("127.0.0.1", port), timeout=20)
        self.seq = 0
        self._in = False

    def recv(self, n: int) -> bytes:
        b = b""
        while len(b) < n:
            part = self.s.recv(n - len(b))
            if not part:
                raise RuntimeError("the USB/IP server closed the connection")
            b += part
        return b

    def devlist(self) -> list[dict]:
        self.s.sendall(struct.pack(">HHI", 0x0111, 0x8005, 0))
        _, code, status, n = struct.unpack(">HHII", self.recv(12))
        assert code == 0x0005 and status == 0
        devs = []
        for _ in range(n):
            d = self.recv(312)
            busid = d[256:288].split(b"\0")[0].decode()
            vid, pid = struct.unpack(">HH", d[300:304])
            speed, = struct.unpack(">I", d[296:300])
            nif = d[311]
            ifs = [tuple(self.recv(4)[:3]) for _ in range(nif)]
            devs.append({"busid": busid, "vid": vid, "pid": pid, "speed": speed, "interfaces": ifs})
        return devs

    def attach(self, busid: str) -> dict:
        self.s.sendall(struct.pack(">HHI32s", 0x0111, 0x8003, 0, busid.encode()))
        _, code, status = struct.unpack(">HHI", self.recv(8))
        assert code == 0x0003, code
        if status:
            return {}
        d = self.recv(312)
        return {"vid": struct.unpack(">H", d[300:302])[0], "pid": struct.unpack(">H", d[302:304])[0]}

    def submit(self, ep: int, din: bool, length: int, data: bytes = b"", setup: bytes = bytes(8), wait=True):
        self.seq += 1
        self.s.sendall(struct.pack(">IIIII", 1, self.seq, 0x00010002, 1 if din else 0, ep) +
                       struct.pack(">IIIII", 0, length if din else len(data), 0, 0, 0) + setup + (b"" if din else data))
        if not wait:
            return self.seq
        return self.result()

    def result(self):
        h = self.recv(48)
        cmd, seq = struct.unpack(">II", h[:8])
        status, actual = struct.unpack(">iI", h[20:28])
        body = self.recv(actual) if cmd == 3 and actual and self._in else b""  # an IN URB's data
        return cmd, seq, status, body

    def control_in(self, setup: bytes, length: int):
        self._in = True
        return self.submit(0, True, length, setup=setup)

    def control_out(self, setup: bytes, data: bytes = b""):
        self._in = False
        return self.submit(0, False, len(data), data, setup)

    def bulk_out(self, ep: int, data: bytes):
        self._in = False
        return self.submit(ep, False, len(data), data)

    def bulk_in(self, ep: int, length: int):
        self._in = True
        return self.submit(ep, True, length)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--embsim", default=str(ROOT / "build" / "dev" / "bin" / "embsim"))
    args = ap.parse_args()
    with tempfile.TemporaryDirectory(prefix="embsim-usbip-") as tmp:
        p = json.loads((EXAMPLE / "project.json").read_text(encoding="utf-8"))
        p["boards"][0]["firmware"]["image"] = str(EXAMPLE / "firmware" / "blink-serial.elf")
        src = Path(tmp) / "usbip.json"
        src.write_text(json.dumps(p), encoding="utf-8")
        with StdioClient([args.embsim, "sidecar", str(src)]) as c:
            r = c.call("usbip.serve", {"port": 0})
            check(r["port"] > 0 and [d["id"] for d in r["devices"]] == ["B1.U2"],
                  f"usbip.serve: port {r['port']}, devices {r['devices']}")
            c.call("control.run", {"speed": 1})
            time.sleep(0.5)
            u = Usbip(r["port"])
            devs = u.devlist()
            st = [d for d in devs if d["vid"] == 0x0483 and d["pid"] == 0x374B]
            check(len(st) == 1 and st[0]["speed"] == 2 and st[0]["interfaces"][0] == (0xFF, 0xFF, 0xFF) and
                  (8, 6, 0x50) in st[0]["interfaces"],
                  f"OP_REQ_DEVLIST: {[(d['busid'], hex(d['vid']), hex(d['pid']), d['interfaces']) for d in devs]}")
            if not st:
                return 1
            u.s.close()
            u = Usbip(r["port"])  # usbip attach: a new connection
            info = u.attach(st[0]["busid"])
            check(info.get("vid") == 0x0483 and info.get("pid") == 0x374B, f"OP_REQ_IMPORT {st[0]['busid']}: {info}")
            _, _, status, dev = u.control_in(bytes([0x80, 6, 0, 1, 0, 0, 18, 0]), 18)
            check(status == 0 and len(dev) == 18 and dev[8:12] == b"\x83\x04\x4b\x37", "the device descriptor")
            _, _, status, cfg = u.control_in(bytes([0x80, 6, 0, 2, 0, 0, 255, 0]), 255)
            check(status == 0 and len(cfg) == struct.unpack("<H", cfg[2:4])[0], f"the configuration ({len(cfg)} bytes)")
            _, _, status, s2 = u.control_in(bytes([0x80, 6, 2, 3, 9, 4, 255, 0]), 255)
            prod = s2[2:].decode("utf-16-le") if status == 0 else ""
            check(prod == "STM32 STLink", f"the product string: {prod!r}")
            _, _, status, _ = u.control_out(bytes([0, 9, 1, 0, 0, 0, 0, 0]))
            check(status == 0, "SET_CONFIGURATION")
            u.bulk_out(1, bytes([0xF1]) + bytes(15))
            _, _, status, v = u.bulk_in(1, 6)
            ver = (v[0] << 8 | v[1]) if len(v) >= 2 else 0
            check(status == 0 and ver >> 12 == 2 and (ver >> 6) & 63 == 37 and ver & 63 == 26 and v[2:6] == b"\x83\x04\x4b\x37",
                  f"GET_VERSION on 0x01 / 0x81: V{ver >> 12}J{(ver >> 6) & 63}M{ver & 63}")
            u.bulk_out(1, bytes([0xF7]) + bytes(15))
            _, _, status, tv = u.bulk_in(1, 8)
            a0, a1 = struct.unpack("<II", tv) if len(tv) == 8 else (1, 0)
            check(status == 0 and abs(2 * 1.2 * a1 / a0 - 3.3) < 0.05, f"GET_TARGET_VOLTAGE: {2 * 1.2 * a1 / a0:.3f} V")
            u.bulk_out(1, bytes([0xF2, 0x30, 0xA3]) + bytes(13))
            _, _, status, e = u.bulk_in(1, 2)
            check(status == 0 and e[:1] == b"\x80", "SWD entered")
            u.bulk_out(1, bytes([0xF2, 0x31]) + bytes(14))
            _, _, status, ids = u.bulk_in(1, 12)
            check(status == 0 and struct.unpack_from("<I", ids, 4)[0] == 0x2BA01477,
                  f"READ_IDCODES: {struct.unpack_from('<I', ids, 4)[0]:#010x}")
            u.bulk_out(1, bytes([0xF2, 0x07]) + struct.pack("<IH", 0x08000000, 8) + bytes(8))
            _, _, status, w = u.bulk_in(1, 8)
            elf_sp = 0x20000000
            check(status == 0 and len(w) == 8 and struct.unpack("<I", w[:4])[0] & 0xFFF00000 == elf_sp,
                  f"the flash's first words through READMEM_32BIT: {w.hex()}")
            # the trace endpoint has nothing: the URB waits, then is unlinked
            u._in = True
            seq = u.submit(2, True, 64, wait=False)
            time.sleep(0.3)
            u.seq += 1
            u.s.sendall(struct.pack(">IIIII", 2, u.seq, 0x00010002, 0, 0) + struct.pack(">I", seq) + bytes(24))
            cmd, rseq, status, _ = u.result()
            check(cmd == 4 and status == -104, f"a URB waiting on the trace endpoint's NAK unlinked: RET_UNLINK {status}")
            # the drive through the same import: INQUIRY
            cbw = b"USBC" + struct.pack("<IIBBB", 7, 36, 0x80, 0, 6) + bytes([0x12, 0, 0, 0, 36, 0]) + bytes(10)
            u.bulk_out(3, cbw)
            _, _, status, inq = u.bulk_in(3, 64)
            _, _, status2, csw = u.bulk_in(3, 13)
            check(status == 0 and inq[8:16] == b"MBED    " and csw[:4] == b"USBS" and csw[12] == 0,
                  f"the drive's INQUIRY: {inq[8:32]!r}")
            u.s.close()
    print("usbip: %s" % ("ok" if not failures else f"{failures} failure(s)"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
