#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""An STM32's package pinout and analog inputs from ST's open pin data
(STMicroelectronics/STM32_open_pin_data, BSD-3-Clause: the MCU files
STM32CubeMX reads), for a package description (schemas/package.schema.json)
and the MCU description's io.analog (SPEC-009 §7).

    stm32pins.py MCU.xml -o PACKAGE.json --id vendor/part/package [--analog ANALOG.json]

Each pin keeps its position; a port pin's signal is its port and number
(PA0, from "PA0-WKUP"), the rest of its name becoming the pin's doc; power,
reset and boot pins keep their names (VCAP_1, NRST, BOOT0). The analog map
names each port pin's ADC inputs as the reference manual does, the ADCs
sharing a channel joined (ADC123_IN0)."""
import argparse
import json
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import Optional

PORT_PIN = re.compile(r"^(P[A-K]\d{1,2})(?:[-_ ](.*))?$")
ADC_IN = re.compile(r"^ADC(\d)_IN(\d{1,2})$")


def tag(el: ET.Element) -> str:
    return el.tag.split("}")[-1]


def read(xml: Path) -> tuple[dict, list]:
    root = ET.parse(xml).getroot()
    return root.attrib, [el for el in root if tag(el) == "Pin"]


def package_of(attrs: dict, pins: list, ident: str, source: str) -> dict:
    out = []
    for p in sorted(pins, key=lambda e: int(e.get("Position", "0"))):
        name = p.get("Name", "")
        m = PORT_PIN.match(name)
        entry = {"number": int(p.get("Position", "0")), "signal": m.group(1) if m else name}
        if m and m.group(2):
            entry["doc"] = name
        out.append(entry)
    pkg = attrs.get("Package", "")
    return {
        "schema_version": 1,
        "kind": "package",
        "id": ident,
        "name": pkg,
        "description": f"{pkg} package of the {attrs.get('RefName', '')}, its pinout from ST's open pin data.",
        "pins": out,
        "provenance": {
            "author": "EmbSIM",
            "license": "Apache-2.0",
            "sources": [{"title": f"STMicroelectronics STM32_open_pin_data {source} (BSD-3-Clause), "
                                  "converted by tools/importers/stm32pins.py"}],
        },
    }


def analog_of(pins: list) -> dict:
    out = {}
    for p in pins:
        m = PORT_PIN.match(p.get("Name", ""))
        if not m:
            continue
        chans: dict[int, list[str]] = {}
        for s in p:
            a = ADC_IN.match(s.get("Name") or "")
            if a:
                chans.setdefault(int(a.group(2)), []).append(a.group(1))
        if chans:
            ch = min(chans)
            out[m.group(1)] = f"ADC{''.join(sorted(chans[ch]))}_IN{ch}"
    return dict(sorted(out.items(), key=lambda kv: (kv[0][:2], int(kv[0][2:]))))


def main(argv: Optional[list[str]] = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("xml", type=Path)
    ap.add_argument("-o", "--output", type=Path, required=True)
    ap.add_argument("--id", required=True, help="the package description's id")
    ap.add_argument("--analog", type=Path, help="also write the io.analog map here")
    args = ap.parse_args(argv)
    attrs, pins = read(args.xml)
    doc = package_of(attrs, pins, args.id, args.xml.name)
    args.output.write_text(json.dumps(doc, indent=1) + "\n", encoding="utf-8")
    print(f"{args.output}: {len(doc['pins'])} pins")
    if args.analog:
        args.analog.write_text(json.dumps(analog_of(pins), indent=1) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
