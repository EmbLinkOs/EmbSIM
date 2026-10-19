# SPEC-006 - MCU, Package and Board Descriptions

**Status:** Accepted 2026-10-15. **Builds on:** 04 §1 to §6; ADR-003, ADR-006,
ADR-017, ADR-018. **Schemas:** `schemas/mcu.schema.json`,
`schemas/package.schema.json`, `schemas/board.schema.json`.
**Requirements:** `docs/requirements/MCU.md`, `docs/requirements/CAT.md`.

---

## 1. MCU description

```json
{
  "schema_version": 1,
  "kind": "mcu",
  "id": "st/stm32f103c8",
  "version": "1.0.0",
  "name": "STM32F103C8",
  "vendor": "st",
  "family": "stm32f1",
  "core": {"backend": "cortex-m", "model": "cortex-m3", "priority_bits": 4, "timing_tier": "estimated"},
  "memory": [
    {"name": "flash", "base": "0x08000000", "size": "64K", "kind": "flash", "erased": "0xFFFFFFFF"},
    {"name": "flash_alias", "base": "0x00000000", "size": "64K", "kind": "alias", "target": "0x08000000"},
    {"name": "sram", "base": "0x20000000", "size": "20K", "kind": "ram", "main": true}
  ],
  "bitband": [{"base": "0x22000000", "size": "0x02000000", "target": "0x20000000"},
              {"base": "0x42000000", "size": "0x02000000", "target": "0x40000000"}],
  "clocks": {
    "sources": [{"name": "hsi", "hz": 8000000}, {"name": "hse", "hz": "board"},
                {"name": "lsi", "hz": 40000}, {"name": "lse", "hz": "board"}],
    "reset_core_hz": 8000000, "max_core_hz": 72000000
  },
  "register_file": {"format": "svd", "file": "STM32F103xx.svd", "vendored": false,
                    "license": "see provenance", "lookup": ["device_pack", "EMBSIM_SVD_PATH"]},
  "peripherals": [
    {"match": "RCC", "model": "stm32-rcc", "order": 0},
    {"match": "GPIO*", "model": "stm32f1-gpio"},
    {"match": "AFIO", "model": "stm32f1-afio"},
    {"match": "EXTI", "model": "stm32-exti"},
    {"match": "USART*", "model": "stm32-usart"},
    {"match": "TIM*", "model": "stm32-tim"}
  ],
  "io": {
    "family": "cmos_3v3",
    "ports": [{"name": "A", "pins": 16}, {"name": "B", "pins": 16}, {"name": "C", "pins": 16}, {"name": "D", "pins": 2}],
    "electrical": {"voh": 2.4, "vol": 0.4, "rout": {"2MHz": 60, "10MHz": 40, "50MHz": 25},
                   "vih": 2.0, "vil": 0.8, "rpull": 40000, "five_volt_tolerant": ["PA8", "PA9", "PA10", "PB10", "PB11"]},
    "analog": {"PA0": "ADC12_IN0", "PA1": "ADC12_IN1"}
  },
  "signals": ["PA0", "PA1", "PC13", "VDD", "VSS", "VDDA", "VSSA", "VBAT", "NRST", "BOOT0", "OSC_IN", "OSC_OUT", "OSC32_IN", "OSC32_OUT"],
  "packages": {"lqfp48": "lqfp48.json"},
  "console": {"default": "USART1"},
  "fidelity": {"functional": {...}, "timed": {...}, "electrical": {...}},
  "provenance": {"sources": [{"title": "RM0008 Reference manual", "rev": "21"}, {"title": "STM32F103x8 datasheet", "rev": "17"}],
                 "license": "Apache-2.0"}
}
```

Numbers for addresses and sizes accept `0x` hexadecimal and `K`, `M` suffixes
(powers of two). `kind` is `ram`, `flash`, `rom`, `alias`; `erased` is the
word value of erased flash; `main` marks the region a project may resize.
`register_file.vendored` says whether the catalog ships the file (ADR-017);
`lookup` lists where to find it otherwise, in order. `peripherals` bind
models (SPEC-009) to register-file peripherals by glob; `order` forces
initialisation order where it matters (clock controllers first). Every
signal an external pin can carry is listed in `signals`; `io.ports` define
GPIO signals `P<port><n>`.

## 2. Package description

```json
{"schema_version": 1, "kind": "package", "id": "st/stm32f103c8/lqfp48", "name": "LQFP48",
 "pins": [{"number": 1, "signal": "VBAT"}, {"number": 2, "signal": "PC13"}, {"number": 3, "signal": "PC14"},
          {"number": 44, "signal": "BOOT0"}, {"number": 47, "signal": "VSS"}, {"number": 48, "signal": "VDD"}],
 "outline": {"svg": "lqfp48.svg"}}
```

Every pin number is unique; every `signal` is one of the MCU's `signals` or
`"NC"`. A signal may appear on several pins (multiple `VSS`), which the
loader joins into one net.

## 3. Board description

```json
{
  "schema_version": 1,
  "kind": "board",
  "id": "emblink/bluepill",
  "version": "1.0.0",
  "name": "Blue Pill (STM32F103C8T6)",
  "revision": "generic 2019",
  "mcu": {"ref": "st/stm32f103c8", "package": "lqfp48", "instance": "U1"},
  "oscillators": {"hse": {"hz": 8000000, "source": "external_crystal"}, "lse": {"hz": 32768, "source": "external_crystal"}},
  "components": [
    {"id": "LED1", "component": "emblink/led", "params": {"color": "green"}},
    {"id": "R1", "component": "emblink/resistor", "params": {"resistance": "1k"}},
    {"id": "LED_PWR", "component": "emblink/led", "params": {"color": "red"}},
    {"id": "R_PWR", "component": "emblink/resistor", "params": {"resistance": "1k"}},
    {"id": "U2", "component": "emblink/ldo", "params": {"vout": 3.3, "dropout": 0.9, "imax": 0.3}},
    {"id": "SW_RESET", "component": "emblink/push_button"},
    {"id": "R_RESET", "component": "emblink/resistor", "params": {"resistance": "10k"}},
    {"id": "Y1", "component": "emblink/crystal", "params": {"hz": 8000000}},
    {"id": "J1", "component": "emblink/header", "params": {"pins": 20}},
    {"id": "J2", "component": "emblink/header", "params": {"pins": 20}}
  ],
  "nets": {
    "5V":   ["U2.IN", "J1.20", "J2.20"],
    "3V3":  ["U2.OUT", "U1.VDD", "U1.VDDA", "U1.VBAT", "R1.1", "R_RESET.1", "R_PWR.1", "J1.19", "J2.19"],
    "GND":  ["U2.GND", "U1.VSS", "U1.VSSA", "LED1.K", "LED_PWR.K", "SW_RESET.2", "J1.18", "J2.18"],
    "PC13": ["U1.PC13", "R1.2", "J1.3"],
    "LED1_A": ["LED1.A", "R1.2"],
    "NRST": ["U1.NRST", "SW_RESET.1", "R_RESET.2", "J1.7"]
  },
  "ports": {
    "C13": {"net": "PC13", "connector": "J1", "pin": 3},
    "3V3": {"net": "3V3", "connector": "J1", "pin": 19},
    "GND": {"net": "GND", "connector": "J1", "pin": 18},
    "A0":  {"net": "PA0", "connector": "J2", "pin": 11}
  },
  "leds": [{"name": "user", "component": "LED1", "gpio": "PC13", "active": "low"},
           {"name": "power", "component": "LED_PWR"}],
  "buttons": [{"name": "reset", "component": "SW_RESET", "signal": "NRST"}],
  "console": {"peripheral": "USART1", "tx": "PA9", "rx": "PA10", "baud": 115200},
  "artwork": {"svg": "bluepill.svg", "width_mm": 53, "height_mm": 23,
              "hotspots": [{"component": "LED1", "selector": "#led-user"}, {"component": "SW_RESET", "selector": "#btn-reset"},
                           {"port": "C13", "selector": "#pin-c13"}]},
  "toolchains": {
    "embcc":   {"target": "thumbv7m-none-eabi", "link": ["-Tflash=0x08000000:64K", "-Tram=0x20000000:20K"]},
    "gcc-arm": {"cpu": "cortex-m3", "flags": ["-mthumb", "-mcpu=cortex-m3"], "linker_script": "stm32f103c8.ld"},
    "clang":   {"target": "thumbv7m-none-eabi", "linker_script": "stm32f103c8.ld"}
  },
  "fidelity": {"functional": {...}, "timed": {...}, "electrical": {"claims": "...", "omissions": ["decoupling", "USB"], "validation": []}},
  "provenance": {"sources": [{"title": "Blue Pill schematic", "url": "..."}], "license": "Apache-2.0"}
}
```

Rules:

- Pin references are `<instance>.<pin>`; for the MCU instance the pin is a
  signal name (`U1.PC13`), resolved through the package to physical pins;
  for components, a pin name or alias; for connectors, a pin number.
- A net is a list of at least one pin reference; a pin appears in at most one
  net; the MCU's power signals must be on nets with a `power` source or the
  load fails (`board.unpowered_mcu`).
- `ports` expose nets to the project by label; projects reference
  `<board_instance>.<port>`; `connector` and `pin` place the port on the
  artwork and the schematic board symbol.
- `leds`, `buttons`, `console` summarise what the workbench shows as quick
  controls and what importers map; they must agree with `components` and
  `nets` (validated).
- `console.bridge` is the board's USB-serial bridge (ADR-031): `part` (the
  Uno's `ATmega16U2`, the Explorer 16/32's `MCP2221A`, a `J-Link OB`), and
  `dtr_reset {component, ms}` when raising DTR resets the MCU. On the Uno,
  the 16U2's DTR pin falls and its 100 nF capacitor pulls RESET low for
  about 0.7 ms, until the 10 kOhm pull-up recharges it past the threshold.
  The named component's contacts (the reset switch's) join RESET to ground
  for as long. A board with a bridge is the one whose console `port.open`
  puts on the host (SPEC-008).
- `oscillators` fill the MCU's `"board"` clock sources.
- `toolchains` give per-profile target settings for `embsim build`.
- The board's artwork hotspots bind to on-board component inputs or outputs
  and to ports (for wiring by clicking a header pin in the board view).
- `bench` (optional) is the board as the 3D bench builds it (ADR-027): the
  PCB's `thickness_mm`, the `standoff_mm` of its underside above the desk,
  whether its ports are `female` sockets, `male` pins or pads (`headers`),
  and its physical `features`, each a `kind` (`usb_b`, `usb_micro`,
  `usb_c`, `barrel_jack`, `dip`, `qfp`, `qfn`, `button`, `crystal`, `led`,
  `box`) centred `at` [x, y] millimetres from the board's top-left corner,
  with a `size` [w, d, h], a `rotation`, a `pins` count, a `label` and a
  `color`; a feature may drive one of the board's components' inputs
  (`input`: a button's `SW_RESET.pressed`) or show an output (`output`:
  an LED's `LED_L.luminance`), validated against the board's components.

### 3.1 Mechanical reference

A board qualified for the bench has a `reference.json` beside its
description (kind `board_reference`, `schemas/board-reference.schema.json`).
It records mechanical data taken from sources independent of the artwork:
the vendor's drawings, or a maintained footprint library. Each source is
listed with its URL, the date it was read and its licence. The data are:
- the outline;
- the mounting holes;
- every terminal's pad centre (each port, and positions such as NC that
  have no port);
- connector bodies.

All of them are in the artwork's frame: millimetres from the top-left
corner, y down. The validator checks the following within
`tolerance_mm`, and refuses a port with no reference position
(`board.reference_*`):
- the artwork's size against the outline;
- each pad against its terminal;
- the `mount` circles against the holes;
- the bench features of the same kind against the connector bodies.

What the artwork draws without reference data is listed as `unqualified`.
The Uno R3's reference (2026-10-18) comes from:
- Arduino's A000066 datasheet (§5.4, outline and holes);
- KiCad's `Arduino_UNO_R3` footprint, for the pads, and its symbol, for
  which pad is which pin.

## 4. Import from EmbLinkRTOS board YAML

`tools/import/rtos_board.py` maps `board`, `soc`, `oscillators`, `leds`,
`buttons`, `console`, `pins` to a draft board description with the same
field names where they coincide, synthesising components (an LED and a
series resistor per `leds` entry, a button and a pull per `buttons` entry
with placeholder values marked `"todo": true`), and records `provenance
.imported_from`. The author completes nets, ports and artwork; validation
refuses `todo` fields.

## 5. Validation beyond the schema

Refused with file and node named: a pin referenced twice or unknown; a
signal not in the MCU; a package pin without a signal; a port whose net does
not exist; a peripheral glob matching nothing in the register file (warning
when the file is absent at validation time); a clock source `"board"` not
supplied by `oscillators`; `leds`/`buttons` entries that disagree with the
nets; a register file declared vendored but absent; a licence missing on
vendored material.
