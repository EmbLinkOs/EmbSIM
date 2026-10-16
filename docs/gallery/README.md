# Gallery

Screenshots of the workbench at each milestone, to show how EmbSIM
evolves. Newer milestones come first. Every set from M1 on is captured by
`ui/apps/workbench/scripts/screenshots.mjs` (the same views each time:
`pnpm build`, then `node scripts/screenshots.mjs --out ../../../docs/gallery/<milestone>`).

## M2 workbench (2026-10-16)

The workbench edits the circuit against the engine, draws boards, decodes
serial lines and runs as a desktop application.

| | |
|---|---|
| ![Desktop application](m2/desktop-linux.png) | **Desktop.** The EmbSIM application on Linux (WebKitGTK): the Tauri shell starts the engine as its sidecar with the Blue Pill example. |
| ![Design, Blue Pill](m2/design-bluepill.png) | **Design.** The Blue Pill drawn as a block of its headers, the circuit wired to the pins it uses, power by net labels. |
| ![Decoded serial](m2/decode-bluepill.png) | **Decoding.** The firmware's boot banner on USART1, decoded frame by frame under the TX trace; the board's artwork with its power and PC13 LEDs lit. |
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
