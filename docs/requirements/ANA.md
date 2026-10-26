# ANA - Analog Engine

Group `ANA`. Design: 03 §5, §6; SPEC-004.

Each requirement has a statement, a rationale, a status (Accepted, FUTURE),
a verification method and a trace to the design. Verification names the
test or review that establishes it; STATUS.md records whether it passed.

---

### ANA-001  MNA with Newton-Raphson
**Statement.** Each analog island shall be solved by modified nodal analysis with Newton-Raphson iteration to the tolerances of SPEC-004 §2.
**Rationale.** Standard, validated method.
**Status.** Accepted 2026-10-15
**Verification.** Fixtures compared to ngspice within tolerances.
**Trace.** SPEC-004 §2

### ANA-002  Operating point robustness
**Statement.** Operating-point failure shall fall back to gmin and source stepping before reporting no convergence with the worst nodes named.
**Rationale.** Usability on nonlinear circuits.
**Status.** Accepted 2026-10-15
**Verification.** Fixtures: BJT and MOSFET switches, diode clamps.
**Trace.** SPEC-004 §3

### ANA-003  Adaptive transient with breakpoints
**Statement.** Transient integration shall control step size by local truncation error and honour source breakpoints exactly.
**Rationale.** Accuracy on PWM and edges.
**Status.** Accepted 2026-10-15
**Verification.** Fixtures: PWM into RC, pulse edges, LTE checks.
**Trace.** SPEC-004 §4

### ANA-004  Device models v1
**Statement.** The primitives of SPEC-004 §7 shall be implemented with their parameters, defaults and ranges.
**Rationale.** Acceptance circuits.
**Status.** Accepted 2026-10-15
**Verification.** One fixture per primitive minimum.
**Trace.** SPEC-004 §7

### ANA-005  Bridges
**Statement.** Thevenin drivers, comparators with hysteresis, pulls, ADC terminals and DAC sources shall be inserted from pin models as SPEC-004 §8 specifies.
**Rationale.** Mixed signal with MCUs.
**Status.** Accepted 2026-10-15
**Verification.** Fixtures with crossing counts and times.
**Trace.** SPEC-004 §8

### ANA-006  Deterministic pivoting
**Statement.** Pivot order shall depend only on topology so results are reproducible on a platform.
**Rationale.** Replay.
**Status.** Accepted 2026-10-15
**Verification.** Determinism suite includes analog projects.
**Trace.** SPEC-004 §5

### ANA-007  ngspice is not a runtime dependency
**Statement.** The product shall not link or spawn a SPICE engine; fixtures are generated offline and committed with versions.
**Rationale.** ADR-004.
**Status.** Accepted 2026-10-15
**Verification.** Build has no such dependency; fixture provenance files reviewed.
**Trace.** ADR-004; SPEC-004 §10

### ANA-008  Overstress diagnostics
**Statement.** Exceeding a device's declared absolute maximum voltage or current shall raise a warning naming the device and the value.
**Rationale.** Engineering value.
**Status.** Accepted 2026-10-15
**Verification.** Fixtures.
**Trace.** SPEC-004 §11


### ANA-009  Electrically driven discrete RGB indication
**Statement.** A four-lead RGB LED shall load its three channels through independently oriented diode junctions and external resistors under electrical policy. Observations shall expose channel current, normalized luminance and illustrative mixed color. Functional/timed indication shall require a complete known logic path. Display rendering shall consume observations. Generic fits shall not imply manufacturer, spectral, thermal, damage or perceived PWM qualification.
**Rationale.** A driven GPIO level alone does not establish LED current or light.
**Status.** Accepted 2026-10-25
**Verification.** Common-anode/cathode nominal/current-limited/reverse/unpowered fixtures, all binary colors and floating-common fixtures, switch-driven acceptance scenario and actual renderer material tests.
**Trace.** SPEC-003 The digital engine; SPEC-004 Device models; ADR-030; docs/engineering/circuit-construction.md
