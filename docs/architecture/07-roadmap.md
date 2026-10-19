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
CI evidence for all of it.

The owner's direction of 2026-10-17 (01 §9) puts the flagship (M6) and the
world and breadboard (M7) before the release (M8), so that 1.0 is the
complete experience on one board family rather than a broad first cut;
breadth (M9) follows the release. Board completeness on the boards already
supported continues alongside, peripheral by peripheral.

The owner's discussion of 2026-10-18 (01 §10, P11) adds a real bench to
these milestones: USB as it is, real time with the host's tools,
instruments used as the real ones, imperfections and damage, and AI
access (ADR-030 to ADR-035). Every item is built by ADR-030's five
questions.

The owner's decision of 2026-10-19 orders the next work: the two boards
EmbLinkRTOS is developed against are completed first, one at a time. First
the NUCLEO-F446RE (STM32F446RE, Cortex-M4F; EmbLinkRTOS's ADR-025), then the
ESP32-C3-DevKitM-1 (M6's ESP32-class board, Q11). Each board is done when
its MCU, its on-board devices, its vendor's examples and EmbLinkRTOS's
conformance suite all pass ([the plan](../engineering/rtos-boards.md)).
M6's remaining parts and M7 follow.

## 2. Milestones

| Milestone | Deliverable | Exit criteria |
|---|---|---|
| **M0 Foundation** | Architecture, specifications, requirements, repository and CI; `libembsim` with scheduler, nets, pin models, digital engine, analog engine v1 (R, C, L, sources, switch, diode, LED, BJT, MOSFET), bridges, model registry, JSON loader and validator, session, protocol over stdio and WebSocket, CLI (`run`, `validate`, `serve`, `catalog`); workbench shell with design system, schematic canvas, library, inspector, run controls, board view, oscilloscope and logic analyzer on probes; Tauri shell; first catalog (passives, switches, LEDs, transistors, gates, sources) with fixtures; Blue Pill board description without firmware | C tests and golden analog fixtures pass on Linux, macOS, Windows with GCC, Clang, TinyCC, MSVC; the button-transistor-LED circuit runs headless and in the workbench with the same observations; protocol conformance tests pass; UI typecheck, lint and Playwright smoke pass |
| **M1 Firmware** | Cortex-M backend extracted from EmbCC behind SPEC-009; STM32F1 register file, GPIO with pin models, RCC, USART, TIM, EXTI, NVIC; ELF/HEX/DWARF loaders; GDB stub; `embcc`, `gcc-arm`, `clang` toolchain profiles; debug perspective | the acceptance scenario of 01 §8 passes on three platforms from the CLI and the workbench; EmbCC's Cortex-M golden fixtures pass as differential tests; a breakpoint-and-continue run matches an uninterrupted run |
| **M2 Workbench 1.0** | Schematic editing complete (wires, buses, labels, undo, copy, hierarchy), board artwork with hotspots, serial terminal, protocol decoders (UART, SPI, I²C), meters, command palette, docking, themes, keyboard and assistive checks, desktop installers | SPEC-011 checklist passes on three platforms; 500-component schematic at 60 Hz; installers produced by CI |
| **M3 Catalog and SDK** | Lua model tier; component SDK and conformance harness; 100 components with fidelity and fixtures; importers (RTOS board YAML, DeviceTree, KiCad netlist); library installation; `embsim catalog` | an independent contributor adds a component and a board with the SDK alone; every catalog entry passes validation and its fixtures in CI |
| **M4 Core families and boards** | RISC-V, AVR, PIC32 and PIC24 backends extracted; nRF52840 DK, Pico (two cores), Arduino Uno, a RISC-V board, Explorer 16/32; `gcc-riscv`, `gcc-avr`, `xc16`, `xc32` profiles; multi-board projects | EmbCC's golden fixtures per family pass; each board runs its vendor example; a two-board UART project exchanges bytes deterministically |
| **M5 Verification** | Scenario editor, assertions, recordings, replay divergence reports, coverage and profiling from the backends, CI runner documentation, Python client | a recorded scenario replays identically; EmbLinkRTOS's CI uses EmbSIM as an emulated target |
| **M6 Flagship** | The Arduino Uno complete (every peripheral its core and usual libraries touch: SPI, TWI, the watchdog, the analog comparator, input capture) and an ESP32-class board (Q11); the thirty most used parts as verified models (DHT22, DS18B20, BME280, MPU6050, HC-SR04, SSD1306, HD44780 with I2C backpack, WS2812, servo, stepper and driver, 4x4 keypad, DS3231, MFRC522, NEO-6M GPS among them); a world panel (ADR-026) setting ambient values and distances; a coverage table per board; paced real time (ADR-032); USB power with the port's and the board's limits and the USB-serial bridge as a host serial port (ADR-031); contact bounce and tolerances (ADR-034) | each part's library example sketches run unmodified and pass scenarios; each part's fixtures check its datasheet's timing and values; the Uno's coverage table has no peripheral its core touches left unmodelled; a host terminal opens the Uno's port and talks to a running sketch; a circuit fed from the Uno's 5V pin sags and trips its polyfuse at the datasheet's current |
| **M7 World and breadboard** | The world as engine state with moving objects and ray queries (ADR-026); the 3D breadboard view (ADR-027): breadboards, jumper wires and part models placed on holes, every wire an edit of the project's nets, the world's objects and controls in the scene; frame budgets enforced (ADR-028); bench instruments used as the real ones (ADR-033); the fault mode and damage beyond absolute maximum ratings (ADR-034); a robot on the floor (motors, wheels, encoders, line and distance sensors) | a circuit built on the breadboard is the same project the schematic edits (round trip both ways, undo across views); an HC-SR04 measures an object moved in the scene; a DHT22 follows the room's temperature; a meter's current range across a supply blows its fuse; 120 Hz on the reference machine with a 60 Hz floor |
| **M8 Release 1.0** | Programming the real way (ADR-031: Optiboot with avrdude on the Uno, UF2 on the Pico, the debug probes); the host terminal and IDE integration (ADR-032); the explain mode, guided lessons and teachers' assignments; the MCP server (ADR-035); MicroPython on the Pico; power and battery life from the MCUs' datasheet currents; performance work, platform validation, documentation site, packaging, signing, support policy | 01 §8 in full; the Arduino IDE's Upload button flashes a virtual Uno; an assistant builds and verifies a circuit through the MCP server; STATUS.md lists evidence for every claim |
| **M9 Breadth** | Every supported board completed against its coverage table, the Explorer 16/32 first (its potentiometer, 25LC256, LCD on the PMP, TC1047A, S5; the PIC32MX's and PIC24's ADC, SPI, I2C, capture, compare and PMP, refereed by Microchip's examples, Q15); more boards (Arduino Nano, Mega and Leonardo, STM32 Nucleo with its ST-Link, ESP32 and ESP32-S3, Pico W, BBC micro:bit, STM32F4 and G0, SAMD21, RP2350); WiFi and Bluetooth to the real internet; CAN, RS-485 and Modbus, 1-Wire, LIN, Ethernet, IR, GPS, LoRa and nRF24 in the world; CircuitPython, Rust (Embassy) and TinyGo; RTOS awareness; comparison against real-hardware captures; firmware CI as a GitHub Action; community libraries of parts through the SDK; importers for further part sources | per board: vendor examples and the coverage table; per library: validation and fixtures in its CI; per capture: the simulation within its declared tolerance |
| **Later** | Native USB devices on the host through USB/IP (ADR-031); mixing real and virtual hardware; export to KiCad for PCB work; FPGAs through Yosys and Verilator; Linux boards through QEMU; shared projects and a classroom mode; translations | each with its own ADR when it is taken up |

M0 is delivered. M1 is implemented on the Blue Pill (STM32F103C8) with the
`embcc`, `clang` and `gcc-arm` profiles exercised (an unmodified GCC ELF
passes the acceptance scenario); its evidence and the part of its exit
criteria still open (the three-platform run of the acceptance scenario
from the workbench) are in STATUS.md. M2
is implemented (the inside of a board or composite opens read-only); its
installers and three-platform smoke run in CI (Chromium, not the platform
webviews). M3 is implemented: 101 components each validated by the engine
with its fixtures, the Lua tier, the SDK commands and the three importers;
libraries install from a directory, a zip archive or a URL, and Lua
models read the analog side (`ctx.analog`).
M4 is under way: the AVR (the Arduino Uno) and RISC-V (the HiFive1 Rev B)
backends run their boards' firmware, built by clang and EmbCC, and a
two-board project exchanges bytes deterministically; the Raspberry Pi Pico
(the RP2040's core 0) and the nRF52840 DK run theirs, and so does the
Explorer 16/32 with its PIC32MX795F512L (the MIPS32 M4K) and with its
PIC24FJ1024GB610 (an XC16 image); the Pico runs both of its cores. M5 is
implemented: a recorded scenario replays identically (every example, and
recordings made in a running session), and EmbLinkRTOS's Arduino Uno
conformance suite runs on EmbSIM as an emulated target, in its CI
(EmbLinkOs/EmbLinkRTOS#12's `avr-embsim` job); the coverage, profile, Python client, CI
documentation and the Verify perspective are in STATUS.md. M6 to M9 are
planned, not delivered; M6's Uno SPI, TWI, watchdog and analog comparator are in (the ATmega328P's coverage table is complete), and so is Timer/Counter1's input capture. M7's breadboard was begun inside M6 at the owner's request (2026-10-18): breadboards, `layout.bench` and the 3D bench editing the same nets as the schematic (ADR-027's status); the world, glTF models and the rest of M7 remain. Do not bulk-copy all EmbCC boards and call them supported.

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
| Breadth before depth: many shallow models nobody trusts | the flagship first (01 §9); a model is in the catalog only with fixtures; coverage tables per board |
| 3D rendering cost on modest machines | ADR-028's budgets and a 2D fallback; level of detail; the simulation never waits on a frame |
| Licences of reused models and code (part models, 3D meshes, GPL simulators) | ADR-029: permissive code only, attribution and share-alike recorded per asset, GPL projects as referees only |
| Real time on large circuits (ADR-032) | the achieved ratio reported, never hidden; benchmarks of the largest circuit paced at 1x |
| Host serial ports and USB/IP differ by platform (a driver on Windows) | Q16; pseudo-terminals first on Linux and macOS; the session's own console stays for CI |
| Bootloaders and vendor compilers under restrictive licences (Optiboot, XC16, XC32) | fetched at test time like the vendor SDKs, never vendored (ADR-029); Q15 |
| Realism by default makes beginners' circuits fail (ADR-034) | diagnostics that name the real-world cause and the fix; ideal parts available for a lesson, labelled |
