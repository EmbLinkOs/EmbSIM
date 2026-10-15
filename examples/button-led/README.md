# button-led

The smallest EmbSIM project: a push button, a pull-down and an indicator at
the **functional** tier. No MCU and no firmware; everything is logic levels.

```
VCC ──┤ SW1 ├──┬── D1 (indicator) ── GND
               │
             R1 (pull-down)
```

- `project.json` declares the four parts and three nets. `R1` is an
  `emblink/pull_down`, a weak `0` driver, so the `BTN` net reads `0` until the
  button connects it to `VCC`.
- `scenarios/press.json` presses `SW1` at 10 ms and releases it at 30 ms, then
  asserts the net is `1` at 12 ms, `0` again at 35 ms, that the indicator is lit
  while held, and that no contention or floating-input diagnostic appears.

Run it headless:

```sh
embsim validate examples/button-led/project.json
embsim run examples/button-led/project.json --scenario examples/button-led/scenarios/press.json --json
```

Exit status `0` means every assertion passed (SPEC-008 §9).
