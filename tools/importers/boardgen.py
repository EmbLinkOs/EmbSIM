# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""The board writer shared by the board importers (SPEC-014 §5): an
intermediate description of what a board file says (MCU, oscillators,
console, LEDs, buttons, bus devices, the GPIOs it brings out) becomes a
draft EmbSIM board.json with the parts that make it run in the simulator:
the MCU, crystals, LEDs with resistors, buttons with pull resistors, the
NRST pull-up and BOOT0 pull-down, catalog parts for known bus devices,
and a header exposing every used signal and the supply. A device the
catalog has no part for is a `todo` component, which validation refuses
until someone replaces it (SPEC-006 §4).

The draft also gets a power fixture, so `embsim catalog validate` checks
it end to end."""
import json
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

ROOT = Path(__file__).resolve().parents[2]
CATALOG = ROOT / "catalog"

# DeviceTree / RTOS compatible strings -> catalog parts and how their pins
# attach to the bus ("sda", "scl", "sck", "mosi", "miso", "cs", "irq")
COMPATIBLES = {
    "ti,tmp102": ("emblink/tmp102", "i2c", {"SDA": "sda", "SCL": "scl", "ALERT": "irq"}),
    "atmel,24c02": ("emblink/eeprom_24c02", "i2c", {"SDA": "sda", "SCL": "scl"}),
    "atmel,at24": ("emblink/eeprom_24c02", "i2c", {"SDA": "sda", "SCL": "scl"}),
    "solomon,ssd1306": ("emblink/ssd1306", "i2c", {"SDA": "sda", "SCL": "scl"}),
    "solomon,ssd1306fb-i2c": ("emblink/ssd1306", "i2c", {"SDA": "sda", "SCL": "scl"}),
    "solomon,ssd1306fb": ("emblink/ssd1306", "i2c", {"SDA": "sda", "SCL": "scl"}),
    "nxp,pcf8574": ("emblink/pcf8574", "i2c", {"SDA": "sda", "SCL": "scl", "INT_N": "irq"}),
    "maxim,ds1307": ("emblink/ds1307", "i2c", {"SDA": "sda", "SCL": "scl"}),
    "microchip,mcp3008": ("emblink/mcp3008", "spi", {"DIN": "mosi", "DOUT": "miso", "CLK": "sck", "CS_N": "cs"}),
    "microchip,mcp4921": ("emblink/mcp4921", "spi", {"SDI": "mosi", "SCK": "sck", "CS_N": "cs"}),
    "microchip,25lc040": ("emblink/eeprom_25lc040", "spi", {"SI": "mosi", "SO": "miso", "SCK": "sck", "CS_N": "cs"}),
    "maxim,max7219": ("emblink/max7219_display", "spi", {"DIN": "mosi", "CLK": "sck", "CS": "cs"}),
    "aosong,dht": ("emblink/dht22", "gpio", {"DATA": "data"}),
    "aosong,am2302": ("emblink/dht22", "gpio", {"DATA": "data"}),
}
# parameters a part takes from the device node
ADDRESS_PARAM = {"emblink/tmp102": "address", "emblink/eeprom_24c02": "address", "emblink/ssd1306": "address", "emblink/pcf8574": "address"}


@dataclass
class Device:
    name: str
    compatible: str
    bus: Optional[str] = None          # "i2c1", "spi1", or None
    address: Optional[int] = None
    pins: dict = field(default_factory=dict)  # role -> GPIO ("cs": "PA4", "irq": "PB0", "data": "PA1")


@dataclass
class BoardModel:
    name: str
    title: str
    mcu: str                           # the SoC as the source names it
    oscillators: dict = field(default_factory=dict)   # "hse" -> hz
    console: Optional[dict] = None     # {peripheral, tx, rx, baud}
    leds: list = field(default_factory=list)          # {name, gpio, active, color}
    buttons: list = field(default_factory=list)       # {name, gpio, active}
    buses: dict = field(default_factory=dict)         # "i2c1" -> {"sda": "PB7", "scl": "PB6"}
    devices: list = field(default_factory=list)
    gpios: list = field(default_factory=list)         # further signals to bring out
    source: str = ""
    tool: str = ""
    notes: list = field(default_factory=list)


def _mcus():
    for p in sorted((CATALOG / "mcus").glob("*/*/mcu.json")):
        doc = json.loads(p.read_text())
        yield doc["id"], p.parent, doc


def resolve_mcu(name: str, override: Optional[str] = None):
    """The catalog MCU for a SoC name ("st/stm32f1/stm32f103c8.yaml",
    "stm32f103X8", "st,stm32f103c8"): its id, package and signals. An x in
    the name matches any character (DeviceTree file names use it)."""
    if override:
        for mid, d, doc in _mcus():
            if mid == override:
                return mid, d, doc
        raise SystemExit(f"no MCU {override} in the catalog")
    stem = re.split(r"[/,]", name.strip())[-1].lower()
    stem = re.sub(r"\.(ya?ml|dtsi?)$", "", stem)
    rx = re.compile("^" + re.escape(stem).replace("x", ".") + "$")
    for mid, d, doc in _mcus():
        part = mid.split("/")[-1].lower()
        if rx.match(part) or part == stem:
            return mid, d, doc
    return None, None, None


def _signals(mcu_dir: Path, doc: dict):
    pkgs = doc.get("packages") or {}
    if isinstance(pkgs, dict):
        pkg_name, rel = next(iter(pkgs.items()))
    else:
        pkg_name, rel = pkgs[0], f"{pkgs[0]}.json"
    pkg_file = mcu_dir / rel
    sigs = []
    if pkg_file.exists():
        sigs = [p["signal"] for p in json.loads(pkg_file.read_text())["pins"]]
    return pkg_name, sigs


def write_board(model: BoardModel, library: str, out_dir: Path, mcu_override: Optional[str] = None) -> list:
    """Writes <out_dir>/boards/<name>/board.json and tests/power.json; returns warnings."""
    warnings = list(model.notes)
    mid, mdir, mdoc = resolve_mcu(model.mcu, mcu_override)
    if not mid:
        raise SystemExit(f"the catalog has no MCU for {model.mcu!r}; pass --mcu with one of: " + ", ".join(m for m, _, _ in _mcus()))
    pkg, sigs = _signals(mdir, mdoc)
    U = "U1"
    comps, nets, ports, used = [], {}, {}, []

    def net(name, *pins):
        nets.setdefault(name, [])
        for p in pins:
            if p not in nets[name]:
                nets[name].append(p)

    def gpio(sig):
        if sig not in sigs:
            warnings.append(f"{sig} is not a signal of {mid} ({pkg}); left out")
            return None
        net(sig, f"{U}.{sig}")
        if sig not in used:
            used.append(sig)
        return sig

    # supplies
    for s in sigs:
        if re.match(r"^(VDD|VBAT|VDDA|VCC|AVCC)", s):
            net("3V3", f"{U}.{s}")
        elif re.match(r"^(VSS|GND|AGND|VSSA)", s):
            net("GND", f"{U}.{s}")
    # reset and boot pins
    if "NRST" in sigs:
        comps.append({"id": "R_NRST", "component": "emblink/resistor", "params": {"resistance": "10k"}, "doc": "NRST pull-up"})
        net("NRST", f"{U}.NRST", "R_NRST.2")
        net("3V3", "R_NRST.1")
        used.append("NRST")
    if "BOOT0" in sigs:
        comps.append({"id": "R_BOOT0", "component": "emblink/resistor", "params": {"resistance": "10k"}, "doc": "BOOT0 pull-down: boot from flash"})
        net("BOOT0", f"{U}.BOOT0", "R_BOOT0.1")
        net("GND", "R_BOOT0.2")
    # oscillators
    oscs = {}
    for kind, hz in model.oscillators.items():
        pair = {"hse": ("OSC_IN", "OSC_OUT"), "lse": ("PC14", "PC15")}.get(kind)
        if not pair or not all(p in sigs for p in pair):
            warnings.append(f"oscillator {kind} has no crystal pins on {mid}; recorded without a crystal")
            oscs[kind] = {"hz": int(hz), "source": "external_clock"}
            continue
        y = f"Y{len(oscs) + 1}"
        params = {"hz": int(hz)}
        if int(hz) == 32768:
            params.update({"c0": "1.5p", "cm": "3f", "lm": "7.8k", "rm": "35k"})
        comps.append({"id": y, "component": "emblink/crystal", "params": params, "doc": f"{kind.upper()} crystal"})
        net(pair[0] if pair[0] != "PC14" else "PC14", f"{U}.{pair[0]}", f"{y}.1")
        net(pair[1] if pair[1] != "PC15" else "PC15", f"{U}.{pair[1]}", f"{y}.2")
        oscs[kind] = {"hz": int(hz), "source": "external_crystal", "component": y}
    # clock sources the MCU takes from the board but the source file leaves out
    for src in (mdoc.get("clocks") or {}).get("sources", []):
        if src.get("hz") == "board" and src.get("name") not in oscs:
            oscs[src["name"]] = {"hz": 32768 if "lse" in src["name"] else 8000000, "source": "none"}
            warnings.append(f"no {src['name']} oscillator in the source: recorded as absent")
    # LEDs
    leds = []
    for i, led in enumerate(model.leds, 1):
        g = gpio(led["gpio"])
        if not g:
            continue
        lid, rid = f"LED{i}", f"R_LED{i}"
        color = led.get("color") or "green"
        comps.append({"id": lid, "component": "emblink/led", "params": {"color": color if color in ("red", "green", "blue", "yellow", "white") else "green"}})
        comps.append({"id": rid, "component": "emblink/resistor", "params": {"resistance": "1k"}})
        if led.get("active", "high") == "low":
            net("3V3", f"{rid}.1")
            net(f"{lid}_A", f"{rid}.2", f"{lid}.A")
            net(g, f"{lid}.K")
        else:
            net(g, f"{rid}.1")
            net(f"{lid}_A", f"{rid}.2", f"{lid}.A")
            net("GND", f"{lid}.K")
        leds.append({"name": led["name"], "component": lid, "gpio": g, "active": led.get("active", "high"), "color": color})
    # buttons
    buttons = []
    for i, b in enumerate(model.buttons, 1):
        g = gpio(b["gpio"])
        if not g:
            continue
        sw, pr = f"SW{i}", f"R_SW{i}"
        comps.append({"id": sw, "component": "emblink/push_button"})
        comps.append({"id": pr, "component": "emblink/resistor", "params": {"resistance": "10k"}})
        if b.get("active", "low") == "low":
            net(g, f"{sw}.1", f"{pr}.2")
            net("GND", f"{sw}.2")
            net("3V3", f"{pr}.1")
        else:
            net(g, f"{sw}.2", f"{pr}.1")
            net("3V3", f"{sw}.1")
            net("GND", f"{pr}.2")
        buttons.append({"name": b["name"], "component": sw, "gpio": g, "active": b.get("active", "low")})
    # console
    console = None
    if model.console:
        c = model.console
        console = {"peripheral": c["peripheral"].upper()}
        for k in ("tx", "rx"):
            if c.get(k) and gpio(c[k]):
                console[k] = c[k]
        if c.get("baud"):
            console["baud"] = int(c["baud"])
    # buses and devices
    bus_pullups = set()
    for i, dev in enumerate(model.devices, 1):
        did = re.sub(r"[^A-Za-z0-9_]", "_", dev.name.upper()) or f"DEV{i}"
        if not re.match(r"^[A-Za-z_]", did):
            did = "D_" + did
        known = COMPATIBLES.get(dev.compatible)
        roles = dict(model.buses.get(dev.bus or "", {}))
        roles.update(dev.pins)
        if not known:
            comps.append({"id": did, "component": "emblink/header", "params": {"pins": 1}, "todo": True,
                          "doc": f"TODO: {dev.compatible} on {dev.bus or 'gpio'}" + (f" at 0x{dev.address:02X}" if dev.address is not None else "") +
                          ": no catalog part; replace this placeholder"})
            warnings.append(f"device {dev.name} ({dev.compatible}) has no catalog part: a todo placeholder")
            continue
        part, kind, pinmap = known
        inst = {"id": did, "component": part}
        if dev.address is not None and part in ADDRESS_PARAM:
            inst["params"] = {ADDRESS_PARAM[part]: dev.address}
        comps.append(inst)
        for pin, role in pinmap.items():
            sig = roles.get(role)
            if sig and gpio(sig):
                net(sig, f"{did}.{pin}")
        if kind == "i2c" and dev.bus not in bus_pullups:
            bus_pullups.add(dev.bus)
            for role in ("sda", "scl"):
                sig = roles.get(role)
                if sig and sig in nets:
                    rid = f"R_{(dev.bus or 'I2C').upper()}_{role.upper()}"
                    comps.append({"id": rid, "component": "emblink/resistor", "params": {"resistance": "4.7k"}, "doc": f"{dev.bus} {role.upper()} pull-up"})
                    net(sig, f"{rid}.2")
                    net("3V3", f"{rid}.1")
    for sig in model.gpios:
        gpio(sig)
    # the header: supply first, then every used signal
    exposed = ["3V3", "GND"] + [s for s in used if s in nets]
    comps.append({"id": "J1", "component": "emblink/header", "params": {"pins": min(40, len(exposed))},
                  "doc": "Every signal the description uses, and the supply"})
    for k, sig in enumerate(exposed[:40], 1):
        net(sig, f"J1.{k}")
        ports[sig.replace("3V3", "3V3")] = {"net": sig, "connector": "J1", "pin": k}
    if len(exposed) > 40:
        warnings.append(f"{len(exposed) - 40} signals did not fit the 40-pin header")
    board = {
        "schema_version": 1, "kind": "board", "id": f"{library}/{model.name}", "version": "0.1.0", "name": model.title,
        "description": f"Imported from {Path(model.source).name} by {model.tool}: a draft to review against the schematic.",
        "keywords": [model.name, mid.split("/")[-1]],
        "mcu": {"ref": mid, "package": pkg, "instance": U},
        "components": comps, "nets": nets, "ports": ports,
    }
    if oscs:
        board["oscillators"] = oscs
    if leds:
        board["leds"] = leds
    if buttons:
        board["buttons"] = buttons
    if console:
        board["console"] = console
    board["fidelity"] = {
        "functional": {"claims": "The MCU, LEDs, buttons, console pins and bus devices the source file describes, wired to the MCU's pins.",
                       "omissions": ["parts the source file does not describe (regulators, connectors, protection)", "the real header layout"],
                       "validation": ["tests/*.json"]},
        "timed": {"claims": "As functional.", "omissions": ["as functional"]},
        "electrical": {"claims": "LED resistors and pull resistors with typical values chosen by the importer; supply from the 3V3 port.",
                       "omissions": ["the real resistor values", "decoupling", "the supply circuit"], "validation": ["tests/*.json"]},
    }
    board["provenance"] = {"author": "imported", "license": "Apache-2.0",
                           "notes": f"Imported from {Path(model.source).name} by {model.tool}; review before use." + (" " + " ".join(warnings) if warnings else "")}
    board["tests"] = ["tests/*.json"]
    d = out_dir / "boards" / model.name
    (d / "tests").mkdir(parents=True, exist_ok=True)
    (d / "board.json").write_text(json.dumps(board, indent=2) + "\n")
    test = {
        "schema_version": 1, "kind": "component_test", "name": "power",
        "description": "3.3 V on the 3V3 port: the NRST pull-up holds reset released." if "NRST" in nets else "3.3 V on the 3V3 port reaches the supply net.",
        "policy": "electrical", "until": "2ms",
        "circuit": {"boards": [{"id": "B1", "board": board["id"]}],
                    "components": [{"id": "V1", "component": "emblink/vsource", "params": {"kind": "dc", "dc": 3.3}}, {"id": "G", "component": "emblink/gnd"}],
                    "nets": {"VDD": ["V1.+", "B1.3V3"], "GND": ["V1.-", "G.GND", "B1.GND"]} | ({"RST": ["B1.NRST"]} if "NRST" in ports else {})},
        "expect": {"net:RST": {"at": "1ms", "min_v": 3.1, "max_v": 3.4}} if "NRST" in ports else {"net:VDD": {"at": "1ms", "min_v": 3.2, "max_v": 3.4}},
    }
    (d / "tests" / "power.json").write_text(json.dumps(test, indent=2) + "\n")
    lib_json = out_dir / "library.json"
    if not lib_json.exists():
        lib_json.write_text(json.dumps({"schema_version": 1, "kind": "library", "name": library, "version": "0.1.0",
                                        "description": "Imported boards.", "license": "Apache-2.0"}, indent=2) + "\n")
    return warnings
