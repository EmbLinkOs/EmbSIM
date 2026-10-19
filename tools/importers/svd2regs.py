#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Convert a CMSIS-SVD file into an EmbSIM register description
(schemas/registers.schema.json) for the register file (SPEC-009 §5).

    svd2regs.py DEVICE.svd -o registers.json [--id vendor/part/registers]

Peripherals keep their names, bases and derivedFrom; clusters and register
arrays (dim, dimIncrement, dimIndex) are flattened into registers named as
the SVD's %s substitutions give them; fields keep their bit ranges, and
their access, modifiedWriteValues and readAction become the reference
manuals' notation (r, w, rw, w1, rw1, rc_w0, rc_w1, rs, t, rc_r). An SVD
file usually carries its vendor's licence: the description made from it is
for the user's own use unless that licence allows distributing it.

    --reset PERIPH.REG=0xVALUE   (repeatable) a reset value where the SVD
                                 and the reference manual disagree, the
                                 manual's; recorded in the description's
                                 source so the conversion can be repeated
    --interrupts PERIPH=A,B      (repeatable) the interrupts a peripheral
                                 requests, where the SVD lists them on
                                 another (an STM32's TIM9 on TIM1_BRK_TIM9);
                                 recorded likewise
    --field PERIPH.REG=NAME:LSB:WIDTH[:ACCESS]
                                 (repeatable) a field the SVD leaves out or
                                 sizes wrongly, added or replaced
    --merge PERIPH.REG.NAME      (repeatable) the one-bit fields NAME0,
                                 NAME1, ... into one field NAME, as the
                                 manual names a number ST's SVDs split
                                 (RCC PLLCFGR's PLLM0..PLLM5)
    --drop PERIPH=WHY            (repeatable) a peripheral the SVD lists that
                                 is another's registers at the same address
                                 (Espressif's RNG, APB_CTRL's RND_DATA)"""
import argparse
import json
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import Optional


def num(text: Optional[str], default: int = 0) -> int:
    if text is None:
        return default
    t = text.strip().lower().replace("_", "")
    if t.startswith("#"):
        return int(t[1:].replace("x", "0"), 2)
    if t.startswith("0x"):
        return int(t, 16)
    return int(t)


def access_code(acc: Optional[str], mwv: Optional[str], ract: Optional[str]) -> Optional[str]:
    if ract == "clear":
        return "rc_r"
    if mwv in ("oneToClear",):
        return "rc_w1"
    if mwv in ("zeroToClear",):
        return "rc_w0"
    if mwv in ("oneToSet",):
        return "rs"
    if mwv in ("oneToToggle",):
        return "t"
    return {"read-only": "r", "write-only": "w", "writeOnce": "w1", "read-writeOnce": "rw1"}.get(acc or "", None)


def dim_names(el: ET.Element, name: str) -> list[tuple[str, int]]:
    """[(name, offset delta)] for an element that may be an array"""
    dim = el.findtext("dim")
    if not dim:
        return [(name, 0)]
    n = num(dim)
    inc = num(el.findtext("dimIncrement"), 0)
    idx = el.findtext("dimIndex")
    if idx and "-" in idx and "," not in idx:
        a, b = idx.split("-")
        labels = [str(i) for i in range(int(a), int(b) + 1)] if a.isdigit() else [chr(c) for c in range(ord(a), ord(b) + 1)]
    elif idx:
        labels = idx.split(",")
    else:
        labels = [str(i) for i in range(n)]
    out = []
    for i in range(n):
        lab = labels[i] if i < len(labels) else str(i)
        nm = name.replace("[%s]", lab).replace("%s", lab)
        out.append((re.sub(r"[^A-Za-z0-9_]", "_", nm), i * inc))
    return out


def fields_of(reg: ET.Element, size: int, inherit_acc: Optional[str]) -> list:
    out = []
    for f in reg.findall("fields/field"):
        name = f.findtext("name")
        if f.findtext("bitRange"):
            m = re.match(r"\[(\d+):(\d+)\]", f.findtext("bitRange").strip())
            msb, lsb = int(m.group(1)), int(m.group(2))
            width = msb - lsb + 1
        elif f.findtext("lsb") is not None:
            lsb, msb = num(f.findtext("lsb")), num(f.findtext("msb"))
            width = msb - lsb + 1
        else:
            lsb, width = num(f.findtext("bitOffset")), num(f.findtext("bitWidth"), 1)
        if width < 1:
            continue
        code = access_code(f.findtext("access") or inherit_acc, f.findtext("modifiedWriteValues"), f.findtext("readAction"))
        for nm, step in dim_names(f, name):
            # a field array steps by dimIncrement bits (Espressif's RMT
            # CH%s_TX_END, two channels' bits side by side)
            if lsb + step + width > size:
                continue
            entry = [nm, lsb + step, width]
            if code:
                entry.append(code)
            out.append(entry)
    return out


def registers_of(parent: ET.Element, base_off: int, prefix: str, dflt_size: int, dflt_acc: Optional[str]) -> list:
    out = []
    for el in parent:
        if el.tag == "register":
            if el.findtext("alternateRegister") or el.findtext("alternateGroup"):
                continue  # another view of a register already described: the first names it
            size = num(el.findtext("size"), dflt_size)
            acc = el.findtext("access") or dflt_acc
            for nm, delta in dim_names(el, el.findtext("name")):
                reg = {"name": prefix + nm, "offset": "0x%02X" % (base_off + num(el.findtext("addressOffset")) + delta)}
                reset = num(el.findtext("resetValue"), 0) & ((1 << size) - 1)
                if reset:
                    reg["reset"] = "0x%08X" % reset
                if size == 16:
                    reg["size"] = 16
                elif size != 32:
                    continue
                f = fields_of(el, size, acc)
                if f:
                    reg["fields"] = f
                elif acc in ("read-only", "write-only"):
                    reg["fields"] = [[prefix + nm, 0, size, "r" if acc == "read-only" else "w"]]
                out.append(reg)
        elif el.tag == "cluster":
            for nm, delta in dim_names(el, el.findtext("name")):
                out += registers_of(el, base_off + num(el.findtext("addressOffset")) + delta, prefix + nm + "_",
                                    num(el.findtext("size"), dflt_size), el.findtext("access") or dflt_acc)
    return out


def convert(svd: Path, ident: str) -> dict:
    root = ET.parse(svd).getroot()
    dflt_size = num(root.findtext("size"), 32)
    dflt_acc = root.findtext("access")
    periphs, irqs = [], {}
    for p in root.iter("peripheral"):
        entry = {"name": p.findtext("name"), "base": "0x%08X" % num(p.findtext("baseAddress"))}
        names = []
        for i in p.findall("interrupt"):
            irqs[i.findtext("name")] = num(i.findtext("value"))
            names.append(i.findtext("name"))
        if names:
            entry["interrupts"] = names
        if p.get("derivedFrom") and p.find("registers") is None:
            entry["derived_from"] = p.get("derivedFrom")
        else:
            regs = registers_of(p.find("registers"), 0, "", num(p.findtext("size"), dflt_size), p.findtext("access") or dflt_acc) if p.find("registers") is not None else []
            if not regs:
                continue
            entry["registers"] = regs
        periphs.append(entry)
    known = {p["name"] for p in periphs if "registers" in p}
    periphs = [p for p in periphs if "registers" in p or p.get("derived_from") in known]
    return {
        "schema_version": 1,
        "kind": "registers",
        "id": ident,
        "version": "1.0.0",
        "device": root.findtext("name") or svd.stem,
        "source": f"converted from {svd.name} by tools/importers/svd2regs.py",
        "interrupts": dict(sorted(irqs.items(), key=lambda kv: kv[1])),
        "peripherals": sorted(periphs, key=lambda p: int(p["base"], 16)),
    }


def apply_resets(doc: dict, resets: list[str]) -> None:
    """Corrected reset values, PERIPH.REG=0xVALUE, on peripherals that have
    their own registers (those derived from one share its correction)."""
    by_name = {p["name"]: p for p in doc["peripherals"]}
    done = []
    for spec in resets:
        target, _, value = spec.partition("=")
        pname, _, rname = target.partition(".")
        p = by_name.get(pname)
        if p is None or "registers" not in p:
            raise SystemExit(f"--reset {spec}: no peripheral {pname} with its own registers")
        reg = next((r for r in p["registers"] if r["name"] == rname), None)
        if reg is None:
            raise SystemExit(f"--reset {spec}: {pname} has no register {rname}")
        v = num(value)
        if v:
            reg["reset"] = "0x%08X" % v
        else:
            reg.pop("reset", None)
        done.append(f"{pname}.{rname}=0x{v:08X}")
    if done:
        doc["source"] += "; reset values corrected from the reference manual: " + ", ".join(done)


def apply_interrupts(doc: dict, specs: list[str]) -> None:
    """Interrupt lists, PERIPH=NAME[,NAME...], replacing the SVD's; every
    name must be one of the device's interrupts (empty: none)."""
    by_name = {p["name"]: p for p in doc["peripherals"]}
    done = []
    for spec in specs:
        pname, _, names = spec.partition("=")
        p = by_name.get(pname)
        if p is None:
            raise SystemExit(f"--interrupts {spec}: no peripheral {pname}")
        irqs = [n for n in names.split(",") if n]
        bad = [n for n in irqs if n not in doc["interrupts"]]
        if bad:
            raise SystemExit(f"--interrupts {spec}: unknown interrupts {', '.join(bad)}")
        if irqs:
            p["interrupts"] = irqs
        else:
            p.pop("interrupts", None)
        done.append(f"{pname}={','.join(irqs)}")
    if done:
        doc["source"] += "; interrupts reassigned from the reference manual: " + "; ".join(done)


def find_reg(doc: dict, target: str, opt: str) -> dict:
    pname, _, rname = target.partition(".")
    p = next((q for q in doc["peripherals"] if q["name"] == pname), None)
    if p is None or "registers" not in p:
        raise SystemExit(f"{opt} {target}: no peripheral {pname} with its own registers")
    reg = next((r for r in p["registers"] if r["name"] == rname), None)
    if reg is None:
        raise SystemExit(f"{opt} {target}: {pname} has no register {rname}")
    return reg


def apply_registers(doc: dict, specs: list[str]) -> None:
    """Registers PERIPH.REG=0xOFFSET the SVD leaves out, added without
    fields (--field gives them theirs) and with a reset value of 0
    (--reset changes it)."""
    done = []
    for spec in specs:
        target, _, off = spec.partition("=")
        pname, _, rname = target.partition(".")
        p = next((q for q in doc["peripherals"] if q["name"] == pname), None)
        if p is None or "registers" not in p or not rname or not off:
            raise SystemExit(f"--register {spec}: PERIPH.REG=0xOFFSET on a peripheral with its own registers")
        if any(r["name"] == rname for r in p["registers"]):
            raise SystemExit(f"--register {spec}: {pname} already has {rname}")
        p["registers"].append({"name": rname, "offset": f"0x{num(off):02X}", "fields": []})
        p["registers"].sort(key=lambda r: num(r["offset"]))
        done.append(spec)
    if done:
        doc["source"] += "; registers added from the reference manual: " + ", ".join(done)


def apply_fields(doc: dict, specs: list[str]) -> None:
    """Fields PERIPH.REG=NAME:LSB:WIDTH[:ACCESS] added, or replacing those
    they overlap (the SVD's narrower or missing field)."""
    done = []
    for spec in specs:
        target, _, f = spec.partition("=")
        parts = f.split(":")
        if len(parts) not in (3, 4):
            raise SystemExit(f"--field {spec}: NAME:LSB:WIDTH[:ACCESS]")
        reg = find_reg(doc, target, "--field")
        name, lsb, width = parts[0], int(parts[1]), int(parts[2])
        mask = ((1 << width) - 1) << lsb
        fields = [x for x in reg.get("fields", [])
                  if x[0] != name and not ((((1 << x[2]) - 1) << x[1]) & mask)]
        fields.append([name, lsb, width] + parts[3:])
        reg["fields"] = sorted(fields, key=lambda x: -x[1])
        done.append(f"{target}.{f}")
    if done:
        doc["source"] += "; fields corrected from the reference manual: " + ", ".join(done)


def apply_merges(doc: dict, specs: list[str]) -> None:
    """The one-bit fields NAME0..NAMEn, contiguous, joined into NAME."""
    done = []
    for spec in specs:
        target, _, name = spec.rpartition(".")
        reg = find_reg(doc, target, "--merge")
        bits = {}
        for x in reg.get("fields", []):
            m = re.fullmatch(re.escape(name) + r"(\d+)", x[0])
            if m and x[2] == 1:
                bits[int(m.group(1))] = x
        if not bits or sorted(bits) != list(range(len(bits))):
            raise SystemExit(f"--merge {spec}: no fields {name}0..{name}n")
        lsb = bits[0][1]
        if any(bits[k][1] != lsb + k for k in bits):
            raise SystemExit(f"--merge {spec}: {name}0..{name}{len(bits) - 1} are not contiguous")
        acc = bits[0][3:]
        rest = [x for x in reg["fields"] if x not in bits.values()]
        reg["fields"] = sorted(rest + [[name, lsb, len(bits)] + acc], key=lambda x: -x[1])
        done.append(spec)
    if done:
        doc["source"] += "; split fields joined: " + ", ".join(done)


def apply_drops(doc: dict, specs: list[str]) -> None:
    """leave out each PERIPH of `specs` (PERIPH=WHY), recorded in source"""
    done = []
    for spec in specs:
        name, _, why = spec.partition("=")
        if not any(p["name"] == name for p in doc["peripherals"]):
            sys.exit(f"svd2regs: --drop {spec}: no peripheral {name}")
        doc["peripherals"] = [p for p in doc["peripherals"] if p["name"] != name]
        done.append(f"{name} ({why})" if why else name)
    if done:
        doc["source"] += "; peripherals left out: " + "; ".join(done)


def main(argv: Optional[list[str]] = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("svd", type=Path)
    ap.add_argument("-o", "--output", type=Path, required=True)
    ap.add_argument("--id", default=None, help="the description's id (default: the device name)")
    ap.add_argument("--reset", action="append", default=[], metavar="PERIPH.REG=0xVALUE",
                    help="a reset value from the reference manual where the SVD differs")
    ap.add_argument("--interrupts", action="append", default=[], metavar="PERIPH=A,B",
                    help="a peripheral's interrupts where the SVD lists them elsewhere")
    ap.add_argument("--field", action="append", default=[], metavar="PERIPH.REG=NAME:LSB:WIDTH[:ACCESS]",
                    help="a field the SVD leaves out or sizes wrongly")
    ap.add_argument("--register", action="append", default=[], metavar="PERIPH.REG=0xOFFSET",
                    help="a register the SVD leaves out (its fields with --field)")
    ap.add_argument("--drop", action="append", default=[], metavar="PERIPH=WHY",
                    help="a peripheral left out, with the reason recorded in source")
    ap.add_argument("--merge", action="append", default=[], metavar="PERIPH.REG.NAME",
                    help="one-bit fields NAME0..NAMEn joined into NAME")
    args = ap.parse_args(argv)
    doc = convert(args.svd, args.id or (args.svd.stem.lower() + "/registers"))
    apply_drops(doc, args.drop)
    apply_merges(doc, args.merge)
    apply_registers(doc, args.register)
    apply_fields(doc, args.field)
    apply_resets(doc, args.reset)
    apply_interrupts(doc, args.interrupts)
    args.output.write_text(json.dumps(doc, indent=1) + "\n", encoding="utf-8")
    nregs = sum(len(p.get("registers", [])) for p in doc["peripherals"])
    print(f"{args.output}: {len(doc['peripherals'])} peripherals, {nregs} registers, {len(doc['interrupts'])} interrupts")
    return 0


if __name__ == "__main__":
    sys.exit(main())
