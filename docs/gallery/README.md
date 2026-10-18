# Gallery

Screenshots of the workbench at each milestone, to show how EmbSIM
evolves. Newer milestones come first. The milestone sets from M1 on are captured by
`ui/apps/workbench/scripts/screenshots.mjs` (the same views each time:
`pnpm build`, then `node scripts/screenshots.mjs --out ../../../docs/gallery/<milestone>`).

## M6 parts on the bench (2026-10-18)

Parts run by their libraries' own examples, unmodified, on the Uno. The
TM1637 module shows TM1637Test's first step (every segment and the colon);
the 28BYJ-48 has turned its shaft through the Stepper library's 200 steps
while the ULN2003 board's LEDs A and B show the two coils the sketch left
on.

| | |
|---|---|
| ![The TM1637 display at 88:88](m6-parts/board-tm1637.png) | ![The 28BYJ-48 through its ULN2003 board: LEDs A and B lit, the shaft turned](m6-parts/board-stepper.png) |

[TM1637Test in the Design perspective](m6-parts/design-tm1637.png), [stepper_oneRevolution in the Run perspective](m6-parts/run-stepper.png).

## Workbench layout pass (2026-10-17)

Panels fold to a titled rail and unfold from it, the choice kept with the
layout. The Run perspective folds the instrument with nothing to show (the
oscilloscope of a project with only digital probes) and gives its actions to
the other. Meters read in SI prefixes down to femto (residue below that
shows as about zero) and leave growing texts to the Serial tab. A compact
window starts with the Inspector folded, and the sheet navigator steps aside
on a narrow canvas. The Debug perspective demangles C++ names.

| | |
|---|---|
| ![Run: oscilloscope folded, logic analyzer and SI meters](workbench-layout/wide-run.png) | ![Design at 1024 pixels: the Inspector folded to a rail](workbench-layout/compact-design.png) |

[Debug with demangled C++ names](workbench-layout/wide-debug.png).

The EmbSIM mark is now the project menu, with the project's name beside it:
New project (a blank sheet, or a board from the catalog to start from), Open,
Save and Save a copy, the same commands as the palette.

| | |
|---|---|
| ![New project: start from a catalog board](workbench-layout/new-project.png) | ![A new project on the Raspberry Pi Pico](workbench-layout/new-project-created.png) |

[The project menu](workbench-layout/project-menu.png).

## Arduino SPI and Wire on the Uno (2026-10-16)

A sketch built with the unmodified ArduinoCore-avr (`tests/vendor/arduino/sketches/spi_wire`)
drives the ATmega328P's new SPI and TWI models through the core's own SPI and
Wire libraries. The 25LC040's RDSR after WREN reads back status 0x02, clocked at
4 MHz through D13, whose L LED puts SCK on an analog net (the edge that once
answered a bit late). Wire's first START addresses the 24C02 at 99.9 kHz, and
the Serial terminal now joins the text the engine streams into whole lines,
one port at a time.

| | |
|---|---|
| ![Uno SPI decode: RDSR 0x05, then status 0x02](arduino-spi-wire/decode-uno-spi.png) | ![Uno TWI decode: START, 0x50 W ACK at 100 kHz](arduino-spi-wire/decode-uno-i2c.png) |

[The sketch's Wire results in the Serial terminal](arduino-spi-wire/serial-uno-spi-wire.png).

## Serial and analog peripherals on the Pico and the nRF52840 DK (2026-10-16)

Vendor firmware, unmodified drivers, against parts from the catalog: the Pico
SDK's `hardware_spi` and `hardware_i2c` on the RP2040's PL022 and DW_apb_i2c,
and nrfx's SPIM, TWIM, PWM and SAADC drivers on the nRF52840, each talking to
a 25LC040 SPI EEPROM and a 24C02 I2C EEPROM (`tests/vendor/pico-sdk/apps/spi_i2c`,
`tests/vendor/nrfx/apps/nrfx_periph`). The logic analyzer decodes the traffic
the cores drive on the pins: the SPI WREN and RDSR commands (the nRF52840's
MISO shown undriven, orange, while the EEPROM's SO is high-impedance), and an
I2C START with the 24C02's address acknowledged.

| | |
|---|---|
| ![Pico SPI decode: WREN and RDSR to a 25LC040](serial-analog-peripherals/decode-pico-spi.png) | ![Pico I2C decode: START, 0x50 W ACK](serial-analog-peripherals/decode-pico-i2c.png) |
| ![nRF52840 DK SPIM decode with MISO high-impedance](serial-analog-peripherals/decode-nrf52840dk-spi.png) | ![nRF52840 DK TWIM decode: START, 0x50 W ACK](serial-analog-peripherals/decode-nrf52840dk-i2c.png) |

The Serial terminal shows what the firmware prints: these shots are the first
since the engine streams `serial.data`, which it had never sent, leaving the
terminal empty.

| | |
|---|---|
| ![Pico SDK SPI and I2C results in the Serial terminal](serial-analog-peripherals/serial-pico-spi-i2c.png) | ![nrfx TWIM, PWM and SAADC results in the Serial terminal](serial-analog-peripherals/serial-nrf52840dk-periph.png) |
## Schematic graphics and interaction (2026-10-16)

Captured from the native engine with `tests/browser/test_schematic.py`: separate
wire/symbol colors, hollow pin sockets and nearby reference/name/effective-value
annotations. No model values or electrical connections are changed for styling.
The browser regression also checks direct wiring, exact undo and group operations.

| | |
|---|---|
| ![Colored wires and component labels on paper](schematic-graphics/light.png) | ![Theme-aware wires and labels in dark mode](schematic-graphics/dark.png) |

[Terminal and annotation detail](schematic-graphics/terminal-detail.png) ·
[Compact workspace](schematic-graphics/compact.png).

## Circuit organization (2026-10-16)

Captured against the native engine by `tests/browser/test_schematic.py`. The
Objects panel selects an input-stage group; both member resistors have locked
placement while their parameters remain editable. The regression verifies native
metadata, blocked dragging and undo before capturing light/dark/compact views.

| | |
|---|---|
| ![Named group and object browser in light theme](circuit-organization/light.png) | ![Named group and object browser in dark theme](circuit-organization/dark.png) |

[Compact 1024-pixel workspace](circuit-organization/compact.png).

## Schematic authoring and repair (2026-10-16)

Captured from the native engine by `tests/browser/test_schematic.py` after track
repair, shared-parameter editing and exact undo checks. The selected net remains
electrically connected while its drawing is cleared; the Checks report locates
the missing geometry. Bounds and connectivity checks are not electrical-rule or
physical-clearance validation.

| | |
|---|---|
| ![Native schematic checks in light theme](schematic-authoring/checks-light.png) | ![Native schematic checks in dark theme](schematic-authoring/checks-dark.png) |

[Compact 1024-pixel workspace](schematic-authoring/compact.png).

## Canvas appearance (2026-10-16)

Captured against the native engine with `tests/browser/test_canvas_appearance.py`.
The continuous background covers negative coordinates as well as the original
sheet area. Appearance preferences survive three consecutive native-server reloads.

![Continuous paper workspace in focus mode](canvas-appearance/paper-workspace.png)

| | |
|---|---|
| ![Paper engineering grid and controls](canvas-appearance/engineering-paper.png) | ![Blueprint surface in dark theme](canvas-appearance/blueprint-dark.png) |
| ![Warm surface with dots](canvas-appearance/warm-dots.png) | ![Graphite surface with crosses](canvas-appearance/graphite-crosses.png) |

## Schematic Studio (2026-10-16)

Captured against the native engine with `tests/browser/test_schematic.py`.
The [user guide](../user/schematic-studio.md) explains the controls and routing limits.

| | |
|---|---|
| ![Connectivity-driven layout preview](schematic-studio/preview-light.png) | **Preview before applying.** Connected groups placed and routed, with moved/route counts, navigation and one-step Apply/Undo. |
| ![Schematic Studio in dark theme](schematic-studio/dark.png) | **Net spotlight.** Find and focus a net while retaining circuit context; the same tools in the dark theme. |

## M5 stacks and faults (2026-10-16)

The firmware's stacks and faults measured from its core: how deep each
stack went, by function, and each fault decoded as the core took it.

| | |
|---|---|
| ![The Blue Pill's stack depths](m5-2/verify-stacks.png) | **How deep the stacks went.** After a run with the button pressed: the main stack's 40 of 20,472 bytes (down to `_ebss`), and by function the deepest stack pointer, its own frame and its depth with its callees; the interrupt handler is deepest, on the frame its entry stacked. |
| ![A HardFault decoded](m5-2/verify-faults.png) | **A fault, decoded.** `tests/firmware/fault` loads from 0x30000000, where nothing answers: the HardFault it escalates to with HFSR and CFSR in words, BFAR, the faulting `ldr`, the stacked frame, and the backtrace `peek`, `read_sensor`, `main`, `Reset_Handler` from `.debug_frame`; its handler is `Default_Handler`. |

## M5 verification (2026-10-16)

A run becomes a repeatable, measured check: scenarios edited and run in
place, recordings replayed identically or turned into scenarios, and the
firmware's coverage and profile from its core.

| | |
|---|---|
| ![The scenario editor after a run](m5/verify-scenario.png) | **Edit and run a scenario.** `scenarios/press.json` of the Blue Pill example opened in the editor: its inputs timeline and its sixteen assertions, each row with its result after the run, the report below. |
| ![Coverage of the Blue Pill's firmware](m5/verify-coverage.png) | **Coverage and profile.** After a run with the button pressed: each source file's lines and functions hit, `main.c` with each line's count, and where the cycles went by function. |
| ![Coverage, dark](m5/verify-coverage-dark.png) | **The same, dark.** The source view starts at the first line with code; lines that never ran are marked. |
| ![A recording replayed and made a scenario](m5/verify-recording.png) | **Record, replay, convert.** A session recorded while the button was held, replayed identically (291 observations), and turned into a scenario now open in the editor with its inputs and assertions. |

## Interactive component library (2026-10-16)

Captured by `tests/browser/test_library.py` against the native engine, including
real placement and live control/waveform checks. Original component artwork and
schematic symbols are rendered by the shared workbench.

| | |
|---|---|
| ![Library explorer, light](component-library/library-light.png) | **Discover and inspect.** Manufacturer/package filters, multiword search, source reference status and a preview before placement, alongside the wired optocoupler circuit. |
| ![Library explorer, dark](component-library/library-dark.png) | **Place a real package.** B3F-1000's numbered pads and model limits in dark mode. The explorer is also checked at 1024 × 700. |

## M4 core families and boards, part 5 (2026-10-16)

The Pico runs both of its RP2040's cores: core 0 launches core 1 through
its bootrom's FIFO handshake, and the two share a counter, the FIFOs and the
LED.

| | |
|---|---|
| ![The Pico's two cores running](m4-5/run-pico-dual-core.png) | **Two cores running.** `examples/pico-dual-core`: core 1 blinking the LED from its timer alarm and taking the button, core 0 printing on the terminal what core 1 answers through the FIFO. |
| ![Debugging core 1](m4-5/debug-pico-core1.png) | **Debugging core 1.** Stopped in `sio_irq_proc1` on core 1, which the toolbar names: its registers, its source line, the disassembly from flash, while core 0's banner is half printed. |

## M4 core families and boards, part 4 (2026-10-16)

The same Explorer 16/32 with its PIC24 plug-in module: an XC16 image on the
PIC24FJ1024GB610, the 8 MHz crystal through the 96 MHz PLL to a 16 MHz FCY,
UART2 on the USB-UART bridge by the Peripheral Pin Select.

| | |
|---|---|
| ![The Explorer 16/32 with the PIC24 running](m4-4/run-explorer16-pic24.png) | **The PIC24 running.** `examples/explorer16-pic24`: D3 blinking from Timer1's interrupt, the banner with FOSC, FCY and the FOSCSEL word read back by a table read on the terminal. |
| ![The PIC24 PIM up close](m4-4/board-explorer16-pic24.png) | **The PIC24 PIM.** The Explorer's artwork with the PIC24FJ1024GB610 plug-in module (MA240023), live. |
| ![Debugging the PIC24](m4-4/debug-explorer16-pic24.png) | **Debugging the PIC24.** Stopped at `_T1Interrupt`'s first instruction: XC16's line table on the source, the PIC24 disassembly at program addresses, W0 to W15, RAM at data address 0x0800, and the device pack's peripherals with their models. |
| ![The PIC24 Explorer's schematic](m4-4/design-explorer16-pic24.png) | **Design, Explorer 16/32 with the PIC24.** 5 V on VIN and the terminal on RF5 and RF4. |

## M4 core families and boards, part 3 (2026-10-16)

An Explorer 16/32 runs its PIC32MX795F512L's MIPS32 M4K: the 8 MHz crystal
through the PLL to 80 MHz from the configuration words, with firmware built
by clang and by EmbCC.

| | |
|---|---|
| ![The Explorer 16/32 running](m4-3/run-explorer16.png) | **The Explorer 16/32 running.** `examples/explorer16-pic32mx`: D3 blinking from Timer1's interrupt, S3, S6, S4 and MCLR as controls, the banner with SYSCLK and PBCLK on the terminal through UART2. |
| ![The Explorer up close](m4-3/board-explorer16.png) | **The Explorer's artwork.** The PIC32MX795F512L PIM, D3 to D10, the buttons, the crystals, the USB-UART bridge and the PICtail edge, live. |
| ![Debugging the PIC32MX](m4-3/debug-explorer16.png) | **Debugging the PIC32MX.** Stopped in `isr` at Timer1's first interrupt: the source, the MIPS32 disassembly from program flash at 0x9D000000, the MIPS registers, the RAM, and the device pack's peripherals with their models. |
| ![The Explorer's schematic](m4-3/design-explorer16.png) | **Design, Explorer 16/32.** 5 V on VIN and the terminal on the USB-UART bridge's RF5 and RF4. |

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
