# Project status and evidence

**Reviewed:** 2026-10-15 at the architecture baseline. This inventory is
maintained by hand until evidence tooling exists. It is not a generated
support matrix. Specifications define target contracts; their acceptance
does not mean the described behaviour exists.

## Implemented subset

| Area | Current implementation | Evidence and limits |
|---|---|---|
| Design | Architecture 01 to 09, SPEC-001 to SPEC-014, 91 requirements | Reviewed documents; `tools/docs/check_docs.py` passes |
| Repository | CMake presets, Makefile wrapper, CI matrix, documentation and policy workflows, templates, ruleset JSON, policy and banned-construct checkers | Local runs of the checkers; remote workflow results are recorded here when observed |
| Core: scheduler and time | see the M0 section below | |
| Core: nets and digital engine | see below | |
| Core: analog engine | see below | |
| Core: models and catalog loading | see below | |
| Core: session and protocol | see below | |
| CLI | see below | |
| Workbench | see below | |
| MCU runtime | Not implemented. No core family is extracted. | M1 |
| Toolchain profiles | Not implemented. | M1 |
| Record and replay | Not implemented beyond input recording in the session. | M1/M5 |
| Lua models | Not implemented. | M3 |

The M0 implementation sections are updated as the code lands in this branch;
each entry must name the tests that ran and the platforms they ran on.

## Specified but unimplemented

Everything in SPEC-009, SPEC-010, SPEC-012 (beyond input recording),
SPEC-013, SPEC-014 (beyond library loading by path); boards with firmware;
instruments beyond the oscilloscope and logic analyzer; the Debug and Verify
perspectives; installers; importers.

## Validation recorded in this workspace

Recorded as it happens; none yet beyond the documentation and policy checks.

## Known limits and unresolved evidence

- No remote CI run has been observed for this baseline; workflow files are
  definitions, not results.
- The GitHub ruleset in `.github/rulesets/main.json` is prepared, not
  activated; activation is an administrator action described in the
  repository workflow page.
- Instruction timing of every planned core is an estimate except AVR
  (ADR-015).

## Updating this page

Record source revision, configuration, tool versions, runner results, cases,
skips and artifacts together. Move a feature to implemented only after
inspecting the code; call a requirement verified only after its named
evidence passes.
