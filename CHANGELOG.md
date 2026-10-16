# Changelog

All notable changes are recorded here. The format follows Keep a Changelog;
versions follow semantic versioning. There is no release yet.

## Unreleased

### Added
- Component explorer with grouped categories, manufacturer/package/model filters,
  multiword search and pre-placement Overview/Pins/Models sheets.
- 18 named interactive parts plus reusable relay contacts: NTCs, GL5528, BPW34/S,
  four VO617A bins, Bourns trimmers, Omron switches/relays and Phoenix plugs;
  original symbols/artwork, six electrical primitives, 52 component fixtures and
  four GUI/CLI circuit examples. Numeric artwork text supports decimal precision.
- Manufacturer source review for 25 named parts: exact PDF revisions, hashes,
  dates and pin-map references; TMP36 TO-92 map confirmed directly. Sensor
  supply endpoint and shifted-ground fixtures; full package pin-map regressions.
- First named-part catalog batch: 26 manufacturer/package entries for discrete
  semiconductors, regulators, LM358P, LM393P, NE555P, LM35DZ, TMP36GT9Z and
  six complete 74HC gate packages. Original symbols/artwork, searchable part
  identity and inspector details, documented approximation/source-review limits.
- Open-collector comparator, temperature-sensor and 555 behavioral primitives;
  op-amp DC output headroom; packaged HC rail sensing and per-channel timing.
- Executable catalog fixture runner in CTest and a named-part temperature alarm.

- Architecture baseline v1.0: vision, system architecture, simulation engine,
  MCU runtime and toolchains, workbench and protocol, ADR-001 to ADR-020,
  roadmap, open questions, reuse assessment.
- Specifications SPEC-001 to SPEC-014 and the requirements catalog.
- Repository: CMake build and presets, CI matrix, documentation and policy
  workflows, templates, coding standard, status page.
- M0 core: scheduler and rational clocks, nets and pin models, digital engine,
  analog engine v1, model registry and primitives, JSON loader and validator,
  session and in-process protocol, stdio and WebSocket endpoints, `embsim`
  command line, C tests and fixtures.
- M0 catalog: schemas, first components, the Blue Pill board description.
- M0 workbench: design system, shell, schematic canvas, library, inspector,
  run controls, instruments; desktop shell scaffold.

- M1 firmware: the Cortex-M3 core, bus, loaders and semihosting extracted
  from EmbCC; register files from EmbSIM JSON descriptions (ADR-020) and the
  STM32F103C8's written from RM0008; STM32F1 RCC, GPIO, AFIO, EXTI, USART,
  timer, flash and power models; the MCU runtime in quanta on the scheduler
  (ADR-019) with pins, serial lines on pins and streams, reset and boot pins.
- Debugging: breakpoints and watchpoints that stop the session, `debug.*`
  methods, a Thumb disassembler, ELF symbols, DWARF line tables, the GDB
  remote protocol server (`embsim gdb`), the workbench's Debug perspective.
- Toolchains: detection and builds from the profiles (`embsim toolchains`,
  `embsim build`, `toolchain.detect`, `toolchain.build`).
- `tools/importers/svd2regs.py`: SVD files to EmbSIM register descriptions.
- `examples/bluepill-button-led` runs its firmware: button by EXTI, LED
  through an NPN transistor, a terminal on USART1.

- M2 workbench: schematic editing reaches the engine through project
  transactions and `project.save` (place, wire, move, rotate, delete,
  copy, paste, duplicate, net labels, undo, redo, save); boards drawn on
  the schematic as blocks of their ports and in the board view with their
  artwork; UART, SPI and I2C decoders on the logic analyzer; meters with
  units; a 500-part schematic near 60 Hz with its generator and frame-time
  script; the Blue Pill example drawn with wires and labels; buses drawn
  with their live value (`layout.buses`) and `examples/counter-bus`; the
  inside of a board or composite part opened on a read-only, live sheet.
- Desktop: the Tauri shell builds and runs (Linux, WebKitGTK), opens a
  project given on its command line, waits for the engine before
  connecting, forwards the workbench's errors, has its icon; CI builds
  deb, AppImage, dmg and msi installers and runs the Playwright smoke on
  Linux, macOS and Windows.
- `embsim serve --allow-origin`; a sanitizer CI job (GCC address and
  undefined-behaviour sanitizers).

- M3 catalog and SDK: the Lua model tier (SPEC-013 §2) with Lua 5.4.7
  vendored, per-instance states, an instruction budget and memory cap, and
  I2C and SPI slaves decoded in C; `embsim catalog validate` in the engine
  (each item under each declared policy, its fixtures run), `catalog new`
  templates for components, Lua models and boards, `catalog install` into
  user libraries (`$EMBSIM_LIBRARIES`).
- The catalog grows to 101 components with 150 fixtures: 74HC and CD4000
  logic, counters, decoders, latches, the 555, regulators, displays
  (HD44780 LCD, SSD1306 OLED, MAX7219), EEPROMs, a real-time clock,
  sensors, a servo, a stepper driver and a buzzer.
- Importers: EmbLinkRTOS board YAML (`rtos2board.py`), DeviceTree sources
  (`dts2board.py`) and KiCad netlists (`kicad2project.py`);
  `examples/display-showcase` and `examples/kicad-flasher`.
- Board view artwork states with value maps, formats and text; scenario
  text expectations on value probes.

- M4 core families and boards, part 1: the AVR core extracted from EmbCC
  (cycle-exact) with the ATmega328P's ports, external and pin-change
  interrupts, three timers with PWM pins, USART0, ADC, EEPROM and clock
  prescaler, and the Arduino Uno R3; the RISC-V core (RV32IMAC) extracted
  from EmbCC with the FE310-G002's CLINT, PLIC, GPIO, UARTs and clocks, and
  the HiFive1 Rev B. Disassemblers for both; their DWARF, registers and
  peripherals in the Debug perspective.
- Projects with several boards: `examples/two-boards-uart`, an Uno and a
  Blue Pill on one serial line, and the `multiboard` determinism test;
  `examples/uno-blink-serial` and `examples/hifive1-blink`, built by clang
  and EmbCC.
- The register file takes 8-bit registers and 24-bit offsets; the runtime
  takes pins named GPIOn and gives an ADC its pin's voltage; the regulator
  model converges with a supply on its output; the board view scopes each
  artwork's styles and gradient ids and colours an LED by its parameter.
- M4 part 2: the RP2040 (Cortex-M0+) with its register file converted from
  Raspberry Pi's RP2040.svd, its resets, clocks and PLLs, watchdog tick and
  timer, IO_BANK0 and pads, SIO (GPIO, spinlocks, divider, FIFO), PL011
  UARTs, SSI and a bootrom stand-in that boots boot2, and the Raspberry Pi
  Pico; the nRF52840 (Cortex-M4F) with its register file converted from
  Nordic's nrf52840.svd, CLOCK, the GPIO ports, GPIOTE, UARTE with EasyDMA,
  TIMER and RTC, and the nRF52840 DK. `examples/pico-blink-serial` and
  `examples/nrf52840dk-blink`, built by clang and EmbCC.
- The register file's bus rules (the RP2040's atomic aliases and byte
  writes) and peripherals held in reset, shown as "in reset" in the Debug
  perspective (`debug.peripherals` reports `reset`).
- M4 part 3: the MIPS32 core (the PIC32MX's M4K with MIPS16e, and QEMU
  malta's 24Kc) and the PIC32MX795F512L with its register file converted
  from Microchip's PIC32MX device pack: the EVIC, the oscillator from the
  configuration words, Timer1-5, ports A-G with the change notices and
  UART1-6; the Explorer 16/32 with the PIC32MX795F512L PIM and
  `examples/explorer16-pic32mx`, built by clang and EmbCC.
- `tools/importers/edc2regs.py`: a PIC32's EDC to a register description
  with CLR/SET/INV registers, shared vectors and pin functions
  (`clr_set_inv`, `vectors` and `pins` in the register schema).
- The clang and EmbCC profiles build `mipsel-none-elf`; a scenario may press
  a board's own button (`B1.S3`).
- M4 part 4: the PIC24 core (the PIC24F, with EmbCC's dsPIC33E mode) and the
  PIC24FJ1024GB610 with its register file converted from Microchip's
  PIC24F-GA-GB device pack: the interrupt controller, the oscillator from
  the configuration words with OSCCON's unlock sequences and the PPS lock,
  Timer1-5, ports A-G with the interrupt-on-change, and UART1-6 timed on the
  pins the PPS gives them; the Explorer 16/32 with the PIC24FJ1024GB610 PIM
  and `examples/explorer16-pic24`, built by XC16. The `mcu.trap`
  diagnostic.
- Record and replay (M5, SPEC-012): recordings with the project's and
  images' SHA-256 and observations (interrupt entries per core, digital
  probe transitions, serial text, diagnostics); `embsim replay` with a
  first-divergence report (exit 4) and drift refusal (`--allow-drift`);
  `embsim record to-scenario`; `record.replay` over the protocol;
  `tools/verify/compare_reports.py`; a determinism and replay test over
  every example (`record_replay`).
- Coverage and profiling from the backends (M5, SPEC-009 §12): every core
  (Cortex-M, AVR, RISC-V, MIPS, PIC24, and each of the RP2040's two) watched
  instruction by instruction; `embsim run --coverage` (gcov-style text with
  the source, lcov, JSON) and `--profile` (by function, self and inclusive,
  collapsed stacks for flame graphs, JSON); `analysis.start`, `.stop`,
  `.coverage`, `.profile` over the protocol; the run report's `coverage`
  and `profile` summaries; the `analysis` test over every firmware example.
  Extracted from EmbCC's analyses, with per-core call stacks.
- The RP2040's second core: two Cortex-M0+ cores on one clock
  (`core.cores`; each core's NVIC, SysTick, CPUID, divider and FIFO; WFE and
  SEV between them; core 1 launched through its bootrom's FIFO handshake and
  powered by the PSM; IO_BANK0's PROC1 interrupts), and
  `examples/pico-dual-core`, built by clang and EmbCC.
- `edc2regs.py` converts a PIC24's EDC (`config_words` and `pps_outputs` in
  the register schema; `core.prog_end` and `core.ram_base` in the MCU
  schema); the XC16 profile's `pic24fj1024gb610` target.

### Changed
- A toolchain profile's `link_flags` may name the build's variables: the XC
  profiles' `-Wl,-Map=${map}` was passed through as written.
- The DWARF line reader takes XC16 images (their debug sections hold a byte
  in each 16-bit unit), so the Debug perspective shows a PIC24's source.
- Two weak pulls that disagree on a net are a divider, no longer reported
  as `net.contention`.
- A component's own parameters win over its part preset's.
- The runtime finds a UART's RX pin again when a pin's function changes.
- An MCU's GPIO pins take their high level and pulls from its description's
  supply (`io.electrical.vdd`): the nRF52840 DK's 3.0 V.
- The runtime and the loader take PIC pin names (RA0, RD6) as GPIO pins.

### Removed
- The draft 0.1 foundation (Python runtime, HTTP server, single-page UI);
  its kernel semantics and visual direction are retained in the baseline.

### Fixed
- Restore runtime input lists and waveform subscriptions when component placement
  or another structural edit restarts the engine; retain layout-only measurements.
- Catalog clock frequency binding, timed `tpd` application, functional pulse
  source terminal/period binding, and logic-probe output/value observation.
- Legacy current-source fixture polarity and an underdriven PMOS fixture load.
