# rc-filter

A first-order RC low-pass at the **electrical** tier: `V1` (1 kHz, 0 to
3.3 V pulse) through `R1` = 10 kΩ into `C1` = 10 nF, so τ = RC = 100 µs.

```
 V1 ─── R1 10k ───┬─── OUT
                  │
                 C1 10n
                  │
                 GND
```

`scenarios/step.json` checks the textbook response: after one τ the output
is at 63 % of 3.3 V (2.09 V), after five τ it is within 1 % of the rail, and
during the low half-period it decays the same way. The window assertions
also catch numerical overshoot from the integrator (SPEC-004 §2).

```sh
embsim validate examples/rc-filter/project.json
embsim run examples/rc-filter/project.json --scenario examples/rc-filter/scenarios/step.json --json
```
