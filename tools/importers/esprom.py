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
source; it may be given again for a table of its own (the ESP32-C3's
Bluetooth controller data, copied by ESP-IDF's
btdm_controller_rom_data_init from the entry at 0x400591F4). Some read-only sections are linked at the ROM's address on a
second bus and no segment loads them either (the ESP32-C3's
.rodata.interface, at 0x3FF1EE3C on the data bus: the Wi-Fi and Bluetooth
code's pointers and constant tables): --alias VADDR=PADDR:SIZE places each
section with bytes in VADDR's SIZE bytes at the same offset from PADDR. Words the mask holds outside every section are given with --word
ADDRESS=VALUE, the value a number or the address of a symbol of the ELF
(the ESP32-C3's ets_rom_layout_p, ROM1's last word, which ESP-IDF's ROM
linker script places at 0x3FF1FFFC and reads as a pointer to the ROM's
ets_rom_layout). Bytes nothing covers read as zeros (the ELF does not say
what the mask holds there). The SHA-256 of the result is printed for the
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


SHT_SYMTAB = 2


def symbol(elf: bytes, name: str) -> int:
    """the value of the ELF's symbol `name`"""
    shoff, = struct.unpack_from("<I", elf, 0x20)
    shentsize, shnum = struct.unpack_from("<HH", elf, 0x2E)
    heads = [struct.unpack_from("<10I", elf, shoff + i * shentsize) for i in range(shnum)]
    for h in heads:
        if h[1] != SHT_SYMTAB:
            continue
        strtab = heads[h[6]]
        for o in range(h[4], h[4] + h[5], 16):
            st_name, st_value = struct.unpack_from("<2I", elf, o)
            at = strtab[4] + st_name
            if elf[at:elf.index(b"\0", at)].decode("utf-8", "replace") == name:
                return st_value
    raise SystemExit(f"esprom: no symbol {name}")


def build(elf: bytes, base: int, size: int, tables=(), words=(), aliases=()) -> bytes:
    img = bytearray(size)
    for paddr, data in segments(elf):
        if paddr < base or paddr + len(data) > base + size:
            continue  # not in the ROM (a debugger's view of RAM)
        img[paddr - base:paddr - base + len(data)] = data
    for vaddr, paddr, length in aliases:
        for addr, data in sections(elf):
            if vaddr <= addr and addr + len(data) <= vaddr + length:
                o = paddr + (addr - vaddr) - base
                if o < 0 or o + len(data) > size:
                    raise SystemExit(f"esprom: section at 0x{addr:08x} falls outside the image")
                img[o:o + len(data)] = data
    for start, end in tables:
        entries = [struct.unpack_from("<3I", img, o - base) for o in range(start, end, 16)]
        for addr, data in sections(elf):
            for dst, dst_end, src in entries:
                if dst <= addr and addr + len(data) <= dst_end and dst < dst_end:
                    o = src + (addr - dst) - base
                    if 0 <= o and o + len(data) <= size:
                        img[o:o + len(data)] = data
    for addr, value in words:
        if not base <= addr <= base + size - 4:
            raise SystemExit(f"esprom: word 0x{addr:08x} outside the image")
        v = int(value, 0) if value[:1].isdigit() else symbol(elf, value)
        struct.pack_into("<I", img, addr - base, v)
    return bytes(img)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("elf", type=Path)
    ap.add_argument("--base", type=lambda s: int(s, 0), required=True)
    ap.add_argument("--size", type=lambda s: int(s, 0), required=True)
    ap.add_argument("--data-table", action="append", default=[], metavar="START:END",
                    help="a copy table of the ROM's RAM variables' initial values")
    ap.add_argument("--word", action="append", default=[], metavar="ADDRESS=VALUE",
                    help="a word outside every section: a number or a symbol's address")
    ap.add_argument("--alias", action="append", default=[], metavar="VADDR=PADDR:SIZE",
                    help="the ROM seen at VADDR on another bus: its sections are placed from PADDR")
    ap.add_argument("-o", "--output", type=Path, required=True)
    args = ap.parse_args(argv)
    aliases = []
    for a in args.alias:
        v, _, rest = a.partition("=")
        p, _, n = rest.partition(":")
        if not n:
            raise SystemExit(f"esprom: --alias {a}: VADDR=PADDR:SIZE")
        aliases.append((int(v, 0), int(p, 0), int(n, 0)))
    tables = [tuple(int(x, 0) for x in t.split(":")) for t in args.data_table]
    words = []
    for w in args.word:
        addr, _, value = w.partition("=")
        if not value:
            raise SystemExit(f"esprom: --word {w}: ADDRESS=VALUE")
        words.append((int(addr, 0), value))
    img = build(args.elf.read_bytes(), args.base, args.size, tables, words, aliases)
    args.output.write_bytes(img)
    print(f"{args.output}: {len(img)} bytes, sha256 {hashlib.sha256(img).hexdigest()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
