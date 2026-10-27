# CAT - Catalog and Descriptions

Group `CAT`. Design: 02 §6, §7; SPEC-005, SPEC-006, SPEC-007, SPEC-014; ADR-006, ADR-017.

Each requirement has a statement, a rationale, a status (Accepted, FUTURE),
a verification method and a trace to the design. Verification names the
test or review that establishes it; STATUS.md records whether it passed.

---

### CAT-001  Schemas for every description kind
**Statement.** Component, MCU, package, board, project, scenario, recording, toolchain and library files shall have JSON Schema 2020-12 definitions in schemas/ and be validated by the core and the Python tool.
**Rationale.** ADR-006.
**Status.** Accepted 2026-10-15
**Verification.** Schema tests with valid and invalid fixtures; unknown-field rejection.
**Trace.** ADR-006

### CAT-002  Mandatory fidelity declarations
**Statement.** A description without a fidelity declaration shall be refused.
**Rationale.** ADR-011.
**Status.** Accepted 2026-10-15
**Verification.** Validation tests.
**Trace.** SPEC-005 §6

### CAT-003  Provenance and licences
**Statement.** Every description shall carry provenance; vendored third-party material shall carry its licence and a hash.
**Rationale.** ADR-017.
**Status.** Accepted 2026-10-15
**Verification.** Licence check in CI.
**Trace.** ADR-017; SPEC-005 §11

### CAT-004  Boards are data
**Statement.** A board shall be describable entirely in board.json with catalog components, nets, ports and artwork; adding a board requires no C.
**Rationale.** P5.
**Status.** Accepted 2026-10-15
**Verification.** The Blue Pill, nRF52840 DK and Pico boards exist as descriptions; an independent contributor adds one (M3).
**Trace.** SPEC-006 §3

### CAT-005  Alignment with EmbLinkRTOS descriptions
**Statement.** Board and MCU descriptions shall share field names with the RTOS hardware schema where concepts coincide and an importer shall produce drafts from RTOS board YAML.
**Rationale.** Ecosystem sharing.
**Status.** Accepted 2026-10-15
**Verification.** Importer test on the RTOS repository's board files.
**Trace.** SPEC-006 §4

### CAT-006  Conformance fixtures per entry
**Statement.** Every catalog component and board shall ship at least one fixture and validation shall run them.
**Rationale.** Earned support.
**Status.** Accepted 2026-10-15
**Verification.** catalog validate in CI.
**Trace.** SPEC-005 §9; SPEC-014 §3

### CAT-007  Project layout separate from electrical truth
**Statement.** Projects shall keep nets as the electrical truth and layout as presentation; a client edits both in one transaction.
**Rationale.** No rendering-dependent behaviour.
**Status.** Accepted 2026-10-15
**Verification.** Loader ignores layout for simulation (test).
**Trace.** SPEC-007 §1

### CAT-008  Migration
**Statement.** Schema version changes shall ship with a migration tool that reports what it could not convert.
**Rationale.** Long-lived projects.
**Status.** Accepted 2026-10-15 (FUTURE until schema 2)
**Verification.** Migration tests when a version changes.
**Trace.** SPEC-007 §4


### CAT-009  Named part identity and executable evidence
**Statement.** Named manufacturer parts shall declare manufacturer, exact part number, package and datasheet URL, a physical number for every terminal, honest model limits, and executable behavioral fixtures. Presence in the library shall not imply datasheet qualification.
**Rationale.** A generic transistor symbol cannot stand in for every package or accuracy grade.
**Status.** Accepted 2026-10-16; first named-part batch implemented; 25 of 26 entries have manufacturer package/source review with revision and hash. BAT54 source and electrical model qualification remain open.
**Verification.** Schema/semantic validation, reviewed package maps in `tools/test_catalog_parts.py`, `catalog_fixtures` and `catalog_fixture_assertions` under CTest; source record in `docs/engineering/named-part-sources.md`.
**Trace.** SPEC-005 §2; SPEC-004 §7; catalog/README.md

### CAT-010  Interactive electrical components
**Statement.** Environmental inputs, switches, optical devices and relays shall change actual electrical circuit behavior through the shared engine. Package pad pairs, isolated paths and delayed contacts shall have executable regression evidence and explicit approximation limits.
**Rationale.** Interactive hardware development requires circuit behavior beyond symbols and metadata.
**Status.** Accepted 2026-10-16; initial interactive batch implemented.
**Verification.** Catalog fixtures, the four interactive examples, and the real-engine library browser regression.
**Trace.** SPEC-004 §7; SPEC-005 §8; docs/engineering/interactive-library.md

### CAT-011  Named package body evidence
**Statement.** Named bench package bodies shall retain electrical terminal identity, declare geometry sources and representative-coordinate limits, and distinguish surface-mount packages from direct breadboard insertions. Shared metal tabs shall not invent electrical nodes. Reference-only package or pin maps shall remain explicitly unqualified.
**Rationale.** A familiar-looking package must not teach an incorrect pin arrangement, mounting method or qualification level.
**Status.** Accepted 2026-10-18; SOT-23/SOT-223 bodies for BSS138, LM1117IMP-3.3 and reference-only BAT54.
**Verification.** Datasheet pitch/pin-side and actual mesh-envelope fixtures, all-catalog shape checks, direct-over-hole non-insertion fixture, schema/native catalog validation and native gallery inspection.
**Trace.** SPEC-005 bench representation; SPEC-011 Scene rendering; docs/engineering/package-geometry.md

### CAT-012  Bounded named indicator evidence
**Statement.** Source-checked indicators shall retain exact manufacturer/package/polarity mapping, pinned source revisions and hashes, a declared operating envelope, calibration-only fits and independent source holdouts linked to executable native acceptance bounds. UI evidence shall distinguish nominal DC/source package checks from full manufacturer, optical, transient, thermal or mechanical qualification.
**Rationale.** A familiar name, reviewed source or passing self-derived model output cannot establish device fidelity.
**Status.** Accepted 2026-10-26; first WP7113ID and WP154A4SUREQBFZGC static 25 °C, 2–20 mA envelope.
**Verification.** Pinned PDF vector reproduction; malformed/drifting evidence tests; forced-current and driver load-line fixtures; native all-color reference scenario; actual body-envelope/pitch and UI evidence tests.
**Trace.** SPEC-005 Indicator evidence; SPEC-004 Source-checked indicator curves; ADR-030; docs/engineering/indicator-qualification.md


### CAT-013  Pinned library content and selection
**Statement.** Opt-in project locks shall pin loaded library file bytes and effective catalog selections, reject drift before model instantiation, report changed paths and require explicit baseline replacement. Machine root paths shall not affect identity; unsupported tree entries and stale loaded descriptions shall be refused.
**Rationale.** Unchanged version strings cannot establish reproducible local model inputs.
**Status.** Accepted 2026-10-27
**Verification.** Native CLI/RPC relocation, binary hash, drift, stale description, tree rejection and load refusal acceptance.
**Trace.** SPEC-014; ADR-043; docs/engineering/library-locks.md


### CAT-014  Native declared version contracts
**Statement.** Explicit component, board, MCU, shared block member, composite member and library dependency versions shall be checked against actual loaded revisions. Missing/ambiguous dependencies, incompatible versions and malformed or unsupported ranges shall be rejected; first-match selection shall not silently switch revisions. Native and offline semantics shall share executable precedence/boundary vectors.
**Rationale.** A saved version request is a load contract, independently of content pinning or model qualification.
**Status.** Accepted 2026-10-27
**Verification.** Native range vectors, independent npm comparison, both offline backends and native project/block/composite/board/MCU/dependency protocol acceptance.
**Trace.** SPEC-014; ADR-044; docs/engineering/library-versions.md


### CAT-015  Native assembly population and alternatives
**Statement.** Named alternatives shall address concrete project-owned parts independently, validate replacement symbol compatibility and compile unfitted footprints without electrical behavior, controls, ties or composite children. Derived provenance shall verify exact retained base bytes and support recovery.
**Rationale.** Assembly alternatives must change the real compiled circuit while preserving an inspectable, recoverable source.
**Status.** Accepted 2026-10-27
**Verification.** Native protocol/CLI and actual native browser acceptance, offline validation, population/edit/report fixtures.
**Trace.** SPEC-008; SPEC-014; ADR-045; docs/engineering/assembly-variants.md
