# EmbSIM Architecture, v1.0 baseline

This directory is the architecture baseline for EmbSIM, the EmbLink virtual
electronics workbench. It supersedes the draft 0.1 documents that preceded it
(their content is folded into the reuse assessment, document 09) and fixes the
vocabulary, layering, engines, data formats and decisions before the
implementation grows around them.

## Reading order

| # | Document | What it answers |
|---|---|---|
| 01 | [Vision and principles](01-vision-and-principles.md) | Mission, who it is for, what "useful" and "modern" mean here, principles, non-goals, positioning, definition of success |
| 02 | [System architecture](02-system-architecture.md) | Layers, responsibilities, concept vocabulary, dependency rules, how a project becomes a running machine, how a component or a board arrives |
| 03 | [Simulation engine](03-simulation-engine.md) | Time, the scheduler, nets and pins, the digital engine, the analog engine, mixed-signal islands, diagnostics, determinism |
| 04 | [MCU runtime and toolchains](04-mcu-runtime-and-toolchains.md) | Cores, memory, peripherals from register files, pin multiplexing, packages, boards, debugging, record and replay, toolchain profiles |
| 05 | [Workbench, protocol and command line](05-workbench-protocol-and-cli.md) | The session protocol, the CLI, the graphical workbench's perspectives and design system, desktop and browser delivery |
| 06 | [Decision records](06-decision-records.md) | ADR-001 to ADR-022 with alternatives, consequences and per-record status |
| 07 | [Roadmap](07-roadmap.md) | The 1.0 boundary, milestones M0 to M6, specification work order, risks |
| 08 | [Open questions](08-open-questions.md) | Decisions awaiting the project owner, each with a recommendation |
| 09 | [Reuse assessment](09-reuse-assessment.md) | What the EmbCC simulator and the earlier EmbSIM foundation give us, what is kept, wrapped, or redesigned |

The specifications in [`docs/specs/`](../specs/index.md) turn these documents
into contracts; the requirements in [`docs/requirements/`](../requirements/index.md)
number every obligation with its verification method; [`docs/STATUS.md`](../STATUS.md)
says what is implemented and with what evidence. A design being accepted does
not mean a feature exists; STATUS.md is the only place that says so.

## Conventions

- **Decisions** are recorded as ADRs in document 06 and referenced by number.
  Nothing in 02 to 05 contradicts an accepted ADR; a change needs a new ADR.
- **Status words**: LOCKED (changing it is a new major design), ACCEPTED,
  PROPOSED, FUTURE. A document section carries its status where it differs
  from the document's.
- **Identifiers** in C start with `es_` (public), `esk_` (core-private);
  protocol methods are `domain.verb`; description files are JSON validated by
  the schemas in `schemas/`.
- **Units** are written out: picoseconds (`ps`), volts, ohms. Virtual time is
  an unsigned 64-bit count of picoseconds everywhere a number is exchanged.
