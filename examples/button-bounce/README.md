# button-bounce

A push button's contacts bounce: for a couple of milliseconds after they
close or open they spring apart and touch again. EmbSIM's buttons and switches
do this by default (ADR-034), as real ones do, and this project shows the
consequence and the usual cure side by side, under the electrical policy.

```
3.3 V ── R1 10k ──┬── RAW            3.3 V ── R2 10k ──┬── RC ──▷o── CLEAN
                  SW1                                  ├─ C1 100 nF ─ GND
                  GND                                  R3 100 ── SW2 ── GND
```

- `RAW` is the button pulled up straight to a logic line. Pressed at 10 ms,
  it falls and rises with every bounce: six edges in the first two
  milliseconds, and as many again when it is released at 60 ms. Firmware that
  counts edges on such a line counts a press several times.
- `RC` and `CLEAN` are the hardware debounce: the button discharges C1
  through R3 at once, and while the contacts spring open the capacitor
  recharges through R2 too slowly (about 1 ms per time constant) to reach the
  Schmitt-trigger inverter's 1.8 V threshold. `CLEAN` changes once per press
  and once per release. It also pulses high for the first millisecond after
  power-up, while C1 charges from empty, as it would on a bench.
- R1 to R3 and C1 are drawn inside their tolerances (5 % and 10 %) from the
  project's seed (`settings.seed`); the run report's `snapshot.drawn` lists
  each part's value. Another seed draws other parts; the scenario's checks
  hold for any of them.

```sh
embsim run examples/button-bounce/project.json --scenario examples/button-bounce/scenarios/press.json
```

Firmware can debounce instead: `examples/nucleo-f446ze-blink-serial` counts a
press only on a rising edge after 20 ms without one. A project that wants
ideal contacts says so: `"bounce": false` on a part, or
`"settings": {"imperfections": "ideal"}` for every part (Q17).
