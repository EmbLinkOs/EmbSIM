# UI - Workbench

Group `UI`. Design: 05 §3; SPEC-011; ADR-008.

Each requirement has a statement, a rationale, a status (Accepted, FUTURE),
a verification method and a trace to the design. Verification names the
test or review that establishes it; STATUS.md records whether it passed.

---

### UI-001  No simulation in the client
**Statement.** The workbench shall compute no simulation state; every value shown comes from the protocol.
**Rationale.** P1.
**Status.** Accepted 2026-10-15
**Verification.** Lint rule; code review.
**Trace.** SPEC-011 §6.8

### UI-002  Four perspectives
**Statement.** Design, Run, Debug and Verify perspectives with the panels of SPEC-011 §2 shall exist by M2.
**Rationale.** Workflow coverage.
**Status.** Accepted 2026-10-15
**Verification.** Playwright scenarios per perspective.
**Trace.** SPEC-011 §2

### UI-003  Accessibility
**Statement.** Every control shall be keyboard operable and labelled for assistive technology; both themes shall pass WCAG AA contrast.
**Rationale.** Product quality.
**Status.** Accepted 2026-10-15
**Verification.** axe in CI; keyboard-only Playwright run; manual screen-reader check per release.
**Trace.** SPEC-011 §5, §6

### UI-004  Performance budget
**Statement.** A 500-component schematic shall pan at 60 Hz and instruments shall stream without blocking control requests.
**Rationale.** Usability on real projects.
**Status.** Accepted 2026-10-15
**Verification.** Benchmark in CI on the reference machine.
**Trace.** SPEC-011 §3, §6

### UI-005  Three platforms, one codebase
**Statement.** The desktop app and the browser mode shall ship from one TypeScript codebase with a platform adapter; webview differences shall be covered by CI.
**Rationale.** ADR-008.
**Status.** Accepted 2026-10-15
**Verification.** Playwright on Linux, macOS, Windows.
**Trace.** SPEC-011 §1

### UI-006  Interactive hotspots
**Statement.** Board and component artwork hotspots shall support hold, toggle and drag gestures with keyboard equivalents and announce state.
**Rationale.** Real interaction.
**Status.** Accepted 2026-10-15
**Verification.** Playwright tests on the button and the LED.
**Trace.** SPEC-005 §8; SPEC-011 §5

### UI-007  Big-integer time
**Statement.** The client shall handle virtual time as bigint and display units without precision loss.
**Rationale.** 2^53 limit.
**Status.** Accepted 2026-10-15
**Verification.** Unit test with times beyond 2^53.
**Trace.** SPEC-011 §5


### UI-008  Component discovery and inspection before placement
**Statement.** The library shall combine categorized browsing, manufacturer/package/model filters and keyboard-accessible previews, expose model omissions and disable placement for unsupported policies. Waveforms and controls shall remain usable after component placement restarts a circuit.
**Rationale.** Users must understand a component's identity and simulation support before using it.
**Status.** Accepted 2026-10-16; initial explorer implemented.
**Verification.** Library filter tests, accessibility checks, and `tests/browser/test_library.py` against the actual engine.
**Trace.** SPEC-011 §2; SPEC-005 §8

### UI-009  Scenario draft validation
**Statement.** The scenario editor shall prevent Run and Save for invalid visible drafts, preserve invalid expectation text for correction, explain validation errors, and clear previous assertion results on edits.
**Rationale.** A successful run must describe the assertions the user sees.
**Status.** Accepted 2026-10-16; editor validation implemented.
**Verification.** Scenario validation unit tests and ScenarioEditor interaction regressions for malformed JSON, correction and invalid times.
**Trace.** SPEC-011 §2; SPEC-007 §2

### UI-010  Connected layout and schematic navigation
**Statement.** The schematic shall offer reviewable connectivity-driven placement, obstacle-aware routing, alignment/distribution and opt-in arrangement after connecting, preserving electrical data and whole-operation undo. Search, selection focus, net spotlight and a navigator shall support large sheets; stale previews shall not apply.
**Rationale.** Circuit authors need legible diagrams and fast navigation without losing manual work or changing circuit behavior.
**Status.** Accepted 2026-10-16; Schematic Studio implemented with documented heuristic limits.
**Verification.** Scene geometry/layout tests, layout transaction-boundary test and `tests/browser/test_schematic.py` against the native engine.
**Trace.** SPEC-011 §2; SPEC-007 §1; SPEC-008 §5
