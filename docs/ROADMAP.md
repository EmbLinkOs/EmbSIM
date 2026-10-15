# Delivery roadmap

Every stage must have executable acceptance checks and documented omissions.
Catalog size is not a substitute for behavioral coverage.

| Stage | Deliverable | Exit criteria |
|---|---|---|
| 0 — foundation | Specifications, C kernel, JSON project, CLI and browser controls | Deterministic digital events; press/release visible in both interfaces; validation and CI pass |
| 1 — firmware | Audit and extract one Cortex-M backend, one MCU and board | Unmodified ELF runs; timed external button changes reach GPIO/interrupts; firmware drives LED; debugger works; regression fixtures retained |
| 2 — reusable digital circuits | Pin contracts, reactive models, clocks, UART/SPI/I²C wiring, schematic editor | Same circuit runs headless and graphically; contention/floating diagnostics; event-storm tests; serial peripheral integration |
| 3 — mixed signal | Solver ADR, resistors/capacitors/diodes/LEDs/transistors, GPIO bridges and ADC | RC analytic checks; transistor operating-point checks; timed firmware-to-circuit feedback; convergence failure diagnostics |
| 4 — engineering workbench | Logic analyzer, oscilloscope, protocol decode, assertions, record/replay | Saved scenarios reproduce input timing and observations; waveform export; debugging remains deterministic |
| 5 — extensible catalog | Versioned component/MCU/package/board schemas, validation tools, model SDK | An independent contributor adds a board and component with conformance fixtures and fidelity documentation |
| 6 — breadth and performance | More audited CPU families, boards and devices; multicore/multiboard | Target-specific compatibility matrix and benchmarks; no shared-state leakage; supported-platform CI |

Stage 0 is the first implementation in this repository. Stages 1–6 are planned,
not delivered. Do not bulk-copy all legacy boards and call them supported.

## Before stage 1

- Choose the first MCU/board based on available datasheets and distributable
  device metadata. Blue Pill is a candidate, not a final hardware commitment.
- Define adapter ownership, structured failures, teardown, and clock boundaries.
- Preserve upstream commit attribution, Apache notices, regression fixtures and
  provenance when extracting code. Upstream authorship is provenance, not a new
  commit co-author trailer.
- Fix or isolate the observed GCC and compiler-link defects, with targeted tests.
- Establish ISA/MMIO test baselines before changing scheduling behavior.

## Definition of done

Code, runnable examples, meaningful tests, model limitations, migration notes
where needed, and reproducible setup must agree. Report tests actually run and
separate skipped external-tool or vendor-pack comparisons. Never label a board
fully supported merely because its registers are listed.
