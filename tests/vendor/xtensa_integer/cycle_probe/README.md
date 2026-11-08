# Original ESP32 hardware cycle capture

This project builds an ESP-IDF v4.4.7 firmware that records **complete framed IRAM
kernel costs**, using CCOUNT. It is preparation for hardware timing qualification,
not a simulator instruction-cost table. No physical capture has been collected.
The reference QEMU run checks firmware/control-flow and output only; its counter
values must not calibrate silicon timing.

## Measurement contract

The firmware runs nine kernels on PRO and APP sequentially, with MEMCTL loop
buffer disabled/enabled, six iteration counts (1/4/16/64/256/1024) and 17 repeats:
3,672 samples. Both cores do not benchmark concurrently. Maskable interrupts are
disabled only around each short measurement and MEMCTL is restored afterward.
All samples are collected before UART output. Normal peer idle tasks, NMI/debug,
SDK state and physical bus/power effects are not fully isolated.

Kernels are NOP, dependent ADDI/MULL, independent/dependent QUOU, DRAM L32I/S32I/
L32AI, and a matched WSR.SCOMPARE1 + S32C1I pair. Independent QUOU divides the
iteration count by 3; dependent QUOU starts at 0x12345678 and repeatedly divides
by 3. Other arithmetic starts at 0x12345678; MULL multiplies by 3. The atomic
pair uses a zero word and stores zero so every iteration matches. These operands
and dependencies are part of the measurement, not universal instruction latency.

Kernel bodies use assembler `no-transform` to preserve the requested 24-bit
encodings (rather than silently relaxing ADDI/L32I/S32I/NOP to density forms).
CCOUNT differences include LOOP setup, RSYNC/DSYNC/counter framing and assembler
layout. There is no automatic overhead subtraction or per-instruction fit. The
reported CPU frequency comes from SDK clock configuration, not an independent
frequency measurement. SDK revision codes are reported raw. No MAC, flash dump,
keys or calibration fuses are collected.

## Build and collect

With the actual original-ESP32 GCC toolchain and ESP-IDF v4.4.7 environment:

```sh
idf.py -C tests/vendor/xtensa_integer/cycle_probe -B /tmp/embsim-cycle-probe \
  -D SDKCONFIG=/tmp/embsim-cycle-probe-sdkconfig -D COMPONENTS=main \
  -D CMAKE_POLICY_VERSION_MINIMUM=3.5 build
```

The CMake policy argument supports CMake 4 with this older SDK. The build produces
bootloader, partitions, ELF and application outside the repository. Defaults
select dual-core original ESP32, 240 MHz and a 4 MiB image; verify the effective
configuration on the host that will program the board.

Collection requires deliberately installing the measurement firmware on the
USB-connected board; that replaces the current application. Preserve it first
if needed, then use the normal `idf.py ... -p PORT flash monitor` workflow with
the same build/configuration arguments. Save the UART log through the final
`EMBSIM_TIMING` completion record. This project has not been uploaded to the
owner's board by this session.

Analyze a collected log:

```sh
python3 tests/vendor/xtensa_integer/cycle_probe/analyze.py capture.log \
  --origin physical-hardware --report timing-report.json
python3 tests/vendor/xtensa_integer/cycle_probe/test_analyze.py
```

Use `--origin reference-emulator` for emulator output. Origin is a caller claim,
not authenticated. Analysis requires one complete context and all 3,672 unique
samples; duplicate/missing/bad/zero-cycle records and false qualification flags
are rejected. It retains every accepted observation, including slow outliers,
and reports minimum/median/maximum for each complete kernel group. Every report
continues to state `timing_model_qualified: false`.

The [build/reference evidence](../evidence/cycle-probe-build-2026-11-05.json)
records exact input/output hashes and which checks were actually performed.
Real hardware captures and a separately reviewed timing model are still needed.
