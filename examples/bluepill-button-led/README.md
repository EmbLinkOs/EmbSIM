# Blue Pill, button, LED and a terminal (firmware)

The M1 acceptance circuit: the bundled Blue Pill board running
[`firmware/main.c`](firmware/main.c) on its STM32F103C8 runtime.

- USB 5 V feeds the board's regulator, which holds 3.3 V for the MCU.
- A push button from A0 to ground. The firmware sets PA0 as an input with
  its internal pull-up and listens on EXTI line 0, both edges.
- An external red LED with a 1 kΩ resistor from C13 to ground. The EXTI
  interrupt drives PC13 high while the button is held, so the LED lights
  (and the board's own LED, lit by a low PC13, goes out).
- A virtual terminal on A9 (the MCU's USART1 TX) and A10 (RX). The firmware
  prints a banner and "pressed"/"released" at 115200 baud; the terminal
  decodes the TX pin's waveform. What the terminal sends is decoded by the
  MCU from the RX pin and echoed back in upper case.

The scenario presses the button from 3 ms to 5 ms and types `ok` at 6 ms,
then checks the 3.3 V rail, the button net, PC13, the LED current (dark
below 0.1 mA before and after, at least 0.5 mA while pressed), the console
(`serial:B1`) and the terminal's text.

```sh
embsim run examples/bluepill-button-led/project.json --scenario examples/bluepill-button-led/scenarios/press.json
```

## The firmware

`firmware/` holds the sources and the built `button-led.elf`, committed so
the example runs without a cross compiler. Rebuild it with Clang and LLD
(`sh firmware/build.sh`) or the GNU Arm toolchain (`sh firmware/build.sh
gcc`). The register header `stm32f103.h` is written from the reference
manual (RM0008); no vendor header or HAL is needed.
