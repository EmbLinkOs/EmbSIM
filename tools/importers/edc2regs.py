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


# a 16-bit part's peripheral, named by its first register: the family's own
# where the register's letters say little (EmbCC's edc.c table)
NAMES16 = [("INTCON1", "INT"), ("IFS0", "INT"), ("OSCCON", "OSC"), ("PMCON", "PMP"), ("CRCCON1", "CRC"),
           ("REFOCON", "REFO"), ("CTMUCON1", "CTMU"), ("RTCCON1", "RTCC"), ("CMSTAT", "CMP"), ("U1MODE", "UART"),
           ("I2C1", "I2C"), ("DMACON", "DMA"), ("U1OTG", "USB"), ("TRISA", "GPIO"), ("TRISB", "GPIO"),
           ("PADCON", "PAD"), ("NVMCON", "NVM"), ("RPINR", "PPS"), ("RPOR", "PPS"), ("HLVDCON", "HLVD"),
           ("ADC1BUF", "ADC1"), ("AD1CON1", "ADC1"), ("TMR1", "TMR"), ("IC1CON", "IC"), ("OC1CON", "OC"),
           ("CCP1CON", "CCP"), ("SPI1CON", "SPI"), ("CLC1CON", "CLC"), ("PMD1", "PMD"), ("RCON", "RCON"),
           ("ANCFG", "ANCFG"), ("ALRMVAL", "RTCC"), ("DCICON1", "DCI"), ("C1CTRL1", "ECAN"), ("C2CTRL1", "ECAN"),
           ("PTCON", "PWM"), ("PADCFG1", "PAD"), ("U3MODE", "UART"), ("U4MODE", "UART"), ("U1PWMRRS", "USB"),
           ("PTGCST", "PTG"), ("ADC2BUF", "ADC2"), ("FEXL", "ICD")]


def periph_name16(first: str) -> str:
    for pre, nm in NAMES16:
        if first.startswith(pre):
            return nm
    m = re.match(r"[A-Z]+", first)
    return m.group(0) if m else first


def convert16(root: ET.Element, bus: int):
    """a PIC24's or dsPIC's EDC: the peripherals as runs of one module's
    registers in the data space (the core's own, W0 to TBLPAG, are the
    core's), at `bus` + their data address; the interrupts by number; the
    configuration words by their program addresses"""
    ds = next((e for e in root.iter() if tag(e) == "DataSpace"), None)
    if ds is None:
        sys.exit("edc2regs: no <edc:DataSpace>: not a 16-bit EDC file")
    sfrs = []
    for sec in ds.iter():
        if tag(sec) != "SFRDataSector":
            continue
        for x in sec:
            if tag(x) == "SFRDef":
                r = read_sfr(x)
                r["mod"] = attr(x, "_modsrc")
                sfrs.append(r)
            elif tag(x) == "JoinedSFRDef":
                for y in x:
                    if tag(y) == "SFRDef":
                        r = read_sfr(y)
                        r["mod"] = attr(y, "_modsrc")
                        sfrs.append(r)
    seen, uniq = set(), []
    for s in sorted(sfrs, key=lambda s: s["addr"]):
        if s["addr"] not in seen:
            seen.add(s["addr"])
            uniq.append(s)
    periphs, order, last = {}, [], None
    i = 0
    while i < len(uniq):
        j = i + 1
        while j < len(uniq) and uniq[j]["mod"] == uniq[i]["mod"]:
            j += 1
        run = uniq[i:j]
        mod = run[0]["mod"] or ""
        if "CPU" in mod or run[0]["name"] == "WREG0":
            i = j
            continue
        nm = clean(periph_name16(run[0]["name"]))
        key = None
        for k, (pn, pm) in periphs.items():
            if pn == nm and pm == mod:
                key = k
        if key is None and last is not None and periphs[last][0] == nm:
            key = last
        if key is None:
            dup = sum(1 for pn, _ in periphs.values() if pn == nm)
            key = nm if not dup else f"{nm}_{dup + 1}"
            periphs[key] = (nm, mod)
            order.append((key, []))
        last = key
        dict(order)[key].extend(run)
        i = j
    # the UARTs' registers are one module's in the EDC: one peripheral a
    # UART (UART1 to UART6), by the number in each register's name
    split = []
    for key, regs in order:
        if key != "UART":
            split.append((key, regs))
            continue
        units = {}
        for s in regs:
            m = re.match(r"U(\d)", s["name"])
            units.setdefault(f"UART{m.group(1)}" if m else key, []).append(s)
        split.extend(sorted(units.items(), key=lambda u: u[1][0]["addr"]))
    order = split
    out_p = []
    for key, regs in order:
        base = regs[0]["addr"]
        rl = []
        for s in regs:
            r = {"name": s["name"], "offset": f"0x{s['addr'] - base:X}"}
            if s["reset"]:
                r["reset"] = f"0x{s['reset']:04X}"
            r["size"] = 16
            r["fields"] = s["fields"]
            rl.append(r)
        out_p.append({"name": key, "base": f"0x{bus + base:08X}", "registers": rl})
    irqs = {}
    il = next((e for e in root.iter() if tag(e) == "InterruptList"), None)
    for q in il if il is not None else []:
        cn = attr(q, "cname")
        if tag(q) == "Interrupt" and cn:
            irqs[clean(cn[:-9] if cn.endswith("Interrupt") and len(cn) > 9 else cn)] = num(attr(q, "irq"))
    cfg = {}
    ps = next((e for e in root.iter() if tag(e) == "ProgramSpace"), None)
    sub = next((e for e in ps.iter() if tag(e) == "ProgramSubspace"), None) if ps is not None else None
    for d in (sub.iter() if sub is not None else []):
        if tag(d) == "DCRDef" and attr(d, "cname") and attr(d, "cname") not in cfg:
            cfg[attr(d, "cname")] = f"0x{num(attr(d, '_addr')):06X}"
    return out_p, irqs, cfg


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
    ap.add_argument("--bus", help="a 16-bit part's data space on EmbSIM's bus (default 0x2000000)")
    ap.add_argument("--package-pins", type=int, help="the package's pin count: the PinList's first pins (it lists the largest package's)")
    a = ap.parse_args()

    root = ET.parse(a.edc).getroot()
    device = attr(root, "name", "?")
    if not (attr(root, "arch", "") or "").startswith("32"):
        out_p, irqs, cfg = convert16(root, num(a.bus) if a.bus else 0x2000000)
        doc = {"schema_version": 1, "kind": "registers", "id": a.id or f"microchip/{device.lower()}/registers",
               "version": "1.0.0", "device": device,
               "description": f"{device}'s special function registers in its data space (on EmbSIM's bus at 0x2000000), converted from Microchip's device pack by tools/importers/edc2regs.py",
               "source": a.source or Path(a.edc).name,
               "interrupts": dict(sorted(irqs.items(), key=lambda kv: (kv[1], kv[0])))}
        if cfg:
            doc["config_words"] = cfg
        pins, pkg = read_pins(root)
        if pins:
            doc["pins"] = dict(sorted(pins.items()))
        pps = {attr(v, "name"): num(attr(v, "ppsval")) for v in root.iter()
               if tag(v) == "VirtualPin" and attr(v, "ppsval") and attr(v, "name")}
        if pps:
            doc["pps_outputs"] = dict(sorted(pps.items(), key=lambda kv: (kv[1], kv[0])))
        doc["peripherals"] = out_p
        Path(a.output).write_text(json.dumps(doc, indent=1) + "\n")
        print(f"edc2regs: {device}: {len(out_p)} peripherals, {sum(len(p['registers']) for p in out_p)} registers, "
              f"{len(irqs)} interrupts, {len(pins)} pin functions, {len(pps)} PPS outputs, "
              f"{len(cfg)} configuration words")
        write_package(a, device, pkg)
        return 0
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

    pins, pkg = read_pins(root)
    if pins:
        doc["pins"] = dict(sorted(pins.items()))
    doc["peripherals"] = out_p
    Path(a.output).write_text(json.dumps(doc, indent=1) + "\n")
    print(f"edc2regs: {device}: {len(out_p)} peripherals, {sum(len(p['registers']) for p in out_p)} registers, "
          f"{len(irqs)} interrupts, {len(pins)} pin functions")
    write_package(a, device, pkg)
    return 0


def read_pins(root: ET.Element):
    """each function on its port pin; the package's pins in order"""
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
    return pins, pkg


def write_package(a, device: str, pkg: list) -> None:
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


if __name__ == "__main__":
    sys.exit(main())
