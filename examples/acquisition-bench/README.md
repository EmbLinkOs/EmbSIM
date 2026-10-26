# Loaded RC acquisition bench

A 5 V pulse starts at 1 ms with a 4 ms period and 2 ms high time. The generator's
50 Ω output and external 950 Ω resistor charge a 1 µF capacitor. A wired scope
input adds 1 MΩ in parallel with 15 pF. Two ideal 200 ksample/s observations watch
the generator terminal and RC response; they add no extra loading.

The first charge approaches `5 × 1 MΩ / (1 MΩ + 1 kΩ)` with time constant
`(1 kΩ ∥ 1 MΩ) × (1 µF + 15 pF) = 0.999016 ms`. The three scenario checkpoints
compare against this independent exponential with a 6 mV tolerance. The source
has a finite 1 ns rise; this simple bench does not qualify high-frequency behavior.

Open Run, choose trigger `drive`, rising edge, 1.65 V, 0.05 V hysteresis, a 2 ms
window and 25% pre-trigger. Arm Single, advance 2 ms, then another 1 ms. The first
qualifying sampled edge is near 1.005 ms; the capture spans approximately
0.505–2.505 ms. The time-weighted panel shows full coverage. Export CSV, advance
again, and export: the capture remains unchanged while live meters continue.
Drag `drive` to ground or choose GND in its keyboard net selector, then select
Live to see the new capture. Rearm to capture another edge.

```sh
embsim run examples/acquisition-bench/project.json \
  --scenario examples/acquisition-bench/scenario.json --json
embsim serve examples/acquisition-bench/project.json --ui ui/apps/workbench/dist
```

See [acquisition contract and limits](../../docs/engineering/schematic-acquisition.md).
