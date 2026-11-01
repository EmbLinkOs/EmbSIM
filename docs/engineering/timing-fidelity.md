# Timing fidelity and performance

The owner's direction (2026-11-01): the simulator has to model the supported
hardware's timing as rigorously as an HDL simulator models a design (Vivado
is the reference for discipline, not for method), while staying fast enough
to develop and debug on. The measure that matters is an RTOS context switch
on a simulated board: how many cycles and how much simulated time it takes,
between boundaries that are stated, whatever the host's speed. No release
until this holds.

This document is the audit (§1), the accuracy model each target is held to
(§2), the plan (§3), and what has been validated, with every remaining source
of error (§4). It is updated as each step lands. A figure is quoted only with
its source; a figure the documents do not give is named as missing, never
filled in.

## 1. Audit (2026-11-01)

### 1.1 What is sound

- **Time and ordering.** Simulated time is an integer count of picoseconds
  (`core/time`). Clocks are exact rationals (a period of `num/den` ps, edges
  counted from an origin), so a 16 MHz or 180 MHz clock never drifts. Events
  order by time, then by class (EXTERNAL, CORE, PERIPHERAL, SIGNAL, ANALOG,
  PROBE), then by insertion (architecture 03 §2, SPEC-002 §4). A run is
  deterministic: the same project and inputs give the same events in the
  same order on every host. Host speed never enters simulated time; paced
  real time (ADR-032) only waits between quanta.
- **On-chip peripherals follow the core cycle by cycle.** A machine's
  devices (timers, SysTick, UARTs, ADCs) see every cycle the core charges
  (`esk_mach_advance`), lazily but synchronised before any access or event,
  so a timer's overflow raises its interrupt at the cycle it overflows,
  inside a quantum.
- **The AVR is exact** (ADR-015): each instruction's cycles are the
  datasheet's, and the interrupt response charges 4 cycles (4 more from
  SLEEP).

### 1.2 Gaps

The gaps are listed by how much they matter to an RTOS context-switch or
interrupt-latency measurement on an ARM target, the main use case.

1. **Cortex-M exception entry, return and tail-chaining take no cycles.**
   `exc_entry` and `exc_return` (`core/mcu/cortexm/cortexm.c`) stack and
   unstack the frame without charging anything. The Cortex-M4 TRM (ARM DDI
   0439B §3.9.1) gives at most 12 cycles from an interrupt to the handler's
   first instruction, 12 for the return, and 6 for a tail-chain, all with
   zero-wait-state memory. A PendSV context switch is two of these around
   the handler, so today it reads about 24 cycles short.
2. **Late arrival, the extended (FP) frame and lazy stacking take no
   cycles either**, and an interrupt during a divide or LDM/STM is not
   abandoned or continued as the TRM describes (§3.9.1: the divide restarts;
   LDM/STM continue through EPSR.ICI).
3. **Pipeline refill is a fixed 2 cycles.** Table 3-1's P ranges from 1 to
   3, depending on the target's alignment and width and on whether the
   branch address was speculated early. The cost table is the TRM's,
   simplified (`cost.h`), and declared `estimated`.
4. **One cost table for every Cortex-M.** The RP2040's Cortex-M0+ (two-stage
   pipeline, its own cycle counts and interrupt latency, which the M0+ TRM
   gives and this work has yet to fetch) and the STM32F103's Cortex-M3 are charged the M4's table.
5. **No memory wait states.** The STM32's FLASH_ACR LATENCY (0–7 wait
   states on the F446 at 180 MHz), its ART accelerator and prefetch, and the
   other parts' flash and bus timing are not modelled. Every access is zero
   wait. For code in flash at speed, the timing is optimistic by an amount
   that depends on the ART's hit rate.
6. **DWT is CYCCNT only.** The other counters (CPICNT, EXCCNT, SLEEPCNT,
   LSUCNT, FOLDCNT), which firmware and tools read to break down a
   measurement, are not there, and nothing can be compared against them.
7. **No measurement instrument.** There is no way to ask "cycles and time
   from here to there": no named boundaries, no per-event trace with cycle
   stamps across cores and peripherals, and no report that keeps simulated
   time apart from host time.
8. **The other cores are estimates without exception costs**: RISC-V (the
   FE310's E31, the ESP32-C3), MIPS (PIC32 M4K) and PIC24. Their traps and
   returns, wait states and pipeline effects are simplified like the
   Cortex-M's.
9. **Performance has no reproducible benchmark.** Speed has been tuned
   (lazy clocks, sleeping islands, adaptive quanta), but no committed
   benchmark reports simulated time per host second for fixed workloads in
   CI.

## 2. The accuracy model

Each statement EmbSIM makes about timing belongs to one of these levels.
Every result says which level it is at. A target is never described above
the level its model has been validated to.

| Level | What it promises | How it is checked |
|---|---|---|
| **Functional** | Correct results and the order of visible effects; no claim about when | Vendor examples and scenarios pass |
| **Estimated** (ADR-015's `estimated`) | Instruction costs from the vendor's documented table, simplified as stated; exception, memory and pipeline effects as listed for the target | The listed simplifications, each with a bound where known |
| **Cycle-exact for a stated scope** | Every cycle the vendor documents for a stated set of instructions, exception sequences and memory configurations | Fixtures that time each documented case, and hardware captures where available |
| **Electrical** | Voltages, currents and edges in time from component models (ADR-036, the analog engine) | ngspice references, datasheet curves |

Peripheral timing (baud rates, timer periods, ADC conversion times) is exact
with respect to the modelled clock tree, at every core level (ADR-015).

Per target today and the level each step aims for:

| Target | Today | Aimed for in this work |
|---|---|---|
| ATmega328P (AVR) | Cycle-exact: instructions, interrupt response | Kept; validated against the datasheet's interrupt and timer figures |
| STM32F446 (Cortex-M4F) | Estimated | Cycle-exact for exception entry, return, tail-chain, late arrival and the FP frame at zero wait states; flash wait states and the ART modelled as far as RM0390 documents them, with the remainder bounded |
| STM32F103 (Cortex-M3) | Estimated (M4 table) | The M3's own figures where they differ; same exception scope |
| RP2040 (Cortex-M0+) | Estimated (M4 table) | The M0+'s own table and exception latency |
| nRF52840 (Cortex-M4F) | Estimated | As the F446, with its own flash and cache |
| FE310, ESP32-C3 (RISC-V), PIC32 (MIPS), PIC24 | Estimated | Stay estimated; trap costs added where the manuals give them |

## 3. Plan

1. **This audit and model.** Done when it is reviewed and in the repository.
2. **Cortex-M exception timing** at zero wait states, from ARM DDI 0439B
   §3.9.1: entry 12, return 12, tail-chain 6, late arrival. The extended
   frame and lazy stacking are added too, but only with a figure ARM
   documents. Fixtures time each case with DWT.CYCCNT.
3. **Measurement.** Named boundaries (a PC, a symbol, an exception entry or
   return, a pin edge, a register write) give start and end stamps with the
   cycle count, simulated picoseconds and the core clock, across cores and
   peripherals. A trace records instructions, exceptions and peripheral
   events with cycle stamps. Reports keep simulated time and host time in
   separate fields. A measurement made by the simulator itself costs the
   firmware nothing; one made by firmware code (DWT reads) is reported with
   its own overhead.
4. **The context-switch benchmark.** EmbLinkRTOS and FreeRTOS on the
   NUCLEO-F446RE: a PendSV switch measured from a stated boundary (the
   instruction that pends it, or the interrupt's assertion) to a stated
   boundary (the first instruction of the next task), in cycles and in time,
   reproducible from one command.
5. **Memory timing.** FLASH_ACR wait states and the ART and prefetch on the
   STM32F4 (RM0390 §3.4), then the F1's prefetch buffer. Each documented
   case gets a fixture; the undocumented parts (the ART's exact replacement
   policy) are bounded and labelled.
6. **The other Cortex-M variants**: the M0+ table and latency for the
   RP2040, and the M3's figures for the F103.
7. **Performance.** A benchmark set run in CI that reports simulated time
   per host second for an idle RTOS, a busy loop, a UART stream and an
   analog-heavy board. Speed work keeps the declared timing level; any
   optimisation is checked against the timing fixtures.
8. **Validation and documentation.** Each figure is checked against its
   document. A hardware capture is used where one can be obtained. What is
   still estimated, and by how much it can be off, is listed in §4 and in
   each MCU's fidelity in the catalog.

## 4. Validated, assumed, unsupported

Filled in as the steps land. Nothing is validated by this document alone.

### 4.1 Cortex-M exception timing (step 2, ARMv7-M)

`core/mcu/cortexm/cortexm.c` charges ARM DDI 0439B 3.9.1's figures at zero
wait states on ARMv7-M (the STM32F446, the STM32F103's M3, the nRF52840):

| Sequence | Cycles | Boundary |
|---|---|---|
| Entry | 12 | from the end of the instruction after which the exception is taken, to the start of the handler's first instruction |
| Return | 12 | from the end of the EXC_RETURN instruction (`bx lr`, `pop {.., pc}`, charged 1: its refill is the return's) to the start of the instruction returned to |
| Tail-chain | 6 | from the end of one handler's EXC_RETURN instruction to the start of the next handler's first instruction, in place of the return and the entry |
| Late arrival | 12, unchanged | an interrupt that becomes pending 1 to 12 cycles into a frame's stacking and outranks the one being entered takes the frame; the other then tail-chains |

**Validated** by `tests/unit/test_cm_timing.c`, which runs hand-assembled
code one instruction at a time and checks each count against the machine's
cycles:
- an entry (12), a tail-chain (6) and a return (12), each with the cost
  table's charge for the instruction in between subtracted, as stated in
  the test;
- SysTick firing at each cycle of IRQ0's stacking. At 1 to 12 cycles in, its
  handler runs first, 12 cycles after IRQ0 pended, and IRQ0 tail-chains in
  6 after it. From cycle 13, IRQ0's handler has started and SysTick
  preempts it.

DWT.CYCCNT counts these cycles, as on the part, and `exc_cycles` in the
core's state totals them.

**Assumed:**
- The TRM gives the entry as a maximum, and the model charges that maximum.
- The STM32F103's Cortex-M3 is charged the M4 TRM's figures. The M3 TRM has
  not yet been checked against them.

**Not charged** (no figure in DDI 0439B; each is listed in the MCUs'
fidelity):
- the extended frame's 17 extra words when the FP context is stacked;
- lazy stacking's later write;
- flash wait states on the stacking and the vector fetch;
- an interrupt arriving during unstacking (the core takes it after the
  return, with a full entry, where the hardware abandons the unstacking);
- ARMv6-M (the RP2040's M0+) entirely: its TRM's figures are not yet in
  the sources.

### 4.2 Measurement (step 3)

`run --measure FROM..TO` (`core/mcu/latency.c`, documented in
`docs/user/cli.md`) stamps four kinds of boundary:
- an instruction at an address or symbol starting;
- an exception being taken (`exc:N`);
- an exception becoming pending (`pend:N`);
- an exception's return being done (`ret:N`).

Each sample is given in cycles and in simulated picoseconds, converted
exactly at the core clock that ran each cycle. Host wall time is reported
apart. The analysis observes the run and adds nothing to the firmware's
cycles; it does slow the host, because every step goes through the analysis
hook.

**Validated** by `tests/vendor/freertos/run.py` (CI's linux-gcc job, the
unmodified FreeRTOS V11.1.0 kernel on the NUCLEO-F446RE, core on HSI at
16 MHz):
- the tick (`exc:SysTick..exc:SysTick`) averages exactly 16 000 cycles, 1 ms
  at 1 kHz, with at most an instruction's jitter;
- each context switch (`pend:PendSV..ret:PendSV`) equals its request
  latency (`pend:PendSV..exc:PendSV`) plus PendSV's entry to return, sample
  by sample;
- a switch takes at least the 24 cycles of an entry and a return;
- two runs measure exactly the same.

**Measured** on that run: a context switch takes 155 to 167 cycles (9.69 to
10.44 µs at 16 MHz). This counts the request latency (0 to 5 cycles), the
12-cycle entry, the handler (the kernel's `xPortPendSVHandler` and
`vTaskSwitchContext`, at the cost table's estimate), and the 12-cycle return.
The instructions are estimated, so this is a simulated figure, not a hardware
one. No capture from a real NUCLEO-F446RE is available yet to compare it
with. Flash wait states are not modelled yet (step 5). What they add at this
clock, and at 180 MHz, will be stated when step 5 takes their figures from
RM0390.

