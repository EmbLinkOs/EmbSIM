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


### UI-024  Wired instruments and observed electrical feedback
**Statement.** Wired instrument cards shall display configured impedances, lead nets and engine readings with locate/trace actions. Schematic net selection shall highlight all named-net terminals and reveal horizontal pin names with observed logic states. Live warnings shall distinguish high impedance, unknown inputs and confirmed opposing known digital drives; no analog voltage alone shall imply floating.
**Rationale.** The diagram and measurement tools must expose electrical loading and current connection problems.
**Status.** Accepted 2026-10-26
**Verification.** Native electrical feedback protocol checks, scene feedback tests, wired-instrument action tests and browser schematic selection checks; native loading bench captures.
**Trace.** SPEC-008 Net observation; SPEC-011 Scene rendering; docs/engineering/instrument-loading.md


### UI-025  Native live schematic symbols and controls
**Statement.** Design and Run shall render declared schematic display bindings from native component observations, keep instance styles independent, restore default display state when observations disappear, and operate declared inputs independently of editing. Momentary controls shall release on interruption. Run shall center the schematic without loading optional 3D assets. Ideal A/B measurement leads shall compare finite solved voltages without modifying connectivity or claiming physical loading.
**Rationale.** Board interaction, visible peripheral state and measurements must be possible directly in the electrical drawing.
**Status.** Accepted 2026-10-26
**Verification.** Native live circuit and browser acceptance, scene/store/control fixtures, component binding validation and existing instrument loading checks.
**Trace.** SPEC-005 §8; SPEC-011; docs/engineering/live-schematic.md


### UI-026  Sampled acquisition and movable observation probes
**Statement.** Triggered captures shall qualify native sampled edges, require complete source prehistory, wait for participating producer frontiers and preserve immutable completed records during continued simulation. Source movement shall preserve probe identity and subscriptions while separating old and new histories, without altering connectivity/loading. Pointer cancellation and keyboard retargeting shall be supported. Missing observations shall not become zero or future held readings.
**Rationale.** A useful interactive schematic needs reproducible timing evidence and movable observations with explicit electrical meaning.
**Status.** Accepted 2026-10-26
**Verification.** Acquisition and waveform unit fixtures, native probe movement protocol tests, loaded RC analytic scenario and native browser frozen-export/retarget workflow.
**Trace.** SPEC-008 §5; ADR-038; docs/engineering/schematic-acquisition.md


### UI-027  Editable hierarchy, reusable blocks and native tracing
**Statement.** The workbench shall author shared project block definitions and their instance sheets, expose explicit ports, extract connected selections, support validated undo/redo and preserve saved hierarchy. Concrete instance interaction and cross-sheet tracing shall use native qualified components and net aliases. Uninstantiated definitions shall not display another instance's live state. Drawing changes shall preserve time; structural changes shall report simulation restart.
**Rationale.** Reusable circuits need explicit electrical scope and coherent editing rather than visual grouping alone.
**Status.** Accepted 2026-10-26
**Verification.** Native loaded/isolated divider calculations, shared edits and reverse edits, failed-transaction retention, native browser navigation/port/extraction workflow, pure editing fixtures and cross-platform browser workflow.
**Trace.** SPEC-007; SPEC-008; ADR-039; docs/engineering/editable-hierarchy.md


### UI-028  Project-wide reliability and electrical review
**Statement.** Hierarchical editing shall preserve scope when copying selections, carry reusable dependencies, author typed parameter declarations and propagate renames to bindings/overrides. Removal of referenced ports/parameters shall reject with actionable information. Ordered bus groups shall retain separate electrical nets. Search and checks shall navigate concrete instances and shared definitions. Advisory metadata checks shall distinguish possible driver conflicts from confirmed native contention and export their scope and locations.
**Rationale.** Larger reusable circuits need predictable edits and inspectable electrical evidence rather than drawing appearance alone.
**Status.** Accepted 2026-10-27
**Verification.** Pure topology/parameter/bus/search/ERC fixtures, browser editing workflow and native loaded-divider GUI parameter/bus acceptance.
**Trace.** SPEC-007; SPEC-008; SPEC-011; ADR-040; docs/engineering/schematic-reliability.md


### UI-029  Native loaded instrument configuration and acquisition
**Statement.** Run shall configure actual instrument impedance/leads through validated transactions, distinguish these changes from ideal observation movement, state restart/acquisition effects, protect bound parameters and stale drafts, and preserve time for unchanged settings. Repeated project instances shall expose qualified native readouts/traces. Signed voltage/current observations shall support declared sample rates and sampled acquisition without introducing another electrical load.
**Rationale.** Measurement configuration must affect the native circuit and preserve the distinction between loading, sampling and display.
**Status.** Accepted 2026-10-27
**Verification.** Instrument configuration/card fixtures, native loading/differential/shunt/generator/preset/frozen-capture browser acceptance and independent two-instance native scenario.
**Trace.** SPEC-008; SPEC-011; ADR-041; docs/engineering/loaded-instrument-controls.md


### UI-030  Native assembly and model reports
**Statement.** Schematic reports shall group concrete project parts using native resolved configurations, retain catalog packages/boards as whole assemblies, separate their compiled model records, display procurement/qualification limits and export verifiable source/BOM artifacts. References shall select their exact schematic instance. Changed project/policy shall invalidate stale export actions; native report reads shall preserve simulation state.
**Rationale.** Engineering deliverables need correct package counts and traceable native configuration rather than symbol counts or inferred defaults.
**Status.** Accepted 2026-10-27
**Verification.** Native manifest protocol/CLI checks, schema/hash/CSV fixtures, stale snapshot browser workflow and native GUI/CLI/offline verification acceptance.
**Trace.** SPEC-008; SPEC-011; ADR-042; docs/engineering/engineering-reports.md


### UI-031  Explicit library baseline review
**Statement.** Engineering reports shall review native library inputs and differences before pinning, checking, replacing or removing a project lock. Metadata edits shall be undoable, preserve the simulation clock and retain the previous project on validation failure. Demo sessions shall not claim native lock success.
**Rationale.** Reproducibility needs a reviewable baseline rather than silent automatic lock updates.
**Status.** Accepted 2026-10-27
**Verification.** Native review/pin/check/remove/undo/export browser acceptance, transaction checks and demo browser workflow.
**Trace.** SPEC-008; ADR-043; docs/engineering/library-locks.md


### UI-032  Native schematic version requests
**Statement.** Engineering reports shall expose loaded and requested revisions with native satisfaction evidence and allow explicit undoable request changes. Shared-definition scope and successful restart/acquisition effects shall be stated before Apply. Rejected edits shall retain the prior circuit/clock and show the native diagnostic reason; demo/legacy evidence shall not imply native compatibility validation.
**Rationale.** Engineers need to repair and review dependency contracts at the schematic references they affect.
**Status.** Accepted 2026-10-27
**Verification.** Diff/evidence/error fixtures and native shared-sheet rejection, restart, exported satisfaction and Undo browser acceptance.
**Trace.** SPEC-008; SPEC-014; ADR-044; docs/engineering/library-versions.md


### UI-033  Native variant review and recovery
**Statement.** The schematic shall author named population/value alternatives, preview isolated native validation and resolved BOMs without disturbing time, explicitly restart on verified activation, retain DNP terminals and permit return-to-base. Derived circuit edits shall be refused; optional DNP BOM rows shall have zero quantity.
**Rationale.** Assembly alternatives must change the real compiled circuit while preserving an inspectable, recoverable source.
**Status.** Accepted 2026-10-27
**Verification.** Native protocol/CLI and actual native browser acceptance, offline validation, population/edit/report fixtures.
**Trace.** SPEC-008; SPEC-014; ADR-045; docs/engineering/assembly-variants.md


### UI-034  Source-bound schematic sweep review
**Statement.** The schematic shall configure numeric parameter ranges and voltage targets, plot available native samples with gaps for missing/failed results, retain diagnostics and source identities, and export CSV/JSON without disturbing the live clock. Demo transport shall refuse fabricated analysis.
**Rationale.** Analysis must expose its physical loading, evidence and supported method rather than manufacture confidence from a chart.
**Status.** Accepted 2026-10-27
**Verification.** Native loaded-divider/RC equations, independent repeats and failures; CLI/RPC, actual native browser and both offline backends.
**Trace.** SPEC-004; SPEC-008; ADR-046; docs/engineering/parameter-sweeps.md


### UI-035  Native transient waveform review
**Statement.** The schematic shall review source-bound native traces with visibility, sample-window zoom, exact sample cursors, explicitly ideal differential subtraction, raw timestamps and exports. Missing values shall break traces and incomplete captures shall remain explicitly incomplete.
**Rationale.** A useful circuit waveform needs actual timing/loading, recoverable evidence and clear limits instead of interpolated or fabricated confidence.
**Status.** Accepted 2026-10-27
**Verification.** Native RC/loading equations, warm-up/history preservation, maximum grid, DNP and scripted failure; actual native browser, UI and both offline backends.
**Trace.** SPEC-004; SPEC-008; ADR-047; docs/engineering/transient-capture.md
