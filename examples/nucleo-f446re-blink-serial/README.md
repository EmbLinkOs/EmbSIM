# NUCLEO-F446RE: 180 MHz from the ST-LINK's MCO, LD2, B1 and the virtual COM port (firmware)

The bundled NUCLEO-F446RE board running [`firmware/main.c`](firmware/main.c)
on its STM32F446RE, a Cortex-M4F. The firmware writes the registers
directly (no HAL), in the order STM32CubeF4's `SystemClock_Config` does:

- HSE in bypass: on the NUCLEO (MB1136 C-02 and later) the ST-LINK's
  STM32F103 drives OSC_IN with its 8 MHz MCO, so HSEON without HSEBYP
  never becomes ready, as on the real board.
- Voltage scale 1, the main PLL at 8 MHz / 8 x 360 / 2 = 180 MHz, the
  over-drive switched on (ODEN, then ODSWEN), five flash wait states with
  the caches, APB1 at 45 MHz and APB2 at 90 MHz, then SYSCLK from the PLL.
- SysTick interrupts every millisecond from HCLK; every 500 ms it toggles
  LD2 on PA5, so LD2 blinks at 1 Hz.
- The blue USER button B1 pulls PC13 low through R29 (the board's R30
  holds it high); EXTI13 on its falling edge counts the presses.
- USART2 on PA2/PA3 (AF7) at 115200 baud (BRR 24 + 7/16 from 45 MHz) is
  the ST-LINK's virtual COM port: the session's console, and a serial port
  on the host with `embsim serve --serial B1`. What arrives comes back in
  upper case.
- The banner reads the flash size from the factory's F_SIZE word and the
  reset cause from RCC_CSR.
- Between interrupts the core waits in `wfi`.

5 V comes in on the ST-LINK's USB (U5V) from a voltage source, as a
computer's port brings it: T2 and JP5 take it to +5V, U4 makes 3.3 V and
JP6 carries it to the STM32.

The scenario checks the banner
(`nucleo-f446re: STM32F446RE, flash 512 KB, SYSCLK 180000 kHz, reset por`),
VDD at 3.3 V, LD2 off at 0.4 s and on at 0.6 s and four edges in two
seconds, a press of B1 at 1.2 s printed as `button 1`, and `nucleo` typed
into the virtual COM port at 1.5 s echoed as `NUCLEO`.

```sh
embsim run examples/nucleo-f446re-blink-serial/project.json --scenario examples/nucleo-f446re-blink-serial/scenarios/blink.json
```

The image is committed; to rebuild it with clang and lld (or the GNU Arm
toolchain, `--toolchain gcc-arm`, whose image passes the same scenario):

```sh
cd examples/nucleo-f446re-blink-serial && embsim build project.json [--toolchain gcc-arm]
```
