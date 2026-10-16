# Two boards on one serial line: an Uno and a Blue Pill (firmware)

Two MCUs of different architectures in one session, talking over a wire
pair the way the real boards would:

- **B1, an Arduino Uno** (ATmega328P, AVR, 5 V) runs
  [`firmware/main.c`](firmware/main.c): every 100 ms it sends `ping N` on
  D1 (TX); when `PING N` comes back on D0 (RX) it answers `ok N`, counts the
  round trip and, from the third, lights its L LED. Its USART runs at
  117647 baud (U2X, UBRR 16), the nearest its 16 MHz gets to 115200.
- **B2, a Blue Pill** (STM32F103C8, Cortex-M3, 3.3 V) runs the
  [Blue Pill example's firmware](../bluepill-button-led/firmware/main.c)
  unchanged: it prints its banner and echoes what USART1 receives in upper
  case, at 115200 baud.
- The Uno's TX drives the Blue Pill's PA10 at 5 V (PA10 is 5 V tolerant);
  the Blue Pill's PA9 drives the Uno's D0 at 3.3 V, above the AVR's
  0.6 Vcc input threshold. Both boards take 5 V from one source and share
  ground.

Each receiver decodes the other's waveform at its own baud rate, so the
2.1 % mismatch between the two is in the simulation as it is on the bench.

The scenario checks the Blue Pill's banner, the first echo and answer by
100 ms, the third by 400 ms with the L LED lit, and the line levels (5 V on
the Uno's side, 3.3 V on the Blue Pill's). `tests/protocol/test_multiboard.py`
runs it twice and requires the two runs to agree exactly.

```sh
embsim run examples/two-boards-uart/project.json --scenario examples/two-boards-uart/scenarios/exchange.json
```

The Uno's image is committed (`firmware/build.sh` rebuilds it with clang);
the Blue Pill's is the other example's.
