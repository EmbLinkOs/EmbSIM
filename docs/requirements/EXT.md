# EXT - Extensions

Group `EXT`. Design: ADR-009; SPEC-005 §5, SPEC-013.

Each requirement has a statement, a rationale, a status (Accepted, FUTURE),
a verification method and a trace to the design. Verification names the
test or review that establishes it; STATUS.md records whether it passed.

---

### EXT-001  Declarative composites
**Statement.** Components shall be composable from other components with parameter references, without code.
**Rationale.** Most parts are compositions.
**Status.** Accepted 2026-10-15
**Verification.** Composite fixtures (74HC00, a divider).
**Trace.** SPEC-005 §5

### EXT-002  Sandboxed scripted models
**Statement.** Lua models shall run with io, os, debug, package and load removed and an instruction budget per evaluation.
**Rationale.** Safety of shared libraries.
**Status.** Accepted 2026-10-15
**Verification.** Sandbox escape tests; budget test.
**Trace.** SPEC-013 §1

### EXT-003  Native plugins are explicit
**Statement.** Native plugins shall load only from paths the project marks trusted and shall declare an ABI version checked at load.
**Rationale.** ADR-009.
**Status.** Accepted 2026-10-15
**Verification.** Load tests.
**Trace.** SPEC-005 §5

### EXT-004  One conformance harness for all tiers
**Statement.** The same fixtures shall run against primitive, composite, scripted and native models.
**Rationale.** Equivalence.
**Status.** Accepted 2026-10-15
**Verification.** Harness test with a model in two tiers.
**Trace.** SPEC-013 §4

