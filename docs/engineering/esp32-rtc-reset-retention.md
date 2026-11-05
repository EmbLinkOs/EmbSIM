# Original ESP32 RTC reset state and retention registers

**Implemented: opt-in explicit reset-state/retention snapshots, reset-vector
latches, protected reset causes and DPORT APP-reset integration. This is not a
complete RTC, power/clock tree, wakeup or global reset-domain model.**

## Initial state and real registers

ESP32 TRM v5.9 chapters 7/9 distinguish CPU, digital-core and whole-system reset.
A CPU reset does not reset RTC state or clear RAM. RESET_STATE at 0x3ff48034 has
PRO/APP six-bit cause latches and two reset-vector selects. Its summary table
labels the register RO, but individual fields in register 9.12 and the pinned SDK
header specify the vector-select bits as R/W; cause fields are RO. This profile
follows those field definitions.

`esk_esp32_machine_rtc_configure` supplies an explicit 14-bit reset-state snapshot
and **all eight STORE words**. There is no inferred power-on cause, crystal value,
owner reset history or undefined retention content. Initial cause patterns are
raw fixture inputs, including patterns reserved by hardware; accepting a snapshot
is not claiming a producer can generate every pattern. Unknown upper snapshot
bits or an incomplete word list fail unchanged. NULL/0 detaches this host fixture,
not a physical RTC erase or power action.

| Register | Behavior |
|---|---|
| RESET_STATE bits 5:0 | PRO cause, software read-only |
| RESET_STATE bits 11:6 | APP cause, software read-only |
| RESET_STATE bits 13:12 | PRO/APP vector selects, R/W |
| STORE0..3, 0x4c..0x58 | Shared full-width retention words |
| STORE4..7, 0xb0..0xbc | Shared full-width retention words |

Offsets in the table are relative to 0x3ff48000. Reserved RESET_STATE write bits
are ignored and read zero; cause writes do not overwrite latches. MMIO requires
aligned 32-bit data accesses. Narrow/unaligned operations fail, unknown RTC
registers remain unsupported, and no RTC fetch/CAS path is added. Both CPU data
views see the same latches and retention words.

## Reset and vector integration

The existing DPORT APP-reset input now records APP reason **14** when an assertion
is applied, preserving PRO cause, vector flags and retention. This reason matches
TRM chapter 7 and SDK `EXT_CPU_RESET`. Reset still applies outside the bus callback,
after the writing instruction commits; the peer's CPU state is not mutated during
execution. Register changes and retained words are visible through the normal
machine bus.

The APP vector flag is sampled when this bounded reset is asserted: 1 selects
0x40000400, 0 selects 0x50000000. Subsequent flag writes select the next assertion,
not a live PC jump. The existing host `app_reset_configure` selects the initial
flag and asserts its reset input. This is fixture setup; exact electrical sampling
while reset is held is unqualified. BOOT_ADDR remains firmware-consumed data.

PRO's vector flag has ordinary latch/readback behavior, but no new PRO-reset
producer is claimed. Direct low-level CPU reset APIs bypass this machine event
model and do not synthesize RTC causes. RTC software-reset commands, watchdog,
brownout, deep sleep, wakeup, digital-core/global power-reset domains, analog
configuration, clocks and timing remain unsupported. No status values are supplied
for those devices merely to advance firmware.

## Evidence

`test_esp32_rtc`: **53,787 assertions** across all raw cause-field combinations,
vector writes, read-only protection, both-core shared retention, immutable input,
rejected snapshots/accesses, CPU-reset retention/vector/cause behavior and fetched
DPORT reset-store commit. These raw field combinations qualify widths/readback,
not 4,096 physically supported reset-source combinations.

`run_rtc.py` hash-pins SDK RTC register definitions, reset-reason enums, ROM linker
symbols and an external Espressif QEMU-distributed PRO ROM. It passes **1,281
independent field/retention/reset comparisons**, then calls the unchanged external
`rtc_get_reset_reason` routine for both selected cause fields on both execution
contexts: **256 calls / 1,792 retired instructions**, through native buses. Host
ABI call setup is explicit. This exercises the supplied PRO-ROM routine; it is
not APP-specific ROM qualification, complete SDK boot or a physical RTC capture.
No ROM bytes are embedded in the runner/application/repository.

With `--rtc-power-on-fixture`, the diagnostic explicitly supplies reason 1 for
each core, both vector selects 1 and eight zero retention words. These are chosen
snapshot inputs, not measured owner state or automatic defaults. The same external
ROM/SDK image now retires **6,698 instructions**, then fails at
**RTC_CNTL_ANA_CONF_REG** read 0x3ff48088, PC 0x4000770a, opcode 0x0258. The
[recorded result](../../tests/vendor/xtensa_integer/evidence/boot-with-rtc-2026-11-05.json)
remains blocked. Without that additional fixture, the existing 6,692-instruction
reset-state-read failure remains valid. No unknown-MMIO fallback or PC shortcut
is introduced; native application boot and physical timing remain unverified.

## References and reproduction

ESP-IDF v4.4.7 external inputs:

- `soc/esp32/include/soc/rtc_cntl_reg.h`, SHA-256
  `c2d17880e08b28288fba95c3daa48f9a8672722fd56a5616728ee025dc61e0fb`.
- `esp_rom/include/esp32/rom/rtc.h`, SHA-256
  `90bb6c9a0d497a264041c0ac7abdd0475375edcae812868a9deaed0044994f44`.
- `esp_rom/esp32/ld/esp32.rom.ld`, SHA-256
  `0160cb9e14e83de87ad7363bd4977a9dd353b22e868a9bd4474e167ee86e58ec`.

```sh
python3 tests/vendor/xtensa_integer/run_rtc.py \
  --rtc-reg /path/to/rtc_cntl_reg.h --rtc-header /path/to/rom/rtc.h \
  --rom-symbols /path/to/esp32.rom.ld --rom /path/to/esp32-v3-rom.bin
```

Use the [boot probe](esp32-boot-verification.md) with its existing ROM/flash/eFuse
inputs plus `--rtc-power-on-fixture` to reproduce the diagnostic. The complete
RTC/power domain must be qualified separately before claiming whole-board support.
