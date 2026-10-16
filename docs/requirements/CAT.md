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
