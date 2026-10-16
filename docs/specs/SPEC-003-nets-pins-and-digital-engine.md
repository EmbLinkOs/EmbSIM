# SPEC-003 - Nets, Pins and the Digital Engine

**Status:** Accepted 2026-10-15. **Builds on:** 03 §3, §4; ADR-003.
**Requirements:** `docs/requirements/NET.md`, `docs/requirements/DIG.md`.

---

## 1. Nets

A net is a named node joining pins. Names are unique within their scope (a
board's internal nets are scoped by the board instance: `U1.VCC`; project
nets are top-level). Two nets joined by a wire or a connector are merged at
load into one net with the project-level name winning, aliases retained for
display. Every net has a resolved state: in a digital island a logic value and
strength; in an analog island a voltage; both carry the time of last change.

Limits: `max_nets` (default 65 536), `max_pins_per_net` (default 4 096).

## 2. Pin models

Every pin of every component instance has a pin model, from the component
description (SPEC-005 §3) with the instance's parameters applied:

```json
{"kind": "digital_out", "drive": "push_pull", "strength": "strong",
 "voh": 2.9, "vol": 0.4, "rout": 25, "slew_v_per_us": 500}
{"kind": "digital_in", "vih": 2.0, "vil": 0.8, "hysteresis": 0.2,
 "pull": "up", "rpull": 40000, "leak_a": 1e-6}
{"kind": "digital_io", "out": {...}, "in": {...}}
{"kind": "analog"}
{"kind": "power", "role": "supply", "nominal_v": 3.3, "tolerance": 0.05}
{"kind": "power", "role": "ground"}
{"kind": "passive"}
{"kind": "nc"}
```

`digital_io` pins (GPIO) switch between output and input at run time under
model control (`esk_pin_set_direction`). Defaults for omitted electrical
parameters come from the component's logic family (`cmos_3v3`, `cmos_5v`,
`ttl_5v`, `lvcmos_1v8`) named in the description; a description with no
family and no values is `functional` only.

## 3. Logic values and strengths

Values: `0`, `1`, `Z` (not driven), `X` (unknown or conflict). Strengths:
`supply` (3) > `strong` (2) > `weak` (1) > `hiz` (0). A driver is
`(value, strength)`; `Z` has strength `hiz` by definition.

Resolution of a net's drivers: let `S` be the maximum strength among drivers
whose value is `0` or `1`. If no such driver, the result is `Z` (strength
`hiz`). Otherwise, if all drivers at strength `S` agree, the result is that
value at `S`; if they disagree, the result is `X` at `S` and diagnostic
`net.contention` is raised once per contention episode naming the drivers.
Drivers weaker than `S` never affect the result. An `open_drain` driver
contributes `(0, strength)` or `Z`; `open_source` contributes `(1, strength)`
or `Z`; `tristate` contributes `Z` when disabled.

Pulls are weak drivers: `pull: up` contributes `(1, weak)`, `down`
contributes `(0, weak)`. A `power` pin contributes `(1, supply)` for a
supply, `(0, supply)` for ground, in a digital island.

An input samples the resolved value. Sampling `Z` raises `net.floating_input`
(warning, once per input per episode) unless the sampling input has a pull;
sampling `X` raises `net.unknown_input` (warning).

## 4. Islands

After load and after every topology transaction, nets are partitioned:

- A net is **analog** if any of its pins is `analog` or `passive` attached to
  a non-wire device, or if the session policy is `electrical` and any pin on
  it has an electrical pin model whose component declares `electrical`
  fidelity for that pin. Nets connected through a passive device (resistor,
  capacitor, inductor, switch, potentiometer) belong to one analog island.
- Every other net is **digital**; digital nets form one island per connected
  component through zero-delay buffers only for diagnostics (they are solved
  independently).
- A `digital_out` pin on an analog net gets a **Thevenin bridge**: a source
  of `voh` (value 1) or `vol` (value 0) through `rout`, open for `Z`, with the
  pin's slew rate limiting the source transition; an `open_drain` output
  drives `vol` through `rout` or is open.
- A `digital_in` pin on an analog net gets a **comparator bridge**: the pin
  samples `1` when the node voltage rises above `vih`, `0` when it falls below
  `vil`, and keeps its value between them (hysteresis); crossings are events
  with the analog bracket resolution. Its pull is a resistor `rpull` to the
  island's supply or ground node.
- A `power` pin on an analog island is an ideal source unless a component
  on the island declares it drives that net (a regulator model), in which case
  the component's model is used.

In `functional` policy all nets are digital; electrical parameters are
ignored; a diode or transistor used as a logic element behaves as its
description's `functional` model says (an LED is lit when its anode is `1`
and its cathode `0`; a BJT switch is on when its base is `1`), and a
component with no `functional` model raises `island.requires_electrical`
(error).

The partition is reported to clients as `{net: island_id, island: {kind,
nets}}` after each recomputation.

## 5. The digital engine

- A component's model is evaluated when a pin it declared as an input
  changes (`esk_model_on_input`), when one of its scheduled wake-ups fires
  (`esk_model_on_time`), and at reset. Evaluation reads inputs, updates
  state and schedules outputs with their delays through
  `esk_pin_drive(pin, value, strength, delay_ps)`; a zero delay schedules a
  delta event at the current time.
- Inertial delay semantics: a scheduled change that is superseded by another
  change to the same pin before it takes effect is cancelled (glitches shorter
  than the delay are swallowed), unless the model asks for transport delay.
- Primitive digital models in the core: `buffer`, `inverter`, `and`, `nand`,
  `or`, `nor`, `xor`, `xnor` (2 to 8 inputs), `tristate_buffer`, `mux2`,
  `dff` (with set/reset, edge), `latch`, `counter`, `shift_register`,
  `clock_source`, `pulse_source`, `logic_level` (constant), `pull`,
  `switch` and `push_button` (momentary, toggle, with optional bounce model),
  `indicator` (functional LED), `virtual_terminal` (UART decoder/encoder on
  a net), `logic_probe`. Datasheet timing (`tpd`, setup, hold, minimum pulse)
  come from the description's `timed` section.
- Buses at the logic level (UART, SPI, I²C) are modelled by components
  toggling pins; the engine has no notion of a bus. Protocol decoders are
  probes (SPEC-008 `probe.add` with `decode`).

## 6. Topology transactions

A wire edit while a session is loaded is a transaction
(`project.transaction` with `begin`, `edit...`, `commit|abort`). On commit
the session pauses at the current timestamp, validates the new topology
(all pins resolvable, no power short, limits), recomputes islands and
bridges, re-initialises analog islands from their last operating point as
an initial guess, and either applies everything or leaves the previous
topology intact, returning the diagnostics. Hot-plug electrical effects are
not modelled by an edit; a component that models them (a connector with
contact bounce) does.

## 7. Diagnostics

`net.contention`, `net.floating_input`, `net.unknown_input`,
`net.power_short` (two supplies of different nominal voltage on one net),
`net.unconnected_required_pin`, `sched.delta_overflow`, `dig.setup_violation`,
`dig.hold_violation`, `dig.min_pulse_violation`, `island.requires_electrical`.

### Implemented packaged HC gates

`hc_package` binds the complete 14-pin HC00/02/04/08/32/86 variants. `gate`
selects NAND/NOR/inverter/AND/OR/XOR; four 2-input channels or six inverters
are independent. Logical pins are `1A`, `1B`, `1Y` etc.; physical pin numbers
come from each description. `VCC` and `GND` are sensed inputs rather than ideal
sources. VCC=0 releases outputs; unknown rails produce X. Timed/electrical runs
use the declared inertial `tpd`, functional runs use zero delay. Electrical
amplitude is the nominal 5 V CMOS family, not a full supply-dependent IC model.
