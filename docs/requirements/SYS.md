# SYS - System and Determinism

Group `SYS`. Design: 01, 02, 03 §7; ADR-001, ADR-002, ADR-012, ADR-016.

Each requirement has a statement, a rationale, a status (Accepted, FUTURE),
a verification method and a trace to the design. Verification names the
test or review that establishes it; STATUS.md records whether it passed.

---

### SYS-001  One engine for every client
**Statement.** The CLI, the workbench and embedders shall drive the same core through the same session protocol and obtain identical observations for identical ordered inputs.
**Rationale.** P1; a GUI result must be a CI result.
**Status.** Accepted 2026-10-15
**Verification.** Protocol conformance tests run the acceptance scenario through in-process and WebSocket transports and compare reports.
**Trace.** 02 §1; SPEC-008 §10

### SYS-002  Integer virtual time
**Statement.** All simulated time shall be an unsigned 64-bit count of picoseconds; no model shall read the host clock.
**Rationale.** Determinism; cross-platform equality.
**Status.** Accepted 2026-10-15
**Verification.** CI grep forbids host clock calls outside core/host; unit tests on time arithmetic and overflow.
**Trace.** SPEC-002 §1; ADR-002

### SYS-003  Documented total event order
**Statement.** Simultaneous events shall be ordered by class then insertion sequence as SPEC-002 §4 states; the order is a tested contract.
**Rationale.** Replay and multi-core determinism depend on it.
**Status.** Accepted 2026-10-15
**Verification.** Scheduler unit tests enumerate class interleavings; determinism suite.
**Trace.** SPEC-002 §4

### SYS-004  Replayable runs
**Statement.** A recording of a run's external inputs shall reproduce the same discrete observations on any supported platform.
**Rationale.** Bugs must be reproducible.
**Status.** Accepted 2026-10-15
**Verification.** tests/determinism: two in-process and two cross-process runs compare event traces; replay of committed recordings in CI.
**Trace.** SPEC-012; ADR-012

### SYS-005  No process termination from the library
**Statement.** No library code path shall call exit, abort or print to the process's standard streams; every failure is a status and a diagnostic.
**Rationale.** Embedding, multiple sessions, the workbench's stability.
**Status.** Accepted 2026-10-15
**Verification.** CI grep for exit/abort/printf in core/; API tests inject failures and assert statuses.
**Trace.** SPEC-001 §3; SPEC-009 §11

### SYS-006  Instance isolation
**Statement.** Two sessions in one process shall not share mutable state; interleaved commands on both shall yield results identical to separate processes.
**Rationale.** Multi-board projects, test parallelism.
**Status.** Accepted 2026-10-15
**Verification.** Conformance test with two sessions interleaved.
**Trace.** ADR-016

### SYS-007  Bounded resources with diagnostics
**Statement.** Every capacity (nets, events, probe memory, delta cycles, analog iterations) shall be configurable and its exhaustion reported as a named diagnostic, never as a crash.
**Rationale.** Robustness under event storms and runaway circuits.
**Status.** Accepted 2026-10-15
**Verification.** Limit tests per subsystem.
**Trace.** SPEC-001 §4; SPEC-002 §3

### SYS-008  Honest timing labels
**Statement.** Every client shall label instruction timing with the core's declared tier and never present simulator timing as hardware timing.
**Rationale.** ADR-015.
**Status.** Accepted 2026-10-15
**Verification.** UI review; CLI report includes timing_tiers.
**Trace.** ADR-015; SPEC-008 session.info

