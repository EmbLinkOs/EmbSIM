#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Make an ESP32-series chip's ROM image from Espressif's ROM ELF
(espressif/esp-rom-elfs, Apache-2.0), for an MCU description's ROM region.

    esprom.py esp32c3_rev3_rom.elf --base 0x40000000 --size 0x60000 -o rom.bin

The image holds every loadable segment with bytes in the file at its load
(physical) address: the code, the read-only data, and the initial values of
the ROM's RAM variables, which its startup copies out itself. Some of those
initial values are in sections no segment loads (the ESP32-C3's
.data.interface pointers): --data-table START:END names the ROM's own copy
table, entries of {destination, destination end, source, 0}, and each
section with bytes that falls in a destination range is placed at its
source. Bytes nothing covers read as zeros (the ELF does not say what the
mask holds there). The SHA-256 of the result is printed for the
description."""
import argparse
import hashlib
import struct
import sys
from pathlib import Path

PT_LOAD = 1


def segments(elf: bytes):
    """(paddr, bytes) of each PT_LOAD segment with bytes in the file"""
    if elf[:4] != b"\x7fELF" or elf[4] != 1 or elf[5] != 1:
        raise SystemExit("esprom: not a 32-bit little-endian ELF")
    phoff, = struct.unpack_from("<I", elf, 0x1C)
    phentsize, phnum = struct.unpack_from("<HH", elf, 0x2A)
    for i in range(phnum):
        p_type, p_offset, _vaddr, p_paddr, p_filesz, _memsz, _flags, _align = struct.unpack_from(
            "<8I", elf, phoff + i * phentsize)
        if p_type == PT_LOAD and p_filesz:
            yield p_paddr, elf[p_offset:p_offset + p_filesz]


SHT_PROGBITS = 1


def sections(elf: bytes):
    """(address, bytes) of each PROGBITS section with bytes in the file"""
    shoff, = struct.unpack_from("<I", elf, 0x20)
    shentsize, shnum = struct.unpack_from("<HH", elf, 0x2E)
    for i in range(shnum):
        _name, sh_type, _flags, addr, off, size = struct.unpack_from("<6I", elf, shoff + i * shentsize)
        if sh_type == SHT_PROGBITS and size and addr:
            yield addr, elf[off:off + size]


def build(elf: bytes, base: int, size: int, table=None) -> bytes:
    img = bytearray(size)
    for paddr, data in segments(elf):
        if paddr < base or paddr + len(data) > base + size:
            continue  # not in the ROM (a debugger's view of RAM)
        img[paddr - base:paddr - base + len(data)] = data
    if table:
        start, end = table
        entries = [struct.unpack_from("<3I", img, o - base) for o in range(start, end, 16)]
        for addr, data in sections(elf):
            for dst, dst_end, src in entries:
                if dst <= addr and addr + len(data) <= dst_end and dst < dst_end:
                    o = src + (addr - dst) - base
                    if 0 <= o and o + len(data) <= size:
                        img[o:o + len(data)] = data
    return bytes(img)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("elf", type=Path)
    ap.add_argument("--base", type=lambda s: int(s, 0), required=True)
    ap.add_argument("--size", type=lambda s: int(s, 0), required=True)
    ap.add_argument("--data-table", default=None, metavar="START:END",
                    help="the ROM's copy table of its RAM variables' initial values")
    ap.add_argument("-o", "--output", type=Path, required=True)
    args = ap.parse_args(argv)
    table = tuple(int(x, 0) for x in args.data_table.split(":")) if args.data_table else None
    img = build(args.elf.read_bytes(), args.base, args.size, table)
    args.output.write_bytes(img)
    print(f"{args.output}: {len(img)} bytes, sha256 {hashlib.sha256(img).hexdigest()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
