# TIME - Time, Clocks and Scheduling

Group `TIME`. Design: 03 §1, §2; SPEC-002.

Each requirement has a statement, a rationale, a status (Accepted, FUTURE),
a verification method and a trace to the design. Verification names the
test or review that establishes it; STATUS.md records whether it passed.

---

### TIME-001  Rational clocks without drift
**Statement.** Clock edges shall be computed from a reduced rational period with integer arithmetic so that N edges of a 72 MHz clock are exactly at floor(N*num/den) ps.
**Rationale.** No accumulated error over long runs.
**Status.** Accepted 2026-10-15
**Verification.** Unit test: 10^9 edges of several rates compared to exact big-integer results.
**Trace.** SPEC-002 §2

### TIME-002  Core quantum bound
**Statement.** A core shall never execute past the time of a pending external event; between events it shall execute at most core_quantum_ps before yielding.
**Rationale.** Inputs are seen on time; cores interleave within a bound.
**Status.** Accepted 2026-10-15
**Verification.** Scheduler tests with a mock core and injected inputs.
**Trace.** SPEC-002 §5

### TIME-003  Delta-cycle bound
**Statement.** Zero-delay propagation at one timestamp shall be bounded by max_delta_cycles with a fatal diagnostic naming the last targets.
**Rationale.** Combinational loops must not hang the engine.
**Status.** Accepted 2026-10-15
**Verification.** Unit test: a ring of inverters.
**Trace.** SPEC-002 §4

### TIME-004  Analog step coupling
**Statement.** Analog steps shall never cross a scheduled event; bridge crossings shall be located within crossing_resolution_ps and become events.
**Rationale.** Firmware sees edges at the right time.
**Status.** Accepted 2026-10-15
**Verification.** Analog fixtures with bridge crossings; event times compared to analytic values.
**Trace.** SPEC-002 §6; SPEC-004 §8

### TIME-005  Pacing does not affect models
**Statement.** A paced run and an unbounded run of the same recording shall produce identical event traces.
**Rationale.** Pacing is presentation.
**Status.** Accepted 2026-10-15
**Verification.** Determinism test at speeds 0, 0.1 and 1.
**Trace.** SPEC-002 §8

### TIME-006  Probe sampling after settlement
**Statement.** Probes shall sample only after all state at a timestamp has settled.
**Rationale.** Instruments must not show intermediate values.
**Status.** Accepted 2026-10-15
**Verification.** Unit test with delta-cycle chains and a probe.
**Trace.** SPEC-002 §4

