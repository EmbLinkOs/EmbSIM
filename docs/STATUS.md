# Project status and evidence

**Reviewed:** 2026-10-16 at the M1 implementation on branch `claude/affectionate-allen-40uv0o`. This inventory is
maintained by hand until evidence tooling exists. It is not a generated
support matrix. Specifications define target contracts; their acceptance
does not mean the described behaviour exists.

## Implemented subset

| Area | Current implementation | Evidence and limits |
|---|---|---|
| Design | Architecture 01 to 09 (ADR-001 to ADR-020), SPEC-001 to SPEC-014, 91 requirements | Reviewed documents; `tools/docs/check_docs.py` passes |
| Repository | CMake presets, Makefile wrapper, CI matrix, documentation and policy workflows, templates, ruleset JSON, policy and banned-construct checkers | Local runs of the checkers; remote workflow results are recorded here when observed |
| Core: scheduler and time | Rational clocks, class-ordered event heap, delta-cycle bound, cancellation by generation (SPEC-002) | `test_sched`: ordering, cancellation, overflow, limits, 72 MHz and 32.768 kHz clocks exact over 10^9 edges |
| Core: nets and digital engine | Pin models and logic families, four-value resolution with strengths, contention and floating diagnostics, inertial delays, islands and bridges, the model API; gates, flip-flop, sources, pulls, transmission-gate switches, indicator, probe, UART virtual terminal (SPEC-003) | `test_engine`: resolution, floating inputs, gate timing, inertial pulses, clock, switch, UART loopback |
| Core: analog engine | MNA per island, Newton-Raphson, deterministic dense LU, gmin and source stepping, trapezoidal with LTE control and backward-Euler restarts, breakpoints, bridge crossings bisected to the configured resolution; resistor, capacitor, inductor, V and I sources (dc, pulse, sine, pwl, exp), switch with bounce, potentiometer, diode, LED, Zener, NPN/PNP, NMOS/PMOS, op-amp, regulator, pull (SPEC-004) | `test_analog`: 1121 checks (including multi-island stepping and regulator rails) and seven analytic fixtures under GCC and Clang; ngspice goldens pending (not installed); BDF2, junction capacitances, sparse solver not implemented |
| Core: models and catalog loading | Registry of primitives with merged digital and electrical tiers; libraries by path; component instantiation with parameter validation, presets, `$param` pin models and composite expansion through ports; boards as composites whose MCU runs firmware (SPEC-005, 006, 007 subset) | `test_session`: temporary library, loader, inputs, snapshots, scenario assertions, determinism across sessions, composite expansion, bad project |
| Core: session and protocol | Command path with virtual timestamps and the input log, probes (digital on change, analog and value at a rate, thresholded digital on analog nets), snapshots and topology, pacing slices, notifications, scenarios with assertions and the run report, the JSON-RPC method table over in-process, stdio and WebSocket (SPEC-008) | `test_session` (in-process), `tests/protocol/run_conformance.py` (nine cases on stdio and WebSocket, transports compared), `test_host` (files, sockets, SHA-1, base64, frames) |
| MCU runtime | Cortex-M3 core extracted from EmbCC (Thumb-2, NVIC, SCB, SysTick, DWT, FPU), bus with bit-band and aliases, ELF and Intel HEX loaders, semihosting; register files from EmbSIM JSON (ADR-020); STM32F1 models: RCC, GPIO, AFIO, EXTI, USART, TIM (time base), FLASH, PWR; runs in quanta on the scheduler with pins as signal events (ADR-019); USART frames on the TX pin and decoded from the RX pin; NRST and BOOT0 (SPEC-009) | `test_mcu` (11022 checks): EmbCC's Blue Pill golden fixture with five documented USART timing deltas, the same output from a description converted from ST's SVD, serial input, a breakpoint-and-continue run equal to an uninterrupted one (MCU-006); `examples/bluepill-button-led` runs firmware built by Clang and by EmbCC |
| Debug | Breakpoints by address, symbol, or file and line; watchpoints; step, halt, continue; registers and memory read and write; Thumb disassembler; ELF symbols; DWARF 2 to 5 line tables; `debug.*` methods; GDB remote protocol server (`embsim gdb`) | `test_mcu` (source lines), `tests/protocol/test_gdb.py` (registers, memory, breakpoints, step, continue over RSP); LLDB debugged the example through `embsim gdb` in this workspace |
| Toolchains | Detection from the profiles' version commands, builds by template expansion (`embsim build`, `embsim toolchains`, `toolchain.detect`, `toolchain.build`) (SPEC-010 subset) | `tests/protocol/test_build.py`; the EmbCC and Clang profiles built the example here; the gcc-arm, TinyCC and XC profiles are not exercised (not installed) |
| CLI | `run`, `validate`, `serve`, `sidecar`, `catalog`, `board`, `models`, `gdb`, `build`, `toolchains`, `version` | `tools/ci/run_examples.py`: four examples validate and pass their scenarios (button-led 8/8, button-transistor-led 11/11, rc-filter 7/7, bluepill-button-led 16/16 with its firmware, the LED driven through an NPN transistor as 01 §8 describes) |
| Catalog | 37 components with symbols, artwork and 54 fixtures; STM32F103C8 MCU with its register description and LQFP48 package; Blue Pill board with artwork; eight toolchain profiles; JSON Schemas for every kind; the Python validator with semantic checks | `tools/validate.py --all`: 125 files, 0 errors; the tools' unit tests: 108 tests; the STM32F103C8's register description written from RM0008 (ADR-020) |
| Workbench | TypeScript workspace: typed protocol client and mock session, design system with light and dark themes, schematic canvas, board view with hotspots, oscilloscope, logic analyzer, serial terminal, meters, the four perspectives (the Debug perspective: source, disassembly, registers, breakpoints, memory, peripherals and console; Verify reports "not available"), command palette, platform adapter; Tauri 2 desktop shell with the engine as sidecar | `pnpm typecheck`, `lint`, `test` (56 tests incl. axe), `build` pass; driven in Chromium against `embsim serve` on the transistor example (live net voltages, a 1.000 kHz collector waveform, 6.83 mA LED current) and on the Blue Pill example (a breakpoint on `EXTI0_IRQHandler` hit by a button press, stepped, shown at `main.c:65`). The desktop shell has not been compiled (no WebKitGTK development files here) |

Observed on 2026-10-16 in this workspace: Ubuntu 24.04, GCC 13.3 and Clang 18.1, CMake 3.28, Python 3.13, Node 22. `ctest` on the GCC Debug build: 14 of 14 targets pass (test_analog, test_engine, test_host, test_json, test_sched, test_session, test_mcu, tools_unittests, docs_integrity, banned_constructs, protocol_conformance, gdb_server, firmware_build, examples_headless); the same 14 pass on the Clang build. The CI matrix (Linux with GCC, Clang and TinyCC; macOS with Clang; Windows with MSVC; the workbench; tools and catalog; documentation; repository policy) was observed green on the M0 pull request and on the M1 pull request (EmbLinkOs/EmbSIM#2, head 9bc3d42, 2026-10-16), which ran the Blue Pill golden fixture, the GDB server and the firmware build on all three platforms.

## Specified but unimplemented

MCU families beyond the STM32F1 and cores beyond Cortex-M3/M4 (SPEC-009,
M2 onwards); the ADC, SPI, I2C, DMA and timer capture/compare and PWM
output models (their registers hold values and raise
`mcu.unsupported_access`); DWARF variables (`debug.variables`); the GDB
server over the session protocol (`debug.gdb`); multiple cores per session
scheduler beyond one per board; replay (SPEC-012 beyond input recording),
Lua and native models (SPEC-013), library installation (SPEC-014 beyond
loading by path), topology transactions, binary waveform frames (samples
are JSON arrays with `encoding: "json"`), `catalog.validate` in the core
(the Python tool does it), the Verify perspective, installers, importers
beyond `svd2regs.py`.

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

## Updating this page

Record source revision, configuration, tool versions, runner results, cases,
skips and artifacts together. Move a feature to implemented only after
inspecting the code; call a requirement verified only after its named
evidence passes.
