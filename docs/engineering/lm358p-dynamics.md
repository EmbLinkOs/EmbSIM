# LM358P nominal dynamic evidence

The named `emblink/lm358p` PDIP-8 model now forwards gain-bandwidth and slew
parameters to both existing native op-amp channels. Its defaults are the legacy
LM358's **0.7 MHz** gain-bandwidth and **0.3 V/µs** slew rate. The previous named
wrapper did not forward those values: its child inherited the generic op-amp's
1 MHz pole and no explicit slew limit. Version 1.1.0 corrects those defaults;
existing projects can therefore produce different transient responses.

## Source and supported claim

Texas Instruments SLOS068AB, revised October 2024, physical PDF page 10,
section 5.7 **Electrical Characteristics: LM358, LM358A**:

- Table conditions: VS = 5 V, TA = 25 °C unless otherwise stated.
- Gain-bandwidth product: 0.7 MHz, typical.
- Slew rate: 0.3 V/µs, typical, G = +1.
- The table is for legacy LM358/LM358A, not the different LM358B/BA family.

[Reviewed PDF](https://www.ti.com/lit/ds/symlink/lm358.pdf), re-downloaded and hash
verified on 2026-10-27:
`8277dff7b89138c63b3582d37f378065a1dea3cae5ed1e2926ddc2779eb7e3a2`.
The package pin mapping remains the existing reviewed manufacturer mapping.

These are two nominal dynamic anchors, **not full LM358P qualification**. The
native model uses one dominant pole, symmetric slew, finite gain, representative
50 Ω output resistance and fixed headroom equivalents. It does not implement
input offset/bias, input common-mode failure, phase reversal, output current
limiting, supply current, noise, temperature/process corners, asymmetric slew,
higher poles, overload recovery or qualified capacitive-load stability. Optional
or reversed/unpowered supplies are not a manufacturer-qualified operating mode.
The table's typical values do not establish guaranteed minimum/maximum dynamics.

## Independent circuit expectations

The component fixture suite checks both physical channels, rising and falling
steps, shared-supply channel isolation, finite-output DC headroom and forwarding
of slower experimental parameter overrides. The quiet-channel interval begins at
5 µs, before the other channel's 10 µs input step, to exclude startup settling.
This initialization is not a qualified real-device power-up sequence.

The [rebuildable bench](../../examples/lm358p-dynamics/README.md) combines:

- Channel 1, non-inverting gain 11 from 10 kΩ / 1 kΩ feedback: 0.10 → 0.11 V
  input gives approximately 1.10 → 1.21 V output. The dominant-pole approximation
  gives τ = 11/(2π × 700 kHz) = 2.501 µs and a one-τ output near 1.1695 V.
  Native sampled acceptance is 1.160–1.179 V near this checkpoint, allowing the
  declared numerical/sampling/model approximation rather than asserting exact
  manufacturer waveform matching.
- Channel 2, a follower into 10 kΩ: a 1 → 3 V step takes approximately 6.67 µs
  at 0.3 V/µs. One microsecond into the edge it should be near 1.3 V; a falling
  edge should be near 2.7 V one microsecond later. Settled bounds independently
  check both 1 V and 3 V plateaus.
- Two actual wired scope inputs each add 1 MΩ ∥ 15 pF to the native solve.
  The trace probes are ideal observations of this already loaded circuit.

Acceptance bounds are circuit-equivalent checks derived from the source anchors;
they are not measured hardware captures or manufacturer tolerances. The parameter
override fixtures check composition plumbing, not additional production corners.
Setting `gbw = 0` disables the dynamic pole and its associated slew limiter;
`slew = 0` disables just the explicit slew limit when the pole remains enabled.
Nondefault overrides no longer represent the nominal source point.

## Reproduction

```sh
python tools/catalog/run_fixtures.py --embsim build/ui-layout-native/bin/embsim \
  --component lm358p
build/ui-layout-native/bin/embsim run examples/lm358p-dynamics/project.json \
  --scenario examples/lm358p-dynamics/scenario.json --json
python tests/browser/test_lm358p.py --embsim build/ui-layout-native/bin/embsim \
  --chromium /usr/bin/chromium --screenshots /tmp/embsim-lm358p
```

The browser reads the same native catalog defaults, runs the actual circuit and
checks exported CSV points for gain/pole response, rising slew, falling slew and
settled plateaus. It does not inject waveform values or another electrical model.
Repeating the bench at 20 MHz instead of 10 MHz probe sampling passes the same
seven assertions; its six voltage checkpoints differ by less than 3.1 µV. This
checks numerical/sampling convergence, not real-device accuracy.
Further qualification requires independent reference/hardware waveforms, supply
and load sweeps, invalid/common-mode conditions and the excluded device effects.
