# Blue Pill, button and LED (no firmware yet)

The acceptance circuit of the vision document around the bundled Blue Pill
board description: USB 5 V into the board's regulator, a push button from
A0 to ground, an external LED with a 1 kΩ series resistor on C13. In this
version the MCU is a passive placeholder (`mcu.no_runtime`), so A0 reads
whatever the button and the board's pulls make of it and C13 drives nothing;
the scenario checks the regulated rail, the button net and that the LED stays
dark. "Dark" is below 0.1 mA rather than zero: the board's own user LED and
its 1 kΩ resistor tie C13 to 3.3 V, so the external LED sits in series with
it and the two forward drops just exceed the rail, leaving a few tens of
microamps, as on the real board with C13 left as an input. The M1 firmware milestone turns the placeholder into the STM32F103C8
runtime, and this example then grows the firmware that makes the LED follow
the button.

```sh
embsim run examples/bluepill-button-led/project.json --scenario examples/bluepill-button-led/scenarios/press.json
```
