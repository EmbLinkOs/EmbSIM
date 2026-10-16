# Arduino Uno: blink, ADC, PWM, a button and a terminal (firmware)

The bundled Arduino Uno board running [`firmware/main.c`](firmware/main.c)
on its ATmega328P, at 16 MHz. The firmware uses the registers directly
(no Arduino core), so every peripheral it touches is one the simulator
models:

- Timer0 in CTC mode at /64 with OCR0A = 249 gives a 1 ms tick; the 'L' LED
  on D13 toggles every 500 ms from it.
- A 10 kΩ potentiometer between the 5 V rail and ground, its wiper on A0:
  the ADC reads it every 250 ms (AVCC reference, /128), the firmware prints
  `A0=<value>` and sets the brightness of a blue LED on D9 with Timer1's
  8-bit fast PWM on OC1A (16 MHz / 64 / 256 = 976.6 Hz).
- A push button from D2 to ground, with the pin's internal pull-up, on INT0
  (falling edge): each press is counted and printed.
- A virtual terminal on D1 (TX) and D0 (RX) at 9600 baud, 8N1.
- Between events the core sleeps in idle mode (`SLEEP` with SMCR.SE); the
  timer interrupt wakes it.

5 V comes in on the board's 5V pin from a voltage source, as USB would
bring it.

The scenario turns the potentiometer from half to a quarter at 1 s and
presses the button at 1.6 s, then checks the banner on the console and the
terminal, the ADC readings (`A0=511` at half: the pin's 1 µA leakage holds
the wiper a little under 2.5 V), the press, four edges of the L LED in two
seconds and D9's PWM frequency.

```sh
embsim run examples/uno-blink-serial/project.json --scenario examples/uno-blink-serial/scenarios/blink.json
```

The image is committed; to rebuild it with clang and lld (or EmbCC):

```sh
cd examples/uno-blink-serial && embsim build project.json [--toolchain embcc]
```

[`firmware/startup.S`](firmware/startup.S) is the reset handler and vector
table in the assembly both clang's and EmbCC's assemblers accept, and
[`firmware/atmega328p.ld`](firmware/atmega328p.ld) the memory layout (a copy
of the board's).
