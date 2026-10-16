# Project status and evidence

**Reviewed:** 2026-10-16 at the M0 implementation on branch `claude/affectionate-allen-40uv0o`. This inventory is
maintained by hand until evidence tooling exists. It is not a generated
support matrix. Specifications define target contracts; their acceptance
does not mean the described behaviour exists.

## Implemented subset

| Area | Current implementation | Evidence and limits |
|---|---|---|
| Design | Architecture 01 to 09, SPEC-001 to SPEC-014, 91 requirements | Reviewed documents; `tools/docs/check_docs.py` passes |
| Repository | CMake presets, Makefile wrapper, CI matrix, documentation and policy workflows, templates, ruleset JSON, policy and banned-construct checkers | Local runs of the checkers; remote workflow results are recorded here when observed |
| Core: scheduler and time | Rational clocks, class-ordered event heap, delta-cycle bound, cancellation by generation (SPEC-002) | `test_sched`: ordering, cancellation, overflow, limits, 72 MHz and 32.768 kHz clocks exact over 10^9 edges |
| Core: nets and digital engine | Pin models and logic families, four-value resolution with strengths, contention and floating diagnostics, inertial delays, islands and bridges, the model API; gates, flip-flop, sources, pulls, transmission-gate switches, indicator, probe, UART virtual terminal (SPEC-003) | `test_engine`: resolution, floating inputs, gate timing, inertial pulses, clock, switch, UART loopback |
| Core: analog engine | MNA per island, Newton-Raphson, deterministic dense LU, gmin and source stepping, trapezoidal with LTE control and backward-Euler restarts, breakpoints, bridge crossings bisected to the configured resolution; resistor, capacitor, inductor, V and I sources (dc, pulse, sine, pwl, exp), switch with bounce, potentiometer, diode, LED, Zener, NPN/PNP, NMOS/PMOS, op-amp, regulator, pull (SPEC-004) | `test_analog`: 1121 checks (including multi-island stepping and regulator rails) and seven analytic fixtures under GCC and Clang; ngspice goldens pending (not installed); BDF2, junction capacitances, sparse solver not implemented |
| Core: models and catalog loading | Registry of primitives with merged digital and electrical tiers; libraries by path; component instantiation with parameter validation, presets, `$param` pin models and composite expansion through ports; boards as composites with a placeholder MCU (SPEC-005, 006, 007 subset) | `test_session`: temporary library, loader, inputs, snapshots, scenario assertions, determinism across sessions, composite expansion, bad project |
| Core: session and protocol | Command path with virtual timestamps and the input log, probes (digital on change, analog and value at a rate, thresholded digital on analog nets), snapshots and topology, pacing slices, notifications, scenarios with assertions and the run report, the JSON-RPC method table over in-process, stdio and WebSocket (SPEC-008) | `test_session` (in-process), `tests/protocol/run_conformance.py` (nine cases on stdio and WebSocket, transports compared), `test_host` (files, sockets, SHA-1, base64, frames) |
| CLI | `run`, `validate`, `serve`, `sidecar`, `catalog`, `board`, `models`, `version` | `tools/ci/run_examples.py`: four examples validate and pass their scenarios (button-led 8/8, button-transistor-led 11/11, rc-filter 7/7, bluepill-button-led 5/5) |
| Catalog | 37 components with symbols, artwork and 54 fixtures; STM32F103C8 MCU and LQFP48 package; Blue Pill board with artwork; eight toolchain profiles; JSON Schemas for every kind; the Python validator with semantic checks | `tools/validate.py --all`: 121 files, 0 errors; `tools/test_validate.py`: 98 tests; the Blue Pill loads with its composite crystals and its regulator holds 3.3 V from a 5 V USB rail in `examples/bluepill-button-led` (5/5 assertions, 2 ms in ~13 ms wall) |
| Workbench | In progress on this branch (workspace, protocol client, design tokens, stores) | Not yet built or tested |

Observed on 2026-10-16 in this workspace: Ubuntu 24.04, GCC 13.3 and Clang 18.1, CMake 3.28, Python 3.13, Node 22. `ctest` on the GCC Debug build: 11 of 11 targets pass (test_analog, test_engine, test_host, test_json, test_sched, test_session, tools_unittests, docs_integrity, banned_constructs, protocol_conformance, examples_headless). The same C tests pass on the Clang build. No macOS, Windows, TinyCC or MSVC run has been observed; the CI definitions exist, their results have not.

## Specified but unimplemented

The MCU runtime (SPEC-009; a board's MCU is a passive placeholder and
loading one raises `mcu.no_runtime`), toolchain profiles beyond their
descriptions (SPEC-010), replay (SPEC-012 beyond input recording), Lua and
native models (SPEC-013), library installation (SPEC-014 beyond loading by
path), topology transactions, binary waveform frames (samples are JSON
arrays with `encoding: "json"`), `catalog.validate` in the core (the Python
tool does it), the Debug and Verify perspectives, installers, importers.

## Validation recorded in this workspace

Recorded as it happens; none yet beyond the documentation and policy checks.

## Known limits and unresolved evidence

- The Blue Pill's MCU is a passive placeholder: its pins are high-impedance
  inputs and nothing executes until the M1 runtime exists.
- Comparator-bridge crossings are delivered with the island step's start
  time as `now` for the reacting model (the crossing time is bracketed but
  not yet used as the event time); SPEC-004 §8 is therefore met only to
  within one island step.
- No remote CI run has been observed for this baseline; workflow files are
  definitions, not results.
- The GitHub ruleset in `.github/rulesets/main.json` is prepared, not
  activated; activation is an administrator action described in the
  repository workflow page.
- Instruction timing of every planned core is an estimate except AVR
  (ADR-015).

## Updating this page

Record source revision, configuration, tool versions, runner results, cases,
skips and artifacts together. Move a feature to implemented only after
inspecting the code; call a requirement verified only after its named
evidence passes.
