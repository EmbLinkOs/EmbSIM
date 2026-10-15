# NET - Connectivity and Pin Models

Group `NET`. Design: 03 §3; SPEC-003.

Each requirement has a statement, a rationale, a status (Accepted, FUTURE),
a verification method and a trace to the design. Verification names the
test or review that establishes it; STATUS.md records whether it passed.

---

### NET-001  Nets join any number of pins
**Statement.** A net shall join an arbitrary number of pins of any components, boards and MCUs, with named aliases retained across merges.
**Rationale.** User circuits around boards.
**Status.** Accepted 2026-10-15
**Verification.** Loader tests with merged nets through board ports.
**Trace.** SPEC-003 §1

### NET-002  Every pin has a pin model
**Statement.** Every pin shall carry a pin model from its description or logic family; a description with neither is functional-only and refused under electrical policy.
**Rationale.** ADR-003; electrical meaning everywhere.
**Status.** Accepted 2026-10-15
**Verification.** Catalog validation; loader tests.
**Trace.** SPEC-003 §2

### NET-003  Strength resolution
**Statement.** Digital resolution shall follow SPEC-003 §3 exactly: strongest value wins, equal-strength conflict is X with a contention diagnostic, no driver is Z.
**Rationale.** Predictable logic.
**Status.** Accepted 2026-10-15
**Verification.** Unit tests enumerate strength and value combinations.
**Trace.** SPEC-003 §3

### NET-004  Floating and unknown input diagnostics
**Statement.** Sampling Z without a pull, or sampling X, shall raise a diagnostic once per episode naming the input and the net.
**Rationale.** Catches real hardware mistakes.
**Status.** Accepted 2026-10-15
**Verification.** Unit tests.
**Trace.** SPEC-003 §3

### NET-005  Island partition
**Statement.** Nets shall be partitioned into digital and analog islands by the rules of SPEC-003 §4 after load and after every topology transaction, and the partition reported to clients.
**Rationale.** Mixed signal correctness; UI explanation.
**Status.** Accepted 2026-10-15
**Verification.** Partition tests on the acceptance circuit under each policy.
**Trace.** SPEC-003 §4

### NET-006  Atomic topology transactions
**Statement.** A topology edit shall validate and apply atomically at a paused timestamp or leave the previous topology intact.
**Rationale.** Live rewiring without corrupting state.
**Status.** Accepted 2026-10-15
**Verification.** Transaction tests including failing edits.
**Trace.** SPEC-003 §6

### NET-007  GPIO pins are net pins
**Statement.** MCU signals shall be exposed as pin models in the net graph; no board or MCU shall use callbacks for pin levels.
**Rationale.** ADR-003.
**Status.** Accepted 2026-10-15
**Verification.** Code review rule; M1 acceptance scenario.
**Trace.** SPEC-009 §7

