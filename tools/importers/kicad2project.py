#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Draft an EmbSIM project from a KiCad netlist (Eeschema's File > Export >
Netlist, the KiCad S-expression format of KiCad 5 to 8): each symbol the
table below knows becomes its catalog part with its value as the main
parameter, each net keeps its name, the power nets (GND, +3V3, +5V, VCC...)
get a supply symbol unless a regulator or battery already drives them, and
the parts are laid out on a grid. Symbols without a catalog part (MCUs
belong in a board, see rtos2board.py and dts2board.py) are listed and left
out.

    kicad2project.py design.net -o project/project.json [--policy electrical]

Pins are matched by KiCad's pin function name when the netlist has one, by
the symbol's own pin order for the discrete parts, and by pin number for
ICs (KiCad and the catalog both use the datasheet's numbers)."""
import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CATALOG = ROOT / "catalog" / "components"


# ---- S-expressions ---------------------------------------------------------------------
def sexpr(text):
    toks = re.findall(r'\(|\)|"(?:\\.|[^"\\])*"|[^\s()]+', text)
    stack, cur = [], []
    for t in toks:
        if t == "(":
            stack.append(cur)
            cur = []
        elif t == ")":
            done, cur = cur, stack.pop()
            cur.append(done)
        else:
            cur.append(t[1:-1].replace('\\"', '"') if t.startswith('"') else t)
    return cur[0] if cur else []


def get(node, key):
    for x in node[1:]:
        if isinstance(x, list) and x and x[0] == key:
            return x
    return None


def val(node, key, default=None):
    x = get(node, key)
    return x[1] if x and len(x) > 1 else default


def all_of(node, key):
    return [x for x in node[1:] if isinstance(x, list) and x and x[0] == key]


# ---- values -----------------------------------------------------------------------------------
SI = {"p": 1e-12, "n": 1e-9, "u": 1e-6, "µ": 1e-6, "m": 1e-3, "k": 1e3, "K": 1e3, "M": 1e6, "G": 1e9, "R": 1, "r": 1}


def si_value(text):
    """'4k7' -> 4700, '2.2uF' -> 2.2e-6, '100n' -> 1e-7, '0R1' -> 0.1, '10 kOhm' -> 1e4; None if not a value"""
    s = str(text).strip().replace(" ", "")
    s = re.sub(r"(Ω|ohms?|Ohms?|F|H|Hz|V|A)$", "", s)
    m = re.fullmatch(r"(\d+)([pnuµmkKMGRr])(\d+)", s)
    if m:
        return float(f"{m.group(1)}.{m.group(3)}") * SI[m.group(2)]
    m = re.fullmatch(r"(\d+(?:\.\d+)?)([pnuµmkKMGRr]?)", s)
    if m:
        return float(m.group(1)) * (SI[m.group(2)] if m.group(2) else 1)
    return None


def si_text(x):
    for suffix, k in (("G", 1e9), ("meg", 1e6), ("k", 1e3), ("", 1), ("m", 1e-3), ("u", 1e-6), ("n", 1e-9), ("p", 1e-12)):
        if abs(x) >= k * 0.9999:
            return f"{x / k:.6g}{suffix}"
    return f"{x:g}"


# ---- the symbol table --------------------------------------------------------------------------------
def bjt(order):
    return {str(i + 1): c for i, c in enumerate(order)}


# lib:part pattern -> (catalog part, pin map or "number", value parameter or None)
SYMBOLS = [
    (r"Device:R(_Small|_US)?$", "resistor", {"1": "1", "2": "2"}, "resistance"),
    (r"Device:C(_Small)?$", "capacitor", {"1": "1", "2": "2"}, "capacitance"),
    (r"Device:(C_Polarized|CP)(_Small|_US)?$", "electrolytic_capacitor", {"1": "+", "2": "-"}, "capacitance"),
    (r"Device:L(_Small)?$", "inductor", {"1": "1", "2": "2"}, "inductance"),
    (r"Device:Ferrite_?Bead(_Small)?$", "ferrite_bead", {"1": "1", "2": "2"}, None),
    (r"Device:LED(_Small)?$", "led", {"1": "K", "2": "A"}, None),
    (r"Device:D_Schottky(_Small)?$", "diode_schottky", {"1": "K", "2": "A"}, None),
    (r"Device:D_Zener(_Small)?$", "zener", {"1": "K", "2": "A"}, None),
    (r"Device:D(_Small)?$", "diode", {"1": "K", "2": "A"}, None),
    (r"Device:Q_NPN_(?P<o>[BCE]{3})$", "bjt_npn", "order", None),
    (r"Device:Q_PNP_(?P<o>[BCE]{3})$", "bjt_pnp", "order", None),
    (r"Device:Q_NMOS_(?P<o>[GDS]{3})$", "nmos", "order", None),
    (r"Device:Q_PMOS_(?P<o>[GDS]{3})$", "pmos", "order", None),
    (r"Device:R_Pot(entiometer)?(_Small|_Trim)?$", "potentiometer", {"1": "1", "2": "W", "3": "2"}, "resistance"),
    (r"Device:Crystal(_Small)?$", "crystal", {"1": "1", "2": "2"}, "hz"),
    (r"Device:Battery(_Cell)?$", "battery", {"1": "+", "2": "-"}, None),
    (r"Device:Buzzer$", "buzzer", {"1": "-", "2": "+"}, None),
    (r"Switch:SW_Push(_\w+)?$", "push_button", {"1": "1", "2": "2"}, None),
    (r"Switch:SW_SPST$", "toggle_switch", {"1": "1", "2": "2"}, None),
    (r"Regulator_Linear:(AMS1117|LM1117)", "regulator", "number", None),
    (r"Regulator_Linear:(L78|LM78|MC78)\w*", "regulator_78xx", "number", None),
    (r"74xx:74(HC|LS|HCT|AHC)?(?P<n>00|02|04|08|14|32|74|85|86|138|151|161|165|244|283|595)$", "ic_74hc{n}", "number", None),
    (r"4xxx:(CD)?4017$", "cd4017", "number", None),
    (r"4xxx:(CD)?4511$", "cd4511", "number", None),
    (r"Timer:(NE|LM|TLC)?555\w*$", "timer_555", "number", None),
    (r"Amplifier_Operational:LM358\w*$", "lm358", "number", None),
    (r"Memory_EEPROM:(24LC02|AT24C02|AT24CS02|CAT24C02)\w*$", "eeprom_24c02", "number", None),
    (r"Memory_EEPROM:25(AA|LC)040\w*$", "eeprom_25lc040", "number", None),
    (r"Analog_ADC:MCP3008\w*$", "mcp3008", "number", None),
    (r"Analog_DAC:MCP4921\w*$", "mcp4921", "number", None),
    (r"Sensor_Temperature:TMP102\w*$", "tmp102", "number", None),
    (r"Timer_RTC:DS1307\w*$", "ds1307", "number", None),
    (r"Interface_Expansion:PCF8574\w*$", "pcf8574", "number", None),
    (r"Driver_Motor:L293D?\w*$", "l293d", "number", None),
    (r"Display_Character:LCD-016N002L$", "lcd1602", "number", None),
    (r"Connector(_Generic)?:Conn_01x(?P<n>\d+)\w*$", "header", "number", "pins"),
]
SUPPLY_NETS = {"GND": 0, "GNDD": 0, "GNDA": 0, "+3V3": 3.3, "+3.3V": 3.3, "3V3": 3.3, "VCC": 3.3, "VDD": 3.3, "+5V": 5.0, "5V": 5.0, "+12V": 12.0, "+9V": 9.0}
DRIVES_SUPPLY = {"regulator", "regulator_78xx", "battery"}


def catalog_pins(part):
    d = json.loads((CATALOG / part / "component.json").read_text())
    return {str(p["number"]): p["name"] for p in d["pins"] if "number" in p}, {p["name"] for p in d["pins"]}, d


def convert(netlist: Path, policy: str):
    tree = sexpr(netlist.read_text(encoding="utf-8", errors="replace"))
    if not tree or tree[0] != "export":
        raise SystemExit(f"{netlist}: not a KiCad S-expression netlist (export ...)")
    comps, warnings, pinmaps, kinds = [], [], {}, {}
    for c in all_of(get(tree, "components") or [], "comp"):
        ref, value = val(c, "ref"), val(c, "value", "")
        ls = get(c, "libsource")
        lib, part = (val(ls, "lib", ""), val(ls, "part", "")) if ls else ("", "")
        key = f"{lib}:{part}"
        for rx, target, pins, vparam in SYMBOLS:
            m = re.match(rx, key)
            if not m:
                continue
            target = target.format(**{k: v for k, v in m.groupdict().items() if v})
            if not (CATALOG / target / "component.json").exists():
                continue
            numbers, names, desc = catalog_pins(target)
            if pins == "number":
                pinmap = numbers
            elif pins == "order":
                pinmap = {str(i + 1): ch for i, ch in enumerate(m.group("o"))}
            else:
                pinmap = pins
            inst = {"id": re.sub(r"[^A-Za-z0-9_]", "_", ref), "component": f"emblink/{target}"}
            params = {}
            if vparam == "pins" and m.groupdict().get("n"):
                params["pins"] = min(40, int(m.group("n")))
            elif vparam and si_value(value) is not None:
                params[vparam] = si_text(si_value(value))
            if target == "led":
                for colour in ("red", "green", "blue", "yellow", "white"):
                    if colour in value.lower():
                        params["color"] = colour
            if target == "battery":
                v = si_value(value)
                named = {"CR2032": "CR2032", "AA": "AA", "AAA": "AAA", "18650": "18650"}
                hit = next((p_ for k_, p_ in named.items() if k_ in value.upper()), None)
                if hit:
                    params["part"] = hit
                elif v in (9.0, 3.0, 1.5, 3.7):
                    params["part"] = {9.0: "9V", 3.0: "CR2032", 1.5: "AA", 3.7: "LiPo-1S"}[v]
                elif v:
                    params["voltage"] = v
            if target == "regulator" and "3.3" in part:
                params["part"] = "AMS1117-3.3" if part.startswith("AMS") else "LM1117-3.3"
            if target == "regulator_78xx":
                mm = re.search(r"78(L?)(\d\d)", part)
                if mm and f"78{mm.group(1)}{mm.group(2)}" in ("7805", "7809", "7812", "78L05"):
                    params["part"] = f"78{mm.group(1)}{mm.group(2)}"
            if params:
                inst["params"] = params
            comps.append(inst)
            pinmaps[ref] = (inst["id"], pinmap, names)
            kinds[inst["id"]] = target
            break
        else:
            warnings.append(f"{ref} ({key}, {value}) has no catalog part: left out" + (" (an MCU belongs in a board)" if lib.startswith("MCU_") else ""))
    nets, used_names = {}, set()
    for n in all_of(get(tree, "nets") or [], "net"):
        raw = val(n, "name", "")
        pins = []
        for node in all_of(n, "node"):
            ref, pin, func = val(node, "ref"), val(node, "pin"), val(node, "pinfunction")
            if ref not in pinmaps:
                continue
            iid, pinmap, names = pinmaps[ref]
            name = func if func in names else pinmap.get(str(pin))
            if not name:
                warnings.append(f"{ref} pin {pin} has no catalog pin: left unconnected")
                continue
            pins.append(f"{iid}.{name}")
        if not pins:
            continue
        name = raw.lstrip("/")
        mm = re.fullmatch(r"Net-\((.+?)-(?:Pad)?([^)]+)\)", name)
        if mm:
            name = f"N_{mm.group(1)}_{mm.group(2)}"
        name = re.sub(r"[^A-Za-z0-9_+\-/.~#]", "_", name) or f"N{val(n, 'code', len(nets))}"
        while name in used_names:
            name += "_"
        used_names.add(name)
        nets[name] = pins
    # a 555 in the astable circuit: its timing parts become its parameters
    net_of = {pin: name for name, pins in nets.items() for pin in pins}
    by_id = {c["id"]: c for c in comps}

    def between(kind, a, b):
        for c in comps:
            if kinds.get(c["id"]) in kind:
                ends = {net_of.get(f"{c['id']}.{x}") for x in ("1", "2", "+", "-")} - {None}
                if ends == {a, b}:
                    return c
        return None
    for iid, k in kinds.items():
        if k != "timer_555":
            continue
        vcc_n, dis, thr, trig = (net_of.get(f"{iid}.{p_}") for p_ in ("VCC", "DIS", "THR", "TRIG"))
        gnd = net_of.get(f"{iid}.GND")
        r1, r2 = between(("resistor",), vcc_n, dis), between(("resistor",), dis, thr)
        c1 = between(("capacitor", "electrolytic_capacitor"), thr, gnd)
        if r1 and r2 and c1 and thr == trig:
            def pv(c, key):
                return (c.get("params") or {}).get(key)
            params = by_id[iid].setdefault("params", {})
            params.update({"mode": "astable", "r1": pv(r1, "resistance") or "1k", "r2": pv(r2, "resistance") or "10k",
                           "c": pv(c1, "capacitance") or "100n"})
            warnings.append(f"{iid}: astable timing from {r1['id']}, {r2['id']} and {c1['id']}")
        elif r1 or c1:
            warnings.append(f"{iid}: the 555 times from its r1, r2 and c parameters; set them to match the circuit")
    # supplies for the power nets nothing on the sheet drives
    supplies = 0
    for name, pins in list(nets.items()):
        v = SUPPLY_NETS.get(name.upper())
        if v is None:
            continue
        driven = any(kinds.get(p.split(".")[0]) in DRIVES_SUPPLY and p.split(".")[1] in ("OUT", "+") for p in pins)
        if v == 0:
            supplies += 1
            comps.append({"id": f"PWR{supplies}", "component": "emblink/gnd"})
            pins.append(f"PWR{supplies}.GND")
        elif not driven:
            supplies += 1
            comps.append({"id": f"PWR{supplies}", "component": "emblink/vcc", "params": {"voltage": v}})
            pins.append(f"PWR{supplies}.VCC")
    # a simple grid: a row per kind of part
    rows = {}
    for c in comps:
        rows.setdefault(c["component"], []).append(c)
    y = 60
    for _, row in sorted(rows.items()):
        for i, c in enumerate(row):
            c["layout"] = {"x": 60 + 120 * i, "y": y}
        y += 100
    project = {"schema_version": 1, "kind": "project", "name": netlist.stem, "policy": policy,
               "description": f"Imported from {netlist.name} by tools/importers/kicad2project.py.",
               "components": comps, "nets": nets}
    return project, warnings


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("netlist", type=Path)
    ap.add_argument("-o", "--output", type=Path, required=True)
    ap.add_argument("--policy", default="electrical", choices=["functional", "timed", "electrical"])
    args = ap.parse_args(argv)
    project, warnings = convert(args.netlist, args.policy)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(project, indent=2) + "\n")
    for w in warnings:
        print("warning:", w, file=sys.stderr)
    print(f"wrote {args.output}: {len(project['components'])} parts, {len(project['nets'])} nets")
    return 0


if __name__ == "__main__":
    sys.exit(main())
