#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Writes the solderless breadboards' descriptions, symbols, artwork and
fixtures (catalog/components/breadboard_*), from their geometry:

- 0.1 in (2.54 mm) pitch; terminal strips of five holes (a-e, f-j) across
  a 0.3 in (7.62 mm) centre channel, as a DIP straddles it;
- power rails in pairs along the long edges, + (red line) then - (blue
  line) from the top of each pair, holes in groups of five with a hole's
  gap between groups;
- the BB830 (BusBoard; the MB-102 has the same layout): 165.1 x 54.6 x
  8.5 mm, 63 strips a side, four rails of 50 holes running the full
  length; the half size (BB400): 82.6 x 54.6 x 8.5 mm, 30 strips, rails
  of 25; the mini (BB170): 45.7 x 34.5 x 8.5 mm, 17 strips, no rails.

Run it after changing the geometry: python3 tools/catalog/gen_breadboard.py
"""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
P = 2.54
TOP = "abcde"
BOTTOM = "fghij"

BOARDS = {
    "breadboard_830": dict(name="Breadboard, 830 points", cols=63, rails=50, size=(165.1, 54.6, 8.5),
                           mpn="BB830", doc="Full-size solderless breadboard (BusBoard BB830; the MB-102 has the same layout)"),
    "breadboard_400": dict(name="Breadboard, 400 points", cols=30, rails=25, size=(82.6, 54.6, 8.5),
                           mpn="BB400", doc="Half-size solderless breadboard (BusBoard BB400)"),
    "breadboard_170": dict(name="Breadboard, 170 points", cols=17, rails=0, size=(45.7, 34.5, 8.5),
                           mpn="BB170", doc="Mini solderless breadboard (BusBoard BB170), no power rails"),
}


def r(v: float) -> float:
    return round(v, 3)


def geometry(b: dict) -> tuple[dict, list]:
    """pin -> (x, y) mm, and the ties"""
    w, d, _ = b["size"]
    cols, rails = b["cols"], b["rails"]
    x0 = (w - (cols - 1) * P) / 2
    # rows in pitches from the first rail: rails 0, 1; a..e 3..7; f..j 10..14; rails 16, 17
    span = 17 if rails else 11
    y0 = (d - span * P) / 2
    off = 3 if rails else 0
    pins: dict = {}
    ties: list = []
    for c in range(1, cols + 1):
        x = x0 + (c - 1) * P
        for k, row in enumerate(TOP):
            pins[f"{row}{c}"] = (r(x), r(y0 + (off + k) * P))
        for k, row in enumerate(BOTTOM):
            pins[f"{row}{c}"] = (r(x), r(y0 + (off + 7 + k) * P))
        ties.append([f"{row}{c}" for row in TOP])
        ties.append([f"{row}{c}" for row in BOTTOM])
    if rails:
        groups = rails // 5
        positions = groups * 5 + groups - 1
        rx0 = x0 + ((cols - 1) * P - (positions - 1) * P) / 2
        for name, row in (("TP", 0), ("TN", 1), ("BP", 16), ("BN", 17)):
            names = []
            for i in range(rails):
                g, k = divmod(i, 5)
                pins[f"{name}{i + 1}"] = (r(rx0 + (g * 6 + k) * P), r(y0 + row * P))
                names.append(f"{name}{i + 1}")
            ties.append(names)
    return pins, ties


def artwork(b: dict, pins: dict) -> str:
    w, d, _ = b["size"]
    cols, rails = b["cols"], b["rails"]
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {d}" width="{w}" height="{d}">',
           f'  <rect x="0" y="0" width="{w}" height="{d}" rx="1.6" fill="#f4f1ea" stroke="#d5d0c4" stroke-width="0.3"/>']
    a1 = pins["a1"]
    j1 = pins["j1"]
    ch = (pins["e1"][1] + pins["f1"][1]) / 2
    out.append(f'  <rect x="0.8" y="{r(ch - 1.1)}" width="{r(w - 1.6)}" height="2.2" fill="#e2ddd1"/>')
    if rails:
        for name, sign in (("TP", 1), ("TN", -1), ("BP", 1), ("BN", -1)):
            y = pins[f"{name}1"][1]
            ly = y - 1.9 if name.endswith("P") else y + 1.9
            color = "#d63031" if sign > 0 else "#2e6bd6"
            out.append(f'  <line x1="2" y1="{r(ly)}" x2="{r(w - 2)}" y2="{r(ly)}" stroke="{color}" stroke-width="0.35"/>')
            label = "+" if sign > 0 else "−"
            out.append(f'  <text x="1.3" y="{r(y + 0.8)}" font-size="2.2" fill="{color}" font-family="sans-serif">{label}</text>')
    for c in range(1, cols + 1):
        if c == 1 or c % 5 == 0:
            x = pins[f"a{c}"][0]
            out.append(f'  <text x="{x}" y="{r(a1[1] - 1.6)}" font-size="1.5" text-anchor="middle" fill="#8b867a" font-family="sans-serif">{c}</text>')
            out.append(f'  <text x="{x}" y="{r(j1[1] + 2.7)}" font-size="1.5" text-anchor="middle" fill="#8b867a" font-family="sans-serif">{c}</text>')
    for row in TOP + BOTTOM:
        y = pins[f"{row}1"][1]
        out.append(f'  <text x="{r(a1[0] - 2.2)}" y="{r(y + 0.5)}" font-size="1.5" text-anchor="middle" fill="#8b867a" font-family="sans-serif">{row}</text>')
    for name, (x, y) in pins.items():
        out.append(f'  <rect id="pin-{name.lower()}" x="{r(x - 0.5)}" y="{r(y - 0.5)}" width="1" height="1" fill="#3a3a3a"/>')
    out.append("</svg>")
    return "\n".join(out) + "\n"


def description(key: str, b: dict, pins: dict, ties: list) -> dict:
    w, d, h = b["size"]
    claims = (f"{len(pins)} holes; each strip of five ({b['cols']} a side) and each power rail is one conductor; "
              "strips across the centre channel and adjacent strips are isolated.")
    return {
        "schema_version": 1,
        "kind": "component",
        "id": f"emblink/{key}",
        "version": "1.0.0",
        "name": b["name"],
        "category": "connectors",
        "description": b["doc"] + ": 0.1 in pitch, strips of five holes either side of a 0.3 in channel" +
                       (", power rails along both edges." if b["rails"] else "."),
        "keywords": ["breadboard", "solderless", "protoboard", "bench", b["mpn"].lower()],
        "pins": [{"name": n, "model": {"kind": "passive"}, "required": False} for n in pins],
        "model": {"composite": {"components": [], "nets": {}, "ports": {}}},
        "symbol": {"svg": "symbol.svg", "width": 60, "height": 30, "pins": {}},
        "artwork": {"svg": "artwork.svg", "width_mm": w, "height_mm": d},
        "ties": ties,
        "bench": {"package": "breadboard", "size_mm": [w, d, h], "wiring_aid": True,
                  "pins": {n: [x, y] for n, (x, y) in pins.items()}},
        "fidelity": {
            "functional": {"claims": claims, "omissions": []},
            "timed": {"claims": claims, "omissions": []},
            "electrical": {
                "claims": claims + " The contacts are ideal.",
                "range": {"voltage_v": [-36, 36], "current_a": [0, 2]},
                "omissions": ["contact resistance (tens of milliohms a contact)",
                              "capacitance between adjacent strips (a few pF)",
                              "the rating of 36 V and 2 A a contact is not enforced",
                              "worn contacts and loose wires"],
                "validation": ["tests/*.json"],
            },
        },
        "provenance": {"author": "EmbSIM", "license": "Apache-2.0",
                       "sources": [{"title": "BusBoard Prototype Systems, BB830 / BB400 / BB170 solderless breadboard data sheets"}]},
        "tests": ["tests/*.json"],
    }


def fixture(key: str, name: str, doc: str, nets: dict, expect: dict) -> dict:
    return {
        "schema_version": 1, "kind": "component_test", "name": name, "description": doc,
        "policy": "electrical", "until": "1ms",
        "circuit": {
            "components": [
                {"id": "V1", "component": "emblink/vsource", "params": {"kind": "dc", "dc": 5}},
                {"id": "DUT", "component": f"emblink/{key}"},
                {"id": "RL", "component": "emblink/resistor", "params": {"resistance": "1k"}},
                {"id": "G", "component": "emblink/gnd"},
            ],
            "nets": nets,
        },
        "expect": expect,
    }


SYMBOL = """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 60 30" width="60" height="30">
  <rect x="1" y="1" width="58" height="28" rx="2" fill="var(--symbol-fill)" stroke="var(--symbol-stroke)" stroke-width="1.5"/>
  <g fill="var(--symbol-stroke)">
    <circle cx="10" cy="9" r="1.2"/><circle cx="16" cy="9" r="1.2"/><circle cx="22" cy="9" r="1.2"/><circle cx="28" cy="9" r="1.2"/>
    <circle cx="10" cy="21" r="1.2"/><circle cx="16" cy="21" r="1.2"/><circle cx="22" cy="21" r="1.2"/><circle cx="28" cy="21" r="1.2"/>
  </g>
  <text x="44" y="18" font-size="8" text-anchor="middle" fill="var(--symbol-stroke)" font-family="sans-serif">BB</text>
</svg>
"""


def main() -> None:
    for key, b in BOARDS.items():
        pins, ties = geometry(b)
        d = ROOT / "catalog" / "components" / key
        (d / "tests").mkdir(parents=True, exist_ok=True)
        (d / "component.json").write_text(json.dumps(description(key, b, pins, ties), indent=1) + "\n", encoding="utf-8")
        (d / "symbol.svg").write_text(SYMBOL, encoding="utf-8")
        (d / "artwork.svg").write_text(artwork(b, pins), encoding="utf-8")
        last = b["cols"]
        tests = [
            fixture(key, "strip", "The five holes of a strip are one conductor: 5 V into a3 reaches the load in e3.",
                    {"IN": ["V1.+", "DUT.a3"], "OUT": ["DUT.e3", "RL.1"], "GND": ["V1.-", "RL.2", "G.GND"]},
                    {"net:OUT": {"at": "0.5ms", "min_v": 4.99, "max_v": 5.01}}),
            fixture(key, "channel", "The centre channel isolates a strip from the one across it: e3 does not reach f3.",
                    {"IN": ["V1.+", "DUT.e3"], "OUT": ["DUT.f3", "RL.1"], "GND": ["V1.-", "RL.2", "G.GND"]},
                    {"net:OUT": {"at": "0.5ms", "min_v": -0.01, "max_v": 0.01}}),
            fixture(key, "neighbour", "Adjacent strips are isolated: a3 does not reach a4.",
                    {"IN": ["V1.+", "DUT.a3"], "OUT": ["DUT.a4", "RL.1"], "GND": ["V1.-", "RL.2", "G.GND"]},
                    {"net:OUT": {"at": "0.5ms", "min_v": -0.01, "max_v": 0.01}}),
            fixture(key, "far-strip", f"The last strip on the bottom half works as the first: j{last} reaches f{last}.",
                    {"IN": ["V1.+", f"DUT.j{last}"], "OUT": [f"DUT.f{last}", "RL.1"], "GND": ["V1.-", "RL.2", "G.GND"]},
                    {"net:OUT": {"at": "0.5ms", "min_v": 4.99, "max_v": 5.01}}),
        ]
        if b["rails"]:
            n = b["rails"]
            tests.append(fixture(key, "rail", f"A power rail runs the whole length: 5 V into TP1 reaches TP{n}, and BN1 grounds the load from BN{n}.",
                                 {"IN": ["V1.+", "DUT.TP1"], "OUT": [f"DUT.TP{n}", "RL.1"], "RET": ["RL.2", f"DUT.BN{n}"],
                                  "GND": ["V1.-", "DUT.BN1", "G.GND"]},
                                 {"net:OUT": {"at": "0.5ms", "min_v": 4.99, "max_v": 5.01}}))
            tests.append(fixture(key, "rail-pair", "The two rails of a pair are isolated: TP1 does not reach TN1.",
                                 {"IN": ["V1.+", "DUT.TP1"], "OUT": ["DUT.TN1", "RL.1"], "GND": ["V1.-", "RL.2", "G.GND"]},
                                 {"net:OUT": {"at": "0.5ms", "min_v": -0.01, "max_v": 0.01}}))
        for t in tests:
            (d / "tests" / f"{t['name']}.json").write_text(json.dumps(t, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
