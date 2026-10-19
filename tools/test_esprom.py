# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""esprom.py on a small ELF made here: the loadable segments at their
physical addresses inside the ROM, the ones outside left out, and a section
no segment loads placed where the ROM's copy table says its bytes come
from."""
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "importers"))
import esprom  # noqa: E402

BASE = 0x40000000


def elf(segments, sections):
    """a 32-bit little-endian ELF: `segments` [(paddr, bytes)] as PT_LOAD,
    `sections` [(addr, bytes)] as PROGBITS sections, after a null section"""
    ehsize, phentsize, shentsize = 52, 32, 40
    phoff = ehsize
    data_off = phoff + phentsize * len(segments)
    blobs, offs, at = [], [], data_off
    for _, b in list(segments) + list(sections):
        offs.append(at)
        blobs.append(b)
        at += len(b)
    shoff = at
    head = bytearray(b"\x7fELF" + bytes([1, 1, 1]) + bytes(9))
    head += struct.pack("<HHIIIIIHHHHHH", 2, 0xF3, 1, BASE, phoff, shoff, 0, ehsize, phentsize, len(segments),
                        shentsize, len(sections) + 1, 0)
    for i, (paddr, b) in enumerate(segments):
        head += struct.pack("<8I", 1, offs[i], paddr, paddr, len(b), len(b), 5, 4)
    body = b"".join(blobs)
    shdrs = bytes(shentsize)
    for i, (addr, b) in enumerate(sections):
        shdrs += struct.pack("<10I", 0, 1, 3, addr, offs[len(segments) + i], len(b), 0, 0, 4, 0)
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
        img = esprom.build(e, BASE, 0x100, (BASE + 0x40, BASE + 0x50))
        self.assertEqual(img[0x84:0x88], b"DATA")
        self.assertEqual(esprom.build(e, BASE, 0x100)[0x84:0x88], bytes(4))


if __name__ == "__main__":
    unittest.main()
