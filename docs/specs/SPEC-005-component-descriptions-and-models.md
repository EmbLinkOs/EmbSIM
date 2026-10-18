# SPEC-005 - Component Descriptions and the Model Contract

**Status:** Accepted 2026-10-15. **Builds on:** 02 §6; ADR-006, ADR-009,
ADR-011, ADR-017. **Schema:** `schemas/component.schema.json`.
**Requirements:** `docs/requirements/CAT.md`, `docs/requirements/EXT.md`.

---

## 1. Files and identity

A component lives in a directory of a library (SPEC-014):
`components/<name>/component.json` with its symbol, artwork, model script and
tests beside it. Identity is `id` = `<library>/<name>` (`emblink/resistor`),
with a semantic `version`. Projects reference `id` and may pin a version
range. Names are `[a-z][a-z0-9_-]*`, at most 64 characters.

## 2. The description

```json
{
  "schema_version": 1,
  "kind": "component",
  "id": "emblink/push_button",
  "version": "1.0.0",
  "name": "Push button",
  "category": "switches",
  "description": "Momentary tactile switch, normally open.",
  "keywords": ["button", "tactile", "switch", "momentary"],
  "pins": [
    {"name": "1", "number": 1, "model": {"kind": "passive"}, "aliases": ["A"]},
    {"name": "2", "number": 2, "model": {"kind": "passive"}, "aliases": ["B"]}
  ],
  "params": {
    "ron":    {"type": "number", "unit": "ohm", "default": 0.05, "min": 0, "doc": "Contact resistance when closed"},
    "bounce": {"type": "boolean", "default": false, "doc": "Model contact bounce on press and release"},
    "bounce_ms": {"type": "number", "unit": "ms", "default": 2, "min": 0, "max": 50}
  },
  "inputs": [
    {"name": "pressed", "type": "boolean", "default": false, "doc": "Held while the pointer is down; toggled by keyboard"}
  ],
  "outputs": [
    {"name": "closed", "type": "boolean"}
  ],
  "model": {"primitive": "switch", "bind": {"state": "pressed", "ron": "ron"}},
  "functional": {"primitive": "switch_digital"},
  "symbol": {"svg": "symbol.svg", "width": 60, "height": 30,
             "pins": {"1": {"x": 0, "y": 15, "side": "left"}, "2": {"x": 60, "y": 15, "side": "right"}}},
  "artwork": {"svg": "artwork.svg", "hotspots": [{"input": "pressed", "selector": "#cap", "gesture": "hold"}],
              "states": [{"output": "closed", "selector": "#cap", "attr": "transform", "true": "translate(0 2)", "false": ""}]},
  "fidelity": {
    "functional": {"claims": "Open or closed; no timing.", "omissions": []},
    "timed": {"claims": "Bounce as a burst of toggles at bounce_ms/8 spacing when enabled.", "omissions": ["temperature"]},
    "electrical": {"claims": "Contact resistance ron when closed, 1 GΩ when open.", "range": {"current_a": [0, 0.1]},
                   "omissions": ["contact wear", "capacitance"], "validation": ["tests/bounce-pullup.json"]}
  },
  "provenance": {"author": "EmbSIM", "license": "Apache-2.0", "sources": []},
  "tests": ["tests/*.json"]
}
```

Required: `schema_version`, `kind`, `id`, `version`, `name`, `category`,
`pins`, `model` or `functional` (at least one), `symbol`, `fidelity`,
`provenance`. Unknown fields are errors (`schema.unknown_field`).

### Named manufacturer parts (additive schema-1 extension)

An optional `part` object has four required nonempty strings: `manufacturer`,
`mpn`, `package`, and `datasheet` (an HTTPS URL). It identifies a selected
physical variant; it does not imply manufacturer model qualification. Named
parts must number every terminal. Distinct package mappings use distinct catalog
entries. Abstract parts omit this object. The workbench searches and displays
these fields alongside fidelity. The category set additionally includes
`amplifiers`, `comparators`, and `timers`; existing categories remain valid.
See [the catalog guide](../../catalog/README.md) for current source-review status.

## 3. Pins

Each pin: `name` (unique; what nets reference), `number` (physical, unique,
optional for abstract parts), `model` (SPEC-003 §2), `aliases`, `doc`,
`required` (default true: an unconnected required pin is a warning),
`power_ref` (for digital pins: the name of this component's supply pin, used
by bridges). Pin arrays are written out; the symbol references pins by name.

## 4. Parameters

`params` map names to `{type, unit, default, min, max, enum, doc,
advanced}`; `type` is `number`, `integer`, `boolean`, `string`, `enum`,
`table` (a list of `[x, y]` pairs for curves). Numbers in descriptions and
projects accept SI suffixes (`4.7k`, `100n`, `2.2u`, `1meg`); the loader
normalises to doubles in base units. A parameter without `default` is
required. `min`/`max` are enforced at load (`analog.param_out_of_range`).
Families (`family: "cmos_3v3"`) supply pin-model defaults (SPEC-003 §2).

## 5. Models

`model` binds behaviour for `timed` and `electrical` sessions; `functional`
binds the functional-policy behaviour when it differs. Each is one of:

- `{"primitive": NAME, "bind": {model_param: component_param_or_input}}`:
  a core primitive (SPEC-003 §5, SPEC-004 §7). `bind` maps the primitive's
  parameters and inputs to this component's; omitted names bind by identity.
- `{"composite": {"components": [...], "nets": {...}, "ports": {...}}}`: an
  internal circuit of other components; `ports` map this component's pin
  names to internal pin references; internal parameters may reference this
  component's parameters with `"$param"`.
- `{"script": "model.lua", "api": 1}`: a Lua behavioural model (SPEC-013).
- `{"native": {"plugin": "libfoo", "model": "name", "abi": 1}}`: a native
  plugin model; loadable only when the project marks the library trusted.

## 6. Fidelity declaration

For each tier `functional`, `timed`, `electrical`: `null` (not provided) or
`{claims, omissions, range, validation}`. `claims` is prose stating what is
modelled; `omissions` lists known unmodelled effects; `range` gives operating
limits by quantity; `validation` lists fixture files or external references.
A session policy requiring a tier the component does not provide refuses the
load (`fidelity.tier_missing`). Clients show the declaration in the inspector.

## 7. Inputs and outputs

`inputs` are the interactive controls of the component: what a user, a
scenario or a script can set (`input.set` in SPEC-008). Types: `boolean`,
`number` with `min`/`max`/`unit`, `enum`, `string` (serial text), `bytes`.
`outputs` are the displayable states (LED luminance, a display framebuffer
as `bitmap`, a seven-segment pattern, a meter value) that artwork `states`
bind to. Both are exposed by name in snapshots and probes.

## 8. Symbols and artwork

- `symbol.svg`: a schematic symbol in a 10-unit grid, pins at integer grid
  positions listed in `symbol.pins` with sides; stroke colours use CSS
  variables (`var(--symbol-stroke)`, `var(--symbol-fill)`) so themes apply;
  no embedded raster.
- `artwork.svg` (optional): a realistic picture for the board or breadboard
  view with `hotspots` (an `input`, an element `selector`, a `gesture`:
  `hold`, `toggle`, `drag` with an axis and range) and `states` (an `output`
  bound to an attribute or a CSS class of a selector; numeric outputs may
  map to `opacity` or `fill` through a `scale`; `map` looks an enum or
  integer value up; `format` puts the value, scaled first when a `scale` is
  given, into a template such as `rotate({} 30 30)`, and the pseudo-attribute
  `text` sets an element's text, so displays show strings and paths). Optional
  `precision` (integer 0..6) formats numeric template values to fixed decimal
  places after scaling; it does not alter simulation values.
- Both are rendered by any client; a client may substitute a generic symbol
  when a file is missing, with a warning.

## 9. Tests

`tests/*.json` are conformance fixtures: `{"kind": "component_test",
"circuit": {...}, "inputs": [...], "expect": {...}, "policy": "electrical"}`.
`expect` maps probe targets (`net:`, `pin:`, `device:`, `output:`, and
`serial:` for what a terminal in the circuit received) to an expectation
with a time selector; `inputs` may target the component or a member of a
composite, and `output:` a member's output (SPEC-007). `embsim catalog validate` runs them. A component whose
tests fail is listed as broken in the catalog, not hidden.

## 10. The primitive model contract (C)

A primitive is `esk_model_ops`: `create(instance, params)`, `destroy`,
`reset`, `on_input(pin)`, `on_time(token)`, `on_set(input, value)`,
`observe(outputs)`, plus `adev_ops` for electrical primitives (SPEC-004 §6).
Instances own their state; the registry maps primitive names to ops;
registration is at library initialisation, or at plugin load for native
plugins. Primitives never allocate after `create` except through the
session's bounded pools.

## 11. Versioning and provenance

`version` follows semantic versioning; a project records the versions it
used; the catalog may hold several versions. `provenance.license` is an SPDX
identifier; `provenance.sources` cite datasheets by title, URL and date;
vendored third-party material carries its licence file beside the
description (ADR-017).
