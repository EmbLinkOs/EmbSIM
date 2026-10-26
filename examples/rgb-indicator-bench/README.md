# RGB indicator bench

Open `project.json` in the workbench and start simulation. Hold SWR, SWG or SWB
to light that die; combine buttons for mixed colors. Each die has its own series
resistor. The CLI uses exactly the same electrical model:

```sh
embsim run examples/rgb-indicator-bench/project.json --scenario examples/rgb-indicator-bench/colors.json --json
```

The scenario visits off and every channel combination, then returns to off.
Probes expose each die current, normalized luminance and illustrative mixed color.
At 5 V the 159/144/104 Ω resistors target approximately 20 mA per die using the
generic model's different forward voltages. More resistance reduces current and
brightness; reverse the common and the junctions stop conducting forward.

Select a jumper in Parts & wiring to trace it, recolor it or add route points.
Appearance edits preserve the electrical circuit; reconnecting restarts it.
This is an interactive electrical acceptance circuit, with no firmware or named
manufacturer qualification. The generic LED model omits temperature, damage,
spectral calibration and perceived PWM averaging. `uno-breadboard` supplies the
separate checked firmware/button/potentiometer acceptance circuit.
