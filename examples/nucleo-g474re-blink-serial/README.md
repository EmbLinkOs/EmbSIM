# NUCLEO-G474RE: 170 MHz from the 24 MHz crystal, LD2, B1 and the virtual COM port (firmware)

The bundled NUCLEO-G474RE board running [`firmware/main.c`](firmware/main.c)
on its STM32G474RE, a Cortex-M4F in an LQFP64. The firmware writes the
registers directly (no HAL), in the order STM32CubeG4's `SystemClock_Config`
does, and waits on each ready bit as the part needs: HSE (the board's
24 MHz crystal X3, its 2 ms start-up) and the PLL's lock.

- Range 1 boost (PWR_CR5's R1MODE cleared), the PLL at 24 MHz / 6 x 85 / 2
  = 170 MHz, four flash wait states with the prefetch, the AHB at /2 for
  the switch to the PLL and back to /1 (RM0440 7.2.7).
- SysTick interrupts every millisecond; every 500 ms it toggles LD2 (green,
  PA5), so LD2 blinks at 1 Hz.
- The blue USER button B1 pulls PC13 to ground on the Nucleo-64 (a pull-up
  holds it high otherwise), so EXTI13 takes its falling edge; each press is
  counted and reported on the console.
- LPUART1 on PA2/PA3 (AF12) at 115200 baud (BRR = 256 x 170 MHz / 115200)
  is the STLINK-V3E's virtual COM port: the session's console, and a serial
  port on the host with `embsim serve --serial B1`. What arrives comes back
  in upper case.

5 V comes in on the STLINK-V3E's USB (U5V) from a voltage source, as a
computer's port brings it: JP5 on STLK takes it to +5V, U12 makes 3.3 V and
JP6 (IDD) carries it to the STM32.

The scenario checks the banner
(`nucleo-g474re: STM32G474RE, flash 512 KB, SYSCLK 170000 kHz, reset por`),
VDD, the blink, a press of B1, the echo, RCC's CFGR on the PLL and the
engineering bytes' FLASH_SIZE.

```bash
embsim run examples/nucleo-g474re-blink-serial/project.json --scenario examples/nucleo-g474re-blink-serial/scenarios/blink.json
```

`firmware/build.sh` rebuilds the image with clang and lld; `embsim build`
does the same from the project.
