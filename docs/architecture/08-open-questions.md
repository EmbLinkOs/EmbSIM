# 08 - Open Questions

Decisions that await the project owner. Each has a recommendation; the work
proceeds on the recommendation until the owner decides otherwise.

| # | Question | Recommendation | Affects |
|---|---|---|---|
| Q1 | Redistribution of ST's STM32F103 SVD in the catalog: its licence must be read and recorded | **Closed by ADR-020 (M1):** ST's EULA is not an open licence; the SVD is not vendored. The register description is written from RM0008 and SVD files convert with `tools/importers/svd2regs.py` | ADR-017, ADR-018, ADR-020 |
| Q2 | Default session policy in the workbench: `warn` (exploration) or `strict` (verification) | `warn` in the workbench, `strict` in the CLI unless `--policy` says otherwise; both display it | ADR-011, SPEC-008 |
| Q3 | Should EmbCC's `embsim` later be rebuilt on `libembsim`, so one implementation serves both projects? | Yes, after M4, when every EmbCC family passes its fixtures in EmbSIM; until then both exist and fixes are ported by hand with commit references | ADR-005 |
| Q4 | Workbench state store (Zustand or Redux Toolkit) and component library (Radix primitives) | Zustand and Radix primitives with the project's own design system; revisit only if accessibility testing finds gaps | ADR-008, SPEC-011 |
| Q5 | WebAssembly as a fourth extension tier | FUTURE; reconsider when a maintained C runtime (wasm3 successor, wamr) stabilises | ADR-009 |
| Q6 | Name and home of the Python client (`tools/embsim_client` or a published package) | In-repo first, published to PyPI at 1.0 | 05 §4 |
| Q7 | Breadboard view in 1.0 | Not in 1.0; schematic plus board artwork cover the acceptance scenario; breadboard is a layout skin over the same net graph later | SPEC-011 |
| Q8 | Save-and-resume checkpoints | After 1.0; needs versioned state serialization for cores, peripherals, islands and the scheduler | ADR-012 |
| Q9 | Telemetry or crash reporting in the desktop app | None; crash dumps stay local, with a one-click export the user controls | SPEC-011 |
| Q10 | Minimum supported platforms for 1.0 (Ubuntu 22.04+, macOS 13+, Windows 10+) | As listed; older platforms are not tested | PLT requirements |
