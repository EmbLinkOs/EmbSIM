# A 4-bit counter and its bus

Four D flip-flops count the pulses of a 1 kHz clock: each stage feeds its
inverted output back to its D input, so it toggles on every rising clock
edge, and its inverted output clocks the next stage. The outputs Q0..Q3
are drawn as the bus `Q`, which shows the count (`Q = 0x5`) while the
circuit runs.

The clock's first edge comes 0.5 ms after power-up, when the flip-flops
have settled at 0. The scenario checks that five edges count to 5, that
RESET (pressed at 5.2 ms) clears the count, and that afterwards Q0
toggles on each of 20 clock edges while Q3 rises at the 8th and falls at
the 16th.

```sh
embsim run examples/counter-bus/project.json --scenario examples/counter-bus/scenarios/count.json
```

In the workbench, run: the bus counts, and the logic analyzer shows the
four outputs dividing the clock; RESET (Run perspective) clears it.
