# VER - Verification, Record and Replay

Group `VER`. Design: 04 §8; SPEC-012.

Each requirement has a statement, a rationale, a status (Accepted, FUTURE),
a verification method and a trace to the design. Verification names the
test or review that establishes it; STATUS.md records whether it passed.

---

### VER-006  Time-weighted waveform acceptance
**Statement.** Windowed mean/RMS and digital duty assertions shall integrate recorded sample-and-hold intervals over a completed, nonempty window, requiring retained coverage and known finite values. The workbench shall expose the same measurements and capture coverage.
**Rationale.** Counting samples or averaging GUI frames cannot qualify a PWM-driven electrical load.
**Status.** Accepted 2026-10-26
**Verification.** `test_waveform_measure`, `tests/protocol/test_waveform_measurements.py`, browser waveform tests and the firmware RGB example's source-derived acceptance circuit.
**Trace.** SPEC-007 §2, [PWM evidence](../engineering/indicator-pwm.md)

### VER-001  Recording of all external inputs
**Statement.** Every EXTERNAL event shall be recorded with time, sequence, target, input and value, with the identities of the project, images, catalog and engine.
**Rationale.** ADR-012.
**Status.** Accepted 2026-10-15
**Verification.** Recording tests cover each input kind.
**Trace.** SPEC-012 §1

### VER-002  Replay divergence report
**Statement.** Replay shall compare recorded observations in order and report the first divergence with both values.
**Rationale.** Debuggability.
**Status.** Accepted 2026-10-15
**Verification.** Replay tests with an injected divergence.
**Trace.** SPEC-012 §2

### VER-003  Recording to scenario
**Statement.** A recording shall convert to a scenario with assertions chosen by the user.
**Rationale.** Interactive to regression.
**Status.** Accepted 2026-10-15
**Verification.** CLI test.
**Trace.** SPEC-012 §3

### VER-005  Coverage and profile
**Statement.** A run shall report, per board, the firmware's source lines and functions executed (as lcov) and its instructions and cycles by function and call path (as collapsed stacks), deterministically.
**Rationale.** A CI run against an emulated target measures what it tested and where the time went.
**Status.** Accepted 2026-10-16
**Verification.** `tests/protocol/test_analysis.py` (`analysis` in CTest).
**Trace.** SPEC-009 §12, SPEC-008 §5

### VER-004  Differential suites
**Statement.** CI shall run EmbCC golden fixtures (per extracted family), ngspice analog fixtures and QEMU-derived expectations where available.
**Rationale.** Evidence.
**Status.** Accepted 2026-10-15
**Verification.** CI jobs.
**Trace.** 07 §2

