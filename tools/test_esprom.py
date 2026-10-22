# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""esprom.py on a small ELF made here: the loadable segments at their
physical addresses inside the ROM, the ones outside left out, and a section
no segment loads placed where the ROM's copy table says its bytes come
from, a section linked at the ROM's address on another bus placed through
its alias, and words outside every section given by number or by
symbol."""
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "importers"))
import esprom  # noqa: E402

BASE = 0x40000000


def elf(segments, sections, symbols=()):
    """a 32-bit little-endian ELF: `segments` [(paddr, bytes)] as PT_LOAD,
    `sections` [(addr, bytes)] as PROGBITS sections, after a null section,
    and `symbols` [(name, value)] in a symbol table and its strings"""
    ehsize, phentsize, shentsize = 52, 32, 40
    phoff = ehsize
    data_off = phoff + phentsize * len(segments)
    strtab = b"\0" + b"".join(n.encode() + b"\0" for n, _ in symbols)
    symtab, at_name = bytes(16), 1
    for n, v in symbols:
        symtab += struct.pack("<3IBBH", at_name, v, 0, 0x10, 0, 0xFFF1)
        at_name += len(n) + 1
    blobs, offs, at = [], [], data_off
    for _, b in list(segments) + list(sections) + [(0, symtab), (0, strtab)]:
        offs.append(at)
        blobs.append(b)
        at += len(b)
    shoff = at
    nsec = len(sections) + 3
    head = bytearray(b"\x7fELF" + bytes([1, 1, 1]) + bytes(9))
    head += struct.pack("<HHIIIIIHHHHHH", 2, 0xF3, 1, BASE, phoff, shoff, 0, ehsize, phentsize, len(segments),
                        shentsize, nsec, 0)
    for i, (paddr, b) in enumerate(segments):
        head += struct.pack("<8I", 1, offs[i], paddr, paddr, len(b), len(b), 5, 4)
    body = b"".join(blobs)
    shdrs = bytes(shentsize)
    for i, (addr, b) in enumerate(sections):
        shdrs += struct.pack("<10I", 0, 1, 3, addr, offs[len(segments) + i], len(b), 0, 0, 4, 0)
    k = len(segments) + len(sections)
    shdrs += struct.pack("<10I", 0, 2, 0, 0, offs[k], len(symtab), nsec - 1, 1, 4, 16)
    shdrs += struct.pack("<10I", 0, 3, 0, 0, offs[k + 1], len(strtab), 0, 0, 1, 0)
    return bytes(head) + body + shdrs


class EspromTest(unittest.TestCase):
    def test_segments_at_their_addresses(self):
        img = esprom.build(elf([(BASE + 0x10, b"ROM!"), (0x3FC80000, b"RAM?")], []), BASE, 0x100)
        self.assertEqual(len(img), 0x100)
        self.assertEqual(img[0x10:0x14], b"ROM!")
        self.assertEqual(img[:0x10], bytes(0x10))  # what no segment covers reads 0
        self.assertNotIn(b"RAM?", img)  # a segment outside the ROM is not its

    def test_copy_table_places_sections(self):
        # the table at +0x40: {dst 0x3FCE0000, dst end 0x3FCE0008, src +0x80, 0}
        table = struct.pack("<4I", 0x3FCE0000, 0x3FCE0008, BASE + 0x80, 0)
        e = elf([(BASE + 0x40, table)], [(0x3FCE0004, b"DATA")])
        img = esprom.build(e, BASE, 0x100, [(BASE + 0x40, BASE + 0x50)])
        self.assertEqual(img[0x84:0x88], b"DATA")
        self.assertEqual(esprom.build(e, BASE, 0x100)[0x84:0x88], bytes(4))

    def test_two_copy_tables(self):
        # an entry of its own before the table (the ESP32-C3's Bluetooth
        # data, which ESP-IDF copies itself): both are placed
        tables = struct.pack("<3I", 0x3FCD0000, 0x3FCD0004, BASE + 0x90) + struct.pack(
            "<4I", 0x3FCE0000, 0x3FCE0008, BASE + 0x80, 0)
        e = elf([(BASE + 0x34, tables)], [(0x3FCE0004, b"DATA"), (0x3FCD0000, b"BTDM")])
        img = esprom.build(e, BASE, 0x100, [(BASE + 0x40, BASE + 0x50), (BASE + 0x34, BASE + 0x40)])
        self.assertEqual(img[0x84:0x88], b"DATA")
        self.assertEqual(img[0x90:0x94], b"BTDM")
        self.assertEqual(esprom.build(e, BASE, 0x100, [(BASE + 0x40, BASE + 0x50)])[0x90:0x94], bytes(4))

    def test_alias_places_sections_no_segment_loads(self):
        # the ROM's last 0x80 bytes seen at 0x3FF00000 on the data bus (the
        # ESP32-C3's .rodata.interface: pointers no segment loads)
        e = elf([(BASE, b"CODE")], [(0x3FF00010, b"PTR!"), (0x3FC80000, b"RAM?")])
        img = esprom.build(e, BASE, 0x100, aliases=[(0x3FF00000, BASE + 0x80, 0x80)])
        self.assertEqual(img[0x90:0x94], b"PTR!")
        self.assertNotIn(b"RAM?", img)  # a section outside the alias is not placed
        self.assertEqual(esprom.build(e, BASE, 0x100)[0x90:0x94], bytes(4))
        with self.assertRaises(SystemExit):  # an alias whose bytes fall outside the image
            esprom.build(e, BASE, 0x100, aliases=[(0x3FF00000, BASE + 0xF0, 0x80)])

    def test_words_by_number_and_symbol(self):
        e = elf([(BASE, b"CODE")], [], [("ets_rom_layout", 0x3FF1BE3C)])
        img = esprom.build(e, BASE, 0x100, words=[(BASE + 0xFC, "ets_rom_layout"), (BASE + 0x10, "0x12345678")])
        self.assertEqual(struct.unpack_from("<I", img, 0xFC)[0], 0x3FF1BE3C)
        self.assertEqual(struct.unpack_from("<I", img, 0x10)[0], 0x12345678)
        with self.assertRaises(SystemExit):
            esprom.build(e, BASE, 0x100, words=[(BASE + 0x10, "no_such_symbol")])
        with self.assertRaises(SystemExit):
            esprom.build(e, BASE, 0x100, words=[(BASE + 0x100, "1")])


if __name__ == "__main__":
    unittest.main()
