# CLI - Command Line

Group `CLI`. Design: 05 §2; SPEC-008 §9.

Each requirement has a statement, a rationale, a status (Accepted, FUTURE),
a verification method and a trace to the design. Verification names the
test or review that establishes it; STATUS.md records whether it passed.

---

### CLI-001  Headless runs with assertions
**Statement.** embsim run shall execute a project and scenario headless, print a JSON report with --json and exit with the documented codes.
**Rationale.** CI use.
**Status.** Accepted 2026-10-15
**Verification.** CLI tests on examples.
**Trace.** SPEC-008 §8, §9

### CLI-002  Validation command
**Statement.** embsim validate shall validate any description and explain errors with file and node.
**Rationale.** Authoring.
**Status.** Accepted 2026-10-15
**Verification.** CLI tests on invalid fixtures.
**Trace.** SPEC-014 §3

### CLI-003  Serve and sidecar
**Statement.** embsim serve shall host the workbench over WebSocket on loopback by default; embsim sidecar shall speak the protocol over stdio.
**Rationale.** Both deliveries.
**Status.** Accepted 2026-10-15
**Verification.** Protocol conformance over both.
**Trace.** SPEC-008 §1

### CLI-004  Machine and human output
**Statement.** Every subcommand shall offer human text and --json output; errors go to stderr with the embsim: prefix.
**Rationale.** Scripting.
**Status.** Accepted 2026-10-15
**Verification.** CLI tests.
**Trace.** 05 §2



### CLI-005  Native engineering manifest export
**Statement.** The manifest subcommand shall offer a human inventory/identity summary and a JSON artifact matching project.manifest. It shall preserve loaded native configurations and exact document identities, distinguish assembly packages from compiled models and reject invalid invocations/load failures without emitting a misleading artifact.
**Rationale.** Command-line engineering output must agree with the graphical report and remain independently inspectable.
**Status.** Accepted 2026-10-27
**Verification.** Native manifest CLI/RPC parity, package/default/override checks and offline artifact validation.
**Trace.** SPEC-008; ADR-042; docs/engineering/engineering-reports.md
