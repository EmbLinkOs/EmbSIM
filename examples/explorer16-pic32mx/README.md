# Explorer 16/32 with the PIC32MX795F512L: the PLL, Timer1, a change notice and a terminal (firmware)

The bundled Explorer 16/32 board with its PIC32MX795F512L plug-in module,
running [`firmware/main.c`](firmware/main.c) on the part's MIPS32 M4K core.
The firmware uses the registers directly (no XC32 or Harmony), and starts
the way a PIC32 image does:

- [`firmware/crt0.S`](firmware/crt0.S) is at the reset vector in boot flash
  (0xBFC00000): it sets the stack, puts EBASE at the program's vectors in
  program flash with 32-byte spacing, sets Cause.IV, takes Status out of
  its boot state, copies `.data`, clears `.bss` and calls `main`. Each
  vector jumps to one handler, which saves the registers C may change and
  calls `isr(vector)`.
- The configuration words at 0xBFC02FF0 select the 8 MHz crystal through
  the PLL (/2, x20, /1) for an 80 MHz SYSCLK, and PBCLK at SYSCLK / 2. The
  firmware works SYSCLK out from OSCCON and DEVCFG2 for its banner, with
  the part's DEVID.
- Timer1 counts PBCLK / 256 and interrupts every 100 ms (vector 4); every
  fifth tick toggles D3 through LATAINV, so it blinks at 1 Hz.
- S3 on RD6 (CN15) interrupts through the change notice (vector 26); each
  press is counted, printed and shown in binary on D4 to D10.
- UART2 at 115200 baud (BRGH, U2BRG 86 at 40 MHz) on RF5 (TX) and RF4 (RX),
  the Explorer's USB-UART bridge: what arrives raises the receive interrupt
  (vector 32, shared with UART2's transmit and error requests) and comes
  back in upper case.
- Between interrupts the core waits (`wait`). The EVIC runs in multi-vector
  mode; no interrupt runs at priority 7, which DEVCFG3.FSRSSEL gives the
  shadow register set.

5 V comes in on VIN from a voltage source; a virtual terminal sits on the
USB-UART bridge's pins.

The scenario checks the banner (`explorer16: PIC32MX795F512L, SYSCLK 80000
kHz, PBCLK 40000 kHz`) on the console and the terminal, D3 off at 0.4 s and
on at 0.6 s with four edges in two seconds, S3 pressed at 1.2 s printed as
`S3 pressed 1` with D4 lit, and `pic32 mips` typed at 1.5 s echoed as
`PIC32 MIPS`.

```sh
embsim run examples/explorer16-pic32mx/project.json --scenario examples/explorer16-pic32mx/scenarios/blink.json
```

The image is committed; to rebuild it with clang and lld (or EmbCC):

```sh
cd examples/explorer16-pic32mx && embsim build project.json [--toolchain embcc]
```

Clang is driven with the `mipsel-linux-gnu` triple and `-nostdlib
-nostartfiles -static` (the toolchain profile's `mipsel-none-elf`), since
its generic driver links MIPS through the host's gcc; the image is the same
bare-metal ELF.
