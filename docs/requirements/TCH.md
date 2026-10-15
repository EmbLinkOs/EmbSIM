# TCH - Toolchains

Group `TCH`. Design: 04 §9; SPEC-010; ADR-010.

Each requirement has a statement, a rationale, a status (Accepted, FUTURE),
a verification method and a trace to the design. Verification names the
test or review that establishes it; STATUS.md records whether it passed.

---

### TCH-001  No compiler needed to run
**Statement.** The simulator shall run any supported image without any compiler installed.
**Rationale.** P9.
**Status.** Accepted 2026-10-15
**Verification.** CI job with no toolchain installed runs the examples from prebuilt images.
**Trace.** ADR-010

### TCH-002  Toolchain profiles
**Statement.** Build-and-run shall be driven by JSON toolchain profiles with detection and command templates; profiles for embcc, gcc-arm, gcc-riscv, gcc-avr, clang, xc8, xc16, xc32 shall ship.
**Rationale.** The owner's requirement.
**Status.** Accepted 2026-10-15
**Verification.** Profile schema tests; detection tests with stubs; real builds where CI has the toolchain.
**Trace.** SPEC-010

### TCH-003  Core builds with five compilers
**Statement.** libembsim and embsim shall build warning-free with GCC, Clang, TinyCC, MSVC and EmbCC (Linux host).
**Rationale.** ADR-001; what tcc support means for the host.
**Status.** Accepted 2026-10-15
**Verification.** CI matrix jobs.
**Trace.** ADR-001

### TCH-004  XC8 is explicit
**Statement.** Until a PIC16/18 core exists, the xc8 profile shall build but the loader shall refuse its images with a diagnostic naming the missing core.
**Rationale.** Refuse rather than guess.
**Status.** Accepted 2026-10-15 (FUTURE core)
**Verification.** Test.
**Trace.** SPEC-010 §3

