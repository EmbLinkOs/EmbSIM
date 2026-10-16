# Gallery

Screenshots of the workbench at each milestone, to show how EmbSIM
evolves. Newer milestones come first. Every set from M1 on is captured by
`ui/apps/workbench/scripts/screenshots.mjs` (the same views each time:
`pnpm build`, then `node scripts/screenshots.mjs --out ../../../docs/gallery/<milestone>`).

## M4 core families and boards, part 2 (2026-10-16)

A Raspberry Pi Pico boots its RP2040 through boot2 and an nRF52840 DK runs
its Cortex-M4F, each with firmware built by clang and by EmbCC.

| | |
|---|---|
| ![The Pico running](m4-2/run-pico.png) | **The Pico running.** `examples/pico-blink-serial` on the RP2040's Cortex-M0+ at 125 MHz from PLL_SYS: the LED on GPIO25 lit by the timer's alarm, the banner on the terminal. |
| ![The Pico up close](m4-2/board-pico.png) | **The Pico's artwork.** Its 40-pin edge, the RP2040, the W25Q16 flash, BOOTSEL and the user LED, live. |
| ![Debugging the RP2040](m4-2/debug-pico.png) | **Debugging the RP2040.** Stopped at the timer's alarm interrupt: the source, the Armv6-M disassembly, the registers, and RP2040.svd's peripherals with the ones RESETS holds marked "in reset". |
| ![The Pico's schematic](m4-2/design-pico.png) | **Design, Pico.** The button and the terminal on GP15, GP0 and GP1, VBUS from a 5 V source. |
| ![The nRF52840 DK running](m4-2/run-nrf52840dk.png) | **The nRF52840 DK running.** `examples/nrf52840dk-blink`: LED1 toggled by RTC1 on the 32.768 kHz crystal, the banner with FICR's part number and the FPU's sqrt(2). |
| ![The DK up close](m4-2/board-nrf52840dk.png) | **The DK's artwork.** Its four LEDs and buttons, the Arduino headers and the virtual COM port's pins. |
| ![Debugging the nRF52840](m4-2/debug-nrf52840dk.png) | **Debugging the nRF52840.** Stopped in RTC1's interrupt: the source, the Thumb-2 disassembly, the registers and nrf52840.svd's peripherals. |
| ![The DK's schematic](m4-2/design-nrf52840dk.png) | **Design, DK.** The button on D2 and the terminal on P0.06 and P0.08. |
| ![Two boards on one line](m4-2/run-two-boards.png) | **Two boards.** As in part 1. |
| ![The Uno running](m4-2/run-uno.png) | **The Uno running.** As in part 1. |
| ![The HiFive1 running](m4-2/run-hifive1.png) | **The HiFive1 running.** As in part 1. |
| ![Display showcase](m4-2/board-showcase.png) | **Displays.** |
| ![Design, Blue Pill](m4-2/design-bluepill.png) | **Design, Blue Pill.** |
| ![Debug, Blue Pill](m4-2/debug-bluepill.png) | **Debug, Blue Pill.** |
| ![Dark theme](m4-2/design-bluepill-dark.png) | **Dark theme.** |

## M4 core families and boards, part 1 (2026-10-16)

An Arduino Uno's AVR and a HiFive1's RISC-V core run their firmware, and
boards of different families share one circuit.

| | |
|---|---|
| ![Two boards on one line](m4/run-two-boards.png) | **Two boards.** `examples/two-boards-uart`: an Uno (AVR, 5 V) and a Blue Pill (Cortex-M3, 3.3 V) exchanging `ping` and `PING` over one serial line, each at its own baud rate; the Uno's L LED lit after the third round trip. |
| ![Two boards on the schematic](m4/design-two-boards.png) | **Design, two boards.** Each board a block of its ports, the link and the power by net labels. |
| ![The Uno running](m4/run-uno.png) | **The Uno running.** `examples/uno-blink-serial`: Timer1's PWM on D9 on the logic analyzer, the potentiometer's 2.5 V on A0, the board's artwork live. |
| ![The Uno up close](m4/board-uno.png) | **The Uno's artwork.** Its L and ON LEDs lit, the blue LED on D9 dimmed by the PWM. |
| ![The Uno's schematic](m4/design-uno.png) | **Design, Uno.** The potentiometer, button, LED and terminal wired to the Uno's pins by labels. |
| ![Debugging the AVR](m4/debug-uno.png) | **Debugging the AVR.** Stopped at the Timer0 interrupt (`__vector_14`): the source line, the AVR disassembly, r0-r31 and the ATmega328P's peripherals. |
| ![The HiFive1 running](m4/run-hifive1.png) | **The HiFive1 running.** `examples/hifive1-blink` on the FE310's RISC-V core. |
| ![The HiFive1 up close](m4/board-hifive1.png) | **The HiFive1's artwork.** The RGB LED's green, toggled by the CLINT's timer interrupt. |
| ![Debugging the RISC-V core](m4/debug-hifive1.png) | **Debugging RISC-V.** Stopped in the machine trap handler: the source, the compressed instructions, the registers by their ABI names. |
| ![Display showcase](m4/board-showcase.png) | **Displays.** As in M3, the LED colours now from their parameters. |
| ![Design, Blue Pill](m4/design-bluepill.png) | **Design, Blue Pill.** |
| ![Decoded serial](m4/decode-bluepill.png) | **Decoding.** |
| ![Debug, Blue Pill](m4/debug-bluepill.png) | **Debug, Blue Pill.** |
| ![Dark theme](m4/design-bluepill-dark.png) | **Dark theme.** |

## M3 catalog and SDK (2026-10-16)

The catalog grows to 101 components, each validated by the engine against
its fixtures; parts can be written in Lua; boards and circuits come from
EmbLinkRTOS board files, DeviceTree and KiCad netlists.

| | |
|---|---|
| ![The display showcase's board view](m3/board-showcase.png) | **Displays and actuators.** `examples/display-showcase` in the board view: a 16x2 LCD and an SSD1306 OLED written over their buses by pattern generators, a MAX7219 module over SPI, a 74HC161 counting into a CD4511 and a 7-segment digit and an RGB LED, a CD4017 chasing a bar graph, a servo at 45 degrees and a 555 sounding a buzzer. |
| ![The showcase running](m3/run-showcase.png) | **The showcase running.** The parts' outputs as meters and traces: the counter's count, the servo's angle, the buzzer's tone near 1.9 kHz, the bar graph's lit segment. |
| ![The showcase's schematic](m3/design-showcase.png) | **Design, showcase.** Twenty-two catalog parts on one sheet, connected by net labels. |
| ![A KiCad netlist imported](m3/design-flasher.png) | **Imported from KiCad.** `examples/kicad-flasher`, drawn by `tools/importers/kicad2project.py` from a KiCad netlist: a 9 V battery, a 7805, a 555 astable with its timing taken from R1, R2 and C1, and the LED it flashes at 6.9 Hz. |
| ![Design, Blue Pill](m3/design-bluepill.png) | **Design, Blue Pill.** The library's 101 components by category. |
| ![Decoded serial](m3/decode-bluepill.png) | **Decoding.** As in M2. |
| ![Inside the Blue Pill](m3/inside-bluepill.png) | **Hierarchy.** As in M2. |
| ![A bus counting](m3/design-counter.png) | **Buses.** As in M2. |
| ![The counter on the logic analyzer](m3/run-counter.png) | **The counter running.** |
| ![Run, Blue Pill](m3/run-bluepill.png) | **Run, Blue Pill.** |
| ![Dark theme](m3/design-bluepill-dark.png) | **Dark theme.** |
| ![Debug](m3/debug-bluepill.png) | **Debug.** |
| ![Design, transistor circuit](m3/design-transistor.png) | **Design, transistor circuit.** |
| ![Run, transistor circuit](m3/run-transistor.png) | **Run, transistor circuit.** |

## M2 workbench (2026-10-16)

The workbench edits the circuit against the engine, draws boards, decodes
serial lines and runs as a desktop application.

| | |
|---|---|
| ![Desktop application](m2/desktop-linux.png) | **Desktop.** The EmbSIM application on Linux (WebKitGTK): the Tauri shell starts the engine as its sidecar with the Blue Pill example. |
| ![Design, Blue Pill](m2/design-bluepill.png) | **Design.** The Blue Pill drawn as a block of its headers, the circuit wired to the pins it uses, power by net labels. |
| ![Decoded serial](m2/decode-bluepill.png) | **Decoding.** The firmware's boot banner on USART1, decoded frame by frame under the TX trace; the board's artwork with its power and PC13 LEDs lit. |
| ![Inside the Blue Pill](m2/inside-bluepill.png) | **Hierarchy.** Double-clicking the Blue Pill opens its inside: the MCU, regulator, LEDs, crystals, buttons and headers, every pin labelled with its net and live while the firmware runs. |
| ![A bus counting](m2/design-counter.png) | **Buses.** A 4-bit ripple counter; its outputs drawn as the bus `Q`, which shows the count while it runs (`Q = 0xA`). |
| ![The counter on the logic analyzer](m2/run-counter.png) | **The counter running.** Each output halves the one before on the logic analyzer. |
| ![Run, Blue Pill](m2/run-bluepill.png) | **Run.** The board view with the board's artwork above the circuit's parts, meters with units. |
| ![Dark theme](m2/design-bluepill-dark.png) | **Dark theme.** |
| ![Debug](m2/debug-bluepill.png) | **Debug.** As in M1. |
| ![Design, transistor circuit](m2/design-transistor.png) | **Design, transistor circuit.** |
| ![Run, transistor circuit](m2/run-transistor.png) | **Run, transistor circuit.** |

## M1 firmware (2026-10-16)

A Blue Pill runs its firmware in the circuit, and the workbench debugs it.

| | |
|---|---|
| ![Debug perspective](m1/debug-bluepill.png) | **Debug.** A breakpoint on `EXTI0_IRQHandler` hit by pressing the button, stepped once: the source line (`main.c:65`), the disassembly, the registers with the changed one highlighted, memory and the peripherals. |
| ![Run perspective, Blue Pill](m1/run-bluepill.png) | **Run, Blue Pill.** The board's controls (reset, boot pins), the button held, the regulated 3.3 V rail on the oscilloscope, meters on the board's LEDs. |
| ![Design perspective, Blue Pill](m1/design-bluepill.png) | **Design, Blue Pill.** The acceptance circuit's parts and nets with their live voltages. The board itself is not yet drawn on the schematic and nets without drawn wires show as dashed connections: M2 work. |
| ![Design perspective, dark](m1/design-bluepill-dark.png) | **Dark theme.** The same view under the system's dark preference. |
| ![Design, transistor circuit](m1/design-transistor.png) | **Design, transistor circuit** (the M0 example, for comparison). |
| ![Run, transistor circuit](m1/run-transistor.png) | **Run, transistor circuit.** |

## M0 foundation

The engine runs circuits; the workbench shows them.

| | |
|---|---|
| ![Design perspective](m0/design-transistor.png) | **Design.** The button, transistor and LED circuit on the schematic, its library, inspector and net voltages. |
| ![Run perspective](m0/run-transistor.png) | **Run.** The 1 kHz pulse switching the transistor on the oscilloscope, the LED current and the transistor in saturation on the meters. |
