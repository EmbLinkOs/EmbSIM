# MCU - MCU Runtime

Group `MCU`. Design: 04; SPEC-006, SPEC-009.

Each requirement has a statement, a rationale, a status (Accepted, FUTURE),
a verification method and a trace to the design. Verification names the
test or review that establishes it; STATUS.md records whether it passed.

---

### MCU-001  Unmodified images
**Statement.** ELF and Intel HEX images produced by any supported toolchain shall load and run without modification.
**Rationale.** P9; the owner's compiler requirement.
**Status.** Accepted 2026-10-15
**Verification.** Golden tests with images from EmbCC and GCC (and Clang, XC when available in CI).
**Trace.** 04 §9; SPEC-010

### MCU-002  Backend contract
**Statement.** Every core backend shall implement esk_cpu_ops of SPEC-009 §3 with no process exit, no global state and status returns.
**Rationale.** ADR-005, ADR-016.
**Status.** Accepted 2026-10-15
**Verification.** Extraction checklist review; two-instance test.
**Trace.** SPEC-009 §3, §11

### MCU-003  Register files from vendor descriptions
**Statement.** Peripheral registers shall be instantiated from register descriptions (EmbSIM JSON, converted from SVD, ATDF or EDC; ADR-020) with the field semantics of 04 §4.
**Rationale.** Real register behaviour.
**Status.** Accepted 2026-10-15
**Verification.** Register-file tests; the Blue Pill golden fixture with the description converted from ST's SVD.
**Trace.** 04 §4; SPEC-009 §5

### MCU-004  Unsupported-access policy
**Statement.** Access to unmodelled peripherals or fields shall be observable under strict, warn and permissive policies as 04 §4 defines.
**Rationale.** ADR-011.
**Status.** Accepted 2026-10-15
**Verification.** Tests per policy.
**Trace.** 04 §4; ADR-011

### MCU-005  Peripheral timing from the clock tree
**Statement.** Peripheral timing (baud, timer periods, PWM) shall derive exactly from the modelled clock tree through rational clocks, independent of the core's timing tier.
**Rationale.** Correct communication timing.
**Status.** Accepted 2026-10-15
**Verification.** USART and timer fixtures compared to analytic edges.
**Trace.** SPEC-009 §6; ADR-015

### MCU-006  Debugger neutrality
**Statement.** Attaching a debugger, hitting breakpoints or reading state shall not change virtual-time behaviour.
**Rationale.** Deterministic debugging.
**Status.** Accepted 2026-10-15
**Verification.** Test: breakpoint-and-continue equals uninterrupted run.
**Trace.** 04 §7

### MCU-007  Pin multiplexing
**Statement.** Peripheral signals shall reach pins only through the MCU's multiplexer model driven by its configuration registers.
**Rationale.** Real AF/PPS behaviour.
**Status.** Accepted 2026-10-15
**Verification.** Fixtures: USART TX on remapped pins.
**Trace.** SPEC-009 §7

### MCU-008  Fixture-gated extraction
**Statement.** A core family is complete only when its EmbCC golden fixtures pass as EmbSIM differential tests.
**Rationale.** Guard against regressions.
**Status.** Accepted 2026-10-15
**Verification.** The family's EmbCC golden fixtures in tests/firmware (the Blue Pill's in M1), run by tests/unit/test_mcu.c in CI.
**Trace.** ADR-005; 09 §1

### MCU-009  Multiple MCUs and cores
**Statement.** A project shall run several MCUs, and multicore MCUs, under one scheduler with deterministic interleaving.
**Rationale.** Multi-board products; RP2040.
**Status.** Accepted 2026-10-15
**Verification.** Two-board UART fixture; RP2040 fixture (M4).
**Trace.** 04 §10; ADR-016

