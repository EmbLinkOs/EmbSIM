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
| Q7 | Breadboard view in 1.0 | **Closed by ADR-027 (owner, 2026-10-17):** a 3D breadboard view, a second editor of the same nets, in M7 before the release | SPEC-011, ADR-027 |
| Q8 | Save-and-resume checkpoints | After 1.0; needs versioned state serialization for cores, peripherals, islands and the scheduler | ADR-012 |
| Q9 | Telemetry or crash reporting in the desktop app | None; crash dumps stay local, with a one-click export the user controls | SPEC-011 |
| Q10 | Minimum supported platforms for 1.0 (Ubuntu 22.04+, macOS 13+, Windows 10+) | As listed; older platforms are not tested | PLT requirements |
| Q11 | The flagship's ESP32-class board: the classic ESP32 (Xtensa LX6, two cores, WiFi and Bluetooth) or the ESP32-C3 (RISC-V) first | **Accepted (owner, 2026-10-17):** The ESP32-C3 first: it reuses the RISC-V core and runs ESP-IDF and the Arduino-ESP32 core; then the classic ESP32 with an Xtensa core written from Cadence's ISA reference. Radios answer as stubs (a scripted access point, loopback sockets) until modelled | M6, ADR-029 |
| Q12 | The 3D bench's renderer: Three.js or Babylon.js | Three.js: smaller, MIT, glTF-first, already familiar to contributors; the scene is a component of the workbench, not a separate app | ADR-027 |
| Q13 | The world's physics: kinematic paths only, or a rigid-body engine (Rapier) | **Accepted (owner, 2026-10-17):** Kinematic paths and a simple gravity and IMU step for M6 and M7; Rapier when a scenario needs collisions | ADR-026 |
| Q14 | How "accurate" a part model must be to enter the catalog | **Accepted (owner, 2026-10-17):** Its datasheet's values and timing in fixtures, every mode its usual library uses, and a capture from the real part when one can be made; what is omitted is declared in its fidelity | P2, P8, M6 |
