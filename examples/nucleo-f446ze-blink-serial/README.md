# NUCLEO-F446ZE: 180 MHz from the ST-LINK's MCO, LD1 to LD3, B1 and the virtual COM port (firmware)

The bundled NUCLEO-F446ZE board running [`firmware/main.c`](firmware/main.c)
on its STM32F446ZE, a Cortex-M4F in an LQFP144. The firmware writes the
registers directly (no HAL), in the order STM32CubeF4's `SystemClock_Config`
does, and waits on each ready bit as the part needs: HSE (the ST-LINK's
8 MHz MCO in bypass), the PLL's lock and the over-drive.

- Voltage scale 1, the main PLL at 8 MHz / 8 x 360 / 2 = 180 MHz, the
  over-drive switched on, five flash wait states with the caches, APB1 at
  45 MHz and APB2 at 90 MHz, then SYSCLK from the PLL.
- SysTick interrupts every millisecond; every 500 ms it toggles LD1 (green,
  PB0), so LD1 blinks at 1 Hz.
- The blue USER button B1 pulls PC13 up to VDD on the NUCLEO-144 (a
  pull-down holds it low otherwise), so EXTI13 takes its rising edge; each
  press is counted and toggles LD2 (blue, PB7).
- LD3 (red, PB14) lights after a reset that was not a power-on one (RESET,
  B2, or a watchdog).
- USART3 on PD8/PD9 (AF7) at 115200 baud is the ST-LINK's virtual COM port:
  the session's console, and a serial port on the host with
  `embsim serve --serial B1`. What arrives comes back in upper case.

5 V comes in on the ST-LINK's USB (U5V) from a voltage source, as a
computer's port brings it: the power switch and JP3 take it to +5V, the
regulator makes 3.3 V and JP5 (IDD) carries it to the STM32.

The scenario checks the banner
(`nucleo-f446ze: STM32F446ZE, flash 512 KB, SYSCLK 180000 kHz, reset por`),
VDD, the blink, a press of B1 (LD2 toggled), the echo, RCC's CFGR on the PLL
and the factory's F_SIZE.

```bash
embsim run examples/nucleo-f446ze-blink-serial/project.json --scenario examples/nucleo-f446ze-blink-serial/scenarios/blink.json
```

`firmware/build.sh` rebuilds the image with clang and lld; `embsim build`
does the same from the project.
