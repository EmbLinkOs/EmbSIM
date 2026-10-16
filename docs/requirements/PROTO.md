# PROTO - Session Protocol

Group `PROTO`. Design: 05 §1; SPEC-008; ADR-007.

Each requirement has a statement, a rationale, a status (Accepted, FUTURE),
a verification method and a trace to the design. Verification names the
test or review that establishes it; STATUS.md records whether it passed.

---

### PROTO-001  Method coverage
**Statement.** Every protocol method shall have conformance request and response fixtures; a method without fixtures is not part of the protocol.
**Rationale.** Stable contract.
**Status.** Accepted 2026-10-15
**Verification.** tests/protocol in CI.
**Trace.** SPEC-008 §10

### PROTO-002  Observation streams do not block control
**Statement.** Waveform and observation streams shall drop with gap markers rather than delay control requests.
**Rationale.** Responsiveness.
**Status.** Accepted 2026-10-15
**Verification.** Load test with a slow client.
**Trace.** SPEC-008 §7

### PROTO-003  Inputs acquire virtual time in the session
**Statement.** Every input shall be timestamped by the session at the time of application and reported back with its sequence.
**Rationale.** Recording correctness.
**Status.** Accepted 2026-10-15
**Verification.** Protocol tests; recording tests.
**Trace.** SPEC-008 input.*

### PROTO-004  Versioned with capabilities
**Statement.** session.open shall negotiate protocol version and capabilities; a major mismatch is an error.
**Rationale.** Evolution.
**Status.** Accepted 2026-10-15
**Verification.** Tests.
**Trace.** SPEC-008 §2


### PROTO-005  Static assets during a session
**Statement.** Static HTTP requests shall be serviced while a WebSocket session is active, preserving that session's notification channel and host/origin restrictions. A second session upgrade shall receive a busy response; incomplete auxiliary request heads shall have a bounded deadline.
**Rationale.** Reloads must not deadlock while the previous document's session is still attached.
**Status.** Accepted 2026-10-16; cooperative auxiliary HTTP handling implemented.
**Verification.** `http_assets` tests static loading during a running session, time advancement, host/origin rejection, second-session refusal and incomplete request handling.
**Trace.** SPEC-008 §1; ADR-022
