#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Draft an EmbSIM board from an EmbLinkRTOS board description (the RTOS's
SPEC-014 §4 board YAML): MCU, oscillators, pins, console, LEDs, buttons and
bus devices become a board.json with a power fixture (tools/importers/boardgen.py).

    rtos2board.py boards/st/my_board.yaml --library mylib --out mylib/ [--mcu st/stm32f103c8]

Needs PyYAML. The result is a draft: check it against the schematic, then
run  embsim catalog validate mylib  and  tools/validate.py mylib."""
import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from boardgen import BoardModel, Device, write_board  # noqa: E402


def number(v):
    """RTOS numbers: 0x.., 8MHz, 32kHz, 32768, 1M (power-of-two sizes are not needed here)"""
    if isinstance(v, (int, float)):
        return int(v)
    s = str(v).strip().replace("_", "")
    m = re.match(r"^([0-9.]+)\s*(MHz|kHz|Hz)?$", s, re.I)
    if m:
        f = float(m.group(1)) * {"mhz": 1e6, "khz": 1e3, "hz": 1}.get((m.group(2) or "hz").lower(), 1)
        return int(round(f))
    return int(s, 0)


def gpio(pin):
    """PA2.af7 -> PA2"""
    return str(pin).split(".")[0].upper() if pin else None


def load(path: Path) -> BoardModel:
    import yaml
    doc = yaml.safe_load(path.read_text())
    if not isinstance(doc, dict) or "board" not in doc and doc.get("kind") != "board":
        raise SystemExit(f"{path}: not an EmbLinkRTOS board description")
    name = doc.get("name") or doc.get("board")
    model = BoardModel(name=re.sub(r"[^a-z0-9_]", "_", str(name).lower()), title=str(doc.get("doc") or name).split("\n")[0] if doc.get("doc") else str(name).replace("_", " "),
                       mcu=str(doc.get("soc", "")), source=str(path), tool="tools/importers/rtos2board.py")
    for k, o in (doc.get("oscillators") or {}).items():
        if (o or {}).get("source") in ("external_crystal", "external_clock", None):
            model.oscillators[k] = number(o["hz"])
    pins = doc.get("pins") or {}
    for bus, sigs in pins.items():
        model.buses[bus] = {role: gpio(p) for role, p in (sigs or {}).items()}
    c = doc.get("console")
    if c:
        dev = str(c.get("device", "usart1"))
        cp = c.get("pins") or pins.get(dev) or {}
        model.console = {"peripheral": dev, "tx": gpio(cp.get("tx")), "rx": gpio(cp.get("rx")), "baud": c.get("baud")}
    for led in doc.get("leds") or []:
        model.leds.append({"name": re.sub(r"[^a-z0-9_]", "_", str(led["name"]).lower()), "gpio": gpio(led["gpio"]),
                           "active": led.get("active", "high"), "color": led.get("color") or led.get("name") if led.get("name") in ("red", "green", "blue", "yellow", "white") else led.get("color")})
    for b in doc.get("buttons") or []:
        model.buttons.append({"name": re.sub(r"[^a-z0-9_]", "_", str(b["name"]).lower()), "gpio": gpio(b["gpio"]), "active": b.get("active", "low")})
    for d in doc.get("devices") or []:
        roles = {k: gpio(d[k]) for k in ("cs", "irq", "data") if d.get(k)}
        model.devices.append(Device(name=str(d.get("name", "dev")), compatible=str(d.get("compatible", "")), bus=d.get("bus"),
                                    address=number(d["address"]) if d.get("address") is not None else None, pins=roles))
    for skipped in ("flash_partitions", "defaults", "debug_probe"):
        if doc.get(skipped):
            model.notes.append(f"{skipped} is RTOS configuration and was not imported.")
    return model


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("board_yaml", type=Path)
    ap.add_argument("--library", required=True, help="library name for the board id")
    ap.add_argument("--out", type=Path, required=True, help="library directory to write into")
    ap.add_argument("--mcu", help="catalog MCU id, when the SoC name does not find one")
    args = ap.parse_args(argv)
    model = load(args.board_yaml)
    for w in write_board(model, args.library, args.out, args.mcu):
        print("warning:", w, file=sys.stderr)
    print(f"wrote {args.out / 'boards' / model.name / 'board.json'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
