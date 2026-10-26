# LM358P: bandwidth, slew and wired scope loading

One named LM358P package at 5 V, with two independent channels:

- Channel 1 uses gain 11 (10 kΩ / 1 kΩ). A 0.10 → 0.11 V input step tests the
  nominal 0.7 MHz dominant-pole bandwidth without forcing large-signal slew.
- Channel 2 follows a 1 → 3 → 1 V pulse into 10 kΩ, exposing the nominal
  0.3 V/µs rising/falling slew limit and settled plateaus.
- Each output has a wired 1 MΩ ∥ 15 pF scope input. Ideal trace probes observe
  the native response of this physically loaded circuit.

```sh
build/ui-layout-native/bin/embsim run examples/lm358p-dynamics/project.json \
  --scenario examples/lm358p-dynamics/scenario.json --json
build/ui-layout-native/bin/embsim serve examples/lm358p-dynamics/project.json \
  --ui ui/apps/workbench/dist --open
```

In Run, advance 55 µs and choose **Fit captured waveforms**. Export CSV to inspect
all four actual native traces. In Design, inspect U1's `gbw` and `slew` defaults.
The seven scenario assertions check independent pole/slew/plateau bounds and
solver health. Both step directions occur within the observed window.

The source anchors are TI SLOS068AB's legacy LM358 table at 5 V, 25 °C, not
LM358B specifications. This is a bounded nominal behavioral approximation; noise,
input common-mode failures, bias/offset, output current limits, temperature,
overload recovery, real power-up and capacitive-load stability are not qualified.
See [source, calculations and explicit limits](../../docs/engineering/lm358p-dynamics.md).
