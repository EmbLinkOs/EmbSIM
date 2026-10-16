/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* mach.h -- the machine inside one MCU instance (SPEC-009 §2-§4): the bus,
 * the core interface, the devices that keep time, and the hooks through
 * which the machine reaches the rest of the session (pins, console).
 *
 * Provenance: EmbLinkOs/EmbCC aa4f10a tools/embsim/sim.h. EmbCC's struct sim
 * was already one instance per run; it is renamed esk_mach and loses what
 * belongs to EmbCC's command line (tracing, analyses, recording), which the
 * session provides instead. Every allocation is the machine's
 * (esk_mach_alloc) and freed with it. Nothing here is file-scope state.
 *
 * Time inside the machine is the core's cycle count. The MCU runtime
 * (mcu.c) turns cycles into picoseconds at the core clock the clock model
 * reports, and runs the machine in quanta between the session's events
 * (SPEC-009 §3, ADR-019). */
#ifndef ESK_MCU_MACH_H
#define ESK_MCU_MACH_H

#include <stddef.h>
#include <stdint.h>
#include "embsim/types.h"
#include "util/alloc.h"

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t s32;
typedef int64_t s64;

struct esk_mach;
struct esk_cpu;
struct esk_rf;

/* ---- the bus ------------------------------------------------------------ */

/* MEM_ALIAS: another name for the region at a description's `target` */
enum { ESK_MEM_RAM, ESK_MEM_FLASH, ESK_MEM_ALIAS, ESK_MEM_ROM };

struct esk_region {
    u32 base, size;
    u8 *mem;
    int rom;      /* the core's stores are ignored */
    int kind;     /* ESK_MEM_RAM, ESK_MEM_FLASH or ESK_MEM_ROM */
    u32 erased;   /* each word of it before the image (esk_bus_erase) */
    int mirror;   /* shares another region's memory */
};

/* A memory-mapped device. `off` is the offset of the access from the
 * device's base, `n` its size in bytes (1, 2 or 4); a read's value is
 * masked to n bytes by the bus. Any hook may be 0. */
struct esk_dev_ops {
    const char *type;
    u32 (*read)(void *ctx, u32 off, int n);
    void (*write)(void *ctx, u32 off, int n, u32 v);
    void (*reset)(void *ctx);
    /* `cycles` of core time have passed. Called after every instruction,
     * but only while the device has said its clock runs (esk_mach_clock):
     * a stopped timer costs nothing. */
    void (*tick)(void *ctx, u32 cycles);
    /* 1 and the cycles until the device next raises an interrupt or
     * changes an output, or 0 when it will not without the program */
    int (*next_event)(void *ctx, u32 *cycles);
};

struct esk_device {
    const struct esk_dev_ops *ops;
    void *ctx;
    u32 base, size;
};

/* a bit-band alias: one word at base + 32 * byte + 4 * bit for each bit of
 * the `size / 32` bytes at target */
struct esk_alias {
    u32 base, size, target;
};

/* a debugger's watchpoint, numbered as GDB's Z2, Z3 and Z4 */
enum { ESK_WATCH_WRITE = 2, ESK_WATCH_READ = 3, ESK_WATCH_ACCESS = 4 };
struct esk_watch {
    u32 addr, len;
    int kind;
};

#define ESK_BUS_REGIONS 12
#define ESK_BUS_DEVICES 48
#define ESK_BUS_ALIASES 4
#define ESK_BUS_WATCHES 32

struct esk_bus {
    struct esk_region rg[ESK_BUS_REGIONS];
    int nrg;
    struct esk_device dev[ESK_BUS_DEVICES];
    int ndev;
    struct esk_alias bb[ESK_BUS_ALIASES];
    int nbb;
    u32 fail_addr; /* the address of a failed access */
    /* every store the core makes, as the exclusive monitor sees it */
    void (*snoop)(void *ctx, u32 a);
    void *snoop_ctx;
    struct esk_watch watch[ESK_BUS_WATCHES];
    int nwatch;
    int watch_hit, watch_kind; /* the first watchpoint the core hit */
    u32 watch_addr;
    int debug;     /* the access is a debugger's */
    int dev_fault; /* the device refused it: esk_bus_fault */
    int unmapped;  /* nothing answered the failed access */
    /* a device was accessed since esk_mach_event_coming last looked */
    int touched;
};

es_status esk_bus_add_region(struct esk_mach *m, u32 base, u32 size, int kind);
/* the region as before an image: every 4 bytes its `erased` word */
void esk_bus_erase(struct esk_region *r);
/* the region holding all `n` bytes at `a`, or 0: memory's fast path */
ES_INLINE struct esk_region *esk_bus_region(struct esk_bus *b, u32 a, u32 n)
{
    int i;
    for (i = 0; i < b->nrg; i++) {
        struct esk_region *r = &b->rg[i];
        if (a - r->base < r->size && r->size - (a - r->base) >= n)
            return r;
    }
    return 0;
}
ES_INLINE u32 esk_mem_rd_le(const u8 *p, int n)
{
    u32 v = p[0];
    if (n > 1)
        v |= (u32)p[1] << 8;
    if (n > 2)
        v |= (u32)p[2] << 16 | (u32)p[3] << 24;
    return v;
}
ES_INLINE void esk_mem_wr_le(u8 *p, int n, u32 v)
{
    p[0] = (u8)v;
    if (n > 1)
        p[1] = (u8)(v >> 8);
    if (n > 2) {
        p[2] = (u8)(v >> 16);
        p[3] = (u8)(v >> 24);
    }
}
es_status esk_bus_add_device(struct esk_bus *b, u32 base, u32 size, const struct esk_dev_ops *ops,
                             void *ctx);
es_status esk_bus_add_alias(struct esk_bus *b, u32 base, u32 size, u32 target);
/* a second name for the memory region holding `target`, at `base` */
es_status esk_bus_add_mirror(struct esk_bus *b, u32 base, u32 target);
/* a device's read or write hook refuses the access in progress: the core
 * takes its bus fault, as when nothing answers */
ES_INLINE void esk_bus_fault(struct esk_bus *b) { b->dev_fault = 1; }
/* The core's accesses: 0, or -1 with fail_addr when nothing answers. */
int esk_bus_read(struct esk_bus *b, u32 a, int n, u32 *v);
int esk_bus_write(struct esk_bus *b, u32 a, int n, u32 v);
/* whether an access of the core's touches a watchpoint (1), recording the
 * first one in watch_hit; asked before the access */
int esk_bus_watch_check(struct esk_bus *b, u32 a, int n, int write);
/* a debugger's: no watchpoints, no exclusive monitor, flash writable */
int esk_bus_debug_read(struct esk_bus *b, u32 a, int n, u32 *v);
int esk_bus_debug_write(struct esk_bus *b, u32 a, int n, u32 v);
int esk_bus_watch_add(struct esk_bus *b, int kind, u32 addr, u32 len);
int esk_bus_watch_remove(struct esk_bus *b, int kind, u32 addr, u32 len);

/* ---- a core --------------------------------------------------------------- */

struct esk_cpu_ops {
    const char *name;     /* "cortex-m" */
    int elf_machine;      /* the e_machine of its images */
    const char *elf_name; /* "an ARM image" */
    /* power-on reset: the core's state, then its reset sequence */
    void (*reset)(struct esk_cpu *c);
    /* one instruction, or the entry to an exception */
    void (*step)(struct esk_cpu *c);
    u32 (*pc)(struct esk_cpu *c);
    /* registers by GDB number: the bytes written, 0 for no such register */
    int (*reg_read)(struct esk_cpu *c, int n, u8 *buf);
    int (*reg_write)(struct esk_cpu *c, int n, const u8 *buf);
    /* the registers of a `g` packet, in order, ending with -1 */
    const int *(*gdb_g_regs)(struct esk_cpu *c);
    /* the target description (qXfer:features:read) for `annex`, or 0 */
    const char *(*gdb_xml)(struct esk_cpu *c, const char *annex);
    int bp_kind;
    /* make exception or interrupt `n` pending (ARM numbering: 16 + k is
     * external interrupt k) */
    void (*interrupt)(struct esk_cpu *c, int n);
    int pc_regnum;
    int elf_classes;
    /* the bus address of a program address, or 0 for identity */
    u32 (*phys)(struct esk_cpu *c, u32 a);
    /* the register names for a register view, by GDB number, or 0 */
    const char *(*reg_name)(struct esk_cpu *c, int n);
    /* an exception is pending that would end a wait (WFI) */
    int (*wake_pending)(struct esk_cpu *c);
    /* the instruction at `addr`: its text and its size in bytes (0 when
     * memory there cannot be read) */
    int (*disasm)(struct esk_cpu *c, u32 addr, char *text, size_t cap);
};

struct esk_cpu {
    const struct esk_cpu_ops *ops;
};

/* ---- the machine's way out: implemented by the MCU runtime ------------- */

/* A GPIO pin's configuration as its port model computes it. */
typedef struct esk_mach_pin {
    int drive;      /* -1 not driven, else the level 0 or 1 */
    int open_drain; /* drives 0 only; a 1 releases */
    int pull;       /* 0 none, 1 up, 2 down (weak) */
    int analog;     /* analog mode: the digital input is off */
    int af;         /* an alternate function (a peripheral) drives it */
} esk_mach_pin;

typedef struct esk_mach_host {
    void *ctx;
    /* a byte the program wrote to a stream: 0 the console (semihosting),
     * 1 semihosting's stderr; a UART's go through `uart_tx` */
    void (*out)(void *ctx, int stream, int c);
    /* semihosting's SYS_READC: a byte, or -1 */
    int (*readc)(void *ctx);
    /* a GPIO pin's output configuration changed (port 0 is A) */
    void (*pin_cfg)(void *ctx, int port, int bit, const esk_mach_pin *cfg);
    /* the level other drivers give a pin: 1 and *level when driven,
     * 0 when nothing else drives it */
    int (*pin_in)(void *ctx, int port, int bit, int *level);
    /* a UART's transmitter starts a byte; `bit_cycles` is the bit time in
     * core cycles, for the serial line's waveform */
    void (*uart_tx)(void *ctx, const char *unit, int c, u32 bit_cycles);
    /* a diagnostic (es_severity_t, a code from docs/user/diagnostics.md) */
    void (*diag)(void *ctx, int severity, const char *code, const char *msg);
    /* the voltage on a pin's net, for an ADC: 1 and *v, or 0 when the pin
     * is not connected */
    int (*pin_volts)(void *ctx, int port, int bit, double *v);
    /* a core enters exception or interrupt n (its own numbering: a
     * Cortex-M's exception number, an AVR's vector, 16 + a RISC-V
     * interrupt's cause or the cause of an exception, 16 + a MIPS
     * interrupt's vector or an exception's ExcCode, a PIC24's vector
     * number), for a recording's observations */
    void (*exc)(void *ctx, int core, int n);
} esk_mach_host;

/* ---- the machine ---------------------------------------------------------- */

enum { ESK_MACH_RUN, ESK_MACH_EXIT, ESK_MACH_LOCKUP, ESK_MACH_IDLE, ESK_MACH_BREAK };

#define ESK_MACH_TICKERS 32
#define ESK_MACH_BREAKPOINTS 64

typedef struct esk_mach_alloc_rec {
    void *p;
    size_t n;
} esk_mach_alloc_rec;

/* the UART receiver a host input feeds: room and a byte arriving */
struct esk_rx_port {
    int (*room)(void *ctx);
    void (*put)(void *ctx, int c);
    /* the bit time in core cycles, as the UART is set up now */
    u32 (*bit_cycles)(void *ctx);
    void *ctx;
    const char *unit;
};

#define ESK_MACH_RX_PORTS 8

struct esk_rom_call;

struct esk_mach {
    esk_alloc *a;
    struct esk_bus bus;
    struct esk_cpu *cpu;
    u64 insns, cycles;
    int state, exit_status;
    char end_why[160];
    int semihosting;
    /* the devices that keep time */
    void (*tick_fn[ESK_MACH_TICKERS])(void *ctx, u32 cycles);
    void *tick_ctx[ESK_MACH_TICKERS];
    int ntick_fn;
    struct esk_device *tick[ESK_MACH_TICKERS];
    int ntick;
    int counting; /* devices whose clock is running */
    int ev_known, ev_any;
    u64 ev_at;
    /* set by a model when something outside the machine must see a change
     * now (a pin, the core clock): the runtime ends its quantum */
    int yield;
    /* the core waits for an exception (WFI, WFE, a branch to itself): it
     * runs no instructions; its devices' time still passes */
    int asleep;
    /* the program asked for a system reset (AIRCR.SYSRESETREQ) */
    int reset_req;
    /* the register file and its models, or 0 */
    struct esk_rf *rf;
    /* the core clock in Hz as the clock model has it, or 0 for the
     * description's reset value */
    u32 (*core_hz)(struct esk_mach *m);
    void *clock_ctx;
    u32 reset_hz;
    u32 hse_hz;   /* the board's external oscillator, 0 for none */
    double vdd;   /* the I/O supply in volts (the ADC's AVCC) */
    esk_mach_host host;
    /* the GPIO model's: the pins' outside levels changed; where a UART's
     * TX or RX pin is (0, or -1 when the part has no such UART) */
    void (*pins_changed)(struct esk_mach *m);
    int (*uart_pin)(struct esk_mach *m, const char *unit, int tx, int *port, int *bit);
    /* esk_mach_run returns as soon as the core sleeps, for the runtime to
     * schedule the wake-up itself */
    int return_on_sleep;
    struct esk_rx_port rx[ESK_MACH_RX_PORTS];
    int nrx;
    int bus_errors; /* reported by esk_mach_bus_error */
    /* breakpoints, by program counter; a run resumed from one steps off
     * it first (bp_skip) */
    u32 bp[ESK_MACH_BREAKPOINTS];
    int nbp;
    int bp_skip;
    /* why the last run stopped short of its budget */
    int stop_bp, stop_watch;
    /* everything the machine allocated */
    esk_mach_alloc_rec *allocs;
    size_t nallocs, cap_allocs;
    int oom;
    /* the analyses watching the run (analysis.h), or 0; `steps_cores`: the
     * core interface steps its cores itself, through esk_mach_step_core
     * (smp.c) */
    struct esk_an *an;
    int steps_cores;
    /* the ROM's host functions (struct esk_rom_call): 1 when `bkpt #n` at
     * `pc` is one, 0 for an ordinary breakpoint instruction */
    int (*rom_call)(struct esk_mach *m, u32 pc, unsigned n, struct esk_rom_call *c);
    void *rom_ctx;
};

es_status esk_mach_init(struct esk_mach *m, esk_alloc *a);
void esk_mach_free(struct esk_mach *m);
/* zeroed memory owned by the machine, freed by esk_mach_free; 0 when out
 * of memory (and m->oom set) */
void *esk_mach_alloc(struct esk_mach *m, size_t n);
char *esk_mach_strdup(struct esk_mach *m, const char *s);
/* a device on the bus, keeping time if it has a tick or next_event hook */
es_status esk_mach_add_dev(struct esk_mach *m, u32 base, u32 size, const struct esk_dev_ops *ops,
                           void *ctx);
/* of a tick's `cycles` (they end now), those after `since`, the cycle a
 * device's clock started: all of them, or the part of the instruction that
 * started it; 0 when it started after them (another core's instruction,
 * ahead of the machine's time: mcu/cortexm/smp.c) */
ES_INLINE u32 esk_mach_after(const struct esk_mach *m, u32 cycles, u64 since)
{
    if (m->cycles - cycles >= since)
        return cycles;
    return m->cycles > since ? (u32)(m->cycles - since) : 0;
}
/* the analyses' hooks (analysis.c) */
struct esk_an;
void esk_an_step(struct esk_an *a, int core, struct esk_cpu *cpu);
void esk_an_exc(struct esk_an *a, int core, int n, u32 ret);
void esk_an_exc_return(struct esk_an *a, int core);
/* a core enters exception or interrupt n (esk_mach_host.exc), to return to
 * `ret` */
ES_INLINE void esk_mach_exc(struct esk_mach *m, int core, int n, u32 ret)
{
    if (m->host.exc)
        m->host.exc(m->host.ctx, core, n);
    if (m->an)
        esk_an_exc(m->an, core, n, ret);
}
/* a core returns from an exception (RETFIE, MRET, ERET, RETI, a Cortex-M's
 * EXC_RETURN) */
ES_INLINE void esk_mach_exc_return(struct esk_mach *m, int core)
{
    if (m->an)
        esk_an_exc_return(m->an, core);
}
/* one step of core `core`'s interface `cpu`, watched by the analyses */
ES_INLINE void esk_mach_step_core(struct esk_mach *m, int core, struct esk_cpu *cpu)
{
    if (m->an)
        esk_an_step(m->an, core, cpu);
    else
        cpu->ops->step(cpu);
}
/* end the run: state, and the reason */
void esk_mach_end(struct esk_mach *m, int state, const char *fmt, ...);
/* a byte of the program's console output */
void esk_mach_out(struct esk_mach *m, int stream, int c);
int esk_mach_readc(struct esk_mach *m);
/* `cycles` of time pass for the devices */
void esk_mach_advance(struct esk_mach *m, u32 cycles);
/* a device's clock starts (1) or stops (0) */
void esk_mach_clock(struct esk_mach *m, int *running, int on);
/* 1 and the cycles to the next device event, or 0 when none is due */
int esk_mach_next_event(struct esk_mach *m, u32 *cycles);
/* esk_mach_next_event, asked again only when it may have changed */
int esk_mach_event_coming(struct esk_mach *m);
/* a UART offers its receiver to the host's input */
void esk_mach_rx_port(struct esk_mach *m, const char *unit, int (*room)(void *ctx),
                      void (*put)(void *ctx, int c), u32 (*bit_cycles)(void *ctx), void *ctx);
/* a core's access that nothing answers: a diagnostic (the first ten) */
void esk_mach_bus_error(struct esk_mach *m, u32 addr, int n, int write);
/* the receiver of the UART named `unit` ("USART1"), or NULL */
struct esk_rx_port *esk_mach_rx_find(struct esk_mach *m, const char *unit);
/* reset: the devices and the core; memory as it is */
void esk_mach_reset(struct esk_mach *m);
u32 esk_mach_core_hz(struct esk_mach *m);
/* Run until `budget` cycles have passed (the last instruction may
 * overshoot), `max_insns` instructions have run (0: no limit), or the
 * machine yields, stops, reaches a breakpoint or watchpoint, or sleeps with
 * no device event inside the budget. Returns the cycles that passed. */
u64 esk_mach_run(struct esk_mach *m, u64 budget, u64 max_insns);
/* `cycles` pass with the core asleep (or held): the devices see them */
void esk_mach_idle(struct esk_mach *m, u64 cycles);
int esk_mach_bp_add(struct esk_mach *m, u32 pc);
int esk_mach_bp_remove(struct esk_mach *m, u32 pc);

/* ---- semihosting (semihost.c) ------------------------------------------- */

struct esk_semi_ops {
    u32 (*ld)(void *ctx, u32 a, int n);
    void (*st)(void *ctx, u32 a, int n, u32 v);
    int (*faulted)(void *ctx);
};

/* An ARM semihosting call: operation `op` (r0), parameter `arg` (r1).
 * Returns 1 with the result for r0 in *ret, or 0 when r0 is unchanged. */
int esk_semihost(struct esk_mach *m, const struct esk_semi_ops *ops, void *ctx, u32 op, u32 arg,
                 u32 *ret);

/* ---- a ROM's functions done by the host ---------------------------------- */

/* A part's ROM may hold functions EmbSIM carries out in C instead of
 * running their code (the RP2040's bootrom, which is not shipped): each is
 * `bkpt #n` followed by a return, and a core that executes it calls the
 * machine's `rom_call` with n and the call's registers. */
struct esk_rom_call {
    u32 r[4];      /* r0-r3 as the call finds them; as the function leaves them */
    int set_flags; /* the function sets APSR's N, Z, C and V from nzcv */
    u32 nzcv;      /* N 8, Z 4, C 2, V 1 */
    u32 cycles;    /* the cycles it takes beyond the bkpt's */
    /* the calling core's view of memory; after an access that faulted,
     * `faulted` is 1 and the core takes the fault */
    const struct esk_semi_ops *mem;
    void *mem_ctx;
};

/* ---- images (image.c) ------------------------------------------------------ */

/* An ELF or Intel HEX image into the machine's memory (flash included).
 * `entry` receives the ELF entry point (0 for HEX). Errors name the
 * problem in `why`. */
es_status esk_mach_load_image(struct esk_mach *m, const u8 *data, size_t len, u32 *entry, char *why,
                              size_t why_cap);

/* an ELF image's symbol `name` (a function's address without the Thumb bit):
 * 1 and its value, or 0 */
int esk_elf_symbol(const u8 *f, size_t len, const char *name, u32 *value);
/* a symbol of any kind but a section's or a file's, defined or absolute (the
 * link's __stack_limit, _end): 1 and its value, or 0 */
int esk_elf_label(const u8 *f, size_t len, const char *name, u32 *value);
/* the function or object holding `addr`: 1, its name and the offset in it */
int esk_elf_symbol_at(const u8 *f, size_t len, u32 addr, char *name, size_t cap, u32 *offset);
/* each defined function of an ELF image's symbol table: its name (in the
 * image), its address without the Thumb or MIPS16 bit, its size, and
 * whether it is weak (an alias startup code gives each handler); and each
 * global label in code, with size 0 */
void esk_elf_functions(const u8 *f, size_t len,
                       void (*fn)(void *ctx, const char *name, u32 addr, u32 size, int weak), void *ctx);

/* ---- cores ---------------------------------------------------------------- */

/* a Cortex-M core of `model` ("cortex-m3"), with its NVIC priority bits and
 * its own devices on the bus; ES_ENOTFOUND for an unknown model */
es_status esk_cortexm_create(struct esk_mach *m, const char *model, int prio_bits,
                             struct esk_cpu **out);
/* `cores` Cortex-M cores of one model sharing the machine (mcu/cortexm/smp.c):
 * core 0 runs from reset, the others wait to be launched; with 1, as
 * esk_cortexm_create */
es_status esk_cortexm_create_smp(struct esk_mach *m, const char *model, int prio_bits, int cores,
                                 struct esk_cpu **out);
/* the cores, and the one whose access is in progress (a debugger's: the one
 * that runs next) */
int esk_cortexm_cores(struct esk_mach *m);
int esk_cortexm_core(struct esk_mach *m);
/* exception n pends on one core's NVIC only */
void esk_cortexm_core_interrupt(struct esk_mach *m, int core, int n);
/* a core other than core 0 powered off (reset, idle) or on (waiting in its
 * bootrom to be launched); whether it waits so, or runs (or sleeps) */
void esk_cortexm_core_power(struct esk_mach *m, int core, int on);
int esk_cortexm_core_booting(struct esk_mach *m, int core);
int esk_cortexm_core_running(struct esk_mach *m, int core);
/* a waiting core starts at `entry` with its stack and vector table */
void esk_cortexm_core_launch(struct esk_mach *m, int core, u32 vtor, u32 sp, u32 entry);
/* an AVR core of `model` ("atmega328p"), with its own devices on the bus
 * (mcu/avr/avr.h) */
es_status esk_avr_create(struct esk_mach *m, const char *model, struct esk_cpu **out);
/* an RV32 core of `model` ("rv32imac") whose reset starts at reset_pc
 * (mcu/riscv/riscv.h) */
es_status esk_riscv_create(struct esk_mach *m, const char *model, u32 reset_pc, struct esk_cpu **out);

#endif
