# M5 verification: what was built and how it was checked

This page records M5 as it is built: turning a run into a repeatable,
measured check. A run is recorded and replays identically; a recording
becomes a scenario; scenarios are edited and run in the workbench; the
firmware's coverage and profile come from the cores themselves; a CI runs
the simulator from its command line or from Python; and EmbLinkRTOS's test
suite runs on EmbSIM as an emulated target. It is the source for the M5
rows of [STATUS.md](../STATUS.md); update both together.

## What exists

| Area | Files | Contract |
|---|---|---|
| Recordings | `core/session/record.c`, `core/util/sha256.c`, `schemas/recording.schema.json` | SPEC-012 §1: the project's and images' SHA-256, the inputs (`immediate` when given at the paused instant), observations in time order (each core's interrupt entries from the exception hook every core calls, digital probe transitions, serial text, diagnostics, the end); `from_ps` and `from_seq` for a recording started in a running session, which begins with the digital probes' levels |
| Replay | `record.c`, `cli/commands.c` (`embsim replay`), `rpc.c` (`record.replay`) | SPEC-012 §2: from reset, the inputs at their times (an immediate one after running to it), to the recording's start and its end, the observations compared in order; the first divergence with both observations (exit 4); a changed project or image refused (exit 2) unless `--allow-drift` |
| To a scenario | `record.c` (`esk_record_to_scenario`), `embsim record to-scenario`, `record.to_scenario` | SPEC-012 §3: the inputs, each digital probe's level at the end and its edges from the recording's start, each serial probe's text; times in units |
| Analyses | `core/mcu/analysis.c`, `coverage.c`, `profile.c`, `stack.c`, `fault.c`, `dwarf.c` (`.debug_frame`); the cores' `esk_mach_exc` and `esk_mach_exc_return`; `smp.c` | SPEC-009 §12: extracted from EmbCC's analyses; every core stepped through `esk_an_step` when a machine has analyses; coverage by address mapped to lines; the profile on a shadow stack per core; the stacks' depths from the cores' stack pointers, by function from the same frames, an overflow past the link's limit a diagnostic; each fault decoded with a backtrace by the CFI |
| Analyses in the session | `core/session/measure.c`, `rpc.c` (`analysis.*`), `cli/commands.c` (`--coverage`, `--profile`, `--stack`, `--faults`) | SPEC-008 §5: kept across reloads and new images; the run report's summaries |
| Emulated target | `cli/commands.c` (`--target`, `--image`, `--until-text`, `--console`), `core/mcu/mcu.c` (`esk_mcu_console`) | docs/user/ci.md: a catalog board running an image with no project; the run follows the consoles a millisecond at a time |
| Python client | `python/embsim` (`session.py`, `runner.py`, `transport.py`, `pytest_plugin.py`) | SPEC-008 over stdio; the CLI's exit codes; `tools/embsim_client` re-exports the transport |
| Scenario files | `rpc.c` (`scenario.get`, `scenario.save`) | SPEC-008 §5: paths inside the project's folder only; `add` lists a new one in the project |
| Workbench | `ui/apps/workbench/src/perspectives/verify/*`, `stores/verify.ts` | SPEC-011 Verify: the scenario editor, recordings, the report, coverage |
| EmbLinkRTOS | EmbLinkOs/EmbLinkRTOS `tools/embsim/run_embsim.py`, `EMB_TEST_EMULATOR`, the `avr-uno-embsim` preset, the `avr-embsim` CI job | its SIM-002: the conformance suite on an emulated target, here EmbSIM's Uno beside QEMU's |

## How it was checked

| Check | Where | What it shows |
|---|---|---|
| Determinism and replay | `record_replay` (`tests/protocol/test_replay.py`) | every example's first scenario recorded twice in two processes with equal inputs and observations; each replays with every observation matching; an observation moved by 1 ps diverges there with both values; a changed image is refused and replays with `--allow-drift`; a recording as a scenario passes; `record.replay` over the protocol |
| Interactive recordings | `python_client` | recordings made from the start, from 7 ms into a running session, and after an input given before recording each replay identically, and each passes as a scenario run from reset |
| Coverage, profile and stacks | `analysis` (`tests/protocol/test_analysis.py`) | the nine firmware examples (Cortex-M3, M0+ on both cores, M4F, AVR, RISC-V, MIPS32, PIC24, two boards at once) measured twice alike; the profile's functions add up to the instructions and cycles; lcov agrees with the JSON line for line; the collapsed stacks add up to the cycles; each board's stack within its limit; gcov-style text with the source; a recursion 40 calls deep on a 2 KiB stack (`tests/firmware/stack-overflow`) raises `mcu.stack_overflow` once in `deep`, main's depth its frame plus the recursion's; a load from an address nothing answers (`tests/firmware/fault`) is one `mcu.fault`, a HardFault FORCED from PRECISERR with BFAR, backtraced peek, read_sensor, main, Reset_Handler, handled by `Default_Handler`; a project without firmware refused; `analysis.*` over the protocol |
| Emulated target | `run_target` (`tests/protocol/test_target.py`) | `--console` streams the Uno's console; `--until-text` ends at the same millisecond on two runs and exits 1 when the text never comes; `--image ID=FILE` and an image for another core refused |
| Python client | `python_client` (`python/tests/test_session.py`) | a session's time, inputs, serial text, a probe's samples, a scenario, a recording replayed and made a scenario, coverage, the profile, the debugger; `run()` with a recording and lcov, `replay()` matching and diverging (exit 4) |
| EmbLinkRTOS | `ctest --preset avr-uno-embsim` in EmbLinkRTOS | its nine Arduino Uno conformance tests (smoke, first execution, thread, sched, time, irq, sem, mutex, notify) pass on EmbSIM's Uno in 4.4 s, avr-gcc 7.3 images unchanged; in its CI, EmbLinkOs/EmbLinkRTOS#12's `avr-embsim` job builds EmbSIM's `main` and passes them |
| Cost | by hand, release build | the Pico's two-core scenario in 0.89 s, 0.95 s with coverage, 0.88 s with the profile, 0.85 s with both: within the run-to-run noise |
| Builds | GCC, Clang, TinyCC, ASan/UBSan, a GCC release build with `_FORTIFY_SOURCE`; MSVC in CI | the new code builds warning-free under each |
| Gallery | `scripts/screenshots.mjs` | the [M5 shots](../gallery/README.md): a scenario run in the editor, coverage of the Blue Pill's firmware (light and dark), a recording replayed and made a scenario |

## Decisions taken while building

- **The analyses watch the core, not the bus.** A step through
  `esk_an_step` notes the pc and the machine's counts around the core's own
  step; with no analysis the run loop is unchanged. Each core reports its
  exception entries with their return address and its exception returns, so
  a handler's time is its own and the profile's call paths survive
  interrupts, on every family.
- **A call into the middle of the same function is a branch.** GCC uses BL
  as a far branch in a large Thumb-1 function (the Pico's 6 KB `main`);
  counting it as a call stacked frames that never returned.
- **Code labels are functions.** XC16's and libgcc's assembly routines
  (`___udivsi3`) have global labels and no FUNC symbols; without them their
  time went to the function before them.
- **A recording made in a running session replays from its start.** It
  begins with the digital probes' levels; replay runs from reset to its
  start, then compares. An input given while the session was paused at an
  instant is marked `immediate`, and replay runs to that instant before
  applying it, as the session did: applied as a scheduled event instead it
  lands a picosecond earlier.
- **A loaded session runs as a reset one does.** A pin settling at time 0
  woke a just-loaded core a picosecond before its reset's first quantum,
  so a freshly loaded Blue Pill ran one 8 MHz cycle (125 ns) apart from a
  reset one; a core not yet started since its reset now starts when the
  reset said. `record.start` before anything has run also resets first, so
  the recording and its replay both start from reset.
- **A stack's top is the program's.** The Pico's boot ROM runs on a stack
  of its own above the image's; the top is the highest the stack pointer
  was while the pc was in the image's functions, read before and after each
  step so a reset's value counts. An overflow is a diagnostic and the run
  goes on: the part has no guard, and a scenario forbidding
  `mcu.stack_overflow` makes it a failure.
- **A fault is decoded after the step that took it.** EmbSIM's cores
  report an exception's entry before stacking it (EmbCC's after), so the
  report waits for the step's end, when the frame, EXC_RETURN, mepc and EPC
  are in place and the shadow stack is still the faulting code's.
- **The emulated target is the command line.** `--target --image
  --until-text --console` makes `embsim run` a drop-in for a QEMU machine
  in a test runner: EmbLinkRTOS's runner for EmbSIM is its QEMU runner with
  the command changed.

## Known gaps

The PIC24's fault decoding is not exercised by a test (CI has no XC16); analog observations and full event traces are not recorded
(SPEC-012 §5); the scenario editor's assertion
expectations are edited as JSON.
