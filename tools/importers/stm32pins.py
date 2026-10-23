#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""An STM32's package pinout and analog inputs from ST's open pin data
(STMicroelectronics/STM32_open_pin_data, BSD-3-Clause: the MCU files
STM32CubeMX reads), for a package description (schemas/package.schema.json)
and the MCU description's io.analog (SPEC-009 §7).

    stm32pins.py MCU.xml -o PACKAGE.json --id vendor/part/package [--analog ANALOG.json]
                 [--alternate GPIO_MODES.xml ALTERNATE.json]

Each pin keeps its position; a port pin's signal is its port and number
(PA0, from "PA0-WKUP"), the rest of its name becoming the pin's doc; power,
reset and boot pins keep their names (VCAP_1, NRST, BOOT0). The analog map
names each port pin's ADC inputs as the reference manual does, the ADCs
sharing a channel joined (ADC123_IN0). The alternate-function map, from
the family's GPIO modes file (GPIO-<family>_Modes.xml), gives each of the
package's port pins the signals of each AF number (RM0390 7.3.2's table),
AF15 being EVENTOUT on every pin; the debug port's signals lose their SYS_
prefix (JTMS_SWDIO)."""
import argparse
import json
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import Optional

PORT_PIN = re.compile(r"^(P[A-K]\d{1,2})(?:[-_ ](.*))?$")
ADC_IN = re.compile(r"^ADC(\d)_IN(\d{1,2})$")
AF_VALUE = re.compile(r"^GPIO_AF(\d{1,2})_")


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


def alternate_of(modes: Path, pins: list) -> dict:
    """Each of the package's port pins (`pins`, from the MCU file) mapped
    to its alternate functions: AF number -> the signals on it."""
    have = {m.group(1) for p in pins if (m := PORT_PIN.match(p.get("Name", "")))}
    out = {}
    for g in ET.parse(modes).getroot():
        m = PORT_PIN.match(g.get("Name", "")) if tag(g) == "GPIO_Pin" else None
        if not m or m.group(1) not in have:
            continue
        afs: dict[int, list[str]] = {}
        for s in g:
            if tag(s) != "PinSignal":
                continue
            for v in s.iter():
                a = AF_VALUE.match((v.text or "").strip())
                if a:
                    name = s.get("Name", "")
                    if name.startswith("SYS_"):
                        name = name[4:].replace("-", "_")
                    afs.setdefault(int(a.group(1)), []).append(name)
        afs[15] = ["EVENTOUT"]
        out[m.group(1)] = {str(k): sorted(set(afs[k])) for k in sorted(afs)}
    return dict(sorted(out.items(), key=lambda kv: (kv[0][:2], int(kv[0][2:]))))


def main(argv: Optional[list[str]] = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("xml", type=Path)
    ap.add_argument("-o", "--output", type=Path, required=True)
    ap.add_argument("--id", required=True, help="the package description's id")
    ap.add_argument("--analog", type=Path, help="also write the io.analog map here")
    ap.add_argument("--alternate", type=Path, nargs=2, metavar=("GPIO_MODES.xml", "OUT.json"),
                    help="also write the io.alternate map from the family's GPIO modes file")
    args = ap.parse_args(argv)
    attrs, pins = read(args.xml)
    doc = package_of(attrs, pins, args.id, args.xml.name)
    args.output.write_text(json.dumps(doc, indent=1) + "\n", encoding="utf-8")
    print(f"{args.output}: {len(doc['pins'])} pins")
    if args.analog:
        args.analog.write_text(json.dumps(analog_of(pins), indent=1) + "\n", encoding="utf-8")
    if args.alternate:
        alt = alternate_of(args.alternate[0], pins)
        args.alternate[1].write_text(json.dumps(alt, indent=1) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
