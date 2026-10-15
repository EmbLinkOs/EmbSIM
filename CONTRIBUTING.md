# Contributing to EmbSIM

## Current phase: M0 foundation

The architecture (`docs/architecture/`), specifications (`docs/specs/`) and
requirements (`docs/requirements/`) are accepted. [Project status](docs/STATUS.md)
distinguishes working code from target designs; the
[roadmap](docs/architecture/07-roadmap.md) defines the gates.

Valuable contributions now: review of the specifications; component and board
descriptions with fixtures (see `sdk/`); analog fixtures generated from ngspice
or analytic derivations; the extraction of EmbCC core families against
SPEC-009 with their golden fixtures; workbench panels against SPEC-011; and
documentation.

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
