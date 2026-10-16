# Gallery

Screenshots of the workbench at each milestone, to show how EmbSIM
evolves. Newer milestones come first. Every set from M1 on is captured by
`ui/apps/workbench/scripts/screenshots.mjs` (the same views each time:
`pnpm build`, then `node scripts/screenshots.mjs --out ../../../docs/gallery/<milestone>`).

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
