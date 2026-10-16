# Changelog

All notable changes are recorded here. The format follows Keep a Changelog;
versions follow semantic versioning. There is no release yet.

## Unreleased

### Circuit organization

- Persist named presentation groups, display selectable group outlines, and browse
  parts/groups by reference, model or group name in the Objects panel.
- Lock part/board placement through the inspector or object browser; native
  transactions enforce locks, automatic placement uses locked parts as fixed
  obstacles, and mixed locked selections cannot be moved or arranged partially.
- Support group creation, rename, reassignment, ungroup, group locking and undo;
  deleting members prunes empty groups. Complete copied component groups receive
  new group/member identities and unlocked placement.
- Disable library placement controls during pending/read-only edits so rapid
  placement cannot race a transaction already finishing.
- Export deterministic, aggregated top-level parts-list CSV through the platform
  file adapter. Inventory includes explicit parameters, not board internals or
  procurement-qualified effective part values.

### Cross-platform schematic fixes

- Remove an ambiguous case-insensitive source import that broke macOS/Windows
  workbench and desktop builds; add a portable module-path policy check.
- Keep unchanged server refreshes from cancelling active drawing drafts. Correct
  selection verification for offscreen symbols and retain failed browser traces.

### Schematic authoring workbench

- Reshape existing orthogonal tracks with draggable/keyboard segment handles,
  numeric offsets, live previews and cancellation. Preserve pin endpoints and
  same-net junction contacts; commit one layout-only undoable edit.
- Add snapped position, rotation and mirror controls, relative group movement,
  and explicit mixed-value batch editing of compatible shared parameters.
- Add keyboard-accessible contextual actions, paste at the clicked sheet position,
  net routing/drawing controls and explicit terminal disconnection.
- Add worker-backed structural/drawing checks with severity filters, search,
  target navigation, markers and applicable net-routing repair. Scope and limits
  are explicit; this is not an electrical-rule or physical-clearance checker.
- Preserve panel order and viewport through focus-mode transitions; keep foreign
  tracks fixed when a part moves or is removed at a coincident pin coordinate.
- Reject stale/concurrent edit snapshots, preserve failed undo/redo history entries,
  avoid rerouting unchanged placement, and make drawn tracks keyboard selectable.


### Manual schematic routing

- Place orthogonal wire bends with live previews and visible waypoint markers;
  flip elbows with Space, remove bends with Backspace/Delete, or cancel with Escape.
- Route with arrow keys and Enter as well as the pointer. Explicit paths bypass
  automatic arrangement and smart routing on completion, with one undoable edit.

### Canvas appearance and reload reliability

- Continuous, world-anchored canvas backgrounds eliminate the gray finite-sheet
  strip during pan and zoom. Add engineering/line/dot/cross/plain patterns, four
  theme-aware palettes and adjustable spacing, weight and contrast.
- Appearance preferences persist across reloads and apply to Design, inside and
  Run schematics; restrained system-style chrome, clearer drafting labels and
  keyboard-accessible controls.
- The native server now serves static assets while its session WebSocket is
  active, fixing browser reload stalls. Auxiliary requests preserve authority
  checks, reject another session upgrade and use a bounded header deadline.


### Schematic Studio

- Connected auto-placement, obstacle-aware orthogonal routing and layout previews
  with whole-sheet/selection scopes, two directions and spacing presets. Apply is
  one undoable layout-only transaction; stale previews cannot apply.
- Opt-in auto-arrangement after connecting, free-space component placement,
  smart wire previews, rerouting after transforms, magnetic drag guides,
  alignment/distribution, component/net finder, net spotlight, selection focus,
  side-panel focus mode, undrawn-connection counts, minimap and display controls in a new schematic tool ribbon.
- Layout planning runs in an embedded worker; desktop policy permits the bundled
  worker. Added a user guide and native-engine schematic workflow regression.


### Scenario editor reliability

- Run and Save now reject invalid visible expectation JSON, assertion timing,
  duplicate IDs, missing identifiers and invalid expectation fields. Errors are
  listed in the editor; edits clear assertion badges from a previous run.

### Debugger reliability

- Board switches clear inspected state and discard late responses from the previous
  selection. Overlapping requests keep the busy indicator active until completion.
- Added an evidence-based 1.0 release readiness checklist.

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
- Stack depths (M5, SPEC-009 §12): `embsim run --stack` (text, JSON) and
  `analysis.stack`; each core's main stack (and a Cortex-M's process
  stack): its top, its deepest point and its limit from the link
  (`__stack_limit`, the end of `.data` and `.bss`); each function's own
  frame and its depth with its callees; the error diagnostic
  `mcu.stack_overflow` when the main stack goes past its limit, naming the
  function. Extracted from EmbCC's `stack.c`; `tests/firmware/stack-overflow`.
- The fault report (M5, SPEC-009 §12): `embsim run --faults` (text, JSON)
  and `analysis.faults`; each fault as the core takes it, decoded (a
  Cortex-M's HFSR and CFSR, MMFAR and BFAR; RISC-V's mcause and mtval;
  MIPS's Cause and BadVAddr; a PIC24 trap), with the instruction, the
  registers, a backtrace from the image's `.debug_frame` (read by the new
  CFI reader in `dwarf.c`) or the shadow stack, and the handler; the error
  diagnostic `mcu.fault`. Extracted from EmbCC's `fault.c`;
  `tests/firmware/fault`.
- The analyses name a function by its strong symbol, not a weak alias at
  the same address (startup code's `Default_Handler`).
- `ctx.analog` for Lua models (SPEC-013 §2.1): `voltage(pin)` and
  `between(a, b)` read node voltages; `watch(pin, rising, falling)` makes an
  input's comparator switch at the model's own voltages, for behavioural
  sensors and detectors.
- `embsim catalog install` takes a zip archive of a library (at its top or
  in its one top directory) or an `http(s)://` URL to one (fetched with
  `curl`), as SPEC-014 §4 has it: unpacked into a staging directory,
  validated, copied, the staging removed; an entry outside the library is
  refused. EmbSIM's own DEFLATE decoder and zip reader (`core/util/`).
- The Verify perspective (M5): the scenario editor (a scenario's inputs
  and assertions edited in place, run with each result in its row, saved
  into the project: `scenario.get`, `scenario.save`), recordings replayed
  with the divergence shown or turned into a scenario
  (`record.to_scenario`), coverage with each file's source and counts and
  the functions by cycles.
- Recordings made in a running session replay identically: they begin
  with the digital probes' levels and carry `from_ps` and `from_seq`;
  inputs given at the paused instant are `immediate`.
- The Python client (`python/embsim`, `pip install ./python`): `Session`,
  `run()`, `replay()`, a pytest fixture.
- `embsim run --target BOARD --image FILE` with `--console` and
  `--until-text`: a catalog board as an emulated target for test runners,
  with no project; `--image ID=FILE` over a project. `docs/user/ci.md`.
  EmbLinkRTOS's Arduino Uno conformance suite runs this way.
- The RP2040's second core: two Cortex-M0+ cores on one clock
  (`core.cores`; each core's NVIC, SysTick, CPUID, divider and FIFO; WFE and
  SEV between them; core 1 launched through its bootrom's FIFO handshake and
  powered by the PSM; IO_BANK0's PROC1 interrupts), and
  `examples/pico-dual-core`, built by clang and EmbCC.
- `edc2regs.py` converts a PIC24's EDC (`config_words` and `pps_outputs` in
  the register schema; `core.prog_end` and `core.ram_base` in the MCU
  schema); the XC16 profile's `pic24fj1024gb610` target.

- Arduino sketches built with the unmodified ArduinoCore-avr run on the
  Uno: `tests/vendor/arduino` (Blink and a tour of the core: `delay`,
  `millis`, `analogRead`, `analogWrite`, `attachInterrupt`, `Serial`)
  builds them with the IDE's flags against a pinned checkout of the core
  and runs them under scenarios, in CI's linux-gcc job.
- ST's STM32F1 HAL on the Blue Pill: `tests/vendor/stm32cube` builds an
  application (the clock tree from HSE through the PLL, `HAL_Delay`, EXTI,
  a TIM2 interrupt, the UART both ways) with ST's unmodified HAL, CMSIS
  device files, startup and linker script, at pinned commits, and runs it
  under a scenario, in CI's linux-gcc job.
- The RP2040's bootrom functions (ADR-021): its tables and their functions
  (the lookup, bit operations, memcpy and memset, flash erase and program,
  reset_to_usb_boot, the float and double libraries) done by the host from
  `bkpt` stubs in the ROM, so unmodified Pico SDK images run;
  `tests/vendor/pico-sdk` builds one with the SDK's own CMake and runs it on
  the Pico in CI's linux-gcc job.
- SiFive's freedom-e-sdk examples on the HiFive1: `tests/vendor/freedom-metal`
  builds six of them (hello, sifive-welcome, csr, uart-interrupt,
  example-user-mode, minimal-boot) with the unmodified freedom-metal and the
  board's BSP, as the SDK's build does, and runs them under scenarios in CI's
  linux-gcc job.
- Nordic's nrfx drivers on the nRF52840 DK: `tests/vendor/nrfx` builds an
  application (nrfx_clock, nrfx_uarte both ways through its RX buffers,
  nrfx_timer, nrfx_rtc, nrfx_gpiote) with the unmodified nrfx 4.6.0 and
  its MDK's system file, startup and linker script, and runs it under a
  scenario in CI's linux-gcc job.
- The RP2040's PWM and ADC: eight PWM slices on their pins (the fractional
  divider, free-running and phase-correct, CC and TOP double-buffered,
  inversion, the wrap interrupt) and the ADC (96 clk_adc cycles a
  conversion from GPIO26-29 and the temperature sensor, START_ONCE and
  START_MANY with DIV, round robin, the FIFO and its interrupt).
  `tests/vendor/pico-sdk`'s `pwm_adc` runs the SDK's `hardware_pwm` and
  `hardware_adc` on them.
- The ATmega328P's SPI and TWI masters on their pins: the SPI (MOSI and
  SCK while DDRB makes them outputs, MISO an input, modes 0 to 3, DORD, the
  clock from SPR and SPI2X, WCOL, SPIF cleared by SPSR then SPDR or by its
  vector, SS's mode fault) and the TWI (open-drain SDA and SCL, the
  datasheet's master transmitter and receiver statuses, START, repeated
  START, STOP and STOP with START, TWEA's ACK, TWWC, SCL from TWBR and the
  prescaler, the vector while TWINT and TWIE). `tests/vendor/arduino`'s
  `spi_wire` runs the core's SPI and Wire libraries against the catalog's
  25LC040 and 24C02; `run.py` builds the core's libraries a sketch
  includes, as the IDE's builder does.
- The RP2040's SPI and I2C masters on their pins: the PL022s (eight-entry
  FIFOs, 4 to 16 bits in modes 0 to 3 at clk_peri / (CPSDVSR * (1 + SCR)),
  CSn framing each frame, loopback, overrun and the interrupts) and the
  DW_apb_i2c blocks (the command FIFO with RESTART and STOP, the receive
  FIFO, open-drain SCL and SDA at their HCNT and LCNT with the SDA hold
  time, the bus held between commands, repeated STARTs, aborts with
  IC_TX_ABRT_SOURCE, the raw interrupts and their clear registers).
  `tests/vendor/pico-sdk`'s `spi_i2c` runs the SDK's `hardware_spi` and
  `hardware_i2c` against the catalog's 25LC040 and 24C02.
- The nRF52840's SPIM, TWIM, PWM and SAADC on their pins: SPIM0-3 (EasyDMA
  transfers in modes 0 to 3, ORC, END_START), TWIM0-1 (open-drain START,
  repeated START and STOP, EasyDMA, LASTTX and LASTRX with their shorts,
  SUSPEND and RESUME, ERRORSRC), PWM0-3 (EasyDMA sequences in the four
  decodings, Up and UpAndDown counting, REFRESH, ENDDELAY, loops and
  shorts) and the SAADC (AIN0-7, VDD and VDDH/5, gains and references, 8
  to 14 bits, oversampling, limits, scans into Data RAM). The register file
  keeps TWIM's SHORTS bits beside SPIM's in their shared space, and
  answers the registers nrfx's workaround for SAADC anomaly 212 touches.
  `tests/vendor/nrfx`'s `nrfx_periph` runs nrfx_spim, nrfx_twim, nrfx_pwm
  and nrfx_saadc against the catalog's 25LC040 and 24C02, an RC low-pass
  and a 1.0 V source.
- Toolchain profiles may name the program prefixes a GNU cross toolchain
  is installed under (`detect.prefix`, `detect.alternatives`, each with its
  own link flags when its C library differs): gcc-riscv now finds the
  distributions' riscv64-unknown-elf-gcc (with picolibc) as well as xPack's
  riscv-none-elf-gcc, and `tests/protocol/test_build.py` builds and runs
  the HiFive1 example with it. `toolchain.detect` reports the program it
  found.
- A pin that turns from an input into an output takes its new level at
  once (`esk_pin_output`): an input pulled low that starts driving 0 no
  longer shows a zero-width Z on its net.
- The STM32F1's SPI and I2C masters on their pins: SPI1 and SPI2 in modes
  0 to 3 at their baud prescaler, 8 or 16 bits, TXE, RXNE and BSY; I2C1
  and I2C2 on open-drain SCL and SDA with START, repeated START, STOP,
  ACK and POS, SR1 and SR2's flags with their clearing sequences and BTF
  holding the clock, timed by CCR. `tests/vendor/stm32cube`'s `spi_i2c`
  runs ST's HAL SPI and I2C against the catalog's 25LC040 and 24C02.
- The STM32F1's timers' output compare: CCRx matches with their flags and
  interrupts, the OCxM modes with PWM 1 and 2, CCRx preload, and the
  channels' pins through AFIO's remaps; ADC1 and ADC2's regular group
  (software start, scan and continuous, the sampling times at the ADC
  clock, the pins' voltages, EOC and its interrupt, calibration).
  `tests/vendor/stm32cube`'s `adc_pwm` runs ST's HAL ADC and PWM on them.
- The FE310's always-on domain: its watchdog (the key before each write,
  the scale, the comparator, wdogzerocmp, the sticky interrupt, and a chip
  reset the domain survives, pmucause saying so) and its RTC (the 48-bit
  count, the scale, the compare and its interrupt) on the 32.768 kHz clock.
- RISC-V physical memory protection: the part's entries (`pmp_entries` in
  an MCU's core, 8 on the FE310), OFF, TOR, NA4 and NAPOT, locks that bind
  machine mode and ignore writes, user mode allowed only what an entry
  gives it, MPRV for loads and stores.
- Six more of SiFive's examples on the HiFive1: example-rtc,
  example-watchdog, example-pmp, timer-interrupt, software-interrupt and
  return-pass; `tests/vendor/freedom-metal` prints an example's exit
  status through a wrapper around freedom-metal's `metal_shutdown`, which
  on this board spins.
- A differential test of the Cortex-M4's DSP instructions against QEMU
  (`tests/firmware/dsp`, `tests/protocol/test_dsp.py`): 101 instructions
  over 4096 operand pairs each, their results and GE and Q flags hashed;
  CI's tools job checks the expected hashes against QEMU's mps2-an386.
- A register description's peripheral may name `reserved` ranges, offsets
  with no register that read 0 and ignore writes as the part documents them.

### Changed
- A CMOS output on an analog net drives its rails through `rout` (ADR-023):
  `voh` and `vol` are a datasheet's levels at the rated load, and as the
  Thevenin sources they left a light load 0.4 V from either rail. A pin
  model may give `voc_high` and `voc_low`; TTL outputs keep `voh` and
  `vol`.
- A steady analog island stops solving until something touches it
  (SPEC-004 §4): a busy MCU's 1 µs quanta made every LED and pull-up
  island solve a step each; SiFive's sifive-welcome runs 4 s of a HiFive1
  in 7 s instead of 42, with the same pin voltages.
- The analog fixtures are checked against ngspice too: `ngspice.json` beside
  each (`tools/analog/ngspice_fixture.py`, `--check` in CI), compared by
  `test_analog` within 2 mV + 0.2 %.
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
- An MCU's pin edge on an analog net reached the circuit after the core
  had gone on to its next edges: the next quantum started 1 ps later,
  before the edge's ramp had crossed a device's threshold, so a 25LC040
  clocked at 4 MHz through the Uno's D13 (whose L LED makes the net
  analog) answered a bit late. The next quantum now waits out the ramp
  (less than a cycle, the core going on from where it stopped), and a
  quantum that a wake-up brings earlier waits too: a second change to a
  pin had taken the place of one not yet applied, losing a clock pulse.
- Every pin change cost the core an idle cycle: the quantum after it began
  1 ps later and the core caught up to that time in whole cycles. The
  core now goes on from the cycle it stopped at, so firmware that toggles
  pins runs at its own speed (nrfx_periph's polling loops show it: 616
  RDSR polls, 94 NACKed address polls).
- The engine never sent `serial.data` (SPEC-008 §6), so the workbench's
  Serial terminal stayed empty while the firmware printed: the text was
  there only for a client that asked with `probe.read`. After each
  observation frame the session now streams every serial probe's new
  bytes, base64-encoded, and starts again from the beginning after a
  reset; `tests/protocol/test_serial_stream.py` checks the stream against
  `probe.read`.
- The RISC-V core decoded a store's offset by shifting a negative value
  left, which C leaves undefined: a store at a negative offset (as GCC's
  stack frames make) ran as intended with the compilers here, but UBSan
  stopped on it once gcc-riscv built the HiFive1 example. The offset is
  built unsigned and sign-extended; `test_riscv` stores at -64 and -2048.
- A `digital_io` pin that drove `Z` on an analog net kept its direction as
  an output, and the solver ran no comparator for it: a Lua I2C device's
  SDA, let go after its ACK, never saw the master's edges. A pin driving
  `Z` is an input (SPEC-003 §4). An MCU's open-drain output let go is an
  input too, and the runtime reads a pin on an analog net from its node's
  voltage, so an I2C master reads back its line as the slave holds it.
- The FE310 had nothing at the Debug Module's addresses (0 to 0xFFF),
  where freedom-metal's lfrosc driver reads, given the HiFive1 Rev B
  BSP's offsets as addresses: a load access fault in every example that
  asked the low-frequency clock's rate. The space reads as zeros.
- The Cortex-M4 cores lacked most of ARMv7E-M's DSP instructions (the
  parallel adds and subtracts with GE, SXTB16 and UXTB16 and their
  accumulating forms, SSAT16 and USAT16, the dual multiplies SMLAD to
  SMLSLD, USAD8 and USADA8): newlib's `strlen` for the M4 uses UADD8, and
  any image that called it took a UsageFault.
- The nRF52840's FICR had no words at 0x130 and 0x134 (the part code and
  revision, which the SVD does not list), which nrfx's MDK reads to choose
  its errata workarounds: a bus error.
- The FE310's PLIC had no register for source 0's priority, nor for the
  priority, pending and enable words of the sources past its last, which
  the PLIC specification has: freedom-metal's PLIC set-up writes and reads
  them, and each was a bus error. Source 0's priority reads 0, and the
  rest are reserved ranges.
- The RP2040's RESETS reported RESET_DONE only for the blocks with a
  register description, so the Pico SDK's start-up waited for JTAG, TBMAN
  and the rest without end; every block reports its reset done.
- The ATmega328P's timers set TOVn in fast PWM a timer clock early, as the
  counter reached TOP rather than as it went on to BOTTOM, so a read of
  TCNTn at TOP found the flag already set. The Arduino core's `micros()`
  counted that overflow twice and then ran back by 1024 µs, and `delay()`
  returned early: Blink blinked at 9 Hz, not 0.5 Hz. `test_avr` follows the flag
  clock by clock.
- `embsim build` with a GNU profile found no target for any board: the
  boards name a GNU target by `cpu`, the build looked for `target` only.
  The gcc-avr profile's convert step (`${objcopy}`, `${hex}`) and its
  target's `-DF_CPU` were not expanded, and it ignored a project's linker
  script. A project's firmware takes `link_flags` for the link alone. The
  gcc-arm profile now builds the Blue Pill, Pico and nRF52840 DK examples
  and gcc-avr the Uno, each passing its scenario.
- A digital input on an analog net heard of a threshold crossing when the
  analog step that found it began, up to a step early (0.9 ms on a slow
  ramp); it now hears of it at the crossing's own time, to the bisection's
  resolution, as a `SIGNAL` event, as SPEC-004 §8 has it. MCU inputs, gates and scripted models on analog nets all see
  the corrected times.
- A reset kept the pin configuration of the run before it (an MCU's
  pull-ups), so the analog side of a reset session started from it and ran
  apart from a freshly loaded one; a reset restores every pin's model.
- A digital probe on an analog net recorded the net's digital value (`Z`)
  at each change besides its thresholded samples; it records the
  thresholded level, when it changes.
- The Verify perspective went blank for a project without scenarios or
  probes: the scenario editor's store selectors made a new empty list on
  every render, which rendered again without end.
- Restore runtime input lists and waveform subscriptions when component placement
  or another structural edit restarts the engine; retain layout-only measurements.
- Catalog clock frequency binding, timed `tpd` application, functional pulse
  source terminal/period binding, and logic-probe output/value observation.
- Legacy current-source fixture polarity and an underdriven PMOS fixture load.
