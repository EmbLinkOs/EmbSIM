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

### Removed
- The draft 0.1 foundation (Python runtime, HTTP server, single-page UI);
  its kernel semantics and visual direction are retained in the baseline.
