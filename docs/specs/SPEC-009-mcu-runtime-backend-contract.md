# SPEC-009 - MCU Runtime Backend Contract

**Status:** Accepted 2026-10-15; revised 2026-10-16 to the design M1
implements (ADR-019, ADR-020). **Builds on:** 04; ADR-005, ADR-015, ADR-016,
ADR-019, ADR-020. **Requirements:** `docs/requirements/MCU.md`.

---

## 1. Scope

The internal contracts between the session and an MCU instance: the machine,
core backends, the bus, register files, peripheral models, pins and the
multiplexer, the runtime's quanta, streams, the debug unit, and the rules for
code extracted from EmbCC.

## 2. MCU instance

An MCU is a component made with `esk_mcu_ops` (`core/mcu/mcu.c`).
`esk_mcu_attach` gives it a machine (`struct esk_mach`, `core/mcu/mach.h`)
built from its MCU description, its register description and the board's
external oscillator (`esk_mach_build`), and its firmware image, copied.
Without an image the MCU attaches, warns `mcu.no_image` and holds its core in
reset (state `no_image`); `esk_mcu_set_image` loads one later.

The machine owns: the bus, one core, its devices (the register file and the
core's system devices), breakpoints, the host hooks through which the runtime
sees its outputs (`out`, `readc`, `pin_cfg`, `pin_in`, `uart_tx`, `diag`, and
`exc`, which each core calls as it enters an exception or interrupt, for a
recording's observations: SPEC-012 §1),
the receive ports of its UARTs, and the flags the core and devices raise
(`asleep`, `yield`, `reset_req`, the stop reasons). It is EmbCC's
`struct sim` made per instance: no file-scope state (CS-1); every allocation
through the session's `esk_alloc`.

## 3. Core backend

```c
struct esk_cpu_ops {
    const char *name;      /* "cortex-m" */
    int elf_machine;       /* the e_machine of its images */
    const char *elf_name;  /* "an ARM image" */
    void (*reset)(struct esk_cpu *c);    /* power-on reset, then the reset sequence */
    void (*step)(struct esk_cpu *c);     /* one instruction, or an exception entry */
    u32  (*pc)(struct esk_cpu *c);
    int  (*reg_read)(struct esk_cpu *c, int n, u8 *buf);        /* by GDB number */
    int  (*reg_write)(struct esk_cpu *c, int n, const u8 *buf);
    const int *(*gdb_g_regs)(struct esk_cpu *c);                 /* `g` packet order, -1 ends */
    const char *(*gdb_xml)(struct esk_cpu *c, const char *annex); /* target description */
    int bp_kind;
    void (*interrupt)(struct esk_cpu *c, int n);  /* exception n pending; 16 + k is IRQ k */
    int pc_regnum;
    int elf_classes;
    u32  (*phys)(struct esk_cpu *c, u32 a);       /* bus address, 0 for identity */
    const char *(*reg_name)(struct esk_cpu *c, int n);
    int  (*wake_pending)(struct esk_cpu *c);      /* an exception would end WFI */
};
```

The core steps one instruction at a time; `esk_mach_run(m, budget,
max_insns)` runs it until `budget` cycles have elapsed or a stop condition
and returns the cycles consumed. A run stops early on: a breakpoint or
watchpoint, a lockup, a semihosting exit, a reset request, a device's
`yield` (the clock changed, a pin changed), or the core going to sleep
(WFI, WFE, a branch to itself with nothing pending). The cycle model
(`cortexm/cost.h`) charges a taken branch's pipeline refill, and the
devices see every cycle. M1 implements Cortex-M3 (Thumb-2, the NVIC, SCB,
SysTick, DWT, the FPU for M4F images); its timing tier is `instruction`.

A part with several cores of one model (the MCU description's
`core.cores`, the RP2040's 2) is one machine with one core interface
(`cortexm/smp.c`). Each core keeps its own time; a step runs the core
furthest behind, the machine's time is the earliest running core's, and
the devices see it as it advances, so the cores share one clock and an
access sees the time of the core making it (up to an instruction ahead of
the machine's; a device whose clock that instruction started counts from
it, `esk_mach_after`). Each core has its own NVIC,
SCB, SysTick and DWT, reached by its own accesses; an interrupt line
reaches every core's NVIC, and a part's model can pend one core's only
(`esk_cortexm_core_interrupt`). A core sleeps alone; the machine sleeps
when none runs. SEV sets every core's event register and wakes a core
waiting in WFE. Cores other than core 0 wait out of reset until the part's
model launches them (`esk_cortexm_core_launch`) or powers them off
(`esk_cortexm_core_power`); the model finds the core making an access with
`esk_cortexm_core`. The debugger sees the core that runs next.

## 4. Bus

Per machine: memory regions (RAM, flash, ROM; erased value; mirrors),
aliases, the bit-band regions, devices by address range, watchpoints, an
exclusive monitor and a debugger's access mode (`esk_bus_debug_read`,
`esk_bus_debug_write`: no watchpoints, flash writable). A core's failed
access raises its bus fault and reports `mcu.bus_error` once per address
with the address and the access, whether no region or device answered it
or a device refused it. Flash ignores the core's stores unless the flash
controller model programs it.

## 5. Register files

The core reads one format, EmbSIM's JSON register description
(`schemas/registers.schema.json`, ADR-020): peripherals with base, size and
interrupts, registers with offset, size and reset value, fields with the
reference manual's access notation (r, w, rw, w1, rw1, rc_w0, rc_w1, rc_r,
rs, t). `esk_rf_create` builds one bus device over the peripherals' span
and implements the access table for every field; vendor SVD files convert
with `tools/importers/svd2regs.py`. An MCU description binds peripherals to
models and clock gates (`esk_rf_bind`).

* A read or write of a gated peripheral (its RCC enable bit clear) reads 0,
  ignores the write and warns `mcu.clock_gated` once per peripheral.
* An access to a peripheral with a register file and no model warns
  `mcu.unsupported_access` once per peripheral; the register file's
  behaviour (reset values, access types) still holds.
* An access inside the span to no register is `mcu.bus_error`, unless a
  peripheral's `reserved` ranges (offset and size) hold it: the offsets the
  part documents as reading 0 and ignoring writes (a PLIC's registers for
  sources past its last), which do so silently.
* A binding that names a model the build does not have is `mcu.model_missing`.

The bus can follow the part's own rules. The MCU description's
`register_file.bus_rules` names them: `atomic_aliases` (each peripheral's
registers again at +0x1000 XOR, +0x2000 set, +0x3000 clear, the RP2040's)
and `wide_writes` (a byte or halfword written across the word). A register
the description marks `clr_set_inv` (a PIC32's) has three more after it:
+4 clears the bits written as 1, +8 sets them, +0xC inverts them; a read of
them gives 0, and edc2regs.py marks the registers whose EDC entry has them.

The description's `interrupts` table is kept for the models that find an
interrupt by name (`esk_rf_irq_named`). Where requests share vectors (a
PIC32MX's), `vectors` gives each request's vector (`esk_rf_vector`).
`pins` names the port pin each of the part's pin functions is on (CN15:
RD6), for the models that find one (`esk_rf_pin`); a PIC24's remappable
pins are there too (RP17: RF5). `pps_outputs` gives a Peripheral Pin
Select's output functions their numbers (U2TX: 5, `esk_rf_pps_out`), and
`config_words` the configuration words' program addresses (FOSCSEL:
0x0ABF18, `esk_rf_config`), for the models that read them at reset.

A PIC24's two address spaces share the bus: program memory at 0, each
24-bit word at twice its program address in four bytes (an XC16 image's
layout, through the core's `phys` hook), and the data space at 0x2000000,
its SFRs and RAM by their data addresses. The MCU description's
`core.prog_end` and `core.ram_base` give the program memory's end and the
RAM's start (W15 out of reset). The machine's cycles are instruction
cycles: the core's clock is FCY.

## 6. Peripheral models

```c
struct esk_rf_model_type {
    const char *name;               /* "stm32f1-gpio" */
    const struct esk_dev_ops *ops;  /* read, write, reset, tick, next_event */
    void *(*create)(struct esk_mach *m, struct esk_rf_periph *p); /* 0: not modelled */
};
```

A model hooks its peripheral's reads and writes and keeps its state in the
register file's registers (`esk_rf_reg`, `esk_rf_hw` for hardware updates).
Time inside the machine follows EmbCC (ADR-019): while a model's clock
runs (`esk_rf_clock`), `tick` tells it the cycles that passed after each
instruction and `next_event` says how many cycles until its next interrupt
or output, so a sleeping core skips to it. M1's models (`core/mcu/models.c`):
`stm32-rcc`, `stm32f1-gpio`, `stm32f1-afio`, `stm32-exti`, `stm32-usart`,
`stm32-tim` (time base and interrupts), `stm32f1-flash`, `stm32-pwr`.

## 7. Pins, multiplexing and nets

The GPIO model reports each pin's configuration through `pin_cfg`
(`esk_mach_pin`: driven level, open-drain, pull, analog, alternate
function) and reads input levels through `pin_in`. The runtime applies a
changed configuration as a `SIGNAL` event at the instruction's time
(`esk_pin_refresh`): direction, drive, the weak pull through the analog
bridge. `IDR` reads the resolved net level (`esk_pin_level`). An edge on an
input pin ends the quantum so the EXTI sees it at its time. The AFIO
remap decides which pins a USART uses (`uart_pin`); a USART in alternate
function drives its TX pin's waveform frame by frame at the baud rate, and
its RX decodes the RX pin's waveform at mid-bit.

## 8. Runtime and time

The runtime runs the machine as one CORE event per quantum (ADR-019): up to
the next session event that is not an analog step, at most the session's
`core_quantum_ps` (1 µs by default). A quantum ends early when a pin changes
or the core clock changes; the next starts 1 ps after. A sleeping core
costs nothing: its next quantum is its devices' next event, or the pin
change that wakes it. Cycles convert to picoseconds exactly at the core
clock the RCC reports, the remainder carried; catching up after a sleep
rounds cycles up. The board's NRST pin held low keeps the core in reset;
BOOT0 high (boot from system memory) is `mcu.boot_mode`.

## 9. Streams and semihosting

Semihosting output and each UART's transmitted bytes go to the board's
serial text, observed as `serial:BOARD` (the console UART) and
`serial:BOARD.UNIT`. Semihosting `SYS_READC` and a UART's receive come from
the session's serial input (`input.serial`, target `BOARD` or
`BOARD.UNIT`) at the UART's baud rate. Semihosting exit ends the board's run
with its status (`mcu.exit`); a lockup is `mcu.lockup`.

A part's ROM whose code is not shipped may hold its documented functions as
`bkpt #n` and a return; the machine's `rom_call` does them with the calling
core's registers, flags and memory, and charges their cycles (ADR-021). The
RP2040's bootrom functions and tables are done this way
(`core/mcu/rp2040/rom.c`); a program that calls its `reset_to_usb_boot`
waits in the USB boot (`mcu.usb_boot`).

## 10. Debug unit

Breakpoints (by address, symbol, or file and line) and watchpoints per
machine, held outside memory. A hit stops the scheduler
(`stop_requested`): the session pauses with reason `stopped` and notifies
`debug.stopped`; continuing runs the rest of the quantum, so a stopped and
continued run equals an uninterrupted one. The session protocol's `debug.*`
methods (SPEC-004) read and write registers and memory, step, disassemble
Thumb, list peripherals and source; `embsim gdb` serves GDB's remote
protocol for one board (`es_serve_gdb`). Source lines come from the image's
DWARF 2–5 line tables (`core/mcu/dwarf.c`); symbols from its ELF symbol
table. Variables (DWARF info) are not implemented in M1.

## 11. Extraction rules for EmbCC code

1. Every `die()` becomes a status return or a diagnostic; `exit` is banned.
2. `struct sim` globals become fields of `esk_mach`; the core's global state
   becomes its first parameter.
3. `putchar`/`stdout` output becomes the `out` host hook.
4. `struct board_io` callers become the `pin_cfg`/`pin_in` hooks (§7);
   `--set` inputs become component inputs.
5. `tick`/`next_event` stay inside the machine (§6, ADR-019).
6. File headers keep EmbCC's notice, add `Modifications copyright` and
   `Provenance: EmbLinkOs/EmbCC <file>`; `NOTICE` lists the files.
7. The family's EmbCC golden fixtures pass (`tests/firmware/bluepill-golden`,
   with documented deltas) before any behavioural change is accepted.

## 12. Analyses

A machine may carry analyses (`core/mcu/analysis.h`): coverage and the
profile. With one, the run loop steps each core through `esk_an_step`
(smp.c for each core of a part with several), which notes the pc and the
machine's instruction and cycle counts around the core's own step. Coverage
counts each executed instruction by address, a count per halfword of the
image's functions; the profile gives each step's instructions and cycles to
the call path its pc is on. Calls and returns are recognized from the
instruction the core executes (EmbCC's rules, `analysis.c`): a call that
links to inside its own function, not its entry, is a far branch. Each
core reports an exception's entry with its return address
(`esk_mach_exc(m, core, n, ret)`) and its return (`esk_mach_exc_return`):
RETFIE, MRET, ERET, RETI and a Cortex-M's EXC_RETURN. Each core has its own
shadow stack and root path. Functions are the ELF's function symbols and
its global labels in code; one without a size runs to the next, at most
4 KiB. A machine without analyses steps as before. Reports: SPEC-008 §5
`analysis.*`, `embsim run --coverage`, `--profile`, `--stack`.

The stack analysis (`stack.c`) reads each core's stack pointers after every
step: the main stack's (a Cortex-M's MSP, as banked) and a Cortex-M's
process stack's. A stack's top is the highest it was while the pc was in
the image's functions (a boot ROM's own stack is not the program's); its
deepest, the lowest. Its limit is the link's `__stack_limit`
(`__StackLimit`, `_sstack`, a PIC24's `__SPLIM_init`), or the end of
`.data` and `.bss` (`_end`, `__bss_end`, `_ebss`, `end`) when that is
higher and below the top. The main stack going past its limit raises
`mcu.stack_overflow` (error) once a reset, naming the instruction and its
function; the run goes on, as on a part without a stack guard. By function,
from the shadow stack's frames: the deepest sp while the pc was in it; its
own frame (the most it moved sp below its entry, what `-fstack-usage`
reports; an AVR's and a PIC24's return address counted as the callee's, as
their compilers do); its depth with its callees, not its interrupt
handlers. A PIC24's stack grows up: compared mirrored, reported as W15.

The fault report (`fault.c`) decodes each fault at the end of the step
that took it (the cores report an entry before stacking it): a Cortex-M's
HardFault, MemManage, BusFault and UsageFault (HFSR and CFSR in words,
MMFAR and BFAR when valid, the stacked frame), a RISC-V exception other
than an interrupt, ECALL or EBREAK (mcause, mtval), a MIPS exception other
than an interrupt, SYSCALL or BREAK (Cause's code, BadVAddr, the delay
slot), a PIC24 trap (the core's account of it, W0-W15 and SR as the trap
found them). Each gets the instruction disassembled with its function and
line, the registers, a backtrace by the image's `.debug_frame` (CIE
versions 1, 3 and 4, without DWARF expressions; through a Cortex-M's
exception frame when the unwinding reaches EXC_RETURN), else by the shadow
stack's calls, and the handler the core goes to (a vector without the
Thumb bit, or one that is no function's start, named as none). A function
is named by its strong symbol over the weak aliases at its address. Each
fault raises `mcu.fault` (error) with its summary; a lockup is recorded
too and stays `mcu.lockup`. The first 16 are kept, all counted.
