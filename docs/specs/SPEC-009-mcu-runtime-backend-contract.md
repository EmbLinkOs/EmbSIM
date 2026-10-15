# SPEC-009 - MCU Runtime Backend Contract

**Status:** Accepted in outline 2026-10-15; detailed interfaces are fixed
before M1 code starts (07 §3). **Builds on:** 04; ADR-005, ADR-015, ADR-016.
**Requirements:** `docs/requirements/MCU.md`.

---

## 1. Scope

The internal contracts between the session and an MCU instance: core
backends, the bus, register files, peripheral models, the pin multiplexer and
GPIO exposure, interrupts, streams, and the rules for code extracted from
EmbCC.

## 2. MCU instance

`esk_mcu` owns: one or more cores, a bus, a register file, peripheral model
instances, a clock tree, pin models for every external signal, a console
stream, an unsupported-access policy, statistics. It is created from an MCU
description and a package, destroyed with the session, reset by the session,
and driven only through the scheduler (SPEC-002 §5). It has no file-scope
state.

## 3. Core backend

```c
typedef struct esk_core_ops {
    const char *family;                      /* "cortex-m" */
    int elf_machine; int elf_classes; const char *elf_name;
    es_status (*create)(esk_mcu *m, const char *model, const esk_json *core_desc, esk_core **out);
    void      (*destroy)(esk_core *c);
    es_status (*reset)(esk_core *c);
    /* execute until `cycles` have elapsed or a stop condition; returns the
       cycles actually consumed and the reason */
    es_status (*run)(esk_core *c, uint64_t cycles, esk_run_result *r);
    uint32_t  (*pc)(esk_core *c);
    int       (*reg_read)(esk_core *c, int n, uint8_t *buf);
    int       (*reg_write)(esk_core *c, int n, const uint8_t *buf);
    const int *(*gdb_g_regs)(esk_core *c);
    const char *(*gdb_xml)(esk_core *c, const char *annex);
    int bp_kind; int pc_regnum;
    void      (*interrupt)(esk_core *c, int n);     /* make exception n pending */
    uint32_t  (*phys)(esk_core *c, uint32_t a);     /* program address to bus address, or identity */
    es_status (*sleeping)(esk_core *c, int *asleep); /* WFI/WFE state for idle detection */
    const char *timing_tier;                        /* "exact" | "estimated" | "unmodelled" */
} esk_core_ops;
```

`run` stops early on: a breakpoint or watchpoint (owned by the MCU's debug
unit), a fault with nowhere to go (lockup), a sleep instruction with no
pending event, a semihosting exit, a bus error under `strict`, or a
scheduler wake-up request. Cycles are converted to time by the MCU's core
clock; the core never knows about picoseconds.

## 4. Bus

Per instance: regions, aliases, bit-band, devices by address range,
watchpoints, a `debug` access mode, an `unsupported` hook. `esk_bus_read` and
`esk_bus_write` return status; a failed access raises the core's bus fault
or, under `strict` with `fatal_bus_error`, stops the run with
`mcu.bus_error`. Flash regions ignore stores after image load unless the
flash controller model or a debugger writes them.

## 5. Register files

Formats: CMSIS-SVD (M1), ATDF and EDC (M4). The parser yields peripherals,
clusters, registers, fields with reset values and access semantics; the
register file is a bus device per peripheral implementing the table of 04
§4. Peripheral models obtain register handles by name (`esk_rf_reg(p,
"ODR")`) and may hook reads and writes per register. An access to a
register with no model hook and a field marked `unsupported` by the bound
model raises `mcu.unsupported_access` under the policy; a peripheral with a
register file and no model raises it on first access, once.

## 6. Peripheral models

```c
typedef struct esk_periph_ops {
    const char *name;                         /* "stm32f1-gpio" */
    es_status (*create)(esk_mcu *m, esk_rf_periph *p, const esk_json *desc, esk_periph **out);
    void      (*destroy)(esk_periph *p);
    void      (*reset)(esk_periph *p);
    /* register hooks are installed in create via esk_rf_hook(reg, read_fn, write_fn) */
    /* time: models schedule PERIPHERAL events through esk_sched_at(); no tick hooks */
    const char *const *implements;            /* registers and fields implemented, for fidelity */
} esk_periph_ops;
```

No per-instruction tick callbacks: a timer computes the time of its next
event from its clock and schedules it; a change to its prescaler reschedules.
This replaces the EmbCC `tick`/`next_event` pair and makes idle detection
trivial (no pending event, core asleep: the run is idle).

## 7. GPIO, pin multiplexing and nets

The GPIO model exposes one `digital_io` (or `analog`) pin model per signal
into the net graph through `esk_mcu_pin(m, "PC13")`. On a register write
that changes a pin's mode or output, the model calls `esk_pin_drive` or
`esk_pin_set_direction`; the engine schedules the `SIGNAL` event at the
core's current time. Input registers (`IDR`) are computed on read from the
resolved net state through the pin's input model. The multiplexer consults
`AFIO`/`PPS`-style registers to route a peripheral's signals (USART TX, timer
channel) to pins; a peripheral drives its signal through the multiplexer,
never a pin directly. Analog inputs expose `adc_terminal` devices on the
pin's net when configured.

## 8. Extraction rules for EmbCC code

1. Every `die()` becomes a status return or a diagnostic; `exit` is banned.
2. `struct sim` globals become fields of `esk_mcu` or the backend's instance.
3. `putchar`/`stdout` output becomes `esk_stream_write(m->console, ...)`.
4. `struct board_io` callers become pin-model calls (§7); `--set` inputs
   become component inputs.
5. `tick`/`next_event` become scheduled events (§6).
6. File headers keep EmbCC's comment, gain `Provenance: EmbLinkOs/EmbCC
   <commit> tools/embsim/<file>`; `NOTICE` lists the files.
7. The family's EmbCC golden fixtures pass in `tests/differential/embcc/`
   before any behavioural change is accepted.

## 9. Streams and semihosting

Each MCU has a console stream (semihosting output, the board's `console`
peripheral) exposed as `serial:BOARD.console`; semihosting `SYS_READC` and
UART receive come from `input.serial`, timestamped. Semihosting exit ends
the board's run with its status, reported in the run report.

## 10. Debug unit

Breakpoints and watchpoints per MCU, held outside memory; GDB RSP server
over the host layer's sockets, or over the session protocol's `debug.*`;
DWARF line and variable information from the loader; `monitor` commands for
reset, time, statistics. A debugger's register and memory writes are
`EXTERNAL` events for recording.
