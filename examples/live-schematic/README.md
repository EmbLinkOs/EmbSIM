# Live schematic acceptance circuit

Open this project in Design or Run. The schematic itself shows five LED colors,
RGB current mixing, antiparallel bicolor dies, a selected matrix pixel and a 1 kHz
25% PWM LED. The contacts and potentiometer alter the native electrical circuit.

```sh
embsim serve examples/live-schematic/project.json --open
embsim run examples/live-schematic/project.json \
  --scenario examples/live-schematic/scenarios/controls.json --json
```

In **Run → Operate**, hold HOLD to complete the red LED's return; toggle LATCH
for green. Use pointer movement or Home/End/arrows on POT. The wired 10 MΩ
voltmeter loads the 10 kΩ wiper. With position `p`, its unloaded voltage is
`5 × (1-p)` V; loaded voltage includes `(10 kΩ × p × (1-p)) || 10 MΩ` divider
loading. The acceptance checks positions 0.25/0.75 near 3.7493/1.24977 V.

**Interval average** measures LED emission across successive observations, so
DPWM remains visibly active at an off-time observation boundary. **Instant**
shows the instantaneous off state. The 1×/4×/16× light selector changes display
exposure only (default 4×); native currents and reported measurements are unchanged.
This is illustrative light, not calibrated
photometry. Exposure does not change the currents or the scenario measurements.

Expand **Voltage comparison** and select A/B nets or pick them on the sheet.
Drag the markers to move them. These ideal observations add zero load; use a
wired instrument when its impedance should affect the circuit.

The matrix has row anodes and column cathodes. R1 is high, C1 returns through
330 Ω; other rows are low and other columns high. Only D11 conducts. Current
limiting is external. Named labels connect the separate supply/ground stubs;
there is no hidden browser-side wiring. The bicolor component is two antiparallel
LEDs, and its separate fixtures check reversed polarity and an open return.

The ten scenario assertions include solver health. Model limits and the broader
completion gates are recorded in [live schematic engineering](../../docs/engineering/live-schematic.md).
