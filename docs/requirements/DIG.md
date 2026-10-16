# DIG - Digital Engine

Group `DIG`. Design: 03 §4; SPEC-003 §5.

Each requirement has a statement, a rationale, a status (Accepted, FUTURE),
a verification method and a trace to the design. Verification names the
test or review that establishes it; STATUS.md records whether it passed.

---

### DIG-001  Reactive evaluation
**Statement.** A digital model shall be evaluated on changes of its declared input pins, on its scheduled wake-ups and at reset, and shall schedule its outputs with its declared delays.
**Rationale.** Event-driven efficiency.
**Status.** Accepted 2026-10-15
**Verification.** Unit tests on gates, flip-flops and counters with timing.
**Trace.** SPEC-003 §5

### DIG-002  Inertial delays
**Statement.** A pending output change superseded before it takes effect shall be cancelled unless the model requests transport delay.
**Rationale.** Datasheet-like glitch behaviour.
**Status.** Accepted 2026-10-15
**Verification.** Unit test: pulses shorter than tpd.
**Trace.** SPEC-003 §5

### DIG-003  Primitive digital library
**Statement.** The core shall provide the primitives listed in SPEC-003 §5 with documented parameters.
**Rationale.** Composites need a base.
**Status.** Accepted 2026-10-15
**Verification.** Each primitive has fixtures in tests/digital.
**Trace.** SPEC-003 §5

### DIG-004  Bus signals are pins
**Statement.** Serial buses shall be modelled by components toggling pins so that probes see real edges and decoders operate on them.
**Rationale.** Logic analyzer fidelity.
**Status.** Accepted 2026-10-15
**Verification.** UART virtual terminal fixture: bytes decoded from a toggling net.
**Trace.** SPEC-003 §5

### DIG-005  Timing checks
**Statement.** Models declaring setup, hold or minimum pulse widths shall raise diagnostics on violation.
**Rationale.** Catches timing bugs.
**Status.** Accepted 2026-10-15
**Verification.** Unit tests.
**Trace.** SPEC-003 §7

