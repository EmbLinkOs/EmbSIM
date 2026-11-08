# Original ESP32 digital per-pin hold

**Implemented: SDK-mapped digital per-pin hold, combined local/global/auto-hold
latching, FORCE_UNHOLD override and live resolved input levels. This remains a
software-GPIO route profile, not complete RTC-pad/IO_MUX/peripheral pin routing,
electrical loading, power/reset domains or successful SDK boot.**

## Hardware mapping and supported pins

ESP32 TRM v5.9 sections 6.7 and 6.13 describe pin hold: local and global hold
signals combine, and the pad retains its configuration while held. The SDK
`GPIO_HOLD_MASK` table supplies the digital control mapping. Hold bits are not
GPIO-number shifts.

| Register bit | GPIO |
| --- | --- |
| 0 / 1 | 3 (U0RXD) / 1 (U0TXD) |
| 2 / 3 / 4 / 5 / 6 / 7 | 6 / 7 / 8 / 9 / 10 / 11 |
| 8 / 9 / 10 / 11 / 12 | 5 / 16 / 17 / 18 / 19 |
| 14 / 15 / 16 | 21 / 22 / 23 |

`RTC_IO_DIG_PAD_HOLD_REG`, **0x3ff48474**, initializes to its published zero
control value when the explicit GPIO fixture attaches. Supported writes update
the mapped bits together. Bit 13 refers to package-specific GPIO20 in the TRM,
but the pinned SDK hold table leaves GPIO20 unsupported. It and bits 17..31
therefore fail unchanged when set. RTC-capable pads use other hold registers;
those are not silently mapped onto this digital register. The table does not
establish which pads are exposed on the owner's development board.

## Effective hold and observable behavior

The [software-GPIO/global-hold contract](esp32-gpio-pad-hold.md) still defines
explicit routing and initial controls. The new register adds digital local
hold. For each routed pad:

- FORCE_UNHOLD disables local/auto hold even when their controls remain set.
  Simultaneous FORCE_HOLD/FORCE_UNHOLD remains unsupported, as in the baseline. The fixture's published DIG_ISO default has FORCE_UNHOLD set; the active
  ROM clears it before ordinary pad-hold use. Test setup explicitly releases it.
- Otherwise, local hold, FORCE_HOLD and the armed auto-hold latch combine with
  OR. A transition from unheld to held captures the current modeled controls.
- Adding another hold cause to an already held pad does not recapture changed
  live registers. Removing one cause does not release a pad while another cause
  remains. Removing FORCE_UNHOLD while a cause is pending captures current live
  controls at the new effective hold edge.
- OUT/ENABLE/PAD_DRIVER writes remain readable as live register values while
  pad output projection retains captured level, enable and open-drain state.
  Release exposes current live controls, including HiZ when disabled or an
  open-drain high releases the output. Unrelated pads remain independently live.
- Modeled input-enable and pull controls are captured consistently too. These
  route attributes are fixed in the current fixture; runtime IO_MUX mutation,
  function switching and drive-strength changes remain unsupported.
- Hold freezes controls, not external input samples. An enabled held input can
  still read changes from its externally resolved level, as the SDK documents.
  An unresolved enabled input still fails unchanged instead of becoming zero.

One effective mask and one snapshot per routed pad implement this behavior;
local/global snapshots do not compete for priority. Writes selecting known but
unrouted pad controls preserve the register latch without fabricating a pad
state: output queries for those endpoints remain unsupported. Host re-attach is
fixture setup, not a hardware reset or routing change that preserves old holds.

The register is shared by both native CPU bus views. Fetched hold stores take
effect through the normal serialized bus/commit path. The bounded CPU-only reset
preserves holds and snapshots. Other resets, Deep-sleep transitions, isolation,
physical glitch/synchronizer timing, peripheral signal paths and electrical IV
behavior remain unqualified. In particular, software-route pin tests do not
qualify flash/UART operation through held peripheral pins.

MMIO is aligned 32-bit data only. Unknown bits/features, narrow/misaligned access,
fetch and CAS remain unsupported. Invalid multi-bit requests preserve the entire
register and all captures; no supported bit is partially applied first.

## Proof and reproduction

- **3,455 new native assertions** cover all 16 mapped pads across enable, level
  and open-drain combinations, independent neighbors, live input-mode levels,
  pull/input projections, overlapping local/global/auto-hold causes, force-unhold,
  recapture after override, unsupported bits/unchanged failure, floating inputs,
  fetched stores, shared CPU buses and CPU-reset retention.
- The original **3,313 GPIO assertions** and the default vendor runner's
  216 C calls / 25,380 retired instructions remain passing.
- The extended runner retains **2,048 latch/pad comparisons** and adds **4,096
  independent per-pin/global transition turns**. Its referee parses the actual
  SDK masks, tracks live controls and per-pad snapshots separately, and checks
  projected outputs plus raw register/status/input observations across mixed
  local/global/unhold/auto-hold events.
- **256 actual ESP32 C interactive calls / 30,592 retired instructions** run on
  both native CPUs using **eight unchanged SDK helper bodies**. The C program
  uses the SDK's real hold-mask array, enables a pin hold, changes output controls,
  reads a live button/input, then releases the hold. Host events add/remove a
  global hold while the local hold remains set. Output and input-only operating
  modes are checked. No host PC shortcut or Python instruction-memory callback
  implements the GPIO operations.

Add the following to the [existing GPIO reproduction command](esp32-gpio-pad-hold.md#evidence-and-reproduction):

```sh
--per-pin-hold \
--rtc-io-reg "$IDF_PATH/components/soc/esp32/include/soc/rtc_io_reg.h" \
--gpio-periph "$IDF_PATH/components/soc/esp32/gpio_periph.c"
```

The default command keeps its original scope. Its nine helpers and the extended
mode's eight helpers cover eleven distinct SDK helper bodies in total. In the extended mode the linker
places the SDK mapping constant in the test image's read-only data section, and
the fixture loads both `.text` and `.rodata` into the supplied IRAM image. The
native CPU reads the real compiled lookup table; the host does not replace it.
Selected helper bodies, mapping data and headers retain their source notices in
temporary build files. No vendor runtime is bundled in the simulator.

The original GPIO/hold source hashes remain pinned to ESP-IDF v4.4.7 commit
[38eeba213aa695aabfd6d89aa9f5078dbe5a94c3](https://github.com/espressif/esp-idf/tree/38eeba213aa695aabfd6d89aa9f5078dbe5a94c3).
Additional inputs:

| Source | SHA-256 |
| --- | --- |
| `soc/esp32/include/soc/rtc_io_reg.h` | `a2936c43b3735fae816b7f4c4b76c76ce90694a50bba56d310e36379cf5f1119` |
| `soc/esp32/gpio_periph.c` | `bef42aa91bbea259885a60a535a8fd89fa4137b4c2af3d8ba8ac9b547032329a` |
| Public GPIO behavior `driver/include/driver/gpio.h` | `0cd2f0ad1aaaba56e452943ff1c8022d07d00b707b93b368d5539fa759a193a2` |

## Native boot investigation

The existing GPIO fixture now includes this qualified register, so the diagnostic
passes the prior 6,872-instruction pad-hold blocker. With the same explicit
eFuse/RTC/GPIO/UART-bank/mask-ROM/calibration inputs and one synthetic 1,280-tick
period, it reaches **6,902 retired instructions / 6,903 attempts**, then fails
**IO_MUX U0TXD read 0x3ff49088**, PC **0x40009156**, opcode **0x498**.
[Hash-recorded evidence](../../tests/vendor/xtensa_integer/evidence/boot-with-digital-pad-hold-2026-11-06.json)
retains `boot_verified: false`. The previous 6,872 result remains recorded against
its earlier source hashes; it is superseded by this implementation, not relabeled
as successful boot. UART pin routing/IO_MUX, physical board behavior, full SDK
boot and calibrated instruction timing remain unfinished.
