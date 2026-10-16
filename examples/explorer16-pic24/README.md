# Explorer 16/32 with the PIC24FJ1024GB610: the PLL, Timer1, the interrupt-on-change, PPS and a terminal (firmware)

The bundled Explorer 16/32 board with its PIC24FJ1024GB610 plug-in module
(MA240023), running [`firmware/main.c`](firmware/main.c) on the part's
PIC24F core. The firmware is an ordinary XC16 program: the compiler's own
startup (`crt0` from the C library, the part's linker script), the device
header `<xc.h>`, the configuration words as `#pragma config` and interrupt
handlers by their vector names (`_T1Interrupt`).

- The configuration words select the 8 MHz crystal (HS) through the 96 MHz
  PLL (`PLLMODE = PLL96DIV2`) for a 32 MHz FOSC, a 16 MHz FCY. The firmware
  works FOSC out from OSCCON and CLKDIV for its banner, and reads FOSCSEL
  back from program memory with a table read (`__builtin_tblrdl`).
- The Peripheral Pin Select puts UART2 on the Explorer's USB-UART bridge:
  U2TX on RP17 (RF5), U2RX from RP10 (RF4), between OSCCON's unlock
  sequences (`__builtin_write_OSCCONL`) that clear and set IOLOCK.
- Timer1 counts FCY / 256 and interrupts every 100 ms; every fifth tick
  toggles D3 (RA0), so it blinks at 1 Hz.
- S3 on RD6 interrupts through the interrupt-on-change on its falling edge
  (`_IOCInterrupt`); each press is counted, printed and shown in binary on
  D4 to D10.
- UART2 at 115200 baud (BRGH, U2BRG 34 at 16 MHz): what arrives raises the
  receive interrupt and comes back in upper case.
- Between interrupts the core idles (`Idle()`, PWRSAV #1).

5 V comes in on VIN from a voltage source; a virtual terminal sits on the
USB-UART bridge's pins.

The scenario checks the banner (`explorer16: PIC24FJ1024GB610, FOSC 32000
kHz, FCY 16000 kHz, FOSCSEL 0xff0b`) on the console and the terminal, D3 off
at 0.4 s and on at 0.6 s with four edges in two seconds, S3 pressed at 1.2 s
printed as `S3 pressed 1` with D4 lit, and `pic24 xc16` typed at 1.5 s
echoed as `PIC24 XC16`.

```sh
embsim run examples/explorer16-pic24/project.json --scenario examples/explorer16-pic24/scenarios/blink.json
```

The image is committed (built with MPLAB XC16 v2.10 in its free mode). To
rebuild it, with `xc16-gcc` on the path:

```sh
cd examples/explorer16-pic24 && embsim build project.json
```

or [`firmware/build.sh`](firmware/build.sh), which runs the same commands.
