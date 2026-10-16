# 04 - MCU Runtime and Toolchains

**Status:** ACCEPTED. Contracts in SPEC-006 (descriptions), SPEC-009 (backend
contract), SPEC-010 (toolchain profiles), SPEC-012 (record and replay).
Decisions: ADR-005, ADR-010, ADR-011, ADR-015, ADR-016.

---

## 1. Separation of concerns

```
 user circuit  ->  board  ->  package  ->  MCU  ->  core + peripherals
 (project)        (data)     (data)      (data +    (C backends + models
                                          models)    over register files)
```

Two boards with one MCU reuse the MCU description and its models without
copying; two packages of one MCU share everything but the pin map; a board
revision is a new board description with the same MCU. Firmware never knows
which layer it talks to: it sees registers at addresses and pins at ports.

## 2. Core backends

A core backend implements SPEC-009's `es_core_ops`: create and destroy an
instance with its own state, reset, execute up to a cycle budget or until an
external stop, read and write registers by number, read and write memory
through the instance's bus, raise an interrupt, describe itself for a debugger
(GDB register numbers, target XML, breakpoint kinds), and report every error
as a status rather than terminating. Backends extracted from EmbCC (document
09) are wrapped to that contract: `die()` becomes a status, file-scope state
moves into the instance, `stdout` becomes a byte stream the session owns.

Families planned, with the EmbCC file sets they come from:

| Family | Cores | Source | Cycle accuracy tier (ADR-015) |
|---|---|---|---|
| Cortex-M | M0, M0+, M3, M4(F), M7, M23/M33 later | `cortexm*.c`, `scs.c`, `systick.c`, `dwt.c` | estimated (cost table), documented per instruction class |
| RISC-V | RV32/RV64 IMAFDC | `riscv*.c`, `clint.c` | estimated |
| AVR | ATmega328P and family | `avr*.c` | exact per datasheet cycle table |
| MIPS32 / PIC32 | 24Kc, M4K, PIC32MZ/MX | `mips*.c`, `pic32-*.c` | estimated |
| PIC24 / dsPIC33 | PIC24F/E, dsPIC33E | `pic24*.c` | estimated, DSP engine modelled |
| PIC16/PIC18 (XC8) | FUTURE | new | to be written when XC8 support is scheduled |

A core's cycle model is a declared estimate, except where a datasheet gives
exact timing; STATUS.md lists the tier per core, and timing numbers from the
simulator are labelled accordingly in every client.

## 3. Memory and bus

Each MCU instance owns a bus: regions (RAM, flash with erase values and
write-protection after load, aliases, bit-band), memory-mapped devices by
address range, watchpoints. Flash programming by a debugger or a bootloader
model goes through the same bus with explicit permission. Multiple MCUs in one
project have separate buses; nothing is global.

## 4. Peripherals from register files

The vendor's register description (CMSIS-SVD for Arm parts, ATDF and EDC for
Microchip parts) instantiates a **register file**: every register at its
address with its reset value, access rules, field semantics (write-one-to-
clear, read-to-clear, write-once), clusters and arrays. A register file alone
is storage that behaves as the description says; it is not a peripheral.

**Peripheral models** bind to register-file peripherals by name pattern
(`GPIO*`, `USART*`, `TIM*`) and implement behaviour over the registers: a
USART model shifts bits onto its TX pin net at the baud rate derived from the
clock tree; a timer model counts its clock, compares, generates PWM on pins
and raises interrupts; a GPIO model exposes pin models into the net graph. The
model declares which registers and fields it implements; an access to a
peripheral that has a register file but no model, or to a field a model does
not implement, is an **unsupported access** handled by the session policy:

| Policy | On unsupported access |
|---|---|
| `strict` | stop the run with a diagnostic naming the register, field and PC |
| `warn` (default) | continue with register-file semantics, record the diagnostic once per field |
| `permissive` | continue silently (for exploratory runs of unfamiliar firmware) |

Register files for redistributable descriptions (Apache-2.0 SVDs and similar)
are vendored in the catalog with their licence; others are supplied by the
user through a device-pack path, and the MCU description names the file it
expects, as the EmbCC simulator does today.

## 5. Pin multiplexing and packages

An MCU's pins are signals of its GPIO ports and dedicated signals (oscillator,
reset, boot, supplies, debug). The **pin multiplexer** model decides, from the
GPIO configuration registers and the alternate-function or peripheral-pin-
select registers, which peripheral drives or reads each signal; the GPIO model
exposes one pin model per signal into the net graph, with parameters from the
MCU description (Voh, Vol, Rout per drive strength, Vih, Vil, pull resistance,
analog capability). The **package** maps signals to physical pin numbers and
names (LQFP48 pin 2 = PC13), which boards wire by physical pin. A board
therefore never names an internal signal; a package change is a one-line edit.

## 6. Boards

A board is a composite component: the MCU in its package, the board's own
components (crystal, regulator, decoupling as a lumped model where it matters,
reset button and pull-up, boot jumpers, user LEDs with their series resistors,
user buttons with their pulls, headers), the nets joining them, and **ports**:
the header pins a user wires to in the project, named as the silkscreen names
them. Artwork is an SVG of the board with hotspots bound to components (press
the real button on the picture; the real LED on the picture lights). Boards
import from EmbLinkRTOS board YAML, Zephyr DeviceTree and KiCad netlists as
drafts; the author curates and validates.

## 7. Debugging

Every MCU instance offers the GDB remote serial protocol through the session
(SPEC-008 `debug.*`) and as a plain TCP stub for gdb, lldb and embdbg:
registers, memory, breakpoints and watchpoints held by the simulator (never
written into flash), target XML, memory map, `monitor` commands for reset,
time and statistics. The workbench's debug perspective uses the same methods
plus DWARF (line tables, variables, call frames) read by the core's loader so
source-level stepping does not depend on an external debugger. Attaching a
debugger changes nothing in virtual time: a run that stops at a breakpoint and
continues gives the same counts as one that does not.

## 8. Record and replay

Everything the machine computes is a function of the project and its images,
except what comes from outside: user inputs, serial bytes from the host, a
debugger's writes, a script's actions. A **recording** (SPEC-012) stores each
with its virtual timestamp and sequence number, plus the identities (hashes)
of the project, images, catalog entries and engine, and the run's observed
end. **Replay** runs the same machine with the same inputs at the same times,
compares interrupt entries and discrete observations against the recording,
and reports the first divergence. The EmbCC simulator's step-based record is
replaced by virtual-time-based records so inputs to any component, not only
the console, are covered.

## 9. Toolchain profiles

The simulator consumes images; it never needs a compiler. For the
build-and-run loop, **toolchain profiles** (SPEC-010) describe how to find a
toolchain and build for a target:

| Profile | Detection | Targets | Output | Debug info |
|---|---|---|---|---|
| `embcc` | `embcc --version` | every EmbCC target (`--target=thumbv7m-none-eabi`, `riscv32-unknown-elf`, `avr`, `mipsel-none-elf --mcu=`, ...) | ELF | DWARF |
| `gcc-arm` | `arm-none-eabi-gcc` | Cortex-M, Cortex-A/R | ELF | DWARF |
| `gcc-riscv` | `riscv-none-elf-gcc`, `riscv64-unknown-elf-gcc`, `riscv32-unknown-elf-gcc` | RISC-V | ELF | DWARF |
| `gcc-avr` | `avr-gcc` | AVR | ELF | DWARF |
| `clang` | `clang --target=` with a sysroot and a linker (`lld`) | Cortex-M, RISC-V | ELF | DWARF |
| `xc8` | `xc8-cc` | PIC16/18 | HEX (+ ELF with DWARF where XC8 emits it) | partial |
| `xc16` | `xc16-gcc` | PIC24/dsPIC33 | ELF, HEX | DWARF |
| `xc32` | `xc32-gcc` | PIC32 | ELF, HEX | DWARF |
| `tcc` | `tcc` | host only: builds EmbSIM itself, not firmware | n/a | n/a |

A profile is a JSON description: detection commands, per-target compile, link
and convert command templates, flags for debug info and for the board's
linker script, and how to interpret the outputs. Projects reference a profile
and a target; the workbench and `embsim build` run it; a project built
elsewhere just names its image. EmbSIM's own core builds with GCC, Clang,
TinyCC, EmbCC and MSVC (ADR-001), which is what "works with tcc" means for a
host-only compiler.

## 10. Multiple MCUs and boards

A project may hold several boards wired together (two MCUs on a UART, a
master and a sensor board). Every MCU instance is independent state driven by
the one scheduler; cores interleave at quantum boundaries and at every event
that crosses between them, so a byte sent at 10 µs is received at 10 µs plus
the line's delay regardless of which core the scheduler ran first. Multicore
MCUs (RP2040) are two core instances sharing a bus and peripheral set, with
the shared state owned by the MCU instance and protected by the same
scheduler ordering.
