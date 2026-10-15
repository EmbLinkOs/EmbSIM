# 09 - Reuse Assessment

What exists, what is kept, what is wrapped, what is redesigned. Inspected on
2026-10-15: `EmbLinkOs/EmbCC` at `aa4f10a3f5a94456bd757bcfa0d9c25f461590d9`
(`tools/embsim`, `tools/embsvd`, `tools/bench/cost.h`, `tests/golden/embsim*`),
and `EmbLinkOs/EmbSIM` at `1f57001` (the foundation this baseline replaces).

## 1. The EmbCC simulator

About 37 000 lines in 88 C99 files, dependency-free, warning-free under EmbCC's
flags; it builds here with GCC 13 when two warnings are made nonfatal
(`boards.c:512` possible null `%s`; `pic32-gpio.c:288` a multi-statement
macro under an unbraced `if`). It runs ELF and HEX images on models of QEMU's
boards and of several real boards, and holds its cores to QEMU with golden
fixtures (`tests/golden/embsim*.sh`).

| Part | Lines (approx.) | Verdict | Notes |
|---|---|---|---|
| Cortex-M core (`cortexm.c`, `cortexm-thumb.c`, `cortexm-fpu.c`, `scs.c`, `systick.c`, `dwt.c`, `ieee754.c`) | 4 500 | **extract** in M1 | exceptions, priorities, faults, FPU, bit-banding; cycles from `cost.h` |
| RISC-V core (`riscv.c`, `riscv-fpu.c`, `clint.c`) | 1 800 | extract in M4 | RV32/RV64 IMAFDC |
| AVR core (`avr.c`, `avr-io.c`, `avr-usart.c`, `avr-timer16.c`) | 1 500 | extract in M4 | exact cycle timing |
| MIPS and PIC32 (`mips*.c`, `pic32-*.c`, `malta.c`) | 6 000 | extract in M4 | 24Kc, M4K, PIC32MZ/MX peripherals |
| PIC24 and dsPIC33 (`pic24*.c`) | 7 500 | extract in M4 | DSP engine, PPS, many peripherals |
| Bus (`bus.c`) | 300 | **wrap** in M1 | regions, aliases, devices, watchpoints; make per-instance |
| Register files (`svd-map.c`, `atdf.c`, `edc.c`, `../embsvd/svd.c`) | 3 300 | **extract** in M1 (SVD), M4 (ATDF, EDC) | field semantics table matches SPEC-006 §5 |
| STM32 models (`stm32-rcc.c`, `stm32-gpio.c`, `stm32f1-gpio.c`, `stm32-usart.c`, `stm32-tim.c`) | 1 600 | **extract and rework** in M1 | register behaviour kept; pin callbacks replaced by pin models |
| nRF52 models (`nrf52.c`), RP2040 models (`rp2040*.c`) | 2 800 | extract in M4 | |
| Loaders (`loader.c`, `image.c`) | 1 200 | **extract** in M1 | ELF, HEX; DWARF reading to be added from `embdbg` |
| GDB stub (`gdb.c`, `net*.c`) | 1 100 | **extract** in M1 | sockets move to the host layer |
| Record and replay (`record.c`) | 660 | **redesign** (ADR-012) | step-based; console and debugger only |
| Analyses (`analysis.c`, `coverage.c`, `profile.c`, `stack.c`, `fault.c`, `disasm.c`) | 3 700 | extract in M5 | valuable; keep their output formats |
| Boards (`boards.c`, `mcu-boards.c`, `pic-boards.c`) | 1 900 | **redesign** (ADR-003) | become board descriptions and catalog components; the Explorer's LCD, EEPROM and sensors become Lua or C component models |
| Run loop and CLI (`run.c`, `main.c`, `trace.c`, `semihost.c`) | 1 400 | **redesign** | the session, scheduler and CLI replace them; semihosting is kept as a core feature |

Required mechanical changes on extraction (SPEC-009 §8): 247 `die()` calls
become status returns; the global `struct sim` becomes an instance passed
explicitly; `putchar`/`stdout` become session streams; `struct board_io`
becomes pin models; `u32 cycles` ticks become scheduler time through the
core's clock. Provenance: each extracted file keeps EmbCC's header comment,
gains a provenance line (repository, commit, original path) and is listed in
`NOTICE`; the FSF-copyrighted GDB target descriptions in `avr.c` and
`riscv.c` keep their notices.

Golden fixtures: `tests/golden/embsim/` holds ISA programs with expected
output and instruction counts for every family, exception tests, GDB session
transcripts, replay cases, coverage, profile and stack reports. They are
imported into `tests/differential/embcc/` with their harness scripts adapted
to `embsim run --json`; a family is not "extracted" until its fixtures pass.

## 2. The earlier EmbSIM foundation

1 757 lines: a C99 kernel (`src/kernel.c`: nets, drivers with two strengths,
a sorted event array, nanosecond time), a Python runtime (schema-1 project
validation, ctypes binding, loopback HTTP server), a single-page web UI, a
CI matrix on three platforms, and seven design documents.

| Part | Verdict |
|---|---|
| Kernel semantics: insertion-ordered equal-time events, strength resolution, X on conflict, Z on no driver | kept as the digital engine's base rules (SPEC-003), generalised to four strengths and picoseconds |
| Shared-library ABI with opaque handle and status returns | kept as the public API style (SPEC-001) |
| Python validation and HTTP server | retired; validation moves to the C core and the Python schema tool; the server becomes the core's WebSocket endpoint under the protocol |
| Web UI visual direction (dark navy, mint signal highlight, monospaced identifiers, explicit run state) | kept as the seed of the design system (SPEC-011 §4); the page itself is replaced by the workbench |
| CI on Linux, macOS, Windows | kept and extended |
| Design documents: fidelity contract, live topology transactions, reproducibility records, interface direction, platform rules | folded into documents 01 to 05 and the specifications |
| `docs/EXISTING_WORK.md` observations on EmbCC | folded into §1 above |

## 3. EmbLinkRTOS

Not code to reuse but conventions to share: the hardware description schema
(SPEC-014 there) for boards and SoCs, which EmbSIM's board and MCU
descriptions align with and import; the engineering process (specifications,
numbered requirements, STATUS.md, three stable checks, DCO-style policy
check adapted to the owner's identity rule); and the expectation (its SIM
requirements) that an emulated target exists per architecture in CI, which
EmbSIM will satisfy from M4.
