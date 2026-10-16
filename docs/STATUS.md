# Project status and evidence

**Reviewed:** 2026-10-16 at the M5 implementation (verification: recordings and replay, coverage and profiling, the Verify perspective, the Python client, EmbSIM as EmbLinkRTOS's emulated target) on branch `claude/affectionate-allen-40uv0o`. This inventory is
maintained by hand until evidence tooling exists. It is not a generated
support matrix. Specifications define target contracts; their acceptance
does not mean the described behaviour exists.

## Implemented subset

| Area | Current implementation | Evidence and limits |
|---|---|---|
| Design | Architecture 01 to 09 (ADR-001 to ADR-020), SPEC-001 to SPEC-014, 94 requirements | Reviewed documents; `tools/docs/check_docs.py` passes |
| Repository | CMake presets, Makefile wrapper, CI matrix, documentation and policy workflows, templates, ruleset JSON, policy and banned-construct checkers | Local runs of the checkers; remote workflow results are recorded here when observed |
| Core: scheduler and time | Rational clocks, class-ordered event heap, delta-cycle bound, cancellation by generation (SPEC-002) | `test_sched`: ordering, cancellation, overflow, limits, 72 MHz and 32.768 kHz clocks exact over 10^9 edges |
| Core: nets and digital engine | Pin models and logic families, four-value resolution with strengths, contention and floating diagnostics, inertial delays, islands and bridges, the model API; gates, flip-flop, sources, pulls, transmission-gate switches, indicator, probe, UART virtual terminal (SPEC-003) | `test_engine`: resolution, floating inputs, gate timing, inertial pulses, clock, switch, UART loopback |
| Core: analog engine | MNA per island, Newton-Raphson, deterministic dense LU, gmin and source stepping, trapezoidal with LTE control and backward-Euler restarts, breakpoints, bridge crossings bisected to the configured resolution; resistor, capacitor, inductor, V and I sources (dc, pulse, sine, pwl, exp), switch with bounce, potentiometer, diode, LED, Zener, NPN/PNP, NMOS/PMOS, op-amp, regulator, pull (SPEC-004) | `test_analog`: 1121 checks (including multi-island stepping and regulator rails) and seven analytic fixtures under GCC and Clang; ngspice goldens pending (not installed); BDF2, junction capacitances, sparse solver not implemented |
| Core: models and catalog loading | Registry of primitives with merged digital and electrical tiers; libraries by path; component instantiation with parameter validation, presets, `$param` pin models and composite expansion through ports; boards as composites whose MCU runs firmware (SPEC-005, 006, 007 subset); user libraries from `$EMBSIM_LIBRARIES` or the user data directory | `test_session`: temporary library, loader, inputs, snapshots, scenario assertions, determinism across sessions, composite expansion, bad project |
| Lua model tier | Lua 5.4.7 vendored (`third_party/lua`, MIT), one state per instance with its own environment, text chunks only, a 1,000,000-instruction budget per evaluation and a 64 MiB memory cap; `ctx` with pins (value, level, voltage), drive, release, timers, inputs, diagnostics and `print`; I2C and SPI slaves decoded in C (`ctx.bus.i2c_slave`, `ctx.bus.spi_slave`); `on_input`, `on_time`, `on_set`, `observe`; errors and budget overruns stop the instance until reset (SPEC-013 §2) | `test_lua`: a counter, determinism across runs, faults and the budget, I2C and SPI slaves, syntax errors; 38 catalog parts are Lua models and pass their fixtures |
| Component SDK and validation | `embsim catalog validate [DIR] [--library] [--json]` in the engine: every item instantiated under each fidelity policy it declares and its fixtures run as project and scenario, items reported ok, broken or invalid; `catalog new component\|board NAME [--lua]` writes a template that validates as written; `catalog install DIR [--to] [--force]` (SPEC-014 §3 to §6) | `catalog_sdk` (`tests/protocol/test_sdk.py`): a Lua component and a board from the templates validate, install into a user library and are listed from it through `$EMBSIM_LIBRARIES`; a part whose fixture fails is reported broken and install refuses it |
| Importers | `tools/importers/rtos2board.py` (EmbLinkRTOS board YAML), `dts2board.py` (DeviceTree source, its own parser), `kicad2project.py` (KiCad netlist to a laid-out project), sharing `boardgen.py`; `svd2regs.py` (SPEC-014 §7); `edc2regs.py` (a PIC32's or a PIC24's EDC from Microchip's device pack, with a PIC32's CLR/SET/INV registers and vectors, a PIC24's PPS functions and configuration words, pin functions and the package) | `importers` (`tools/test_importers.py`): the boards they write validate and pass their power fixture; `edc2regs.py` on a made-up PIC32's and PIC24's EDC; `examples/kicad-flasher` is a KiCad netlist imported and run |
| Core: session and protocol | Command path with virtual timestamps and the input log, probes (digital on change, analog and value at a rate, thresholded digital on analog nets), snapshots and topology, pacing slices, notifications, scenarios with assertions and the run report, project transactions (edits from the workbench: layout-only commits keep the run, circuit changes are checked then rebuild it) and `project.save`, the JSON-RPC method table over in-process, stdio and WebSocket (SPEC-008) | `test_session` (in-process), `tests/protocol/run_conformance.py` (nine cases on stdio and WebSocket, transports compared), `test_host` (files, sockets, SHA-1, base64, frames) |
| MCU runtime | Cortex-M3 core extracted from EmbCC (Thumb-2, NVIC, SCB, SysTick, DWT, FPU), bus with bit-band and aliases, ELF and Intel HEX loaders, semihosting; register files from EmbSIM JSON (ADR-020); STM32F1 models: RCC, GPIO, AFIO, EXTI, USART, TIM (time base), FLASH, PWR; runs in quanta on the scheduler with pins as signal events (ADR-019); USART frames on the TX pin and decoded from the RX pin; NRST and BOOT0 (SPEC-009) | `test_mcu` (11022 checks): EmbCC's Blue Pill golden fixture with five documented USART timing deltas, the same output from a description converted from ST's SVD, serial input, a breakpoint-and-continue run equal to an uninterrupted one (MCU-006); `examples/bluepill-button-led` runs firmware built by Clang and by EmbCC |
| MCU runtime: AVR | AVR5 core extracted from EmbCC (cycle-exact, SREG, the vector table, SLEEP), its state a parameter; ATmega328P models over the register file written from the datasheet: PORTB/C/D with pull-ups and PUD, INT0/INT1 and pin-change interrupts, Timer/Counter0, 1 and 2 in every waveform mode with double buffering and the OC pins, USART0 at its baud rate, the 10-bit ADC from the pins' voltages, the EEPROM, the clock prescaler; an AVR disassembler; DWARF with 2-byte addresses | `test_avr` (141 checks: flags, cycles, Timer0's interrupt, a USART frame's cycles, ports, the ADC, the disassembler); `examples/uno-blink-serial` 7/7 with firmware built by clang and by EmbCC; EmbCC's QEMU-refereed AVR fixtures not run here (no QEMU) |
| MCU runtime: RISC-V | RV32IMAC core extracted from EmbCC (Zicsr, machine-mode traps, compressed instructions), its state a parameter, interrupt lines from the devices, WFI as sleep; FE310-G002 models written from SiFive's manual: the CLINT (mtime at the 32.768 kHz RTC, exactly), the PLIC (priorities, threshold, claim and complete), the GPIO (interrupts, IOFs), the UARTs (FIFOs, watermarks), the PRCI (HFROSC, HFXOSC, PLL); the RISC-V disassembler | `test_riscv` (64 checks: ALU and divide cycles, an ECALL trap, the CLINT interrupt at exactly one RTC second, a UART frame, the disassembler); `examples/hifive1-blink` 7/7 with firmware built by clang and by EmbCC |
| MCU runtime: RP2040 | Cortex-M0+ (Armv6-M) on both cores, interleaved on one clock with each core's NVIC, SysTick and event register (WFE, SEV), core 1 launched through its bootrom's FIFO handshake and powered off and on by the PSM; the register file converted from Raspberry Pi's RP2040.svd (BSD-3-Clause) on the part's bus rules (atomic XOR, set and clear aliases; byte writes across the word) with peripherals held in reset by RESETS; models extracted from EmbCC: the clocks (XOSC, ROSC, the PLLs, the generators, FC0) with clk_sys as the core's clock, PSM, the watchdog's microsecond tick and its reset, the timer's alarms, IO_BANK0 and the pads (function select, overrides, pulls, interrupts to each core), the SIO (GPIO, spinlocks; each core's CPUID, divider and FIFO, SIO_IRQ_PROC0/1), the PL011 UARTs timed at their divisor, the SSI with the Pico's flash; a bootrom stand-in that checks boot2's CRC, copies it to SRAM and enters it | `test_rp2040` (825 checks: the boot through boot2 and a bad CRC, the aliases and byte writes, RESETS, PLL_SYS at 125 MHz measured by FC0, a timer alarm on the 1 µs tick, the divider, a UART bit time, core 1's launch, CPUID, a WFE ended by the other core's SEV, the PSM's power); `examples/pico-blink-serial` 7/7 and `examples/pico-dual-core` 9/9 with firmware built by clang and by EmbCC |
| MCU runtime: nRF52840 | Cortex-M4F; the register file converted from Nordic's nrf52840.svd (BSD-3-Clause), peripherals that share an address space merged, FICR with a part's factory words; Nordic's tasks, events and interrupts; models extracted from EmbCC: CLOCK (HFXO, LFCLK), the GPIO ports (drive modes, pulls, SENSE, LATCH, DETECT), UART0/UARTE0 and UARTE1 with EasyDMA and SHORTS, timed at BAUDRATE on their PSEL pins, TIMER0-4, RTC0-2; GPIOTE written for EmbSIM (event and task channels, PORT) | `test_nrf52` (117 checks: FICR, the HFXO's debounce, the ports and GPIOTE's edges and tasks, a UARTE buffer's frames and ENDTX, a TIMER compare, an RTC compare); `examples/nrf52840dk-blink` 7/7 with firmware built by clang and by EmbCC; the radio, PPI and the serial buses are plain registers |
| MCU runtime: PIC32MX | MIPS32r2 with MIPS16e extracted from EmbCC, its state a parameter: the M4K (EIC mode, two register sets, fixed mapping, the core timer) and QEMU malta's 24Kc; WAIT and idle loops sleep; the MIPS32 disassembler; the PIC32MX795F512L's register file converted from Microchip's Apache-2.0 device pack (`tools/importers/edc2regs.py`) with its CLR, SET and INV registers, shared vectors and pin functions; models extracted from EmbCC: the EVIC (priorities, subpriorities, single- and multi-vector modes, the shadow set), the oscillator from the configuration words (the PLL, the switch, PBDIV, SYSKEY's lock) with SYSCLK as the core's clock, Timer1-5 with 32-bit pairs, ports A-G with the change notices and their pull-ups, UART1-6 timed at their baud rate on their pins | `test_mips` (168 checks: a delay slot, exceptions in and out of one, the core timer in compatibility and EIC modes, MIPS16e, ll/sc, the disassembler); `test_pic32` (1326 checks: CLR/SET/INV and the pins, the clock from the configuration words and the lock, Timer1, a change notice, UART2's bit time and persistent interrupt, the example's image); `examples/explorer16-pic32mx` 8/8 with firmware built by clang and by EmbCC; the ADC, SPI, I2C, DMA, USB, Ethernet and CAN are plain registers |
| MCU runtime: PIC24 | The PIC24F core extracted from EmbCC, its state a parameter (and its dsPIC33E mode with the DSP engine, untested here): every PIC24F instruction and addressing mode, REPEAT and the iterative divide, the Extended Data Space and program space visibility, table reads, traps (`mcu.trap`) and interrupts through the vector table, DISI; PWRSAV and a branch to itself sleep; program memory at twice its address and the data space at 0x2000000 on one bus; the disassembler; the PIC24FJ1024GB610's register file converted from Microchip's Apache-2.0 device pack with its PPS functions and configuration words; models extracted from EmbCC: the interrupt controller, the oscillator (FNOSC from the configuration words, OSCCON's unlock sequences, the PLL's 32 MHz and CPDIV) with FCY as the core's clock and the PPS lock, Timer1-5, ports A-G with ANS, the pulls and the interrupt-on-change; UART1-6 written for EmbSIM, timed on the pins the PPS gives them | `test_pic24` (1465 checks: instructions as xc16-as assembles them and their cycles, an address error trap, an interrupt taken from PWRSAV, the clock from the configuration words and OSCCON's lock, Timer1, the interrupt-on-change, UART2 through the PPS and IOLOCK, the disassembler, the example's image and its XC16 line table); `examples/explorer16-pic24` 8/8 with firmware built by XC16 v2.10; the ADC, SPI, I2C, the capture/compare modules, DMA, USB and the RTCC are plain registers |
| Record and replay | Recordings (SPEC-012): the engine, the project's and images' SHA-256, every input, and observations in time order (each core's interrupt entries, digital probe transitions from reset, the serial probes' text, warning and error diagnostics, the end); `embsim run --record`, `embsim replay` (exit 4 at the first divergence, with both observations; a changed project or image refused unless `--allow-drift`), `embsim record to-scenario`, and `record.start`, `record.stop`, `record.replay` (a file or the recording itself) and `record.to_scenario` over the protocol; a recording made in a running session begins with the digital probes' levels and replays from its start (`from_ps`, `from_seq`, `immediate` inputs); `tools/verify/compare_reports.py` diffs two run reports | `record_replay` (`tests/protocol/test_replay.py`): all twenty examples recorded twice in two processes with equal observations, each replayed exactly, an injected divergence, a changed image refused, a recording turned into a scenario passing, `record.replay` over `embsim sidecar`; `python_client`: recordings made from reset, from 7 ms and after an earlier input each replay identically and pass as scenarios; `test_host` checks SHA-256 against FIPS 180-4's vectors; event traces (`--trace-events`) and analog observations are not implemented |
| Coverage, profile, stacks and faults | Every core watched instruction by instruction when a machine carries analyses (SPEC-009 §12; extracted from EmbCC's analyses): coverage per source line and function from the DWARF line tables and the ELF's functions and code labels; the profile by call path on a shadow stack per core, from each core's calls and returns and the exception entries and returns every core reports (Cortex-M, AVR, RISC-V, MIPS with its delay slots, PIC24; the RP2040's two cores rooted apart); `embsim run --coverage` (gcov-style text with the source, lcov, JSON) and `--profile` (text, collapsed stacks, JSON); the stacks' depths (`--stack`, `analysis.stack`: each core's main and process stacks against the link's `__stack_limit` or the end of the data, each function's own frame and its depth with its callees, `mcu.stack_overflow` when the main stack goes past its limit); the fault report (`--faults`, `analysis.faults`, `mcu.fault`: a Cortex-M's faults with HFSR and CFSR decoded, RISC-V and MIPS exceptions, PIC24 traps, the instruction, the registers, a backtrace from `.debug_frame` or the shadow stack, the handler); `analysis.start`, `.stop`, `.coverage`, `.profile`, `.stack`, `.faults`; the run report's summaries | `analysis` (`tests/protocol/test_analysis.py`): all nine firmware examples measured twice in two processes alike, the profile's functions adding up to the instructions and cycles, lcov agreeing with the JSON line for line, the collapsed stacks adding up to the cycles, every board's stack within its limit and main's depth at least its frame, a recursion past `__stack_limit` (`tests/firmware/stack-overflow`) raising `mcu.stack_overflow` once in the right function with the depths adding up, a load from nothing (`tests/firmware/fault`) raising `mcu.fault` once as a HardFault (FORCED, PRECISERR, BFAR) with the backtrace peek, read_sensor, main, Reset_Handler from `.debug_frame`, the protocol's methods; RISC-V and MIPS faults decoded by hand (a load access fault on the HiFive1, an address error on the PIC32MX, each with its `.debug_frame` backtrace); the PIC24's decoding is not exercised (no XC16 in CI); the Pico's two-core scenario runs in the same wall time with both on |
| Python client | `python/embsim` (`pip install ./python`, standard library only): `Session` over `embsim sidecar` (time, inputs, serial text, probes, scenarios, recordings and replay, coverage and profile, the debugger), `run()` and `replay()` over the command line with its exit codes, a pytest fixture | `python_client` (`python/tests/test_session.py`) against this build |
| An emulated target for test runners | `embsim run --target BOARD --image FILE` (a catalog board, no project), `--console`, `--until-text TEXT` (ends at the millisecond the text appears; exit 1 when it never does), `--image ID=FILE` over a project; `docs/user/ci.md` (the CI contract, GitHub Actions, GitLab CI, Python) | `run_target` (`tests/protocol/test_target.py`); EmbLinkRTOS's nine Arduino Uno conformance tests pass on EmbSIM's Uno in 4.4 s (its `avr-uno-embsim` preset and `tools/embsim/run_embsim.py`, branch `claude/affectionate-allen-40uv0o` of EmbLinkOs/EmbLinkRTOS, with an `avr-embsim` CI job that needs this branch merged) |
| Multi-board projects | several boards in one project, each MCU with its own firmware on the session's time, connected by nets | `multiboard` (`tests/protocol/test_multiboard.py`): an Uno and a Blue Pill exchange bytes over one serial line at their real baud rates and logic levels; two runs agree on every event |
| Debug | Breakpoints by address, symbol, or file and line; watchpoints; step, halt, continue; registers and memory read and write; Thumb disassembler; ELF symbols; DWARF 2 to 5 line tables (XC16's packed sections too); `debug.*` methods; GDB remote protocol server (`embsim gdb`) | `test_mcu` (source lines), `tests/protocol/test_gdb.py` (registers, memory, breakpoints, step, continue over RSP); LLDB debugged the example through `embsim gdb` in this workspace |
| Toolchains | Detection from the profiles' version commands, builds by template expansion (`embsim build`, `embsim toolchains`, `toolchain.detect`, `toolchain.build`) (SPEC-010 subset) | `tests/protocol/test_build.py`; the EmbCC and Clang profiles built the examples here, and XC16 v2.10 (free mode, installed by hand) built `examples/explorer16-pic24`; the gcc-arm, TinyCC, XC8 and XC32 profiles are not exercised (not installed), and CI has no XC16 |
| CLI | `run` (with `--record`, `--coverage`, `--profile`, `--target`/`--image`, `--until-text`, `--console`), `replay`, `record to-scenario`, `validate`, `serve` (with `--allow-origin`), `sidecar`, `catalog` (`list`, `validate`, `new`, `install`), `board`, `models`, `gdb`, `build`, `toolchains`, `version` | `tools/ci/run_examples.py`: twenty examples validate and pass their scenarios (40 steps): counter-bus, button-led, button-transistor-led, rc-filter, bluepill-button-led with its firmware (the LED driven through an NPN transistor as 01 §8 describes), display-showcase 6/6, kicad-flasher, temperature-alarm, temperature-divider, light-controlled-switch, optocoupled-input, transistor-relay, uno-blink-serial 7/7, hifive1-blink 7/7, pico-blink-serial 7/7, pico-dual-core 9/9, nrf52840dk-blink 7/7, explorer16-pic32mx 8/8, explorer16-pic24 8/8, two-boards-uart 8/8 |
| Catalog | 147 components in 20 categories with symbols, artwork and 308 component fixtures, named manufacturer parts among them (passives, sources, semiconductors, 74HC and CD4000 logic, the 555, regulators, displays: 7-segment, HD44780 LCD, SSD1306 OLED, MAX7219; I2C and SPI devices: 24C02 and 25LC040 EEPROMs, DS1307, sensors; actuators: servo, stepper driver, buzzer); seven MCUs with their register descriptions and packages (STM32F103C8 LQFP48, ATmega328P DIP-28, FE310-G002 QFN48, RP2040 QFN56, nRF52840 aQFN73, PIC32MX795F512L TQFP100, PIC24FJ1024GB610 TQFP100); seven boards with artwork and power fixtures (Blue Pill, Arduino Uno R3, HiFive1 Rev B, Raspberry Pi Pico, nRF52840 DK, Explorer 16/32 with the PIC32MX795F512L PIM and with the PIC24FJ1024GB610 PIM); eight toolchain profiles; JSON Schemas for every kind; the Python validator with semantic checks | `embsim catalog validate catalog`: 154 ok, 0 broken, 0 invalid, 315 fixtures run (`catalog_fixtures`); `catalog_fixture_assertions` runs the component fixtures through the CLI; `tools/validate.py --all`: 552 files, 0 errors; the tools' unit tests: 124 tests; the STM32F103C8's register description written from RM0008 (ADR-020), the RP2040's and the nRF52840's converted from their vendors' BSD-3-Clause SVD files, the PIC32MX795F512L's and the PIC24FJ1024GB610's from Microchip's Apache-2.0 device packs |
| Workbench | TypeScript workspace: typed protocol client and mock session, design system with light and dark themes; schematic editing against the engine through project transactions (place, wire, move, rotate, mirror, delete, copy, paste, duplicate, net labels and renames, buses with their live value, undo and redo, save to the project file); boards drawn as blocks of their ports, and the inside of a board or composite part opened on a read-only, live sheet; board view with each board's artwork and live LEDs, buttons and jumpers; oscilloscope, logic analyzer with UART, SPI and I2C decoders, serial terminal, meters with units; board view artwork states with value maps, formats and text (SPEC-005 §8); the Debug perspective (source, disassembly, registers, breakpoints, memory, peripherals, console); the Verify perspective (a scenario's inputs and assertions edited in place, run with each assertion's result in its row and saved into the project; recordings replayed with the divergence shown or turned into a scenario; coverage with each file's source and counts and the functions by cycles, lcov and collapsed stacks to download); command palette; platform adapter; Tauri 2 desktop shell with the engine as sidecar and its icon | `pnpm typecheck`, `lint`, `test` (82 tests incl. axe, the decoders, artwork states and their scoping), `build` pass; Playwright smoke (open, run, press, observe, export; keyboard place and wire; theme) passes in Chromium; editing, copy and paste, and save checked in Chromium against `embsim serve`; the Blue Pill's boot banner decoded from USART1 frame by frame; `tools/perf/large_project.py` 500 inverters with 1,499 wires and 1,000 labels measured by `scripts/perf.mjs` in headless software-rendered Chromium: pan and part drag 16.8 ms at the 95th percentile, zoom 33 ms; usable at 1024x700. The desktop shell is built and run here on Linux (WebKitGTK 4.1 under Xvfb) with the Blue Pill example loaded, and its deb is built by `tauri build`; macOS and Windows installers come from CI |

Observed on 2026-10-16 in this workspace: Ubuntu 24.04, GCC 13.3 and Clang 18.1, CMake 3.28, Python 3.13, Node 22. `ctest` on the GCC Debug build: 32 of 32 targets pass (test_analog, test_avr, test_catalog, test_engine, test_host, test_json, test_lua, test_mcu, test_mips, test_nrf52, test_pic24, test_pic32, test_riscv, test_rp2040, test_sched, test_session, catalog_fixtures, tools_unittests, catalog_sdk, importers, docs_integrity, banned_constructs, catalog_fixture_assertions, protocol_conformance, gdb_server, firmware_build, multiboard, examples_headless, record_replay, analysis, run_target, python_client); the same pass on the Clang, TinyCC and AddressSanitizer builds. The CI matrix (Linux with GCC, Clang and TinyCC; macOS with Clang; Windows with MSVC; the workbench; tools and catalog; documentation; repository policy) was observed green on the M0 pull request and on the M1 pull request (EmbLinkOs/EmbSIM#2, head 9bc3d42, 2026-10-16) the M2 pull request (EmbLinkOs/EmbSIM#3, all 17 checks) the M3 pull request (EmbLinkOs/EmbSIM#4, all 17 checks) and the M4 part 1 pull request (EmbLinkOs/EmbSIM#5, all 17 checks, after its MSVC fixes), which ran the Blue Pill golden fixture, the GDB server and the firmware build on all three platforms.

## First named-part catalog batch (component work)

After integration with M3, the catalog contains 128 components, including 26 manufacturer/package entries
and a generic open-collector comparator channel. The named batch adds discrete
semiconductors, four regulators, full LM358P/LM393P packages, NE555P, LM35DZ,
TMP36GT9Z and six full 74HC packages. `part` metadata is searchable and shown
in the inspector; all named parts have original symbols and package illustrations.
See [the catalog guide](../catalog/README.md) for the inventory, exact supported
behaviors and source-review limits. Manufacturer access was blocked during the
initial batch; the follow-up source review below records the recovered evidence.
No manufacturer qualification of the electrical models is claimed.
This batch supplements the merged M3 implementation.

Evidence from the component-work branch on Linux, GCC 14.2, Python 3.12 and Node 24:

- `ctest --preset dev --output-on-failure --no-tests=error`: 20/20 target outcomes
  successful. `firmware_build` explicitly skips without a cross compiler; all
  other targets execute. The new native catalog test has 23 checks.
- `catalog_fixtures` instantiates all 129 catalog items (components plus board);
  `catalog_fixture_assertions` executes all 251 component fixtures through the CLI, including
  all old fixtures, not only schema validation. It checks nominal outputs, all HC
  channels and delays, loaded electrical rails, comparator pull-ups, sensor endpoints,
  555 retained state/reset/divider and RC oscillation, and solver health.
- The combined Address/undefined-behavior sanitizer build passes all 20 CTest
  outcomes, including Lua, catalog instantiation, fixture assertions and examples.
  The firmware-build wrapper explicitly skips without a cross compiler.
- Python tools: 119 tests. `tools/validate.py --all`: 422 files, no errors/warnings.
- Workbench typecheck, lint, production build and 75 existing tests pass.
  A real-engine Chromium check verifies named-part search, manufacturer/package
  labels, the inspector's datasheet link, and the Run view without page errors.
- `examples/temperature-alarm` passes six assertions through the CLI. Its LM35DZ,
  LM393P and 2N3904 circuit lights and extinguishes an LED as temperature changes.

macOS, Windows and TinyCC have not been rerun locally for this batch. Existing
CI defines those checks; remote results must be observed before claiming them.
The earlier workbench Debug and hold-state findings are intentionally outside
this component batch.

## Manufacturer source review after the M4 merge

Based on main `22deb05`, including both the named-part and M4 merges. Reviewed
25 named entries against 23 manufacturer PDFs; the three L78 entries share a
source. Each entry now records revision, SHA-256, review date and page references.
[The source record](engineering/named-part-sources.md) distinguishes reviewed
package facts from model qualification. TMP36GT9Z's TO-92 map is confirmed from
Figure 4 and its exact ordering code from the ordering guide. The onsemi 2N7000
link and the Infineon redirect are replaced by working manufacturer PDF URLs.
Nexperia BAT54 remains reference-only: the document URL is unavailable and the
manufacturer CDN denies alternative document requests.

The five new electrical fixtures check LM35DZ at 4 and 30 V, TMP36GT9Z at 2.7
and 5.5 V, and a TMP36 with its ground at 1.2 V. The latter checks both absolute
net voltage and the sensor's output observation relative to its own ground.
Nominal transfer and pin identity are covered; electrical accuracy, thermal
behavior and operating corners remain unqualified. The existing pin-map
regression now checks every terminal of all 25 source-reviewed entries.

Combined integration evidence on Linux, GCC 14.2 and Python 3.12:

- All 23 development CTest outcomes pass after the two merges; the firmware
  build wrapper reports its missing cross compiler explicitly.
- Current engine catalog validation: 131 valid items (128 components, three
  boards), no broken or invalid entries, 259 fixtures including the boards.
- The independent fixture runner passes 256 component fixtures and 782
  assertions; all 119 Python tools tests pass.
- Schema/semantic validation: 443 files, no errors or warnings. Documentation
  integrity: 89 documents, 92 requirements, no problems.
- Both merged PRs ([#5](https://github.com/EmbLinkOs/EmbSIM/pull/5) and
  [#6](https://github.com/EmbLinkOs/EmbSIM/pull/6)) were observed with all 17
  checks passing, including macOS/Windows core, desktop and browser smoke jobs.
  This is evidence for the merged baseline; the source-review branch needs its
  own CI results.
- No UI or native engine code is changed by this source-review follow-up.

## Interactive library and circuit examples

Based on main `5cc2762` after PR #8. The [interactive library record](engineering/interactive-library.md)
lists 18 new named entries, a reusable relay contact controller, the manufacturer
source status and all electrical approximation boundaries. The catalog totals
147 components, including 44 named parts; 38 have a reviewed manufacturer source,
while six retain explicitly unreviewed references. No vendor qualification is implied.

The explorer adds grouped categories/counts, multiword search, manufacturer and
package filters, model-tier and named-part filters, and Overview/Pins/Models
previews. Unsupported policy/broken entries cannot be placed or dragged. New
artwork is original and supports live input hotspots and readable decimal values.
Six native electrical primitives cover environmental resistance, photodiodes,
optocouplers, relay contact timing, SPDT switches and four-pad tactile buttons.
The B3F-1000 fixtures include permanent pad pairs and unused duplicate pads.

Four wired examples cover a temperature divider/adjustable alarm, a light-controlled
transistor switch, an optocoupled input with offset grounds, and a transistor relay
driver with an R/L coil and external flyback diode. Each has a CLI scenario and
GUI controls. The real-engine browser test caught a restart/subscription defect;
structural edits now reload runtime bindings and restore waveforms.

Observed locally on Linux, GCC 14.2, Python 3.12 and Node 24:

- All 23 development and 23 Address/undefined-behavior sanitizer CTest outcomes
  pass. The firmware-build wrapper explicitly skips compilation without a cross
  compiler; other targets execute, including the M4 multiboard examples.
- Engine catalog validation: 150 valid items, no broken/invalid entries and 311
  fixtures. The independent runner passes 308 component fixtures / 897 assertions.
- All 15 example projects validate and their scenarios pass (30 CLI invocations).
- All 522 JSON files validate; 119 Python tool tests pass. Documentation integrity
  checks 94 documents and 94 requirements with no problems.
- Workbench typecheck, lint, build and 82 UI tests pass, including accessibility
  checks, combined library filtering and numeric artwork formatting.
- `tests/browser/test_library.py` passes in Chromium against the actual engine:
  manufacturer/package search, category browsing, preview sections, placement,
  light/dark themes, live press/release, refreshed input controls and waveform
  samples that cross the expected high/low limits after the placement restart.

The new branch still needs its own remote macOS/Windows/TinyCC and desktop checks.
Local evidence must not be confused with the green checks recorded for older merges.

## Specified but unimplemented

The PIC32MX's ADC, SPI, I2C, DMA, USB,
Ethernet, CAN and watchdog; the PIC24FJ1024GB610's ADC, SPI, I2C, capture
and compare modules, EPMP, RTCC, DMA, USB, watchdog and flash programming,
and a dsPIC33 part; and the PIC32MZ's FPU, DSP ASE and microMIPS; the
RP2040's bootrom's functions, PIO, DMA, SPI, I2C, PWM and ADC;
the nRF52840's radio, PPI, SPIM, TWIM, SAADC and PWM; the RISC-V F and D
extensions; on the STM32F1 the ADC, SPI, I2C, DMA and timer capture/compare
and PWM output models, on the ATmega328P SPI, TWI, the watchdog and the
analog comparator, on the FE310 the always-on domain, SPI, PWM and I2C (their
registers hold values and raise `mcu.unsupported_access`); DWARF variables (`debug.variables`); the GDB
server over the session protocol (`debug.gdb`); a debugger's choice of core
on a part with two (it sees the core that runs next); full event traces and analog observations in recordings (SPEC-012 §1, §5),
native models and `ctx.analog` for Lua models (SPEC-013 beyond §2), the
per-project `lua_budget` setting (the budget is fixed), library
installation from archives and URLs (SPEC-014: from a directory only), the
`catalog.validate` protocol method (the CLI has it), binary waveform frames
(samples are JSON arrays with `encoding: "json"`), a fault report for the AVR (it has no faults to decode) and MIPS16e
instructions disassembled in it, editing inside a board or
composite (the inside is read-only), signed installers and an update channel,
importers for Eagle, Altium and EasyEDA.

## Validation recorded in this workspace

Recorded as it happens: the checks listed above, on the dates given.

## Known limits and unresolved evidence

- An MCU sees an input that comes from an analog comparator crossing at the
  start of the quantum after the crossing, at most `core_quantum_ps` (1 µs
  by default) late (ADR-019); digital inputs are exact.
- Five USART timing deltas from EmbCC's Blue Pill fixture are documented in
  `tests/firmware/bluepill-golden/deltas.json` (frames on the pin take
  their full duration; EmbCC's are instantaneous).
- `esk_host_run` on Windows (CreateProcess) is exercised only by CI
  (`firmware_build` passed on the M1 run).
- Comparator-bridge crossings are delivered with the island step's start
  time as `now` for the reacting model (the crossing time is bracketed but
  not yet used as the event time); SPEC-004 §8 is therefore met only to
  within one island step.
- The `main` branch is protected by the repository's ruleset; pull
  requests run the CI matrix.
- Instruction timing of every planned core is an estimate except AVR
  (ADR-015).
- A supply symbol (`emblink/vcc`) on a net that a component model drives (a
  regulator's output) is not added as a source (`analog.supply_driven`):
  feed a board's regulated rail from a voltage source, as the Uno and
  HiFive1 examples do.
- The Debug perspective's register view lists registers numbered below 64,
  so a RISC-V core's CSRs are not shown there (GDB reads them); its memory
  view starts at 0x20000000 whatever the core.
- An FE310 starts at 0x20010000, where the HiFive1's bootloader jumps: the
  mask ROM and the bootloader are not simulated.
- The RP2040's bootrom is a stand-in (`core/mcu/rp2040/boot.c`): it boots
  boot2 from flash as the bootrom does, but its function table is empty,
  so a Pico SDK image that calls the bootrom's functions (its float
  library, flash programming) does not run; the BOOTSEL button has no
  effect; the XIP cache is not modelled.

## Updating this page

Record source revision, configuration, tool versions, runner results, cases,
skips and artifacts together. Move a feature to implemented only after
inspecting the code; call a requirement verified only after its named
evidence passes.
