# ESP32 CPU task-context registers and compiler TLS

**Implemented: bounded functional THREADPTR/MISC access and explicitly configured
PRID input in the experimental Xtensa engine. No runnable ESP32 board, SDK boot
or RTOS scheduler is added.**

## Architectural contract

The original ESP32 toolchain configuration enables THREADPTR, PRID and four
MISC registers. The Xtensa ISA Reference Manual, RI-2021.8, defines their access:

| Register | Namespace/address | Access | Mode |
|---|---|---|---|
| THREADPTR | User register 231 | RUR/WUR, full 32 bits | Unprivileged |
| MISC0..3 | Special registers 244..247 | RSR/WSR/XSR, full 32 bits | Privileged |
| PRID | Special register 235 | RSR, full 32 bits | Privileged, read-only |

THREADPTR's user-register namespace is separate from VECBASE's special-register
namespace, although both use number 231. RUR and WUR encode their A-register
operand in different fields. Only the actual operand participates in lazy
register-window checks. A window transition retries the instruction without
changing THREADPTR or MISC. User-mode MISC/PRID access enters the configured
privilege exception, before changing the destination or special register.
Other user registers, FPU controls and nonexistent MISC4 remain unsupported.

THREADPTR holds the compiler's per-thread storage base; writing it does not
allocate, initialize or copy memory. MISC registers hold software-defined scratch
state. They are per CPU, with no host task-storage substitution. Interrupt entry
and return do not automatically save or restore these registers; firmware must
manage its own context. Init zeros them for ABI fixtures. Supported CPU reset
preserves their undefined architectural values as a deterministic fixture policy,
not a claim that silicon retains them.

PRID is a read-only processor identification input. The entire silicon value has
not been established for the requested board. `esk_xtensa_prid_configure` therefore
requires an explicit constant test/SoC input; an unconfigured read is unsupported.
The configured constant survives the supported CPU reset. It is not a sampled
pin waveform, guessed silicon revision or eFuse substitute. The pinned ESP-IDF
ESP32 CPU helper extracts core identity from bit 13; that bit's meaning does not
establish the other bits. Fixture constants `0xa5a50000` and `0xa5a52000` exercise
this extraction and full-width readback; neither is presented as a real chip value.

## Evidence

`test_xtensa_context` passes 9,723 assertions covering every operand register, THREADPTR access across
PS.RING/EXCM combinations, all MISC read/write/exchange operations, full-width
fixture PRID reads, read-only rejection, unsupported neighbors, privilege/window
priority, reset policy, NULL configuration and CPU isolation. Unknown instructions
remain failures, rather than fabricated illegal-instruction exceptions.

`run_context.py` uses independently GNU-assembled instructions and a Python
register oracle: **7,680 comparisons**. It checks all 16 operands, all four MISC
registers and unprivileged THREADPTR behavior. It also hash-pins and checks the
actual original-ESP32 toolchain feature configuration.

`run_tls_c.py` compiles real C `__thread` variables with Espressif GCC and links
local-exec TLS relocations. Firmware writes THREADPTR/MISC0, calls a non-inlined C
function that updates TLS, reads PRID and records the TLS variable address. Both
cores execute through the native shared RAM bus, interleaved one instruction at
a time. Six persistent TLS banks are initialized from ELF PT_TLS, including
zero-filled `.tbss`. The Xtensa linker adds
`align_up(TCB_SIZE=8, TLS alignment)` to TLS offsets, as documented in the pinned
ESP-IDF FreeRTOS port; the fixture accounts for this without changing target code.

**1,536 calls, 16 exact replays and 52,224 retired instructions pass.** Checks
include all inactive banks, initial/updated values, zero initialization, guards,
compiler-produced addresses, full-width MISC cookies, core-ID bit extraction,
window return and allocation cleanup. There are no Python memory callbacks.
Host ABI setup starts each function call; this is firmware-selected bank
acceptance, not proof of interrupt-driven preemption, migration, complete context
switching, FreeRTOS boot or cache coherence. Retired instructions are not cycles.

## Reproduction and provenance

Use the external Espressif GCC 8.4.0 esp-2021r2-patch5 toolchain already pinned in
[the target investigation](esp32-dual-core.md). Its `core-isa.h` SHA-256 is
`60d20f76c7730b24041b9f55b16e0062f30bf85c7661c7ed215f050fedcdb344`.

Additional external ESP-IDF v4.4.7 inputs, inspected on 2026-11-05:

| Source | SHA-256 | Fact used |
|---|---|---|
| [components/hal/esp32/include/hal/cpu_ll.h](https://github.com/espressif/esp-idf/blob/v4.4.7/components/hal/esp32/include/hal/cpu_ll.h) | `0b75eb5517f4ae4a0daf13574ee27aaa33394aa5149e89a4dab89471abaa18e1` | `rsr.prid`, `extui ...,13,1` |
| [components/freertos/port/xtensa/port.c](https://github.com/espressif/esp-idf/blob/v4.4.7/components/freertos/port/xtensa/port.c) | `b81102899f12b67b1228ad3ad9deb8ce5a71fb5155691e225672ccdd146a1052` | TLS linker offset and 8-byte TCB constant |

These files remain external test inputs; no vendor task scheduler or processor
implementation is copied into the runtime. The generated C/assembly fixture is
original test code. From the repository root, with actual tool paths substituted:

```sh
python3 tests/vendor/xtensa_integer/run_context.py \
  --core-isa /path/to/xtensa/config/core-isa.h \
  --assembler /path/to/xtensa-esp32-elf-as \
  --objcopy /path/to/xtensa-esp32-elf-objcopy
python3 tests/vendor/xtensa_integer/run_tls_c.py \
  --compiler /path/to/xtensa-esp32-elf-gcc \
  --objcopy /path/to/xtensa-esp32-elf-objcopy \
  --linker /path/to/xtensa-esp32-elf-ld \
  --nm /path/to/xtensa-esp32-elf-nm \
  --port-source /path/to/esp-idf/components/freertos/port/xtensa/port.c \
  --cpu-ll /path/to/esp-idf/components/hal/esp32/include/hal/cpu_ll.h
```

The runtime remains independent of these tools and SDK files. Full ISA/FPU/debug,
actual ROM/SDK boot, clocks/peripherals/radio and physical board qualification are
still separate gates.
