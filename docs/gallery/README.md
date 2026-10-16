# Gallery

Screenshots of the workbench at each milestone, to show how EmbSIM
evolves. Newer milestones come first. Every set from M1 on is captured by
`ui/apps/workbench/scripts/screenshots.mjs` (the same views each time:
`pnpm build`, then `node scripts/screenshots.mjs --out ../../../docs/gallery/<milestone>`).

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
