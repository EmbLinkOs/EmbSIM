# Reusable loaded measurement stages

Two instances share a stage containing a 1 MΩ source resistor and a physical
voltmeter. Both receive 10 V and an explicit common return through block ports.
Native instance parameters set A's input resistance to 1 MΩ and B's to 10 MΩ:

- A reads 10 × 1/(1+1) = 5 V.
- B reads 10 × 10/(1+10) = 100/11 V ≈ 9.0909 V.

```sh
build/ui-layout-native/bin/embsim run examples/hierarchical-instruments/project.json \
  --scenario examples/hierarchical-instruments/scenario.json --json
build/ui-layout-native/bin/embsim serve examples/hierarchical-instruments/project.json \
  --ui ui/apps/workbench/dist --open
```

Run lists `A.METER` and `B.METER` separately. Their currents/voltages and trace
targets belong to the actual native instances. Configuration displays bound
resistance read-only; change the parent's `r` parameter for an instance override.
Lead edits affect the shared definition and explicitly restart simulation.
These are generic finite-resistance voltmeters, without commercial accuracy,
noise/protection or real hardware qualification. See
[instrument configuration and acceptance](../../docs/engineering/loaded-instrument-controls.md).
