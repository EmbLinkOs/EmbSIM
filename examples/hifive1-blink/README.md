# HiFive1 Rev B: RISC-V timer, PLIC and UART (firmware)

The bundled HiFive1 Rev B board running [`firmware/main.c`](firmware/main.c)
on its FE310-G002, a RISC-V RV32IMAC core. The firmware uses the registers
directly, starting at 0x20010000 where the board's bootloader would jump:

- The PRCI moves the core clock from the internal oscillator (14.4 MHz out
  of reset) to the 16 MHz crystal through the bypassed PLL.
- The CLINT's timer interrupt fires every 16384 ticks of the 32.768 kHz
  real-time clock (half a second); its handler toggles the RGB LED's green
  (GPIO19, active low), so it blinks at 1 Hz.
- A push button from D2 (GPIO18) to ground, with the pin's pull-up, raises
  a falling-edge interrupt through the PLIC; the firmware claims it, clears
  the pin's pending bit, completes it, and prints the count.
- UART0 at 115200 baud (16 MHz / 139) on D1 (TX, GPIO17) and D0 (RX,
  GPIO16), their IOF0. A byte received raises the UART's PLIC source; the
  main loop echoes what arrived in upper case.
- Between interrupts the core waits in `wfi`.

5 V comes in on the board's 5V pin from a voltage source, as USB would
bring it; the board's regulator makes 3.3 V.

The scenario checks the banner on the console and the terminal, the green
LED off at 0.4 s and on at 0.6 s and four edges in two seconds, a press at
1.2 s printed as `button 1`, and `risc-v` typed at 1.5 s echoed as `RISC-V`.

```sh
embsim run examples/hifive1-blink/project.json --scenario examples/hifive1-blink/scenarios/blink.json
```

The image is committed; to rebuild it with clang and lld (or EmbCC):

```sh
cd examples/hifive1-blink && embsim build project.json [--toolchain embcc]
```
