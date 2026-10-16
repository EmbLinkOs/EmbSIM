# nRF52840 DK: RTC blink, GPIOTE, EasyDMA serial and the FPU (firmware)

The bundled nRF52840 DK board running [`firmware/main.c`](firmware/main.c)
on its nRF52840, a Cortex-M4F at 64 MHz. The firmware uses the registers
directly (no nrfx, no SDK), the way Nordic's peripherals are driven: tasks
triggered by writing 1, events cleared by writing 0, interrupts enabled per
event.

- CLOCK starts the 32 MHz crystal (HFXO) and the 32.768 kHz crystal (LFXO).
- RTC1 counts the 32.768 kHz clock; its COMPARE0 every 16384 counts (half a
  second) toggles LED1 (P0.13, active low), so it blinks at 1 Hz.
- A push button from D2 (P1.03) to ground, with the pin's pull-up, is
  GPIOTE channel 0's falling edge: each press is counted and printed.
- UARTE0 at 115200 baud on the virtual COM port's pins (TXD P0.06, RXD
  P0.08) with EasyDMA: the banner and the replies go out of a RAM buffer
  (EasyDMA reaches RAM only), and each byte comes into a one-byte buffer
  that the ENDRX to STARTRX short keeps receiving; what arrives comes back
  in upper case.
- The FPU computes sqrt(2) by Newton's method for the banner, TIMER0 timing
  it in microseconds; the startup enables CP10 and CP11 in CPACR first.
- Between interrupts the core waits in `wfi`.

5 V comes in on the board's 5V pin from a voltage source, as USB would
bring it; the board's regulator makes VDD at 3.0 V.

The scenario checks the banner (`nrf52840: part 52840, sqrt(2) = 1.41421 by
the FPU in ... us`, the part number read from FICR) on the console and the
terminal, LED1 off at 0.4 s and on at 0.6 s and four edges in two seconds, a
press at 1.2 s printed as `button 1`, and `nordic` typed at 1.5 s echoed as
`NORDIC`.

```sh
embsim run examples/nrf52840dk-blink/project.json --scenario examples/nrf52840dk-blink/scenarios/blink.json
```

The image is committed; to rebuild it with clang and lld (or EmbCC):

```sh
cd examples/nrf52840dk-blink && embsim build project.json [--toolchain embcc]
```
