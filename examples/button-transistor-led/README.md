# button-transistor-led

The acceptance circuit of SPEC-007 §1 without the Blue Pill (M1 has no
firmware execution yet): the base drive that the MCU would supply from `C13`
comes from a 1 kHz `pulse_source` instead.

```
                 3V3
                  │
                 D1 (red LED)          3V3 ─ R3 (10k pull-up)
                  │                                │
                 R2 220 Ω                        BTN ──┤ SW1 ├── GND
                  │
 V1 ─ R1 1k ─ B  Q1 (2N3904)  C
                  E
                  │
                 GND
```

- Policy is **electrical**: the pulse, the resistors, the transistor and the
  LED form one analog island solved by the SPEC-004 engine. The button and
  pull-up are a second island, probed digitally through a comparator bridge.
- Probes: `coll` (collector voltage), `pulse`, `iled` (LED current from the
  `led` model's `observe`), `ic` (transistor collector current), `lum` (the
  LED's normalised luminance output) and `btn`.
- `scenarios/pulse-and-press.json` asserts the collector is below 0.4 V while
  the pulse is high and above 2.0 V while it is low (in cutoff the LED still
  drops about 0.66 V from picoamp leakage through `gmin`, so the collector
  settles near 2.6 V rather than at the rail), that the LED conducts 5 to
  9 mA ((3.3 − 1.8 − 0.1) V / 220 Ω ≈ 6.4 mA), that the pulse measures 1 kHz,
  and that the button net is `1` idle, `0` at 12 ms while pressed and `1`
  again at 35 ms.

```sh
embsim validate examples/button-transistor-led/project.json
embsim run examples/button-transistor-led/project.json --scenario examples/button-transistor-led/scenarios/pulse-and-press.json --json
```
