# 07 - Roadmap

**Status:** ACCEPTED plan; dates are not promised. Every milestone has
executable acceptance checks and documented omissions; STATUS.md records
what passed.

---

## 1. The 1.0 boundary

1.0 is the first supported release: the acceptance scenario of 01 §8 on three
platforms, five boards across Cortex-M, RISC-V, AVR and one Microchip
family, a hundred validated components, the four workbench perspectives, the
CLI, record and replay, the model SDK with an independent contribution, and
CI evidence for all of it. Everything else is after 1.0.

## 2. Milestones

| Milestone | Deliverable | Exit criteria |
|---|---|---|
| **M0 Foundation** | Architecture, specifications, requirements, repository and CI; `libembsim` with scheduler, nets, pin models, digital engine, analog engine v1 (R, C, L, sources, switch, diode, LED, BJT, MOSFET), bridges, model registry, JSON loader and validator, session, protocol over stdio and WebSocket, CLI (`run`, `validate`, `serve`, `catalog`); workbench shell with design system, schematic canvas, library, inspector, run controls, board view, oscilloscope and logic analyzer on probes; Tauri shell; first catalog (passives, switches, LEDs, transistors, gates, sources) with fixtures; Blue Pill board description without firmware | C tests and golden analog fixtures pass on Linux, macOS, Windows with GCC, Clang, TinyCC, MSVC; the button-transistor-LED circuit runs headless and in the workbench with the same observations; protocol conformance tests pass; UI typecheck, lint and Playwright smoke pass |
| **M1 Firmware** | Cortex-M backend extracted from EmbCC behind SPEC-009; STM32F1 register file, GPIO with pin models, RCC, USART, TIM, EXTI, NVIC; ELF/HEX/DWARF loaders; GDB stub; `embcc`, `gcc-arm`, `clang` toolchain profiles; debug perspective | the acceptance scenario of 01 §8 passes on three platforms from the CLI and the workbench; EmbCC's Cortex-M golden fixtures pass as differential tests; a breakpoint-and-continue run matches an uninterrupted run |
| **M2 Workbench 1.0** | Schematic editing complete (wires, buses, labels, undo, copy, hierarchy), board artwork with hotspots, serial terminal, protocol decoders (UART, SPI, I²C), meters, command palette, docking, themes, keyboard and assistive checks, desktop installers | SPEC-011 checklist passes on three platforms; 500-component schematic at 60 Hz; installers produced by CI |
| **M3 Catalog and SDK** | Lua model tier; component SDK and conformance harness; 100 components with fidelity and fixtures; importers (RTOS board YAML, DeviceTree, KiCad netlist); library installation; `embsim catalog` | an independent contributor adds a component and a board with the SDK alone; every catalog entry passes validation and its fixtures in CI |
| **M4 Core families and boards** | RISC-V, AVR, PIC32 and PIC24 backends extracted; nRF52840 DK, Pico (two cores), Arduino Uno, a RISC-V board, Explorer 16/32; `gcc-riscv`, `gcc-avr`, `xc16`, `xc32` profiles; multi-board projects | EmbCC's golden fixtures per family pass; each board runs its vendor example; a two-board UART project exchanges bytes deterministically |
| **M5 Verification** | Scenario editor, assertions, recordings, replay divergence reports, coverage and profiling from the backends, CI runner documentation, Python client | a recorded scenario replays identically; EmbLinkRTOS's CI uses EmbSIM as an emulated target |
| **M6 Release 1.0** | Performance work, platform validation, documentation site, packaging, signing, support policy | 01 §8 in full; STATUS.md lists evidence for every claim |

M0 is delivered. M1 is implemented on the Blue Pill (STM32F103C8) with the
`embcc` and `clang` profiles exercised; its evidence and the parts of its
exit criteria still open (the three-platform run of the acceptance
scenario from the workbench, the `gcc-arm` profile) are in STATUS.md. M2
is implemented (the inside of a board or composite opens read-only); its
installers and three-platform smoke run in CI (Chromium, not the platform
webviews). M3 is implemented: 101 components each validated by the engine
with its fixtures, the Lua tier, the SDK commands and the three importers;
archive and URL installation and Lua's analog access are open (STATUS.md).
M4 is under way: the AVR (the Arduino Uno) and RISC-V (the HiFive1 Rev B)
backends run their boards' firmware, built by clang and EmbCC, and a
two-board project exchanges bytes deterministically; the Raspberry Pi Pico
(the RP2040's core 0) and the nRF52840 DK run theirs, and so does the
Explorer 16/32 with its PIC32MX795F512L (the MIPS32 M4K) and with its
PIC24FJ1024GB610 (an XC16 image); the Pico runs both of its cores. M5 is
implemented: a recorded scenario replays identically (every example, and
recordings made in a running session), and EmbLinkRTOS's Arduino Uno
conformance suite runs on EmbSIM as an emulated target (its CI job joins
once this branch is merged); the coverage, profile, Python client, CI
documentation and the Verify perspective are in STATUS.md. M6 is planned,
not delivered. Do not bulk-copy all EmbCC boards and call them supported.

## 3. Specification work order

SPEC-001 to SPEC-008 are written with this baseline (M0); SPEC-009, 010 and
012 are written and accepted before M1 code; SPEC-013 and 014 before M3;
SPEC-011 before M2's interface work, with its checklist used from M0.

## 4. Risks

| Risk | Mitigation |
|---|---|
| Analog engine robustness (convergence on stiff circuits, PWM into inductive loads) | fixtures from ngspice from day one; gmin and source stepping; diagnostics name nodes; `functional` policy keeps firmware work unaffected |
| Extraction of EmbCC cores introduces regressions | import EmbCC golden fixtures first; differential tests on every change; one family at a time |
| Register-file licences (ST, Nordic, Microchip) | ADR-017; user-supplied packs as the fallback; the catalog records what is vendored |
| Workbench scope creep | SPEC-011 fixes the 1.0 perspectives; the checklist is the gate |
| Cross-platform webview differences | Playwright on three platforms in CI; a platform adapter in the UI |
| Determinism violated by a model that reads the host | host layer is the only OS code; a CI grep forbids `time()` and friends outside it; replay tests |
| Performance of event batching across islands and cores | benchmarks in CI with thresholds; quantum tuning documented |
| Contributor onboarding on a large design | docs reading order; SDK with templates; `embsim validate` explains errors |
