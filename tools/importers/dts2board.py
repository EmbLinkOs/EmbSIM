#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Draft an EmbSIM board from a DeviceTree source file (a Zephyr or Linux
board .dts): the SoC from its #include, the model, the console from
`chosen`, HSE/LSE frequencies, gpio-leds and gpio-keys, UART/I2C/SPI pins
from Zephyr-style pinctrl names (usart1_tx_pa9) and the devices on those
buses become a board.json with a power fixture (tools/importers/boardgen.py).

    dts2board.py my_board.dts --library mylib --out mylib/ [--mcu st/stm32f103c8]

The parser reads DTS syntax itself (no dtc): nodes, labels, &label
overlays and properties; included .dtsi files are not read, so labels they
define are taken by name (&usart1 is the SoC's usart1)."""
import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from boardgen import BoardModel, Device, write_board  # noqa: E402

TOKEN = re.compile(r'''\s*(?:
    (?P<str>"(?:\\.|[^"\\])*")|
    (?P<cells><[^>]*>)|
    (?P<bytes>\[[^\]]*\])|
    (?P<ref>&[A-Za-z_][\w-]*)|
    (?P<punct>[{};=,])|
    (?P<word>/?[A-Za-z0-9_,.+\-@#?/:]+)
)''', re.X)


class Node:
    def __init__(self, name, labels=()):
        self.name, self.labels, self.props, self.children = name, list(labels), {}, []

    def find(self, pred):
        if pred(self):
            yield self
        for c in self.children:
            yield from c.find(pred)

    @property
    def compatible(self):
        v = self.props.get("compatible")
        return v[0] if v else None


def strip(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    includes = re.findall(r'^\s*#include\s*[<"]([^>"]+)[>"]', text, flags=re.M)
    text = re.sub(r"^\s*#[^\n]*", " ", text, flags=re.M)
    return text, includes


def tokens(text):
    pos = 0
    out = []
    while pos < len(text):
        m = TOKEN.match(text, pos)
        if not m:
            if text[pos:].strip() == "":
                break
            raise SystemExit(f"cannot read the DTS near: {text[pos:pos + 40]!r}")
        pos = m.end()
        kind = m.lastgroup
        out.append((kind, m.group(kind)))
    return out


def parse_value(toks, i):
    """a property value: a list of strings, cell lists, byte strings, refs"""
    vals = []
    while toks[i][1] != ";":
        kind, v = toks[i]
        if kind == "str":
            vals.append(bytes(v[1:-1], "utf-8").decode("unicode_escape"))
        elif kind == "cells":
            vals.append(cells(v[1:-1]))
        elif kind in ("ref", "word", "bytes"):
            vals.append(v)
        i += 1
        if toks[i][1] == ",":
            i += 1
    return vals, i + 1


def cells(s):
    """the cells of <...>: numbers, &refs and identifiers or macro calls kept as text"""
    out = []
    for t in re.findall(r"&[\w-]+|\w+\([^)]*\)|0x[0-9a-fA-F]+|\d+|[A-Za-z_]\w*", s):
        if re.fullmatch(r"0x[0-9a-fA-F]+|\d+", t):
            out.append(int(t, 0))
        else:
            out.append(t)
    return out


def parse(text):
    toks = tokens(text)
    root, overlays = Node("/"), []
    i = 0

    def body(node, i):
        while toks[i][1] != "}":
            labels = []
            while toks[i][0] == "word" and toks[i][1].endswith(":") and toks[i + 1][1] not in ("=", ";"):
                labels.append(toks[i][1][:-1])
                i += 1
            kind, v = toks[i]
            nxt = toks[i + 1][1]
            if nxt == "{":
                child = Node(v, labels)
                node.children.append(child)
                i = body(child, i + 2)
            elif nxt == "=":
                node.props[v], i = parse_value(toks, i + 2)
            elif nxt == ";":
                node.props[v] = []
                i += 2
            else:
                i += 1                      # directives such as /delete-property/
        i += 1
        if i < len(toks) and toks[i][1] == ";":
            i += 1
        return i

    while i < len(toks):
        kind, v = toks[i]
        if v == "/dts-v1/" or v == "/plugin/":
            i += 2
        elif v == "/" and toks[i + 1][1] == "{":
            i = body(root, i + 2)
        elif kind == "ref" and toks[i + 1][1] == "{":
            n = Node(v[1:])
            overlays.append(n)
            i = body(n, i + 2)
        else:
            i += 1
    return root, overlays


def freq(cell):
    if isinstance(cell, int):
        return cell
    m = re.fullmatch(r"DT_FREQ_([MK])\((\d+)\)", str(cell))
    if m:
        return int(m.group(2)) * (1000000 if m.group(1) == "M" else 1000)
    return None


def gpio_of(spec):
    """<&gpioc 13 GPIO_ACTIVE_LOW> -> ("PC13", "low")"""
    if not spec or len(spec) < 2 or not str(spec[0]).startswith("&gpio"):
        return None, None
    port = str(spec[0])[5:].upper()
    active = "low" if any("ACTIVE_LOW" in str(x) or x == 1 for x in spec[2:3]) else "high"
    return f"P{port}{spec[1]}", active


def pinctrl_pins(node):
    """Zephyr pinctrl names: usart1_tx_pa9, i2c1_scl_pb6, spi1_sck_master_pa5 -> {role: pin}"""
    out = {}
    for group in node.props.get("pinctrl-0", []):
        for ref in group if isinstance(group, list) else [group]:
            m = re.match(r"&\w+?_(tx|rx|scl|sda|sck|miso|mosi|nss)(?:_\w+?)?_p([a-z])(\d+)$", str(ref))
            if m:
                out[m.group(1)] = f"P{m.group(2).upper()}{m.group(3)}"
    return out


def load(path: Path) -> BoardModel:
    text, includes = strip(path.read_text())
    root, overlays = parse(text)
    soc = next((Path(inc).name for inc in includes if re.search(r"stm32|atmega|nrf|rp20|esp32|sam", inc, re.I) and "pinctrl" not in inc), "")
    model_name = (root.props.get("model") or [path.stem])[0]
    compat = root.compatible or path.stem
    name = re.sub(r"[^a-z0-9_]", "_", compat.split(",")[-1].lower()).strip("_") or "board"
    model = BoardModel(name=name, title=model_name, mcu=soc, source=str(path), tool="tools/importers/dts2board.py")
    if not soc:
        model.notes.append("no SoC .dtsi include found; pass --mcu")
    by_label = {}
    for n in root.find(lambda n: True):
        for lab in n.labels:
            by_label[lab] = n
    ovl = {o.name: o for o in overlays}
    # clocks
    for kind in ("hse", "lse"):
        o = ovl.get(f"clk_{kind}")
        if o and (o.props.get("status") or ["okay"])[0] == "okay":
            hz = freq((o.props.get("clock-frequency") or [[None]])[0][0])
            if kind == "lse" and hz is None:
                hz = 32768
            if hz:
                model.oscillators[kind] = hz
    # LEDs and keys
    for n in root.find(lambda n: n.compatible in ("gpio-leds", "gpio-keys")):
        for i, ch in enumerate(n.children):
            g, act = gpio_of((ch.props.get("gpios") or [[]])[0])
            if not g:
                continue
            label = (ch.props.get("label") or [ch.name])[0]
            nm = re.sub(r"[^a-z0-9_]", "_", str(label).lower()).strip("_") or f"item{i}"
            if not re.match(r"^[a-z]", nm):
                nm = "n_" + nm
            if n.compatible == "gpio-leds":
                model.leds.append({"name": nm, "gpio": g, "active": act})
            else:
                model.buttons.append({"name": nm, "gpio": g, "active": act})
    # buses and their devices
    console_ref = None
    chosen = next(root.find(lambda n: n.name == "chosen"), None)
    if chosen:
        v = chosen.props.get("zephyr,console") or chosen.props.get("stdout-path")
        if v:
            console_ref = str(v[0]).lstrip("&").split(":")[0]
    for oname, o in ovl.items():
        if (o.props.get("status") or ["okay"])[0] != "okay":
            continue
        pins = pinctrl_pins(o)
        if re.match(r"^(usart|uart|lpuart)\d+$", oname):
            if oname == console_ref:
                baud = (o.props.get("current-speed") or [[115200]])[0][0]
                model.console = {"peripheral": oname, "tx": pins.get("tx"), "rx": pins.get("rx"), "baud": baud if isinstance(baud, int) else 115200}
            else:
                model.gpios += [p for p in (pins.get("tx"), pins.get("rx")) if p]
        elif re.match(r"^(i2c|spi)\d+$", oname):
            model.buses[oname] = pins
            csg = o.props.get("cs-gpios") or []
            for ch in o.children:
                if not ch.compatible:
                    continue
                reg = (ch.props.get("reg") or [[None]])[0]
                reg = reg[0] if isinstance(reg, list) and reg else None
                dev = Device(name=ch.name.split("@")[0], compatible=ch.compatible, bus=oname)
                if oname.startswith("i2c"):
                    dev.address = reg
                elif isinstance(reg, int) and reg < len(csg):
                    g, _ = gpio_of(csg[reg])
                    if g:
                        dev.pins["cs"] = g
                for pk, role in (("int-gpios", "irq"), ("irq-gpios", "irq"), ("alert-gpios", "irq")):
                    g, _ = gpio_of((ch.props.get(pk) or [[]])[0])
                    if g:
                        dev.pins[role] = g
                model.devices.append(dev)
    # devices hung off GPIOs at the root (a DHT sensor)
    for n in root.children:
        if n.compatible and n.compatible.startswith("aosong,"):
            g, _ = gpio_of((n.props.get("dio-gpios") or [[]])[0])
            if g:
                model.devices.append(Device(name=n.name, compatible=n.compatible, pins={"data": g}))
    return model


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("dts", type=Path)
    ap.add_argument("--library", required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--mcu")
    args = ap.parse_args(argv)
    model = load(args.dts)
    for w in write_board(model, args.library, args.out, args.mcu):
        print("warning:", w, file=sys.stderr)
    print(f"wrote {args.out / 'boards' / model.name / 'board.json'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
