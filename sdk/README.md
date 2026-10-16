# EmbSIM component SDK

How to add a component to a library: the description file, the symbol, the
artwork, the conformance fixtures, and what the validator tells you. The
normative references are SPEC-005 (components), SPEC-003 §2 (pin models),
SPEC-004 §7 (electrical primitives) and SPEC-014 (library layout); this guide
is the practical reading of them. A skeleton lives in
[`templates/component/`](templates/component/).

## 1. Where files go

```
<library>/
  library.json                     name, version, license (SPDX)
  components/<name>/
    component.json                 the description (this guide)
    symbol.svg                     schematic symbol, 10-unit grid
    artwork.svg                    optional: realistic picture for the board view
    model.lua                      optional: Lua behavioural model (SPEC-013)
    tests/*.json                   at least one conformance fixture
```

`<name>` is `[a-z][a-z0-9_-]*` (64 characters at most) and the description's
`id` is `<library>/<name>`: a component in directory `components/led/` of
library `emblink` has `"id": "emblink/led"`. The validator refuses a mismatch.

## 2. component.json, field by field

```json
{
  "schema_version": 1,
  "kind": "component",
  "id": "emblink/push_button",
  "version": "1.0.0",
  "name": "Push button",
  "category": "switches",
  "description": "Momentary tactile switch, normally open.",
  "keywords": ["button", "tactile"],
  "pins": [...], "params": {...}, "inputs": [...], "outputs": [...],
  "model": {...}, "functional": {...},
  "symbol": {...}, "artwork": {...},
  "fidelity": {...}, "provenance": {...},
  "tests": ["tests/*.json"]
}
```

Required: `schema_version`, `kind`, `id`, `version`, `name`, `category`,
`pins`, `symbol`, `fidelity`, `provenance`, and at least one of `model` or
`functional`. Unknown fields are errors (`schema.unknown_field`): the schemas
are closed. `category` is one of `passives`, `sources`, `diodes`,
`transistors`, `switches`, `inputs`, `indicators`, `logic`, `analog`, `power`,
`connectors`, `instruments`, `displays`, `sensors`, `mechanical`, `misc`.

### Pins

```json
{"name": "A", "number": 1, "model": {"kind": "passive"}, "aliases": ["1"], "doc": "Anode", "required": true}
```

- `name` is what nets reference (`D1.A`); it must be unique, and so must
  every alias. Use the **primitive's pin names** (table in §4) so the core can
  bind the pins without a map.
- `number` is the physical pin, unique when given; omit it for abstract parts.
- `model` is a SPEC-003 §2 pin model: `passive` (two-terminal analog parts),
  `analog`, `digital_in`, `digital_out`, `digital_io`, `power` (`role`
  `supply` or `ground`, with `nominal_v`), `nc`. Digital pins take a
  `family` (`cmos_3v3`, `cmos_5v`, `ttl_5v`, `lvcmos_1v8`) that fills in
  `voh`, `vol`, `rout`, `vih`, `vil` and friends; a component-level `family`
  applies to every digital pin.
- `power_ref` on a digital pin names this component's supply pin so the
  bridge knows which rail the output swings to.
- Numeric pin-model fields may be `"$param"` to take the value from a
  parameter: the `vcc` symbol declares
  `{"kind": "power", "role": "supply", "nominal_v": "$voltage"}`.

### Parameters

```json
"resistance": {"type": "number", "unit": "ohm", "default": "1k", "min": "1m", "max": "1G", "doc": "Resistance"}
```

Types: `number`, `integer`, `boolean`, `string`, `enum` (with `enum: [...]`),
`table` (a list of `[x, y]` pairs). Numbers accept SI suffixes everywhere
(`4.7k`, `100n`, `2.2u`, `1meg`, `1M`; lower-case `m` is milli). A parameter
without `default` is required of every instance. `min`/`max` are enforced at
load (`analog.param_out_of_range`). Keep names human (`resistance`,
`capacitance`, `voltage`, `color`); the binding to the primitive renames them.

An enum parameter may carry `presets`: parameter values applied when that
value is chosen. This is how named parts work:

```json
"part": {"type": "enum", "enum": ["generic", "2N3904", "BC547"], "default": "2N3904",
         "presets": {"2N3904": {"is": "6.73f", "bf": 416, "vaf": 74}, ...}}
```

### Inputs and outputs

`inputs` are what a user, a scenario or a script can set (`input.set`):
`boolean`, `number` (with `min`/`max`/`unit`), `integer`, `enum`, `string`,
`bytes`. Their **names must be the ones the primitive reads**: `pressed` for a
momentary switch, `closed` for a latching one, `position` (0..1) for a
potentiometer, `tx_text` for the virtual terminal. `outputs` are displayable
states that artwork binds to and probes can read (`output:D1.luminance`):
`boolean`, `number`, `integer`, `enum`, `string`, `bytes`, `bitmap`.

### Models

`model` binds the behaviour used by `timed` and `electrical` sessions;
`functional` binds the behaviour under the functional policy when it differs.
Each is one of:

```json
{"primitive": "resistor", "bind": {"r": "resistance"}}
{"primitive": "pull", "params": {"level": 1}, "bind": {"r": "resistance"}}
{"composite": {"components": [...], "nets": {...}, "ports": {"1": "C0.1", "2": "C0.2"}}}
{"script": "model.lua", "api": 1}
{"native": {"plugin": "libfoo", "model": "name", "abi": 1}}
```

- `bind` maps **primitive parameter name → component parameter or input
  name** (`"r": "resistance"` reads "primitive param `r` comes from component
  param `resistance`"). Names you leave out bind by identity.
- `params` fixes primitive parameters the component does not expose
  (`pulse_source` is `vsource` with `"params": {"kind": "pulse"}`;
  `pull_up` is `pull` with `"params": {"level": 1}`).
- Composites may reference the component's own parameters as `"$name"` in
  their internal `params`; the `crystal` is an example.
- A part with no behaviour of its own (a rail symbol, a connector) uses an
  empty composite `{"composite": {"components": [], "nets": {}, "ports": {}}}`:
  its pin models do all the work.

Typical pairings: resistor → `model: resistor`, `functional: wire`; LED →
`model: led`, `functional: indicator`; push button → `model: switch`,
`functional: push_button_digital`; pull-up → `model: pull`, `functional:
pull`. A purely electrical part (transistor, capacitor, source) declares no
`functional` model and a `null` functional fidelity; using it in a functional
project raises `island.requires_electrical`.

### Timing

Digital parts put datasheet timing in `timed`: `tpd`, `tplh`, `tphl`, `tco`,
`setup`, `hold`, `min_pulse`, `rise`, `fall` as time strings (`"10ns"`).

### Fidelity

For each of `functional`, `timed`, `electrical`: `null` (not provided) or
`{"claims": "...", "omissions": [...], "range": {"current_a": [0, 0.1]},
"validation": ["tests/*.json"]}`. Be honest and specific: say which
SPEC-004 §7 primitive and which effects are modelled, list what is left out.
A session whose policy needs a tier you did not declare refuses the load
(`fidelity.tier_missing`). At least one tier must be declared.

### Provenance

`{"author": "...", "license": "Apache-2.0", "sources": [{"title": "...", "url": "...", "rev": "..."}]}`.
`license` is an SPDX identifier. Cite datasheets by title whenever a named
part's parameters come from one. Artwork must be original or licensed for
redistribution (ADR-017); never a vendor photograph.

## 3. Symbols and artwork

**symbol.svg** is a schematic symbol on a 10-unit grid. `symbol` in the
description gives `width`, `height` (multiples of 10) and the pins:

```json
"symbol": {"svg": "symbol.svg", "width": 60, "height": 20,
           "pins": {"A": {"x": 0, "y": 10, "side": "left"}, "K": {"x": 60, "y": 10, "side": "right"}}}
```

Every pin needs a position, on the edge its `side` names. Draw strokes and
fills through CSS variables with fallbacks so themes apply:

```svg
<style>.s{stroke:var(--symbol-stroke,#cdd6e4);fill:none;stroke-width:1.5}
       .f{stroke:var(--symbol-stroke,#cdd6e4);fill:var(--symbol-fill,#1b2230)}</style>
```

No embedded raster images. Pin stubs are 10 units long from the edge.

**artwork.svg** is optional, a realistic top view for the board and
breadboard views. `hotspots` bind an input to an element and a gesture:

```json
"hotspots": [{"input": "pressed", "selector": "#cap", "gesture": "hold"}]
"hotspots": [{"input": "position", "selector": "#knob", "gesture": "drag", "axis": "angle", "range": [0, 1], "travel": 270}]
```

`states` bind an output to an attribute or class of an element; numeric
outputs map through a `scale`:

```json
"states": [{"output": "luminance", "selector": "#glow", "attr": "opacity", "scale": {"from": [0, 1], "to": [0, 1]}},
           {"output": "closed", "selector": "#cap", "attr": "transform", "true": "translate(0 1)", "false": ""}]
```

The validator checks that every `#id` selector exists in the SVG and that the
SVG is well-formed XML.

## 4. Primitive reference

Pins and parameters the core implements. Bind `primitive` to exactly these
names.

**Digital (functional and timed):** `wire`, `buffer`, `inverter`, `and`,
`nand`, `or`, `nor`, `xor`, `xnor` (2 to 8 inputs), `tristate_buffer`,
`dff`, `clock_source`, `pulse_source`, `logic_level`, `pull`,
`push_button_digital` (reads input `pressed`), `switch_digital` (reads
`closed`), `indicator` (pins `A`, `K`; output `lit`), `virtual_terminal`,
`logic_probe`.

**Electrical (SPEC-004 §7):**

| Primitive | Pins | Parameters |
|---|---|---|
| `resistor` | `1`, `2` | `r`, `tc1`, `tc2`, `temp` |
| `capacitor` | `1`, `2` | `c`, `ic`, `esr` |
| `inductor` | `1`, `2` | `l`, `ic`, `dcr` |
| `vsource` | `+`, `-` | `kind` (`dc`, `pulse`, `sine`, `pwl`) with SPICE-style `dc`, `v1`, `v2`, `td`, `tr`, `tf`, `pw`, `per`, `vo`, `va`, `freq`, `points` |
| `isource` | `+`, `-` | same kinds |
| `switch` | `1`, `2` | `ron`, `roff`; reads input `pressed` (momentary) or `closed`; optional `bounce` |
| `potentiometer` | `1`, `W`, `2` | `r`, `taper`; reads input `position` 0..1 |
| `diode` | `A`, `K` | `is`, `n`, `rs`, `bv` (plus `cj0`, `tt`) |
| `led` | `A`, `K` | `color`, `is`, `n`, `rs`, `if_nom`; observes `current`, `luminance` |
| `zener` | `A`, `K` | diode plus `vz`, `rz` |
| `bjt_npn`, `bjt_pnp` | `C`, `B`, `E` | `is`, `bf`, `br`, `vaf` (plus `rb`, `rc`, `re`, charges) |
| `nmos`, `pmos` | `D`, `G`, `S` | `vto`, `kp`, `lambda`, `w`, `l` (plus `cgs`, `cgd`) |
| `opamp` | `+`, `-`, `OUT` | `gain`, `gbw`, `vpos`, `vneg`, `rout`, `rin` |
| `regulator` | `IN`, `OUT`, `GND` | `vout`, `dropout`, `imax`, `quiescent` |
| `pull` | `1` | `level` (0 or 1), `voltage`, `r` |

## 5. Fixtures

`tests/*.json` are conformance fixtures (`kind: "component_test"`) that
`embsim catalog validate` runs. Keep expectations simple and physically
right; a component whose fixtures fail is listed as *broken*, not hidden.

```json
{
  "schema_version": 1, "kind": "component_test", "name": "red-220r-3v3",
  "policy": "electrical", "until": "1ms",
  "circuit": {
    "components": [
      {"id": "V1", "component": "emblink/vsource", "params": {"kind": "dc", "dc": 3.3}},
      {"id": "R1", "component": "emblink/resistor", "params": {"resistance": 220}},
      {"id": "DUT", "component": "emblink/led", "params": {"color": "red"}},
      {"id": "G", "component": "emblink/gnd"}],
    "nets": {"VIN": ["V1.+", "R1.1"], "LED_A": ["R1.2", "DUT.A"], "GND": ["V1.-", "DUT.K", "G.GND"]}
  },
  "inputs": [{"at": "0", "target": "DUT", "input": "pressed", "value": true}],
  "expect": {"device:DUT.i": {"at": "0.5ms", "min": 0.005, "max": 0.009}}
}
```

- The fixture must instantiate the component it sits beside.
- `expect` keys are probe targets: `net:NAME` (voltages via `min_v`/`max_v`,
  or `logic`, `edges`, `frequency_hz`), `pin:INST.PIN`, `device:INST.q`
  (`i` for a two-terminal current, `ic` for a collector current),
  `output:INST.NAME` (`equals`, `contains`, `min`, `max`). Each expectation
  takes `at`, `between` or `by`.
- Board fixtures may add `"boards": [{"id": "B1", "board": "emblink/bluepill"}]`
  to the circuit and reference `B1.<port>` in nets.

## 6. What `embsim validate` reports

`python3 tools/validate.py PATH...` (and `embsim validate`) print one line per
finding, `file:node: severity code: message`, then a summary; exit status 1
on any error.

```
catalog/components/led/component.json:/symbol/pins/B: error catalog.pin_unknown: symbol pin B is not a pin of this component
examples/rc-filter/project.json:/nets/OUT/1: error catalog.pin_twice: C1.1 is already in net IN
validated 4 file(s) with jsonschema: 2 error(s), 0 warning(s) -> FAIL
```

Codes come from `docs/user/diagnostics.md` (`schema.unknown_field`,
`schema.missing_field`, `schema.type`, `catalog.not_found`,
`catalog.pin_unknown`, `catalog.pin_twice`, `fidelity.tier_missing`,
`analog.param_out_of_range`, `island.requires_electrical`) plus validator-only
codes for file references (`catalog.file_missing`), identity
(`catalog.id_mismatch`), symbols (`symbol.pin_missing`, `symbol.pin_outside`),
artwork (`artwork.selector`, `svg.malformed`), provenance
(`provenance.license`) and tests (`catalog.no_tests`, `catalog.test_subject`).
`--all` validates the schemas themselves, the catalog and the examples;
`--strict` turns warnings into failures.

## 7. Checklist before you publish

1. `python3 tools/validate.py <library>/components/<name>` passes.
2. The symbol renders in both themes (only CSS-variable colours).
3. Every input and output named in `artwork` exists, every selector resolves.
4. Fidelity names the primitive and the omissions; provenance cites the
   datasheet; licence is SPDX.
5. At least one fixture, with a physically correct expectation.
