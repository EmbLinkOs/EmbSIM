#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Original ESP32 image inspection; independent struct/hashlib format fixtures."""
import argparse
import hashlib
from pathlib import Path
import struct
import subprocess
import tempfile


def image(segments, digest=True, chip=0):
    h = bytearray(24)
    h[:4] = bytes([0xe9, len(segments), 2, 0x20])
    struct.pack_into('<I', h, 4, 0x40080000)
    struct.pack_into('<H', h, 12, chip)
    h[23] = int(digest)
    checksum = 0xef
    for address, data in segments:
        h.extend(struct.pack('<II', address, len(data)))
        h.extend(data)
        for value in data:
            checksum ^= value
    h.extend(bytes((-len(h)-1) % 16))
    h.append(checksum)
    if digest:
        h.extend(hashlib.sha256(h).digest())
    return bytes(h)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--embsim', required=True, type=Path)
    args = ap.parse_args()
    checks = 0
    with tempfile.TemporaryDirectory(prefix='embsim-esp32-image-') as tmp:
        p = Path(tmp) / 'app.bin'

        def run(data, good, message=None):
            nonlocal checks
            p.write_bytes(data)
            r = subprocess.run([str(args.embsim.resolve()), 'image-info', 'esp32', str(p)],
                               capture_output=True, text=True, encoding='utf-8')
            assert r.returncode == (0 if good else 2), (r.returncode, r.stdout, r.stderr)
            if message:
                assert message in (r.stdout if good else r.stderr), (message, r.stdout, r.stderr)
            assert p.read_bytes() == data
            checks += 1

        for digest in [False, True]:
            for length in range(33):
                run(image([(0x40080000, bytes(range(length)))], digest), True,
                    'SHA-256 verified' if digest else 'SHA-256 absent')
        valid = image([(0x40080000, b'\x01\x02\x03\x04'), (0x3ffb0000, bytes(range(16)))])
        run(valid, True, '2 segments')
        for cut in range(len(valid)):
            run(valid[:cut], False)
        for offset in [0, 1, 2, 3, 12, 23, 24, 28, 32, len(valid)-33, len(valid)-1]:
            bad = bytearray(valid)
            bad[offset] ^= 0xff
            run(bad, False)
        # Two data flips can preserve XOR; the digest must still reject them.
        bad = bytearray(valid); bad[32] ^= 1; bad[33] ^= 1
        run(bad, False, 'SHA-256 mismatch')
        for chip in [2, 5, 9, 65535]:
            run(image([(0x40080000, b'abcd')], chip=chip), False, 'chip ID 0')
        run(valid+b'\0', False, 'Trailing bytes')
        run(image([(0xfffffffe, b'abcd')]), False, 'wraps')
        run(image([(0x40080000+i*16, bytes([i])*4) for i in range(16)]), True, '16 segments')
        run(image([(0, b'')]*17), False, '1 to 16')
        run(image([(0, b'')]*0), False, '1 to 16')
        # Valid XOR, invalid padding: SHA disabled so the padding check is exercised.
        bad = bytearray(image([(0x40080000, b'abcd')], False)); bad[36] = 1
        run(bad, False, 'padding')
        # Encodings are parsed, not silently used as board capacities or clocks.
        for mode in range(4):
            for frequency in [0, 1, 2, 15]:
                bad = bytearray(image([(0, b'')], False)); bad[2] = mode; bad[3] = 0x40 | frequency
                run(bad, True, f'SPI flash mode {mode}')
        for raw in [0x03, 0x50]:
            bad = bytearray(image([(0, b'')], False)); bad[3] = raw
            run(bad, False, 'encoding')
        bad = bytearray(valid); struct.pack_into('<I', bad, 28, 0xffffffff)
        run(bad, False, 'wraps')
    print(f'{checks} ESP32 image acceptance checks passed; inputs unchanged')


if __name__ == '__main__':
    main()
