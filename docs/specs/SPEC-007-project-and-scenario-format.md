# SPEC-007 - Project and Scenario Format

**Status:** Accepted 2026-10-15. **Builds on:** 02 §5; ADR-006, ADR-012.
**Schemas:** `schemas/project.schema.json`, `schemas/scenario.schema.json`.
**Requirements:** `docs/requirements/CAT.md`, `docs/requirements/VER.md`.

---

## 1. Project

```json
{
  "schema_version": 1,
  "kind": "project",
  "name": "button-transistor-led",
  "description": "The acceptance scenario of 01 §8.",
  "libraries": ["./lib"],
  "policy": "electrical",
  "boards": [
    {"id": "B1", "board": "emblink/bluepill",
     "firmware": {"image": "build/blink.elf", "format": "elf", "toolchain": "gcc-arm", "sources": ["src/main.c"]},
     "layout": {"x": 100, "y": 100, "rotation": 0}}
  ],
  "components": [
    {"id": "SW1", "component": "emblink/push_button", "params": {"bounce": true}, "layout": {"x": 400, "y": 120}},
    {"id": "R1", "component": "emblink/resistor", "params": {"resistance": "1k"}, "layout": {"x": 400, "y": 220, "rotation": 90}},
    {"id": "Q1", "component": "emblink/bjt_npn", "params": {"part": "2N3904"}, "layout": {"x": 500, "y": 240}},
    {"id": "R2", "component": "emblink/resistor", "params": {"resistance": 220}, "layout": {"x": 500, "y": 140}},
    {"id": "D1", "component": "emblink/led", "params": {"color": "red"}, "layout": {"x": 500, "y": 80}}
  ],
  "nets": {
    "BTN":  ["B1.A0", "SW1.1"],
    "GND":  ["B1.GND", "SW1.2", "Q1.E"],
    "3V3":  ["B1.3V3", "D1.A"],
    "BASE": ["B1.C13", "R1.1"],
    "Q1B":  ["R1.2", "Q1.B"],
    "LED_K": ["D1.K", "R2.1"],
    "COLL": ["R2.2", "Q1.C"]
  },
  "layout": {"wires": [{"net": "BTN", "points": [[380, 120], [300, 120], [300, 150]]}], "labels": [], "sheet": {"width": 1600, "height": 1000}},
  "probes": [
    {"id": "coll", "target": "net:COLL", "kind": "analog", "rate_hz": 1000000},
    {"id": "btn", "target": "net:BTN", "kind": "digital"},
    {"id": "ic", "target": "device:Q1.ic", "kind": "analog"},
    {"id": "uart", "target": "serial:B1.USART1", "kind": "serial"}
  ],
  "scenarios": ["scenarios/press.json"],
  "settings": {"core_quantum_ps": "1000000000", "max_delta_cycles": 10000, "crossing_resolution_ps": 100}
}
```

Rules:

- `libraries` are resolved relative to the project file, then the user
  library directory, then the bundled catalog (SPEC-014).
- `policy` is `functional`, `timed` or `electrical` (SPEC-003 §4; ADR-011);
  the CLI and workbench may override it per run, recorded in the recording.
- `boards[].firmware.image` is relative to the project; `format` is `elf`,
  `hex` or `bin` (inferred from the extension when absent; `bin` is a flash
  image from offset 0 for a part whose flash no address reaches, the
  ESP32-C3's, as esptool's `merge_bin` writes it); `toolchain` and
  `sources` are for `embsim build` only.
- `nets` reference `<instance>.<pin>` for components and
  `<board>.<port>` for boards; a pin appears in at most one net; the loader
  merges nets joined through board ports.
- `layout` is presentation only: wire geometry, label positions, buses,
  sheet size and optional `groups: [{id, name, members}]`. Group IDs and names
  are nonempty strings. Members are unique existing top-level component/board
  instance IDs, and an instance belongs to at most one group. Groups have no
  electrical meaning or hierarchy. Deleting instances prunes group membership
  and removes empty groups.
  Instance placement accepts `locked?: boolean` (default false). A locked
  instance's x/y/rotation/mirror cannot change through layout edits until an
  explicit `locked: false` is supplied. Omitting the flag preserves an existing
  lock. Locks do not restrict parameter changes, routing or explicit deletion.
  A bus (`{name, nets, points}`) draws a group of nets as one
  line; the nets keep their own connections in `nets`. While the circuit
  runs the bus shows its value, the first net being bit 0.
  The electrical truth is `nets`. A client that edits wires updates both in
  one `project.transaction`.
- `world` is the room the circuit sits in (ADR-026, SPEC-003 §8):
  `fields` it declares (`temperature`, `humidity`, `pressure`,
  `illuminance`, `sound`, `wind`; a field left out leaves each sensor at its
  own default) and `bodies` (`{id, name?, shape?: box | sphere, at, size,
  color?}` in bench millimetres) that distance sensors see. The edit
  `set_world {fields?, bodies?}` changes it without a restart; the session
  applies the difference as world inputs at the present time.
- `layout.bench` is the bench (ADR-027, SPEC-011 §3), another drawing of
  the same nets: `parts` maps an instance to the centre of its body on the
  bench, in millimetres (x to the right, y towards the viewer seen from
  above), and its `rotation`, clockwise; `wires` are jumpers, each from one
  terminal to another (`INSTANCE.PIN`, a breadboard's hole such as
  `BB1.a12`, a board's port) with an optional `color`; `notes` are text
  placed on the bench, optionally pointing `to` a place. A part whose legs
  sit on a breadboard's holes is plugged into them: its pins are in the
  nets of those holes, and the holes of one strip share one net. A jumper's
  ends are in one net. Removing an instance removes its place and the
  jumpers to it. A client building on the bench edits `nets` and
  `layout.bench` in one transaction, so the schematic shows what the bench
  built and the bench shows, as unbuilt connections, what the schematic
  drew.
- `probes[].target` kinds: `net:NAME`, `pin:INSTANCE.PIN`,
  `device:INSTANCE.QUANTITY` (currents, powers, model outputs),
  `reg:BOARD.PERIPHERAL.REGISTER`, `mem:BOARD:ADDR:LEN`, `var:BOARD:SYMBOL`
  (DWARF), `serial:BOARD.PERIPHERAL`, `output:INSTANCE.NAME` (or
  `output:INSTANCE.MEMBER.NAME`, an output of a composite's member: a
  board's LED). `kind` selects
  the sample type (`digital`, `analog`, `serial`, `value`); `rate_hz` resamples
  analog probes for instruments; absent, samples are at accepted steps.
  A `reg`, `mem` or `var` probe is a `value` probe of the board's memory as
  a debugger reads it at that time (no side effect): its bytes
  little-endian, up to 8 of them as a number (`var` takes the ELF symbol's
  size, at most 256 bytes); `equals` and `contains` with text compare the
  bytes up to the first NUL (a C string).
- `settings` override SPEC-002 defaults for this project.

## 2. Scenario

```json
{
  "schema_version": 1,
  "kind": "scenario",
  "name": "press-and-release",
  "until": "60ms",
  "policy": "electrical",
  "inputs": [
    {"at": "10ms", "target": "SW1", "input": "pressed", "value": true},
    {"at": "30ms", "target": "SW1", "input": "pressed", "value": false},
    {"at": "5ms",  "target": "B1.USART1", "input": "rx", "value": "hello\n"},
    {"at": "40ms", "target": "B1", "input": "reset", "value": true}
  ],
  "assertions": [
    {"id": "button-seen", "at": "12ms", "probe": "btn", "expect": {"logic": "0"}},
    {"id": "led-on", "between": ["11ms", "29ms"], "probe": "coll", "expect": {"max_v": 0.4}},
    {"id": "led-current", "at": "20ms", "probe": "ic", "expect": {"min": 0.008, "max": 0.015}},
    {"id": "banner", "by": "50ms", "probe": "uart", "expect": {"contains": "ready"}},
    {"id": "no-contention", "always": true, "diagnostics": {"forbid": ["net.contention", "mcu.unsupported_access"]}}
  ]
}
```

- Times are strings with a unit (`ps`, `ns`, `us`, `ms`, `s`) and an optional
  decimal; the loader converts exactly to picoseconds and refuses sub-
  picosecond fractions. Inputs may appear in any order; equal times keep file
  order (they become `EXTERNAL` events in sequence).
- `target` is a component, a board (inputs: `reset`, `power`, and `serial`,
  text into its console as its USB-serial bridge brings it), a board
  peripheral (`rx` bytes for a serial peripheral, `voltage` for an ADC
  terminal through a component), one of a board's own parts (`B1.B1`, a
  NUCLEO's USER button; `B1.JP5_E5V`, a jumper), or `COMPONENT.MEMBER`, a
  member of a composite component (a keypad's key, `KP.k5`). `input` names an entry in
  the target's description. Component fixtures (SPEC-005 §9) take the same
  targets. `world` sets one of the room's fields (the input its name, the
  value a number, or null to undeclare it) and `world.BODY` places a body
  (the input `body`, the value `{shape?, at, size}`, or null to take it
  away), SPEC-003 §8.
- Assertions: `at` samples the probe at a time; `between` checks every
  sample in a window; `by` checks that the expectation holds at some time up
  to the bound; `always` with `diagnostics.forbid` fails on any listed
  diagnostic. `expect` keys: `logic` (`0|1|Z|X`), `min`, `max`, `min_v`,
  `max_v`, `equals`, `contains`, `edges` (a count), `frequency_hz`.
  `tolerance` is the absolute difference a numeric `equals` allows (exact
  otherwise) and the fraction a `frequency_hz` allows (5 % otherwise).
- A run with a scenario ends at `until`; the report (SPEC-008 §8) lists each
  assertion with pass, fail and the observed value.

## 3. Recording link

A run with `--record` produces a recording (SPEC-012) whose `inputs` are the
scenario's inputs plus any interactive ones, in `EXTERNAL` order. A
recording can be converted to a scenario (`embsim record to-scenario`),
which is how an interactive session becomes a regression test.

## 4. Migration

`schema_version` increments on incompatible change; `embsim migrate` rewrites
files in place and reports what it could not convert. Within a version, new
optional fields may be added.
