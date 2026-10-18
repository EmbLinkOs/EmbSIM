# Contributing to EmbSIM

## Current phase

M0 to M5 are delivered; M6, the flagship, is under way (the
[roadmap](docs/architecture/07-roadmap.md) defines the gates).
[Project status](docs/STATUS.md) distinguishes working code from target
designs, and [the coverage tables](docs/coverage.md) show each MCU's
peripherals and whether the engine models them.

Valuable contributions now: component and board descriptions with fixtures
(see `sdk/`), the peripherals the coverage tables list as missing, captures
from real hardware to compare the simulation with, analog fixtures from
ngspice, workbench panels against SPEC-011, and documentation.

## How every feature is designed

EmbSIM is held to how things work in the real world (P11, ADR-030). A pull
request that adds or changes behaviour answers, in its description:

1. **How does it work in real life?** The datasheet, reference manual and
   schematics; how engineers use it, with what tools and steps.
2. **What can anyone observe from outside?** Pins, voltages, currents,
   timing, what tools report, what a person sees and does: all of it must
   match reality.
3. **What is the full implementation?** Every mode and edge case; what is
   left out is declared in the model's fidelity, not silently missing.
4. **Where does the inside differ?** An abstraction is fine when the
   outside is unchanged; name it and say why.
5. **How is it proven?** Vendor code run unmodified, the datasheet's values
   and timing in fixtures, a real-hardware capture where possible.

## Licence

Apache-2.0; inbound licence equals outbound licence. Every source file starts
with an SPDX header:

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 <your name or organization> */
```

Descriptions, scripts and other files use the same two lines in their comment
syntax; JSON files, which have no comments, carry `provenance.license`.

## Commit identity

Commits and pull requests in this repository are made under the repository
owner's Git identity. Do not add `Co-authored-by`, `Signed-off-by` for another
person, or any assistant-authorship trailer; the Repository policy check
rejects them. Check `git config user.name` and `user.email` before committing;
do not invent another person's identity. When maintainers join, this rule is
revised in `docs/engineering/repository-workflow.md` and CODEOWNERS.

## Process

1. Open an issue for anything beyond a small fix. Architecture changes need an
   ADR in `docs/architecture/06-decision-records.md`.
2. Branch from `main`; one logical change per pull request; commit messages
   explain why.
3. A contract change updates its specification and requirements in the same
   pull request; a new behaviour ships with tests or fixtures; a user-visible
   change updates `CHANGELOG.md` under Unreleased.
4. Every C file follows `docs/CODING-STANDARD.md`; warnings are errors on
   every compiler in the matrix; `tools/repository/check_banned.py` runs in
   CI.
5. Required checks: **CI merge gate**, **Documentation build**, **Repository
   policy** (see the repository workflow page).

Code brought in from EmbCC keeps its header comment, gains a provenance line
and is listed in `NOTICE` (ADR-005). Vendor material is vendored only under a
licence that permits it, with the licence beside it (ADR-017).

## Conduct and security

This project follows the [Code of Conduct](CODE_OF_CONDUCT.md). Report
vulnerabilities as `SECURITY.md` describes, not in public issues.
