#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Words for emblink/pattern_generator that play bus transactions, for
fixtures and examples (the catalog's I2C and SPI fixtures use them).

  stimulus.py i2c 0x3C:w:00,AF 0x48:r:2     START, address, bytes or a read count, STOP
  stimulus.py spi 06 02,10,5A,C3            one CS-low frame per argument (mode 0)
  stimulus.py tm1637 40 C0,3F,06 8F          START, bytes LSB first, STOP per argument

I2C: OUT0 = SDA, OUT1 = SCL; use the generator with drive "open_drain" and
idle 3, and pull-ups on both lines. SPI: OUT0 = MOSI, OUT1 = SCK, OUT2 = CS.
TM1637: OUT0 = DIO, OUT1 = CLK, open drain and idle 3 as for I2C.
The output is the "pattern" parameter; --json prints the whole instance."""
import argparse
import json
import sys


def i2c_words(transactions):
    """[(addr, rw, bytes-or-read-count)] -> words"""
    w = [3]
    for addr, rw, data in transactions:
        w += [3, 2, 0]                              # START
        def byte(b):
            for i in range(7, -1, -1):
                bit = (b >> i) & 1
                w.extend([bit, bit | 2, bit])
            w.extend([1, 3, 1])                     # SDA released for the ACK clock
        byte((addr << 1) | rw)
        if rw == 0:
            for b in data:
                byte(b)
        else:
            for k in range(data):
                w.extend([1, 3, 1] * 8)
                ack = 0 if k < data - 1 else 1      # NACK the last byte
                w.extend([ack, ack | 2, ack])
        w += [0, 2, 3]                              # STOP
    return w


def spi_words(frames, bits=8):
    """[[bytes]] -> words, mode 0, CS low for each frame"""
    w = [4]
    for frame in frames:
        w.append(0)
        for b in frame:
            for i in range(bits - 1, -1, -1):
                bit = (b >> i) & 1
                w.extend([bit, bit | 2])
            w.append(0)
        w += [0, 4]
    return w + [4]


def tm1637_words(transactions):
    """[[bytes]] -> words: the TM1637's two-wire frames, LSB first, the
    9th clock with DIO released for the chip's ACK"""
    w = [3]
    for frame in transactions:
        w += [3, 2, 0]                              # START: DIO falls, CLK high
        for b in frame:
            for i in range(8):
                bit = (b >> i) & 1
                w.extend([bit, bit | 2, bit])
            w.extend([1, 3, 0])                     # ACK clock, then DIO held low
        w += [0, 2, 3]                              # STOP: DIO rises, CLK high
    return w


def hexbytes(s):
    return [int(x, 16) for x in s.split(",") if x]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0], formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    ap.add_argument("bus", choices=["i2c", "spi", "tm1637"])
    ap.add_argument("items", nargs="+", help="i2c: ADDR:w:HEX,HEX or ADDR:r:COUNT; spi: HEX,HEX per frame")
    ap.add_argument("--period", default="1u", help="word period (default 1u)")
    ap.add_argument("--json", action="store_true", help="print a pattern_generator instance")
    args = ap.parse_args(argv)
    if args.bus == "i2c":
        tx = []
        for item in args.items:
            addr, rw, rest = item.split(":")
            tx.append((int(addr, 0), 1 if rw == "r" else 0, int(rest, 0) if rw == "r" else hexbytes(rest)))
        words = i2c_words(tx)
        extra = {"drive": "open_drain", "idle": 3}
    elif args.bus == "tm1637":
        words = tm1637_words([hexbytes(f) for f in args.items])
        extra = {"drive": "open_drain", "idle": 3}
    else:
        words = spi_words([hexbytes(f) for f in args.items])
        extra = {}
    pattern = ",".join(f"{x:x}" for x in words)
    if args.json:
        params = {"pattern": pattern, "period": args.period, **extra}
        print(json.dumps({"id": "PG", "component": "emblink/pattern_generator", "params": params}, indent=2))
    else:
        print(pattern)
    return 0


if __name__ == "__main__":
    sys.exit(main())
