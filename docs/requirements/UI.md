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

### UI-011  Continuous configurable canvas appearance
**Statement.** The canvas shall cover every visible coordinate with the selected theme-aware background; grid patterns shall track pan/zoom and offer adjustable spacing, weight and contrast. Appearance shall persist locally, remain usable without storage, and be configurable by keyboard without altering electrical data or editing snap.
**Rationale.** A professional drafting workspace must be consistent, legible and customizable.
**Status.** Accepted 2026-10-16; continuous backgrounds and preference controls implemented.
**Verification.** Canvas appearance/storage unit tests; Playwright persistence/pan smoke; `tests/browser/test_canvas_appearance.py` for patterns, palettes, reload and pixel checks across a panned viewport.
**Trace.** SPEC-011 §2; ADR-022

### UI-012  Manual wire waypoints
**Statement.** The schematic shall support orthogonal bend placement, live elbow flipping, removal of unfinished bends, cancellation without mutation, and one undoable completed connection. Explicit bends shall survive smart-routing and auto-arrange preferences at completion.
**Rationale.** Precise circuit drafting with pointer and keyboard.
**Status.** Accepted 2026-10-16
**Verification.** Geometry unit tests and Playwright manual-wire workflow.
**Trace.** SPEC-011 Manual wire authoring

### UI-013  Existing-track repair and contextual actions
**Statement.** Selected orthogonal wire segments shall support pointer and keyboard reshaping with preview, cancellation, fixed endpoints and preserved same-net contacts. Contextual actions shall be keyboard navigable and distinguish clearing geometry from electrical disconnection. Completed edits shall use transactional undo, reject stale revisions, and retain failed undo/redo entries.
**Rationale.** Repair real drawings without rebuilding connectivity or losing history.
**Status.** Accepted 2026-10-16
**Verification.** Wire-contact unit fixtures, history failure tests, Chromium authoring workflow and native-server schematic regression.
**Trace.** SPEC-011 Schematic authoring and repair; ADR-024

### UI-014  Precise placement and batch properties
**Statement.** The inspector shall edit snapped part/board placement and relative group offsets, and apply deliberately selected compatible shared parameters in one transaction. Mixed values shall remain explicit; invalid numeric, integer, boolean or enum values shall not commit. Layout edits shall preserve electrical data and firmware.
**Rationale.** Efficient precise editing of circuits with repeated parts.
**Status.** Accepted 2026-10-16
**Verification.** Shared-parameter contract tests, stale/read-only edit guards, Chromium and native-server batch editing with exact undo.
**Trace.** SPEC-011 Schematic authoring and repair

### UI-015  Navigable structural and drawing checks
**Statement.** A bounded cancellable worker shall report current-revision schematic structure/drawing issues with severity filtering, target navigation and applicable net-routing repair. Stale results and worker errors shall be handled explicitly. The report shall identify its limits and shall not claim electrical-rule or physical-clearance validation.
**Rationale.** Make drawing problems actionable without blocking the canvas or overstating validation.
**Status.** Accepted 2026-10-16
**Verification.** Deterministic structural/contact fixtures, Chromium check-to-repair workflow and native-server geometry-clear/check regression.
**Trace.** SPEC-011 Schematic authoring and repair; ADR-024

### UI-016  Persistent circuit organization and placement locks
**Statement.** The schematic shall support persistent flat named groups, searchable object/group navigation and placement locks for components and boards. Exclusive membership, deletion pruning and locked placement shall be enforced by native transactions. Direct edits shall reject an entire selection containing a locked member; automatic placement shall retain locked parts as fixed obstacles. Group and lock edits shall support undo, save/reload and preserve electrical data and simulation time.
**Rationale.** Organize growing circuits without disturbing completed placement.
**Status.** Accepted 2026-10-16
**Verification.** Native organization transaction/load/save regression, session unit checks, planner/store fixtures and cross-platform browser organization workflow.
**Trace.** SPEC-007; SPEC-008 Schematic organization edits; SPEC-011 Circuit organization; ADR-025

### UI-017  Exportable top-level parts inventory
**Statement.** The workbench shall export a deterministic CSV aggregation of top-level component/board models, explicit versions and explicit parameter maps with quantities and references through the platform file adapter. Export shall quote fields, escape leading spreadsheet formulas and identify the inventory's limits without implying flattened or procurement-qualified data.
**Rationale.** Review assembly composition without exposing firmware contents.
**Status.** Accepted 2026-10-16
**Verification.** Aggregation/order/quoting/formula fixtures and browser download workflow.
**Trace.** SPEC-011 Circuit organization

### UI-018  Readable graphics and direct-manipulation cues
**Statement.** Wires shall use a configurable color distinct from component bodies and a selection highlight visible during live simulation. Symbol terminals shall appear as hollow sockets supporting pointer and keyboard wiring, with pencil/grab/grabbing cursors appropriate to the action and placement locks. References, names and effective primary values with units shall use deterministic nearby label placement and a compact option. Pins and annotations shall remain visible during navigation. Appearance choices shall persist without modifying electrical project data.
**Rationale.** Make connection targets, selection and component identity understandable on a working schematic.
**Status.** Accepted 2026-10-16
**Verification.** SI/annotation collision fixtures, browser color/selection/socket/cursor/default-value/preference regression and native-engine light/dark/compact schematic workflow.
**Trace.** SPEC-011 Schematic visual language

### UI-019  Inspectable and cancellable physical bench editing
**Statement.** The bench shall identify projected connection targets by terminal name, refuse ambiguous targets, reject unknown typed endpoints and avoid duplicate jumpers. Interrupted gestures shall not commit wiring or placement, and held inputs shall be released. Inspection controls shall provide top/focused views, persistent surface/label/note preferences and an explicitly available keyboard list at compact widths. Renderer initialization/context failure shall provide a usable 2D fallback and retry. Geometry provenance shall not imply mechanical qualification.
**Rationale.** Make physical circuit construction predictable and keep inspection and recovery available across devices.
**Status.** Accepted 2026-10-18
**Verification.** Projected-contact/geometry/endpoint/preference fixtures; browser named-wire cancel/commit/undo, compact-list, preference and context-loss workflows; native bench regression.
**Trace.** SPEC-011 Scene rendering; ADR-027; ADR-030

### UI-020  Physical assembly feedback and occupied contacts
**Statement.** Workbench part and jumper authoring shall distinguish occupied breadboard holes from other holes on the same conductive strip. Per-leg insertion previews shall show destination, occupancy owner and unseated legs in text and on the bench. Rejected placement shall not mutate any part of the project. Exact bench coordinates shall be independently editable from schematic coordinates. Moving or quarter-turning a breadboard shall carry inserted parts while preserving contact identity, nets and transactional undo. These guards shall declare their limits concerning imported layouts, partial insertion, generic geometry and mechanical validation.
**Rationale.** A readable assembly must correspond to identifiable physical contacts without hidden shorts or disconnected riders.
**Status.** Accepted 2026-10-18
**Verification.** Per-leg/occupancy/atomic-swap/rotation fixtures; browser insertion rejection, free-strip construction, exact numeric carriage and undo, and occupied jumper rejection; native protocol regression.
**Trace.** SPEC-011 Physical assembly authoring; ADR-027; ADR-030

### UI-021  Deferred 3D loading and asset recovery
**Statement.** The schematic shall open without requesting optional 3D renderer assets. Opening the 3D bench shall announce loading and retain part selection through its keyboard list. Failed renderer downloads shall preserve the 2D view and keyboard editor with an explicit reload action; successful renderer initialization/context recovery shall remain independently retryable. Loading shall not alter project connectivity or simulation state.
**Rationale.** Optional 3D geometry must not delay ordinary circuit work or make a partial network failure disable the editor.
**Status.** Accepted 2026-10-18
**Verification.** Browser intercepted-request fixtures prove no renderer request during schematic use, selection across a delayed download, 2D/keyboard fallback and recovery after a fresh document load. Production bundle measurements are recorded separately from frame-time qualification.
**Trace.** SPEC-011 Renderer loading; ADR-027; ADR-028; ADR-030

### UI-022  Reachable global navigation and constrained workspaces
**Statement.** Global perspective navigation, project and utility actions shall remain reachable independently of transport settings in compact desktop windows. Long project names shall stay inside their allocated header region. Dock panels shall fit the available extent, scale requested minima only when needed, preserve stored proportions/fold choices through viewport changes, and resize from their visible boundaries. Status details shall remain keyboard-scrollable.
**Rationale.** A window resize or project name must not hide the application controls or silently replace a configured workspace.
**Status.** Accepted 2026-10-25
**Verification.** Browser long-name/all-perspective/utility workflows at 768, 900 and 1440 px; open-panel bounds, unchanged stored weights, restored proportions, constrained-edge keyboard resize and reload; actual light/dark/bench captures.
**Trace.** SPEC-011 Global workspace; ADR-008; ADR-028; ADR-030

### UI-023  Coherent wire construction and tracing
**Statement.** Existing bench jumpers shall expose named endpoint editing, color, bounded rise and intermediate route points, with pointer and keyboard access and transactional undo. Drawing edits shall preserve connectivity/time/probes; reconnection shall atomically validate endpoints and identify its simulation restart. Interrupted route gestures shall restore the prior path. Shared net selection shall trace wires and breadboard-strip contacts and display engine observations without computing simulation state.
**Rationale.** Construction, inspection and live behavior must describe one circuit across views.
**Status.** Accepted 2026-10-25
**Verification.** Wire graph/atomic failure/strip fixtures, browser appearance/undo/invalid endpoint workflows and native retained-clock/measurement/import validation tests; RGB switch acceptance scenario.
**Trace.** SPEC-007 Project; SPEC-011 Scene rendering; ADR-027; ADR-030; docs/engineering/circuit-construction.md
