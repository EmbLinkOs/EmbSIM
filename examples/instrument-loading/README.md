# Instrument loading bench

Four side-by-side comparisons show why wiring an instrument changes a circuit.
These are configurable generic electrical equivalents, not qualified models of
commercial test equipment.

From the repository root:

```sh
build/dev/bin/embsim run examples/instrument-loading/project.json \
  --scenario examples/instrument-loading/scenario.json --json
build/dev/bin/embsim serve examples/instrument-loading/project.json \
  --ui ui/apps/workbench/dist --port 8080
```

In Run, advance by **11us** to inspect the rising edge, or **30us** to inspect the
whole capture, then choose **Fit captured waveforms** to show microsecond
transients at a useful scale. Open **Wired instruments** for impedance, connected net names,
voltage/current readouts, **View leads** and **Add trace**. Select a wire in Design
to highlight all of its terminals. Selected parts and terminals reveal readable
pin names; live digital pins also show their observed 0/1/X/Z state.

| Comparison | Independent target |
|---|---|
| Voltmeter: 10 V through 1 MΩ | Unloaded node ≈10 V; 10 MΩ meter reads 10 × 10/11 = 9.090909 V and draws 0.909091 µA |
| Ammeter: 5 V into 100 Ω | Without meter 50 mA; with 10 Ω insertion resistance 5/110 = 45.454545 mA, burden 0.454545 V |
| Generator: open-circuit 2 V, 50 Ω output | High-impedance load ≈2 V; matched 50 Ω load 1 V and delivered current 20 mA |
| Scope: 10 V step through 1 MΩ | A 1 MΩ input gives final 5 V. With 20 pF in parallel, τ = (1 MΩ ∥ 1 MΩ) × 20 pF = 10 µs; at 11 µs voltage ≈3.160511 V, versus 5 V with zero input capacitance |

The step begins at 1 µs and rises linearly for 1 ns. Its analytic target accounts
for that finite ramp. Default adaptive solver acceptance allows ±15 mV (0.3% of
5 V final value) at one time constant; a separate native test tightens LTE and
caps the step at τ/100, checks error below 0.2 mV and verifies improvement.

The scenario executes **13 assertions**, including solver health. Current is
positive entering the instrument's + terminal; reversing ammeter leads reverses
its reading. Generator current is negative while delivering current to a load.
Waveform parameters specify **open-circuit amplitude**, so a matched termination
receives half that voltage. Use the component inspector in Design to change
input resistance, shunt resistance, capacitance or output resistance.

Observation probes and plots add no load. To measure a physical scope-input
voltage, wire `emblink/scope_input` into the circuit and trace its `voltage` output.
The load comes from that component, whether or not its plot is visible.

See [the electrical contract and limits](../../docs/engineering/instrument-loading.md).
