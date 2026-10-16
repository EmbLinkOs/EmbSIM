#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC)
# Modifications copyright 2026 Junior Deogracias
"""Convert a Microchip EDC file (a device pack's edc/PART.PIC) for a PIC32
into an EmbSIM register description (schemas/registers.schema.json), and
optionally its package's pin map (schemas/package.schema.json).

    edc2regs.py PART.PIC -o registers.json [--atdf PART.atdf]
                [--package tqfp100.json --package-id vendor/part/pkg --package-pins 100]

What it reads, as EmbCC's EDC reader (tools/embsim/edc.c) does:
  - the SFR data sectors of <edc:PhysicalSpace>: each <edc:SFRDef> at its
    physical address (edc:_addr), its power-on value (edc:por, one
    character a bit from the top: 1, 0, - unimplemented, x unknown), each
    bit's access (edc:access: n read-write, r read-only, w write-only,
    c clearable, s settable) and the fields of its datasheet mode
    (<edc:SFRMode edc:id="DS.0">, placed from bit 0 up past its
    <edc:AdjustPoint> gaps); implemented bits no field names get a field
    of their own, so what a program writes there stays. A register whose
    edc:portals has CLR, SET and INV says so (clr_set_inv).
  - the peripherals: each register's instance (ltx:baseofperipheral, else
    ltx:memberofperipheral, else edc:grp), the ports' together as GPIO.
  - DEVID: the device ID with the latest silicon revision listed, read-only.
  - <edc:InterruptList>'s requests (<edc:InterruptRequest>), by number:
    the interrupt flags' bits.
  - the requests' vectors, from the part's ATDF (its <interrupt>s give
    both numbers, index the vector and irq-index the request; the EDC
    lists the vectors and the requests apart, by different names).
  - <edc:PinList>: each pin's functions (CN15, U2TX, AN5...) by the port
    pin among them (RD6), and with --package the pins in the package's
    order.
The ATDF's own registers are not used: for the PIC32MX it places 35
peripherals 0xC low and leaves out port G and the change notices (EmbCC's
tests/golden/embsim-pic32mx.sh). The pack is Apache-2.0: a description
made from it may be distributed with the pack's licence.

Provenance: EmbLinkOs/EmbCC aa4f10a tools/embsim/edc.c (edc_load32,
read_vectors), written again in Python for EmbSIM's JSON description."""
import argparse
import json
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

EDC = "{http://crownking/edc}"


def tag(el: ET.Element) -> str:
    return el.tag.split("}")[-1]


def attr(el: ET.Element, name: str, default=None):
    for k, v in el.attrib.items():
        if k.split("}")[-1] == name:
            return v
    return default


def num(text, default=0) -> int:
    if text is None or text == "":
        return default
    return int(text, 0)


def bits_acc(acc: str, nbits: int, lsb: int, width: int) -> str:
    """the access of bits [lsb, lsb + width) in the reference manuals' notation"""
    kinds = set()
    for b in range(lsb, lsb + width):
        i = nbits - 1 - b
        c = acc[i] if acc and 0 <= i < len(acc) else "n"
        if c != "-":
            kinds.add(c)
    if kinds == {"r"}:
        return "r"
    if kinds == {"w"}:
        return "w"
    if kinds == {"c"}:
        return "rc_w0"
    if kinds == {"s"}:
        return "rs"
    return "rw"


def clean(name: str) -> str:
    n = re.sub(r"[^A-Za-z0-9_]", "_", name)
    return n if re.match(r"[A-Za-z_]", n) else "_" + n


def read_sfr(x: ET.Element):
    name = attr(x, "cname") or attr(x, "name")
    nbits = num(attr(x, "nzwidth"), 32)
    por = attr(x, "por", "")
    acc = attr(x, "access", "")
    reset = impl = 0
    for b in range(nbits):
        i = nbits - 1 - b
        if i < len(por) and por[i] == "1":
            reset |= 1 << b
        if not acc or (i < len(acc) and acc[i] != "-"):
            impl |= 1 << b
    fields, covered = [], 0
    for ml in x:
        if tag(ml) != "SFRModeList":
            continue
        for mode in ml:
            if tag(mode) != "SFRMode" or attr(mode, "id") != "DS.0":
                continue
            at = 0
            for fd in mode:
                if tag(fd) == "AdjustPoint":
                    at += num(attr(fd, "offset"), 0)
                    continue
                if tag(fd) != "SFRFieldDef":
                    continue
                w = num(attr(fd, "nzwidth"), 1)
                fn = attr(fd, "cname")
                if w < 1 or at + w > nbits or not fn:
                    sys.exit(f"edc2regs: field {fn} of {name} is outside it")
                fields.append([clean(fn), at, w, bits_acc(acc, nbits, at, w)])
                covered |= ((1 << w) - 1) << at
                at += w
            break
    rest, lsb = impl & ~covered, 0
    while rest and lsb < nbits:
        if not rest >> lsb & 1:
            lsb += 1
            continue
        w = 0
        while lsb + w < nbits and rest >> (lsb + w) & 1:
            w += 1
        rest &= ~(((1 << w) - 1) << lsb)
        fn = name if w == nbits else f"{name}_{lsb}" if w == 1 else f"{name}_{lsb + w - 1}_{lsb}"
        fields.append([clean(fn), lsb, w, bits_acc(acc, nbits, lsb, w)])
        lsb += w
    fields.sort(key=lambda f: f[1])
    for f in fields:
        if f[3] == "rw":
            del f[3]
    inst = attr(x, "baseofperipheral") or attr(x, "memberofperipheral") or attr(x, "grp") or ""
    inst = inst.split(" ")[0]
    portals = attr(x, "portals", "") or ""
    return {
        "name": clean(name),
        "addr": num(attr(x, "_addr")),
        "size": 32 if nbits > 16 else 16 if nbits > 8 else 8,
        "reset": reset,
        "fields": fields,
        "inst": inst,
        "sci": "CLR" in portals,
    }


def read_vectors(atdf: Path) -> dict:
    """request number -> vector, from the ATDF's interrupts"""
    out = {}
    for q in ET.parse(atdf).getroot().iter("interrupt"):
        if q.get("irq-index") is not None and q.get("index") is not None:
            out[int(q.get("irq-index"), 0)] = int(q.get("index"), 0)
    return out


def port_pin(n: str):
    m = re.fullmatch(r"R([A-K])(\d+)", n or "")
    return (m.group(1), int(m.group(2))) if m and int(m.group(2)) < 32 else None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("edc")
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--id")
    ap.add_argument("--atdf", help="the part's ATDF, for the interrupt vectors")
    ap.add_argument("--source", help="the source to record (default: the EDC's file name)")
    ap.add_argument("--package", help="also write the package's pin map here")
    ap.add_argument("--package-id")
    ap.add_argument("--package-name", default="TQFP100")
    ap.add_argument("--package-pins", type=int, help="the package's pin count: the PinList's first pins (it lists the largest package's)")
    a = ap.parse_args()

    root = ET.parse(a.edc).getroot()
    device = attr(root, "name", "?")
    ps = next((e for e in root.iter() if tag(e) == "PhysicalSpace"), None)
    if ps is None:
        sys.exit("edc2regs: no <edc:PhysicalSpace>: not a PIC32 EDC file")
    devid = None
    sfrs = []
    for e in ps.iter():
        t = tag(e)
        if t == "DeviceIDSector":
            devid = num(attr(e, "value"))
            for r in e:
                if tag(r) == "DEVIDToRev" and num(attr(r, "value")) > (devid & 0xF0000000):
                    devid = (devid & 0x0FFFFFFF) | num(attr(r, "value"))
        elif t == "SFRDataSector":
            for x in e:
                if tag(x) == "SFRDef":
                    sfrs.append(read_sfr(x))
                elif tag(x) == "JoinedSFRDef":
                    sfrs.extend(read_sfr(y) for y in x if tag(y) == "SFRDef")

    # the peripherals, in address order, each register once
    seen, periphs = {}, {}
    prev = "CORE"
    for s in sorted(sfrs, key=lambda s: s["addr"]):
        if s["addr"] in seen:
            if seen[s["addr"]] != s["name"]:
                sys.exit(f"edc2regs: {s['name']} and {seen[s['addr']]} at 0x{s['addr']:08X}")
            continue
        seen[s["addr"]] = s["name"]
        nm = s["inst"] or prev
        prev = nm
        if nm.startswith("_"):
            nm = "ICD"
        if re.fullmatch(r"PORT[A-K]", nm):
            nm = "GPIO"
        nm = clean(nm)
        if s["name"] == "DEVID" and devid is not None:
            s["reset"] = devid
            s["fields"] = [f[:3] + ["r"] for f in s["fields"]]
        periphs.setdefault(nm, []).append(s)

    out_p = []
    for nm, regs in sorted(periphs.items(), key=lambda kv: kv[1][0]["addr"]):
        base = regs[0]["addr"]
        rl = []
        for s in regs:
            r = {"name": s["name"], "offset": f"0x{s['addr'] - base:X}"}
            if s["reset"]:
                r["reset"] = f"0x{s['reset']:08X}"
            if s["size"] != 32:
                r["size"] = s["size"]
            if s["sci"]:
                r["clr_set_inv"] = True
            r["fields"] = s["fields"]
            rl.append(r)
        out_p.append({"name": nm, "base": f"0x{base:08X}", "registers": rl})

    irqs = {}
    il = next((e for e in root.iter() if tag(e) == "InterruptList"), None)
    for q in il if il is not None else []:
        if tag(q) == "InterruptRequest" and attr(q, "cname"):
            irqs[clean(attr(q, "cname"))] = num(attr(q, "irq"))
    doc = {
        "schema_version": 1,
        "kind": "registers",
        "id": a.id or f"microchip/{device.lower()}/registers",
        "version": "1.0.0",
        "device": device,
        "description": f"{device}'s special function registers, converted from Microchip's device pack by tools/importers/edc2regs.py",
        "source": a.source or Path(a.edc).name,
        "interrupts": dict(sorted(irqs.items(), key=lambda kv: (kv[1], kv[0]))),
    }
    if a.atdf:
        vec = read_vectors(Path(a.atdf))
        doc["vectors"] = {n: vec.get(i, i) for n, i in doc["interrupts"].items()}

    # the pins: each function on its port pin; the package's pins in order
    pins, pkg = {}, []
    pl = next((e for e in root.iter() if tag(e) == "PinList"), None)
    for p in pl if pl is not None else []:
        if tag(p) != "Pin":
            continue
        names = [attr(v, "name") for v in p if tag(v) == "VirtualPin" and attr(v, "name")]
        ports = [n for n in names if port_pin(n)]
        if len(ports) == 1:
            for n in names:
                if n != ports[0] and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", n):
                    pins.setdefault(n, ports[0])
            sig = ports[0]
        else:
            sig = clean(names[0].upper()) if names else "NC"
        pkg.append({"number": len(pkg) + 1, "signal": sig,
                    "doc": " / ".join(names)} if len(names) > 1 else {"number": len(pkg) + 1, "signal": sig})
    if pins:
        doc["pins"] = dict(sorted(pins.items()))
    doc["peripherals"] = out_p
    Path(a.output).write_text(json.dumps(doc, indent=1) + "\n")
    print(f"edc2regs: {device}: {len(out_p)} peripherals, {sum(len(p['registers']) for p in out_p)} registers, "
          f"{len(irqs)} interrupts, {len(pins)} pin functions")
    if a.package_pins:
        pkg = pkg[:a.package_pins]
    if a.package:
        Path(a.package).write_text(json.dumps({
            "schema_version": 1, "kind": "package",
            "id": a.package_id or f"microchip/{device.lower()}/{a.package_name.lower()}",
            "name": a.package_name,
            "description": f"{device} in its {a.package_name}: the pins in the device pack's order (EDC PinList), each named by its port pin or its first function; the doc lists all its functions",
            "pins": pkg,
        }, indent=1) + "\n")
        print(f"edc2regs: {len(pkg)} package pins")
    return 0


if __name__ == "__main__":
    sys.exit(main())
