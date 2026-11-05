# Original ESP32 MEMCTL and functional loop buffer

**Implemented: opt-in MEMCTL bit 0, reset/read/write/exchange and a bounded
256-byte functional instruction loop-buffer profile. Silicon fill/replacement,
fetch waveforms, power, pipeline and instruction timing remain unqualified.**

## Real operation and profile boundary

The pinned original ESP32 core configuration has a 256-byte loop buffer and
zero Xtensa I/D-cache bytes. Xtensa ISA RI-2021.8 section 4.3.2 and table 191
specify MEMCTL SR97 bit 0 as loop-buffer enable, reset 1. Clearing it flushes the
buffer; proper instruction-memory synchronization, including ISYNC, flushes too.
The buffer holds part/all of a hardware loop to reduce instruction-memory fetches.
Other MEMCTL cache/snoop fields are not qualified for this configuration; writes
outside bit 0 fail unchanged instead of accepting controls that do nothing.

`esk_xtensa_memctl_configure` explicitly enables this profile. RSR/WSR/XSR have
the existing privilege/window rules; original-ESP32 one-ring mode remains
privileged. Enabled CPU reset restores bit 0 to 1 and empties valid bytes.
Legacy ABI fixtures remain disabled until opt-in, preserving their previous
instruction-bus behavior. State and buffered bytes belong to each core.

The ISA summary does not define the physical buffer organization or exact
capture/replacement schedule. This model chooses the following **deterministic
functional policy**, rather than claiming a measured LX6 microarchitecture:

- Capture fetched bytes in the first 256 bytes of nonwrapping `[LBEG,LEND)`
  while enabled and outside EXCM. Longer loops still execute normally; bytes
  beyond the prefix use the provider. Mixed-width instructions can cross it.
- Replay valid bytes without another provider fetch. Do not synthesize CPU
  cycles, cache-miss penalties or bus transactions from hit/miss counts.
- Invalidate on ISYNC, disable, CPU reset, explicit bound writes, changed bound
  tags and exception/interrupt entry. This conservative exception-flush policy
  is not asserted as the silicon's exact replacement behavior.
- Preserve the previous CPU/buffer state if fetch or execution fails. Bytes are
  staged in a temporary execution state; failed partial instruction fetches and
  unsupported decoding do not install new valid bytes.

Instruction permission checks run before every byte, including hits. The buffer
cannot grant execute access, allocate memory or bypass the region profile.
Exception/interrupt polling precedes fetch, as before. Direct `window_execute`
uses supplied bytes; `window_step` owns actual fetch/capture/replay. An ISYNC or
disable executed from a buffered instruction invalidates its own captured bytes.

A software write to code does not silently snoop/invalidate this buffer. Proper
self-modifying code must synchronize instruction memory. Behavior before the
required synchronization is not a compatibility guarantee. The selected buffer
policy does not qualify all physical fetch traces or debugger/pipeline effects.

## Evidence

`test_xtensa_memctl` passes **268,232 assertions**, including every operand
register/form, upper-bit rejection, privilege/reset, enabled/disabled loops of
2..512 bytes, exact prefix fetch counts for the chosen policy, mixed two/three-
byte instructions across byte 256, self-modifying-code ISYNC, exception/interrupt
flush, bounds, core-state isolation and failed-fetch/decode rollback.

`run_memctl.py` independently GNU-assembles register operations: **96 comparisons**.
Actual ESP32 GCC then compiles original C with a hardware loop, writes MEMCTL,
executes ISYNC, computes a sum and restores MEMCTL. Both cores, both enable
values and five loop counts pass: **20 calls, 20 compiled hardware loops and
2,332 retired instructions**, through native instruction memory. Host ABI setup
is explicit. No CPU timing is inferred from retirement or provider fetch counts.

The subsequent [RTC reset/retention profile](esp32-rtc-reset-retention.md), with
an additional explicit synthetic snapshot, now advances to analog configuration
read after 6,698 instructions. The historical MEMCTL-only result below retains
its original source/fixture scope.

The external real-ROM diagnostic at this milestone passes MEMCTL initialization and retires
**6,692 instructions**, then stops at **RTC_CNTL_RESET_STATE_REG** read,
address 0x3ff48034, PC 0x400081df, opcode 0x0828. The
[hash-recorded probe](../../tests/vendor/xtensa_integer/evidence/boot-with-memctl-2026-11-05.json)
still uses external ROM/SDK flash, a synthetic eFuse image, explicit PRID and
artificial completion scheduling. No fallback MMIO or PC shortcut is introduced.
Full ROM-to-application boot, RTC reset/wakeup state and subsequent requirements
remain incomplete.

## Timing work

The [timing qualification project](esp32-instruction-timing.md) builds a hardware
CCOUNT measurement firmware and validates complete capture logs. It is groundwork
for physical measurement, not a calibrated simulator timing model. The current
loop-buffer profile must not be described as cycle accurate.

## References and reproduction

Xtensa ISA Summary RI-2021.8, loop option and table 191, external PDF SHA-256
`474c4aaf2d429ff79c48127e738c4110cecd8072133169ad6d00a050edf244b7`.
Original ESP32 `core-isa.h` declares `XCHAL_LOOP_BUFFER_SIZE=256` and zero I/D
cache sizes; SHA-256
`60d20f76c7730b24041b9f55b16e0062f30bf85c7661c7ed215f050fedcdb344`.

```sh
python3 tests/vendor/xtensa_integer/run_memctl.py \
  --assembler /path/to/xtensa-esp32-elf-as \
  --compiler /path/to/xtensa-esp32-elf-gcc \
  --objcopy /path/to/xtensa-esp32-elf-objcopy \
  --linker /path/to/xtensa-esp32-elf-ld --nm /path/to/xtensa-esp32-elf-nm
```
