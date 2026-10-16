# Raspberry Pi Pico: boot2, the PLL, a timer alarm, a button and a terminal (firmware)

The bundled Raspberry Pi Pico board running [`firmware/main.c`](firmware/main.c)
on its RP2040, a Cortex-M0+. The firmware uses the registers directly (no
Pico SDK), and boots the way a Pico image does:

- [`firmware/boot2.c`](firmware/boot2.c) is the first 256 bytes of flash,
  its CRC-32 in the last word: the bootrom checks it, copies it to SRAM and
  enters it, and it hands over to the vector table at 0x10000100.
- The clocks as the SDK sets them up: the 12 MHz crystal, PLL_SYS at
  1500 MHz / 6 / 2 = 125 MHz for clk_sys and clk_peri, clk_ref from the
  crystal, and the watchdog's 1 µs tick for the timer. The frequency counter
  measures clk_sys for the banner.
- The timer's alarm 0 fires every 500 ms; its handler toggles the LED on
  GPIO25, so it blinks at 1 Hz.
- A push button from GP15 to ground, with the pad's pull-up, interrupts on
  its falling edge through IO_BANK0; each press is counted and printed.
- UART0 at 115200 baud (IBRD 67, FBRD 52 at 125 MHz) on GP0 (TX) and GP1
  (RX), with its FIFOs: what arrives raises the receive timeout interrupt
  and comes back in upper case. Numbers are printed with the SIO's
  hardware divider, since the M0+ has no divide instruction.
- Between interrupts the core waits in `wfi`.

Registers are written through the RP2040's atomic set and clear aliases
where the firmware changes a few bits (the resets, the pads, the PLL's power
bits). 5 V comes in on VBUS from a voltage source, as USB would bring it.

The scenario checks the banner (`pico: RP2040 B2, clk_sys 125000 kHz`) on
the console and the terminal, the LED off at 0.4 s and on at 0.6 s and four
edges in two seconds, a press at 1.2 s printed as `button 1`, and `pico w`
typed at 1.5 s echoed as `PICO W`.

```sh
embsim run examples/pico-blink-serial/project.json --scenario examples/pico-blink-serial/scenarios/blink.json
```

The image is committed; to rebuild it with clang and lld (or EmbCC):

```sh
cd examples/pico-blink-serial && embsim build project.json [--toolchain embcc]
```
