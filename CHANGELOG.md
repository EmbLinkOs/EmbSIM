# Changelog

All notable changes are recorded here. The format follows Keep a Changelog;
versions follow semantic versioning. There is no release yet.

## Unreleased

### Added
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

### Removed
- The draft 0.1 foundation (Python runtime, HTTP server, single-page UI);
  its kernel semantics and visual direction are retained in the baseline.
