#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""The GDB remote protocol server (`embsim gdb`) against the Blue Pill example:
the stop at reset, registers, memory, a breakpoint on main by its ELF
address, a single step, monitor stats and the kill."""
import argparse
import re
import socket
import struct
import subprocess
import sys
import time
from pathlib import Path


def elf_symbol(path: Path, name: str) -> int:
    f = path.read_bytes()
    shoff, = struct.unpack_from("<I", f, 32)
    shentsize, shnum = struct.unpack_from("<HH", f, 46)
    for i in range(shnum):
        sh = shoff + i * shentsize
        stype, = struct.unpack_from("<I", f, sh + 4)
        if stype != 2:
            continue
        off, size, link = struct.unpack_from("<III", f, sh + 16)
        stroff, = struct.unpack_from("<I", f, shoff + link * shentsize + 16)
        for k in range(size // 16):
            st_name, value = struct.unpack_from("<II", f, off + 16 * k)
            end = f.index(b"\0", stroff + st_name)
            if f[stroff + st_name:end].decode() == name:
                return value & ~1
    raise KeyError(name)


class Rsp:
    def __init__(self, port: int):
        self.s = socket.create_connection(("127.0.0.1", port), timeout=20)
        self.buf = b""

    def _read_packet(self) -> str:
        while True:
            m = re.search(rb"\$([^#]*)#[0-9a-fA-F]{2}", self.buf)
            if m:
                self.buf = self.buf[m.end():]
                self.s.sendall(b"+")
                return m.group(1).decode()
            chunk = self.s.recv(4096)
            if not chunk:
                raise ConnectionError("the server closed the connection")
            self.buf += chunk

    def call(self, data: str) -> str:
        csum = sum(data.encode()) & 0xFF
        self.s.sendall(f"${data}#{csum:02x}".encode())
        return self._read_packet()

    def send_only(self, data: str) -> None:
        csum = sum(data.encode()) & 0xFF
        self.s.sendall(f"${data}#{csum:02x}".encode())


def le32(hexstr: str) -> int:
    return struct.unpack("<I", bytes.fromhex(hexstr[:8]))[0]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--embsim", required=True)
    ap.add_argument("--project", required=True)
    ap.add_argument("--elf", required=True)
    args = ap.parse_args()
    main_addr = elf_symbol(Path(args.elf), "main")
    srv = subprocess.Popen([args.embsim, "gdb", args.project, "--port", "0"], stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, text=True)
    failures = []

    def check(cond: bool, what: str) -> None:
        print(("ok    " if cond else "FAIL  ") + what)
        if not cond:
            failures.append(what)

    try:
        line = srv.stdout.readline()
        m = re.search(r":(\d+) ", line)
        if not m:
            print("no port in:", line)
            return 1
        r = Rsp(int(m.group(1)))
        check("PacketSize" in r.call("qSupported:multiprocess+"), "qSupported")
        check(r.call("?").startswith("T05"), "stopped at reset")
        xml = r.call("qXfer:features:read:target.xml:0,fff")
        check("org.gnu.gdb.arm.m-profile" in xml, "target description")
        vec = r.call("m08000000,8")
        sp, reset = le32(vec[:8]), le32(vec[8:16])
        check(sp == 0x20005000, f"vector table SP 0x{sp:08x}")
        check(le32(r.call("pf")) == reset & ~1, "pc at the reset vector")
        g = r.call("g")
        check(len(g) == 17 * 8, "g packet: r0-pc and xpsr")
        check(r.call(f"Z0,{main_addr:x},2") == "OK", "breakpoint on main")
        check(r.call("c").startswith("T05"), "continue stops")
        check(le32(r.call("pf")) == main_addr, "at main")
        check(r.call("s").startswith("T05"), "a step")
        check(le32(r.call("pf")) != main_addr, "the step moved the pc")
        check(r.call(f"z0,{main_addr:x},2") == "OK", "breakpoint removed")
        check(r.call("m40021000,4") == "83000000", "RCC.CR read as a debugger")
        stats = r.call("qRcmd," + b"stats".hex())
        check(stats.startswith("O") and "instructions" in bytes.fromhex(stats[1:]).decode(), "monitor stats")
        r._read_packet()  # the OK after the console output
        r.send_only("k")
        srv.wait(timeout=20)
        check(srv.returncode == 0, "the server ends on kill")
    finally:
        if srv.poll() is None:
            srv.kill()
    print(f"{len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
