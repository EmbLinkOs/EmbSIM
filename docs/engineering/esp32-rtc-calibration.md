# Original ESP32 TIMG0 RTC calibration counter

**Implemented: an opt-in TIMG0 calibration controller/counter with explicit
full-period XTAL-tick inputs, one-shot completion and single-period cycling.
This does not calibrate CPU instruction timing or implement the complete RTC
clock tree, oscillators, timer group or SDK boot.**

## Real operation and supported sources

ESP32 TRM v5.9, TIMG registers RTCCALICFG/RTCCALICFG1, defines calibration as
counting XTAL clock cycles over a selected number of calibrated-clock cycles.
The result is 25 bits and is read-only. READY is read-only and indicates one-shot
completion. The pinned ESP-IDF v4.4.7 `rtc_time.c` describes both one-off and
continuous operation; continuous operation is enabled at SoC reset. The TRM
labels START_CYCLING reserved, so this profile names the SDK protocol explicitly
rather than inferring additional undocumented modes.

| Selection | SDK source contract | Input responsibility |
| --- | --- | --- |
| 0 | Currently selected RTC slow-clock mux | Caller supplies periods after mux selection |
| 1 | Internal RC_FAST divided by 256 | Caller supplies complete divided-clock periods |
| 2 | External XTAL32K | Caller supplies periods only when that clock exists/runs |

The TRM describes selection 0 as RC_SLOW; the SDK calls it RTC_MUX and handles
other selected slow sources. This implementation uses the SDK's mux-level input
contract. It does not infer the mux, oscillator presence, gating, startup,
frequency, drift, voltage or temperature. The SDK's approximate oscillator
constants are not adopted as measurements of the owner's board.

## Explicit state and observable registers

`esk_esp32_machine_rtc_cal_configure` supplies enable, initial RW control fields
and an explicit prior 25-bit result. It creates a fixture with READY clear; it
is not a generic historical snapshot or a silicon reset producer. The documented
control reset fields form 0x13000 (MAX=1, source 1, cycling enabled). Supplying
result zero is an explicit initial condition, not inferred elapsed calibration
history. Detach removes this profile without pretending to power down the SoC.

| Register | Address | Supported behavior |
| --- | --- | --- |
| TIMG0 RTCCALICFG | 0x3ff5f068 | START/MAX/source/cycling RW; READY RO; reserved bits ignored/zero |
| TIMG0 RTCCALICFG1 | 0x3ff5f06c | Completed VALUE in bits 31:7; writes ignored; low reserved bits zero |

- One-shot supports MAX 1..32767. A START rising edge clears READY and resets
  accumulated periods/ticks. Each selected complete period contributes its
  supplied XTAL ticks. Only after MAX periods does VALUE change and READY set.
  START remains a software-controlled bit; an unchanged write does not retrigger.
- Cycling is qualified only with MAX=1, matching the SDK's documented single-
  period sampling. Every selected period updates VALUE; it does not assert the
  one-shot READY latch. Larger cycling windows are unsupported until qualified.
- VALUE retains the last completed measurement during partial measurement,
  source stalls, stops and reconfiguration. Repeated control writes preserve
  progress. Software cannot forge READY or VALUE by writing RO fields.
- Stop clears pending progress and preserves the completed VALUE/READY latches.
  A later measurement start clears READY. Retargeting/resizing a pending
  one-shot without clearing START fails unchanged. Reconfiguring cycling
  restarts at the next complete supplied period, retaining the last VALUE.
  Physical in-flight phase and synchronizer latency remain unqualified.
- Selection 3, simultaneous START/cycling, active zero MAX, multi-period cycling,
  zero-tick intervals and a result above 25 bits fail unchanged. Overflow wrapping/saturation and
  undocumented ready behavior are not invented. Inactive MAX zero can be stored
  while preparing controls; activating it is unsupported.
- Other TIMG registers and TIMG1 calibration remain unsupported. No completion
  interrupt, watchdog, generic timer, hardware timeout or guessed delay is added.

The SDK restricts its own requested period count to less than 32767 and checks
possible result overflow before starting. Supporting the register's full 15-bit
field does not claim the higher-level SDK accepts every register encoding.

## Clock/event abstraction and integration

`esk_esp32_machine_rtc_cal_period(source, xtal_ticks)` supplies one **complete
selected-clock period** and the integer XTAL ticks observed within it. The
caller aligns these records to the measurement window and supplies clock mux,
gating and phase behavior. Wrong-source records are ignored; missing records
leave calibration pending. Quantized intervals may alternate counts, so the
counter sums the supplied ticks instead of rounding an average frequency.
Reference-clock gating and active zero-tick intervals are unqualified; such
intervals fail unchanged. A stopped source supplies no complete period records.

This API does not accept a fabricated final result or automatically convert CPU
retirements, CCOUNT, wall time or MMIO reads into clock progress. It is a functional
counter/controller driven by clock records, not an edge-level synchronizer or
physical oscillator model. Real captures or a separately qualified clock provider
are still required to establish real calibration timing/frequency.

The original C99 counter is shared by both native CPU buses. A fetched START
store commits before subsequent host periods are supplied. Host events and calls
are serialized between machine turns; re-entry while executing fails. CPU-only
APP reset preserves the calibration state. Full power/watchdog/peripheral/global
reset domains and clock-enable routing are not qualified. Fetch/CAS and narrow
or misaligned MMIO accesses are unsupported.

## Proof and limits of vendor evidence

- **919,967 native assertions** check all three sources, one-shot sizes through
  32767, exact completion boundary, cycling, RO fields, stalls, source filtering,
  rearm, stop, unsupported mutations/overflow with unchanged state, shared buses,
  fetched START and CPU-reset retention.
- **19,208 independent period/result/READY comparisons** use pinned register
  fields and independent sums for one-shot and repeated single-period cycling.
- **486 actual ESP32 C harness calls / 39,108 retired instructions** run on both
  native CPUs. Ten unchanged SDK access macros operate the real register
  addresses; the harness follows the documented setup/start/poll sequence.
  Host periods arrive between execution turns, checking partial progress and
  completion. A separate bounded-poll case receives no clock records and stays
  pending despite firmware execution. No PC skip, memory callback implementing
  the registers, fake oscillator or delay-function stub supplies a result.

These are compiled harness calls using SDK macros, **not a claim that the full
`rtc_clk_cal_internal` implementation or its clock-enable/delay helpers run
unchanged**. Those depend on the remaining RTC clock tree and timing work. The
stalled harness checks pending state with bounded polls; it does not qualify the
SDK's microsecond timeout duration. Full SDK calibration, analog clocks and
instruction timing remain unverified.

```sh
python3 tests/vendor/xtensa_integer/run_rtc_cal.py \
  --timer-reg "$IDF_PATH/components/soc/esp32/include/soc/timer_group_reg.h" \
  --soc-header "$IDF_PATH/components/soc/esp32/include/soc/soc.h" \
  --rtc-time "$IDF_PATH/components/esp_hw_support/port/esp32/rtc_time.c" \
  --compiler xtensa-esp32-elf-gcc --objcopy xtensa-esp32-elf-objcopy \
  --linker xtensa-esp32-elf-ld --nm xtensa-esp32-elf-nm
```

Inputs are pinned to
[ESP-IDF v4.4.7 / 38eeba213aa695aabfd6d89aa9f5078dbe5a94c3](https://github.com/espressif/esp-idf/tree/38eeba213aa695aabfd6d89aa9f5078dbe5a94c3).
Selected macros and register headers retain their source notices in temporary
build files. They are external test inputs; no vendor runtime is copied into the
simulator. The macro fixture selects the SDK's default inactive DPORT-assert
branch; calibration MMIO is outside DPORT.

| Input | SHA-256 |
| --- | --- |
| `timer_group_reg.h` | `9aac43c44e42188bc7381e0936166383575c554157d2b087bf46d89b03d35e6c` |
| `soc.h` | `7a7148c288d7c289c961810e5617fa7b14d105b28349a45f772150b1e0845dd3` |
| `rtc_time.c` | `7542476747cd6175a7f846d3ac1af21914717f1278e3d9b0cdc5ab04c4f21883` |
| Source enum `soc/rtc.h` | `7a23243c95754644c449b093a9d8ee1422a18bf4032ce0875570faadb73b1a05` |

## Native ROM investigation

The following records preserve the calibration milestone. The
[digital pad-hold follow-up](esp32-digital-pad-hold.md) now passes its former
next blocker; the latest boot boundary is recorded there.

Add **`--rtc-cal-fixture`** to the existing diagnostic with explicit
eFuse/RTC/GPIO/UART-bank/mask-ROM fixtures. This supplies control 0x13000 and
prior value zero, with no clock periods by default. That unclocked case follows
a ROM fallback path; it does not prove detection of the owner's 40 MHz crystal.

Optionally add **`--rtc-cal-initial-period-ticks 1280`** to supply one source-1
period before PRO execution. The actual counter then records 1280 (0x500), and
the report includes its retained result. This is a synthetic test period, not a
physical capture, inferred oscillator frequency or instruction-time conversion.
No additional clock periods are generated automatically.

Both fixtures now reach **6,872 retired instructions / 6,873 attempts**, stopping
at **RTC_IO_DIG_PAD_HOLD_REG read 0x3ff48474**, PC **0x4000a67d**, opcode **0x298**.
[Unclocked evidence](../../tests/vendor/xtensa_integer/evidence/boot-with-rtc-cal-unclocked-2026-11-06.json)
and [explicit-period evidence](../../tests/vendor/xtensa_integer/evidence/boot-with-rtc-cal-period-2026-11-06.json)
record the difference and retain `boot_verified: false`. Per-pad hold, complete
ROM/SDK boot, physical calibration and calibrated CPU timing remain unfinished.
