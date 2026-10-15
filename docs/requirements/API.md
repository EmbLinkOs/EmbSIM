# API - Core API

Group `API`. Design: SPEC-001.

Each requirement has a statement, a rationale, a status (Accepted, FUTURE),
a verification method and a trace to the design. Verification names the
test or review that establishes it; STATUS.md records whether it passed.

---

### API-001  Status returns and strong exception safety
**Statement.** Every fallible function shall return es_status and leave state unchanged on failure.
**Rationale.** Embedding.
**Status.** Accepted 2026-10-15
**Verification.** API tests inject failures.
**Trace.** SPEC-001 §3

### API-002  ABI versioning
**Statement.** Public headers shall carry an ABI version incremented on incompatible change; within a major version functions are only added.
**Rationale.** Embedders.
**Status.** Accepted 2026-10-15
**Verification.** Header diff check in CI.
**Trace.** SPEC-001 §8

### API-003  The protocol is available in process
**Statement.** es_session_rpc shall execute any protocol method without a transport.
**Rationale.** CLI and embedders.
**Status.** Accepted 2026-10-15
**Verification.** Protocol conformance over the in-process transport.
**Trace.** SPEC-001 §6

### API-004  Headers compile as C99, C11, C++17 and MSVC C11
**Statement.** Public headers shall compile in all four modes without warnings.
**Rationale.** Portability.
**Status.** Accepted 2026-10-15
**Verification.** CI compile checks.
**Trace.** SPEC-001 §9

