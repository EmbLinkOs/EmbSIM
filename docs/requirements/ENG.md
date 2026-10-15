# ENG - Engineering Process

Group `ENG`. Design: 05 (RTOS-style), docs/engineering; ADR-014.

Each requirement has a statement, a rationale, a status (Accepted, FUTURE),
a verification method and a trace to the design. Verification names the
test or review that establishes it; STATUS.md records whether it passed.

---

### ENG-001  Owner identity on commits
**Statement.** Every commit shall carry the repository owner's Git identity and no co-author or assistant trailers.
**Rationale.** The owner's rule.
**Status.** Accepted 2026-10-15
**Verification.** Repository policy check on every PR.
**Trace.** ADR-014

### ENG-002  Three stable checks
**Statement.** main shall require CI merge gate, Documentation build and Repository policy.
**Rationale.** RTOS precedent.
**Status.** Accepted 2026-10-15
**Verification.** Ruleset JSON and workflow names.
**Trace.** docs/engineering/repository-workflow.md

### ENG-003  Specs before code
**Statement.** A contract change shall update its specification and requirements in the same pull request; architecture changes need an ADR.
**Rationale.** Design-first.
**Status.** Accepted 2026-10-15
**Verification.** PR template; review.
**Trace.** 06; CONTRIBUTING

### ENG-004  STATUS.md is the support matrix
**Statement.** No document other than STATUS.md shall claim a feature is implemented or a target supported.
**Rationale.** Honesty.
**Status.** Accepted 2026-10-15
**Verification.** Docs review; CI check that README links STATUS.
**Trace.** docs/STATUS.md

### ENG-005  Coding standard enforced
**Statement.** Banned constructs shall fail CI through scripts/check-banned.py; warnings are errors on every compiler.
**Rationale.** Quality.
**Status.** Accepted 2026-10-15
**Verification.** CI lint job.
**Trace.** docs/CODING-STANDARD.md

