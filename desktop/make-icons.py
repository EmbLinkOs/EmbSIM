#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Draws the application icon (a chip with a pulse across it, in the
workbench's mint accent on its dark surface) and writes every size the
Tauri bundles use into desktop/src-tauri/icons. Needs Pillow.

    python3 desktop/make-icons.py
"""
from pathlib import Path

from PIL import Image, ImageDraw

OUT = Path(__file__).resolve().parent / "src-tauri" / "icons"
S = 2048  # drawn large, then scaled down with antialiasing
SURFACE = (15, 21, 32, 255)
EDGE = (40, 50, 74, 255)
MINT = (95, 227, 177, 255)
CHIP = (27, 34, 48, 255)


def draw() -> Image.Image:
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    m = S // 16
    d.rounded_rectangle([m, m, S - m, S - m], radius=S // 5, fill=SURFACE, outline=EDGE, width=S // 64)
    # the chip and its pins
    c0, c1 = int(S * 0.30), int(S * 0.70)
    pin_w, pin_l = S // 34, S // 11
    for k in range(4):
        p = c0 + (c1 - c0) * (k + 1) // 5
        d.rounded_rectangle([p - pin_w, c0 - pin_l, p + pin_w, c0], radius=pin_w // 2, fill=MINT)
        d.rounded_rectangle([p - pin_w, c1, p + pin_w, c1 + pin_l], radius=pin_w // 2, fill=MINT)
        d.rounded_rectangle([c0 - pin_l, p - pin_w, c0, p + pin_w], radius=pin_w // 2, fill=MINT)
        d.rounded_rectangle([c1, p - pin_w, c1 + pin_l, p + pin_w], radius=pin_w // 2, fill=MINT)
    d.rounded_rectangle([c0, c0, c1, c1], radius=S // 28, fill=CHIP, outline=MINT, width=S // 48)
    # a pulse across the die: the signal the simulator draws
    y_lo, y_hi = int(S * 0.58), int(S * 0.42)
    x = [int(S * f) for f in (0.35, 0.43, 0.43, 0.57, 0.57, 0.65)]
    pts = [(x[0], y_lo), (x[1], y_lo), (x[2], y_hi), (x[3], y_hi), (x[4], y_lo), (x[5], y_lo)]
    d.line(pts, fill=MINT, width=S // 36, joint="curve")
    return im


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    big = draw()
    sized = lambda n: big.resize((n, n), Image.LANCZOS)
    for name, n in [("32x32.png", 32), ("128x128.png", 128), ("128x128@2x.png", 256), ("icon.png", 512)]:
        sized(n).save(OUT / name)
    sized(256).save(OUT / "icon.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    sized(1024).save(OUT / "icon.icns")
    print(f"icons written to {OUT}")


if __name__ == "__main__":
    main()
