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


### CLI-006  Library lock review
**Statement.** Lock capture/check shall emit inspectable JSON without editing project files, allow drift review through a temporary copy, preserve normal loader enforcement, and return distinct match, difference and usage/load failure exit codes.
**Rationale.** A rejected locked project needs a deliberate repair path usable in automation.
**Status.** Accepted 2026-10-27
**Verification.** Native CLI/RPC parity, matching/drift/missing baseline and explicit recapture checks.
**Trace.** SPEC-014; ADR-043; docs/engineering/library-locks.md


### CLI-007  Validated assembly variant export
**Statement.** Variant list/render shall inspect named alternatives and emit only natively validated derived projects without modifying source files. Native RPC shall share materialization, base-hash checks and recovery behavior.
**Rationale.** Assembly alternatives must change the real compiled circuit while preserving an inspectable, recoverable source.
**Status.** Accepted 2026-10-27
**Verification.** Native protocol/CLI and actual native browser acceptance, offline validation, population/edit/report fixtures.
**Trace.** SPEC-008; SPEC-014; ADR-045; docs/engineering/assembly-variants.md


### CLI-008  Native sweep artifacts and exit status
**Statement.** The sweep command shall share native RPC physics and emit source-bound ordered samples without modifying input files, distinguish complete/partial/request failure exit codes, and preserve inspectable failed points.
**Rationale.** Analysis must expose its physical loading, evidence and supported method rather than manufacture confidence from a chart.
**Status.** Accepted 2026-10-27
**Verification.** Native loaded-divider/RC equations, independent repeats and failures; CLI/RPC, actual native browser and both offline backends.
**Trace.** SPEC-004; SPEC-008; ADR-046; docs/engineering/parameter-sweeps.md


### CLI-009  Native transient prefix export
**Statement.** Transient capture CLI shall share native RPC execution, preserve source files, emit inspectable complete/partial artifacts and distinguish complete, partial and usage/load/request exit codes.
**Rationale.** A useful circuit waveform needs actual timing/loading, recoverable evidence and clear limits instead of interpolated or fabricated confidence.
**Status.** Accepted 2026-10-27
**Verification.** Native RC/loading equations, warm-up/history preservation, maximum grid, DNP and scripted failure; actual native browser, UI and both offline backends.
**Trace.** SPEC-004; SPEC-008; ADR-047; docs/engineering/transient-capture.md


### CLI-010  Stateless native spectrum export
**Statement.** The spectrum command shall transform saved captured data with explicit target/window/mean options, preserve inputs, share native RPC semantics and require no loaded original project or model execution. Invalid or incomplete records shall fail without producing a misleading spectrum.
**Rationale.** Frequency plots require defined units, sample/window semantics and independently inspectable evidence.
**Status.** Accepted 2026-10-27
**Verification.** Native analytic/direct-DFT vectors, independent NumPy checks, loaded RC reference, source-bound CLI/RPC, actual native browser and both offline backends.
**Trace.** SPEC-004; SPEC-008; ADR-048; docs/engineering/frequency-spectrum.md

### CLI-011  Source-bound native AC exports
**Statement.** The AC CLI shall share the native RPC linear phasor solver, preserve project/request inputs and export complete source-bound complex results with explicit source/reference/units. Invalid, unsupported or failed solves shall return nonzero without a successful artifact.
**Rationale.** Engineers need the same measured circuit response in the workbench and headless verification.
**Status.** Accepted 2026-10-27
**Verification.** CLI/RPC coefficient parity, analytic and ngspice cases, failed inputs and both offline artifact validators.
**Trace.** SPEC-004; SPEC-008; ADR-049; docs/engineering/ac-sweeps.md
