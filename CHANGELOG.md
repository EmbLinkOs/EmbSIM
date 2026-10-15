# Changelog

All notable changes are recorded here. The format follows Keep a Changelog;
versions follow semantic versioning. There is no release yet.

## Unreleased

### Added
- Architecture baseline v1.0: vision, system architecture, simulation engine,
  MCU runtime and toolchains, workbench and protocol, ADR-001 to ADR-018,
  roadmap, open questions, reuse assessment.
- Specifications SPEC-001 to SPEC-014 and the requirements catalog.
- Repository: CMake build and presets, CI matrix, documentation and policy
  workflows, templates, coding standard, status page.
- M0 core: scheduler and rational clocks, nets and pin models, digital engine,
  analog engine v1, model registry and primitives, JSON loader and validator,
  session and in-process protocol, stdio and WebSocket endpoints, `embsim`
  command line, C tests and fixtures.
- M0 catalog: schemas, first components, the Blue Pill board description.
- M0 workbench: design system, shell, schematic canvas, library, inspector,
  run controls, instruments; desktop shell scaffold.

### Removed
- The draft 0.1 foundation (Python runtime, HTTP server, single-page UI);
  its kernel semantics and visual direction are retained in the baseline.
