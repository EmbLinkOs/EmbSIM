# Original ESP32: supported CPU reset and vector profiles

This is a bounded extension of the [experimental ESP32 foundation](esp32-dual-core.md),
not a registered board target or a complete silicon reset model. It separates
static reset entry, relocatable exception locations and firmware's boot-address
handoff. No Espressif ROM or ROM implementation is bundled.

## References and supported state

Reset addresses and exception offsets come from the actual Espressif ESP32
GCC 8.4.0 esp-2021r2-patch5 `xtensa/config/core-isa.h`:
SHA-256 `60d20f76c7730b24041b9f55b16e0062f30bf85c7661c7ed215f050fedcdb344`.
The main foundation document records its archive URL/hash. The Xtensa ISA PS,
loop, relocatable-vector and interrupt register definitions distinguish defined
reset fields from undefined state.

TRM v5.9 chapter 12 and the pinned ESP-IDF v4.4.7
[dport_reg.h](https://github.com/espressif/esp-idf/blob/v4.4.7/components/soc/esp32/include/soc/dport_reg.h)
define the APP reset-input register at `3FF0002C`, bit 0, initial field 1.
[ESP-IDF's CPU startup](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_system/port/cpu_start.c)
uses clock/stall/reset controls; its
[linker vector layout](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_system/ld/esp32/sections.ld.in)
places handlers at the configured dynamic offsets. These are references, not
vendor startup code executed by the acceptance fixture below.

`esk_xtensa_lx6_reset` operates on an initialized CPU profile:

| State | Supported reset behavior |
| --- | --- |
| Static reset input 0 | PC `50000000` |
| Static reset input 1 | PC `40000400`, the configured default address |
| PS | `0000001F`: EXCM set, interrupt mask 15, window-overflow enable clear |
| LCOUNT | 0 |
| VECBASE | `40000000`, relocatable-vector profile enabled |
| Exception/CPU interrupt profiles | Enabled for supported operations |
| WAITI and last-delivery metadata | Cleared |
| Pending CPU requests | Chosen zero; the provider must re-drive external levels |
| Undefined architectural registers | Prior initialized fixture values preserved |

Preserving undefined registers is a deterministic fixture policy, not a claim
that silicon retains them. It covers ARs/window state, SAR/SCOMPARE1, loop
addresses, exception context, INTENABLE and counter/compare values. INTERRUPT
is architecturally undefined after reset; choosing no pending requests is also
an explicit policy. Firmware must initialize states it depends on. No reset
values are fabricated for unimplemented debug, FPU, MMU/cache, MEMCTL, security
or other state. Invalid reset-input selections fail before changing the profile.

## Relocatable exception locations

`esk_xtensa_vecbase_configure` enables the explicit ESP32 vector profile.
RSR/WSR/XSR.VECBASE use privileged SR231. The supported interface accepts
1024-aligned bases only. Other writes fail unchanged with an unsupported
operation diagnostic: low-bit masking and optional lock-bit behavior have not
been silicon-qualified. This alignment restriction is a functional profile,
not a measured claim about those silicon bits.

| Dynamic group | Offsets from VECBASE |
| --- | --- |
| Window overflow/underflow 4 | `000`, `040` |
| Window overflow/underflow 8 | `080`, `0C0` |
| Window overflow/underflow 12 | `100`, `140` |
| Interrupt levels 2..5 | `180`, `1C0`, `200`, `240` |
| Debug/level-6 location | `280`; debug exception delivery remains unsupported |
| NMI/level 7 | `2C0` |
| Kernel, user, double exception | `300`, `340`, `3C0` |

Dynamic entry uses VECBASE plus the pinned offset. The static reset addresses
do not move with VECBASE. Configuring locations does not itself enable exception
or interrupt delivery. Legacy ABI fixtures can still use caller-supplied vectors
until this mode is enabled. Once enabled, VECBASE is authoritative; legacy
configuration APIs cannot substitute arbitrary locations. Cached vector arrays
are refreshed for inspection, and entry derives its address from the active base.
Register-window and privilege checks precede a register exchange or rejection.
No pipeline, RSYNC latency, MMU translation or automatic bus-fault dispatch is
inferred from successful relocation.

## APP reset hold and release

`esk_esp32_machine_app_reset_configure(machine, vector_input)` explicitly enables
the reset-input profile and asserts APP reset. The caller supplies reset-vector
input 0 or 1; no board strap, RTC wake or reset-cause selection is inferred.
Without opting in, the legacy debug machine keeps the reset register unsupported.

In this profile DPORT `3FF0002C` bit 0 holds/releases APP. Assertion applies the
supported CPU reset state at an instruction boundary; a self-resetting store
commits first. A host write between turns applies immediately. Repeated writes
of an already asserted bit do not create another transition. This does not
qualify hardware assertion/deassertion phase or synchronizer latency.

| Execution condition | Machine event |
| --- | --- |
| Host debug pause | 19 |
| APP held in reset | 22 |
| APP clock disabled | 20 |
| APP run-stalled | 21 |

The table is in priority order. Held reset prevents fetch, retirement, interrupt
entry and gate-aware supplied-clock advancement; its compare deadline is absent.
Matrix source levels remain outside the CPU and are re-driven on release. A
held peripheral request is not acknowledged by resetting its destination CPU.
CPU-only reset preserves the peer, RAM, supplied ROM, remap/matrix/control
registers and diagnostic retirement totals within this model. Global/RTC/watchdog,
power-on and retention-domain behavior remain separate unimplemented contracts.

Release does not override a debug/fault pause, enable the clock or clear stall.
It does not use BOOT_ADDR as PC. ROM code must fetch from the selected static
reset entry and perform any handoff. A missing ROM causes a fetch diagnostic at
the static address even when BOOT_ADDR points to valid executable RAM. Existing
`machine_configure` remains a debug fixture operation, not a reset/boot sequence.
APP fixture configuration is rejected after opting into its reset-input profile,
so it cannot silently replace the new profile with an ABI fixture. PRO fixture
setup remains available for a supervisor.

## Proof and reproduction

- `test_xtensa_vectors`: 2,281 assertions for defined reset fields, undefined
  fixture policy, all register operands, rejected unaligned writes, all six
  window entries, general exceptions and supported CPU interrupt levels.
- `test_esp32_reset`: 104 assertions for hold/release, self-reset commit,
  pause/clock/stall priority, missing-ROM rejection, live-source preservation,
  alternate static entry and remap isolation.
- `run_vectors.py`: 3,072 GNU-assembled VECBASE accesses, 960 dispatches using
  parsed vendor offsets and 128 static reset comparisons using the pinned header.
- `run_controls.py --include-reset`: 512 register-field comparisons through both
  CPU bus views using parsed vendor addresses, widths and defaults.
- `run_reset_c.py`: 128 compiled supervisor/worker reset cycles through an
  original test-ROM program, 128 static ROM entries, 16 exact replays and 11,904
  retired instructions. The fixture initializes window state/INTENABLE/PS and
  VECBASE, reads BOOT_ADDR in firmware and calls the worker through emulated
  instructions. It checks stack guards, cumulative shared state, a peripheral
  source surviving reset and caller/window state. Its 2,448 held-reset turns
  are instruction attempts, not physical cycles.

The original test ROM is compiled into a temporary image and supplied through
the external ROM API. It is neither Espressif ROM nor evidence of real ROM,
ESP-IDF/Arduino boot, flash/cache, full reset coverage or board support. There
are no Python memory callbacks or host PC overrides in its APP handoff.

```sh
python tests/vendor/xtensa_integer/run_vectors.py \
  --core-isa /path/to/xtensa-esp32-elf/include/xtensa/config/core-isa.h \
  --assembler /path/to/xtensa-esp32-elf-as \
  --objcopy /path/to/xtensa-esp32-elf-objcopy
python tests/vendor/xtensa_integer/run_controls.py \
  --dport-reg /path/to/esp-idf/components/soc/esp32/include/soc/dport_reg.h \
  --soc-header /path/to/esp-idf/components/soc/esp32/include/soc/soc.h \
  --include-reset
python tests/vendor/xtensa_integer/run_reset_c.py \
  --compiler /path/to/xtensa-esp32-elf-gcc \
  --objcopy /path/to/xtensa-esp32-elf-objcopy \
  --linker /path/to/xtensa-esp32-elf-ld \
  --nm /path/to/xtensa-esp32-elf-nm
```
