# Original ESP32 software GPIO and global pad hold

**Implemented: explicit software-GPIO routing fixtures, native shared latches,
resolved digital inputs, push-pull/open-drain/HiZ output projection and the global
pad-hold subset of DIG_ISO. This is not complete GPIO/IO_MUX, physical pad loading,
power isolation, package/board integration or SDK boot.**

The [digital per-pin follow-up](esp32-digital-pad-hold.md) now adds the SDK-mapped
local hold register and combines all hold causes per pad. This page retains the
original global-hold baseline and its reproducible vendor evidence.

## Register identity correction

The earlier RTC evidence mislabeled 0x3ff48088 as ANA_CONF. The pinned ESP-IDF
header identifies it as **RTC_CNTL_DIG_ISO_REG**. Historical fault PC/address and
retirement counts are unchanged; its descriptive label is corrected and annotated.
The observed ROM path clears global pad force-hold and pulses auto-hold clear.
This implementation does not infer analog/power behavior from that address.

## Explicit routing and initial values

`esk_esp32_machine_gpio_configure` supplies **14 words**, in pairs of low/high
banks: route, input-enable, pull-up, pull-down, PAD_DRIVER, OUT, ENABLE. Each route
means an already selected, noninverted software-GPIO path through the otherwise
unimplemented IO_MUX/matrix. All initial latches/directions are supplied; other
PIN fields are fixed disabled choices in this fixture, not asserted chip reset
values. GPIO register headers mark many reset values undefined.

The logical GPIO mask follows the pinned SoC capability header. GPIO24/28..31 are
excluded; GPIO34..39 are input-only with no internal pulls. GPIO20 is a logical
slot in that SDK mask; this does not assert it is bonded/exposed on the owner's
package or 30-pin board. Routes are never inferred from a product photo or
package marking. Unrouted endpoints return unsupported, not a fabricated HiZ
physical pad. Detach/reconfigure are host fixture operations, not silicon reset.

## GPIO behavior

- OUT/OUT1 and ENABLE/ENABLE1 have ordinary writes and W1TS/W1TC aliases. Banks
  mask to 32/8 bits. Shared latches are visible to both serialized CPU contexts.
  Input-only pins never drive, even if firmware sets their direction bits.
- Selected GPIO_PIN registers expose PAD_DRIVER bit 2 in the fixed disabled-
  interrupt/wakeup profile. Other feature requests fail unchanged. OUT_SEL
  readback is the fixed software signal 256; writing the same selection is
  supported, while peripheral/inverted/changed selections fail.
- Output queries return **drive-low, drive-high or HiZ**, plus input-enable and
  pull controls. Open-drain high releases the output. Pull controls describe
  inputs to an external circuit solver; the model does not turn them into
  guessed voltage/current or drive strength.
- `gpio_input` supplies a resolved known/unknown digital level. IN/IN1 use the
  configured input-enable mask; disabled buffers read zero, as documented by
  the SDK helper. If any enabled input in the requested bank is unresolved,
  the read fails unchanged instead of assuming a floating level is ground.
  Outputs do not silently override external resolution or mask contention.
- GPIO W1 aliases are WO in the TRM and their reads remain unsupported. Some
  SDK high-bank write helpers read a WO alias through a bitfield macro first;
  those executions are not qualified. Native high-bank writes/latches and
  high-bank input reads are separately checked.

MMIO is aligned 32-bit only. Unknown GPIO registers, IO_MUX changes, matrix
peripheral routes, RTC per-pad hold, IRQ/wakeup/filter/strap/sleep configuration and
pad IV/voltage/glitch/timing behavior remain unsupported. This profile is an
unregistered experimental backend, not a graphical board-pin binding.

## Global pad hold at RTC DIG_ISO

Only global FORCE_HOLD, FORCE_UNHOLD, AUTOHOLD_EN, latched AUTOHOLD status and
write-one auto-hold clear are implemented. Other isolation fields retain their
published default bits. Any request to change memory, ROM, Wi-Fi, digital-wrap,
pad isolation or undocumented force controls fails unchanged; no memory-bank
map, power transition or floating-bus result is guessed.

When hold becomes effective, current output level, enable and open-drain controls
are captured. Subsequent register writes remain readable but do not alter the
projected held output. Release exposes current controls. FORCE_UNHOLD disables
hold; simultaneous FORCE_HOLD/FORCE_UNHOLD is explicitly unsupported because its
priority is unqualified. Global capture covers selected endpoints; local hold
now shares the same effective per-pad latch, as specified in the follow-up.

The auto-hold event is an explicit external latch input and only sets the latch
when armed. It is **not** a complete SoC Deep-sleep transition: the CPU, clocks,
power and wakeup path are not put to sleep by this API. Disarming does not silently
clear the latch; the dedicated clear pulse does. Clear reads zero and status is
read-only. Inputs remain resolved live levels while output controls are held,
consistent with the SDK's documented input behavior. Fixed input/pull route
configuration cannot be changed through unsupported IO_MUX operations.

All host input/output/event calls occur between instruction turns, including
while the core is marked running. Native fetched stores apply their bus effects
in the existing serialized functional contract. CPU-only reset preserves shared
GPIO/hold state; digital-core/global power reset and sleep retention are not
qualified by that observation. No physical instruction/bus duration is inferred.

## Evidence

- `test_esp32_gpio`: **3,313 assertions** covering all selected logical pins,
  direction/level/open-drain combinations, input-only outputs, bank widths,
  W1 aliases, resolved/unresolved/disabled inputs, explicit copied initial
  latches, fixed routing, failed-operation preservation, hold/release/latch
  clear, fetched stores and CPU-only reset preservation.
- `run_gpio.py`: **2,048 independent latch/output comparisons**, with register
  addresses and valid/output masks parsed from pinned vendor inputs.
- Actual ESP32 GCC executes **216 interactive calls / 25,380 instructions**
  through native buses and **nine unchanged SDK helper bodies**. Both CPU
  contexts use low-bank outputs (avoiding the unqualified WO alias reads),
  and GPIO34 as a resolved input. Between fetched instructions the host changes
  the input and supplies an auto-hold latch event; firmware observes the changed
  input and modifies/disables output while the projected pad remains captured.
  After the clear pulse, the output is HiZ. There are no Python memory callbacks,
  host PC handoffs or claims of executing a full Deep-sleep/SDK application.

With the additional `--gpio-active-fixture` (no routed board pads, explicitly
zero initial OUT/ENABLE, defined active DIG_ISO controls), the external ROM probe
reaches **6,734 retired instructions**. It then stops at **UART0 INT_CLR** write
0x3ff40010, PC 0x4000900b, opcode 0x08a9. The
[recorded probe](../../tests/vendor/xtensa_integer/evidence/boot-with-gpio-2026-11-05.json)
retains all earlier ROM/eFuse/RTC fixture limitations. No fallback MMIO is added;
UART, full board boot and calibrated instruction timing remain unverified.

## References and reproduction

ESP32 TRM v5.9 GPIO pin hold and RTC DIG_ISO; ESP-IDF v4.4.7 inputs:

| Input | SHA-256 |
|---|---|
| gpio_reg.h | 87609e6eb2349cfaa569344e3ee0fb6c38b9a1dbb23b81af32e701c53af1cf84 |
| gpio_ll.h | e1c839b9fe34ce525af1c51935a8b0409aade38452516daadf2f3f62062a2907 |
| gpio_struct.h | d7a6a542b8a0aa6e9530ca3a9201aefd4f12b195169cc8613a191bda2ed6cfc8 |
| misc.h | 552e2d4ce5be7d93d228eb3b48bd959d0485290a7476ce5a7de438fe7ab9e152 |
| soc_caps.h | 8f353538e6b94a0dac7097b41247f65c5429c3f4c7a0be3481a5e9c9fa71668d |
| gpio_sig_map.h | 40d374f73e03922ac13f4a9d7b6fa780f54e5afde7178ac5369bef4a41cc7c49 |
| driver/gpio.h | 0cd2f0ad1aaaba56e452943ff1c8022d07d00b707b93b368d5539fa759a193a2 |

RTC header pin and other target references are in the [RTC contract](esp32-rtc-reset-retention.md).
Unchanged test headers/helpers retain their vendor notices in temporary files;
no vendor implementation is compiled into the runtime.

```sh
python3 tests/vendor/xtensa_integer/run_gpio.py \
  --gpio-reg /path/to/gpio_reg.h --gpio-ll /path/to/gpio_ll.h \
  --gpio-struct /path/to/gpio_struct.h --rtc-reg /path/to/rtc_cntl_reg.h \
  --misc /path/to/hal/misc.h --caps /path/to/soc_caps.h \
  --sig-map /path/to/gpio_sig_map.h \
  --compiler /path/to/xtensa-esp32-elf-gcc \
  --objcopy /path/to/xtensa-esp32-elf-objcopy \
  --linker /path/to/xtensa-esp32-elf-ld --nm /path/to/xtensa-esp32-elf-nm
```
