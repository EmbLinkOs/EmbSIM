<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Junior Deogracias -->
# Analog validation fixtures (SPEC-004 §10)

Each directory `tests/analog/fixtures/<name>/` holds three files. The unit test
`tests/unit/test_analog.c` (`test_fixtures`) builds the circuit through the
engine API, computes the operating point, advances to every sample time in
order and compares.

## `circuit.json` — a bare island

```json
{
  "name": "rc-charge",
  "ground": "gnd",
  "supplies": { "vcc": 3.3 },
  "components": [
    { "id": "V1", "device": "vsource",   "params": { "dc": 5 },     "pins": ["vin", "gnd"] },
    { "id": "R1", "device": "resistor",  "params": { "r": "1k" },   "pins": ["vin", "out"] },
    { "id": "C1", "device": "capacitor", "params": { "c": "1u" },   "pins": ["out", "gnd"] }
  ]
}
```

- `ground`: the net that becomes node 0 (a power pin with role ground is
  attached to it). Without it the island gets an implicit ground and the
  diagnostic `analog.implicit_ground`.
- `supplies` (optional): nets held by an ideal source through a power pin
  (`{"net": volts}`), the same path an MCU's VDD pin takes.
- `components[]`: `device` is an analog primitive name from
  `core/analog/devices.c`; `params` are the primitive's parameters (numbers or
  strings with SI suffixes: `"4.7k"`, `"100n"`); `pins` lists the nets in the
  primitive's pin order (see the table at the top of `devices.c`: resistor
  `1, 2`; diode `A, K`; BJT `C, B, E`; MOSFET `D, G, S`; potentiometer
  `1, W, 2`; sources `+, -`; op-amp `+, -, OUT`; regulator `IN, OUT, GND`).
  Pulse/PWL source times are in seconds (`"0.5m"` = 500 µs).

## `expected.json`

```json
{
  "tolerance": { "abs": 0.001, "rel": 0.01 },
  "op": { "out": 0.0 },
  "op_devices": [ { "device": "Q1", "key": "region", "equals": "saturation" } ],
  "samples": [
    { "t": "1ms", "nets": { "out": 3.1606 }, "rel": 0.01 },
    { "t": "5ms", "nets": { "out": 4.9663 },
      "devices": [ { "device": "C1", "key": "i", "max": 1e-4 } ] }
  ]
}
```

- `tolerance`: default absolute and relative tolerances (`|got - expected| <=
  abs + rel * |expected|`); a sample may override them with `abs` / `rel`.
- `op`: node voltages of the operating point (time 0).
- `op_devices` and `samples[].devices`: device observations (`observe()`
  keys such as `i`, `p`, `ic`, `vce`, `region`, `luminance`) with `equals`
  (a string, or a number with `tol`) or a `min` / `max` range.
- `samples`: in increasing time order (`t` is a time with a unit,
  SPEC-007 §2).

## `source.md`

Names the generator: an analytic derivation (with the formulas), or an
ngspice netlist.

## `ngspice.json`

ngspice's solution of the same circuit, written by
`tools/analog/ngspice_fixture.py` (the netlist it ran, the version, every
node a source does not hold, at time 0 and at each sample time of
`expected.json`); `--check` regenerates them and fails on a difference, as
CI's tools job does. `test_analog` compares EmbSIM against it within 2 mV +
0.2 %, besides `expected.json`.

Measured with ngspice 42 (RELTOL 1e-6): the operating points (divider,
LEDs, the NPN in saturation, the potentiometer) agree to the seven digits
ngspice prints; the transients differ by at most 6.4 mV (0.21 %, the RC
charge at 2 ms) where ngspice agrees with the analytic solution to 1e-5:
EmbSIM's step control, not its models.

## Fixtures present

| Name | Status |
|---|---|
| `divider` | analytic; ngspice reference |
| `rc-charge` | analytic; ngspice reference |
| `rl` | analytic; ngspice reference |
| `npn-switch` | analytic bounds; ngspice reference |
| `led-resistor` | analytic bounds; ngspice reference |
| `pwm-rc` | analytic (periodic steady state); ngspice reference |
| `pot-divider` | analytic; ngspice reference |

Still required at M0 by SPEC-004 §10 and not yet present: RC discharge, NPN
emitter follower, NMOS switch, op-amp inverting amplifier, comparator with
hysteresis, bouncing switch into a pull-up (these behaviours are covered by
unit tests in `test_analog.c` but have no fixture directory yet).
