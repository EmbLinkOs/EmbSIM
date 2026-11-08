# Original ESP32 instruction timing and qualification

**Current mode: deterministic functional execution, not cycle-accurate original
ESP32 simulation. No instruction latency table is installed.**

Instruction retirement counts, externally advanced CCOUNT ticks, flash-chip
operation deadlines and artificial test-event schedules are different quantities.
They must not be converted into elapsed CPU time by treating retirement as one
cycle. Clock capability (up to 240 MHz) also does not establish current frequency,
instruction duration, cache waits, bus contention or physical GPIO timing.

## Current implemented boundaries

- Selected instruction results, windows, exceptions, interrupts and both core
  contexts execute through serialized native callbacks. No issue/retire pipeline,
  dependencies, branch penalties or timed bus arbitration is implemented.
- CPU compare timers advance from explicitly supplied processor-clock ticks.
  WAITI, APP gate/stall/reset behavior follows the existing bounded clock contract.
- Flash operations advance from separately supplied microseconds with named-part
  datasheet duration bounds. These are fixture deadlines, not measured CPU cycles.
- The [MEMCTL loop-buffer profile](esp32-memctl-loop-buffer.md) implements bit 0,
  functional fill/replay and invalidation. Its exact fill/flush geometry is an
  explicitly selected policy; it is not a measured silicon fetch or timing model.

## Concrete hardware measurement firmware

The new [cycle-probe project](../../tests/vendor/xtensa_integer/cycle_probe/README.md)
builds against pinned ESP-IDF v4.4.7 with actual ESP32 GCC. Its nine IRAM kernels
exercise NOP/arithmetic/division, dependencies, local-memory operations and an
atomic pair. It captures both cores sequentially, loop buffer disabled/enabled,
six loop counts and 17 repeats: 3,672 complete kernel observations.

The capture uses CCOUNT with explicit DSYNC/RSYNC framing and short maskable-
interrupt exclusions. MEMCTL is restored, output is deferred until collection
ends, and no MAC/security data is logged. Counts include framing, setup and
layout; they are not automatically subtracted or converted into instruction
latencies. Peer idle tasks, NMI/debug, clock calibration, flash/peripheral paths
and general bus contention remain separate qualification work.

The project was compiled successfully and reference-executed in Espressif QEMU
only. QEMU success qualifies firmware/control-flow and output, not physical
cycles. No physical board was programmed or measured in this session. A capture
analyzer rejects incomplete, duplicated, malformed, zero-cycle or falsely
qualified data, preserves outliers, and records provenance as unauthenticated.
Its reports always retain `timing_model_qualified: false`.

## Requirements before claiming timing support

1. Collect reproducible physical-board data with the exact silicon revision,
   effective clocks, code layout/placement, operands, interrupt/debug conditions
   and toolchain recorded. Compare repeated groups and retain their distributions.
2. Qualify instruction classes and dependency/branch/window/exception costs on
   internal RAM, separately from external flash, SoC cache state and local/PIF
   access paths. Loop-buffer behavior requires its own hardware comparisons.
3. Introduce timed issue/completion and shared-bus events so side effects,
   CCOUNT/interrupt deadlines, WAITI, core gates and peripheral time agree. Adding
   durations after synchronous writes alone does not establish those semantics.
4. Qualify clock-tree changes, memory wait states, two-core contention, DMA,
   cache misses and peripheral activity, then validate complete firmware traces.
   Unknown timing must remain explicitly unsupported in any qualified profile.

There is no fallback table of guessed one-cycle instructions. A future model may
be qualified for a documented subset; that must not be labeled cycle-accurate
whole-board execution until the relevant clock, pipeline and memory behaviors
have independent evidence.
