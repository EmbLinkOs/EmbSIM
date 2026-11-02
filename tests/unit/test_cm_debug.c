/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* test_cm_debug.c -- the Cortex-M's halting debug as a probe drives it
 * through the debug port (ARM DDI 0403E C1, DDI 0419E C1): DHCSR's halt,
 * step and restart, DCRSR and DCRDR, the FPB's breakpoint comparators, a
 * DWT watchpoint, BKPT, DEMCR's reset vector catch across a system reset
 * and its clearing at power-on, and the core unable to set C_DEBUGEN
 * itself. Hand-assembled Thumb runs from RAM. */
#include "harness.h"
#include "mcu/mach.h"

#define DHCSR 0xE000EDF0u
#define DCRSR 0xE000EDF4u
#define DCRDR 0xE000EDF8u
#define DEMCR 0xE000EDFCu
#define DFSR 0xE000ED30u
#define FP_CTRL 0xE0002000u
#define FP_COMP0 0xE0002008u
#define DWT_COMP0 0xE0001020u
#define DWT_MASK0 0xE0001024u
#define DWT_FUNC0 0xE0001028u
#define KEY 0xA05F0000u

#define PC_RESET 0x100u
#define LOOP (PC_RESET + 2u)
#define WATCHED 0x20000100u

static void word(struct esk_mach *m, u32 a, u32 v) { CHECK_EQ(esk_bus_write(&m->bus, a, 4, v), 0); }

/* the debug port's MEM-AP: an access that is not the core's */
static u32 dap_rd(struct esk_mach *m, u32 a)
{
    u32 v = 0;
    m->bus.dap = 1;
    CHECK_EQ(esk_bus_read(&m->bus, a, 4, &v), 0);
    m->bus.dap = 0;
    return v;
}

static void dap_wr(struct esk_mach *m, u32 a, u32 v)
{
    m->bus.dap = 1;
    CHECK_EQ(esk_bus_write(&m->bus, a, 4, v), 0);
    m->bus.dap = 0;
}

static u32 pc_of(struct esk_mach *m) { return m->cpu->ops->pc(m->cpu); }

static void run(struct esk_mach *m, u64 insns) { esk_mach_run(m, 1000, insns); }

/* PC_RESET: ldr r1, =WATCHED; LOOP: adds r0, #1; str r0, [r1]; b LOOP */
static int machine(struct esk_mach *m, esk_alloc *a, const char *model)
{
    struct esk_cpu *cpu = NULL;
    esk_mach_init(m, a);
    m->reset_hz = 16000000u;
    CHECK_EQ(esk_bus_add_region(m, 0x00000000u, 4096u, ESK_MEM_RAM), ES_OK);
    CHECK_EQ(esk_bus_add_region(m, 0x20000000u, 4096u, ESK_MEM_RAM), ES_OK);
    if (esk_cortexm_create(m, model, 4, &cpu) != ES_OK) {
        FAIL(model);
        return 0;
    }
    CHECK_STR(cpu->ops->name, "cortex-m");
    word(m, 0x00, 0x20001000u);
    word(m, 0x04, PC_RESET | 1);
    CHECK_EQ(esk_bus_write(&m->bus, PC_RESET, 2, 0x4900u | ((0x200u - ((PC_RESET + 4u) & ~3u)) / 4u)), 0);
    CHECK_EQ(esk_bus_write(&m->bus, LOOP, 2, 0x3001u), 0);       /* adds r0, #1 */
    CHECK_EQ(esk_bus_write(&m->bus, LOOP + 2, 2, 0x6008u), 0);   /* str r0, [r1] */
    CHECK_EQ(esk_bus_write(&m->bus, LOOP + 4, 2, 0xe7fcu), 0);   /* b LOOP */
    word(m, 0x200, WATCHED);
    m->reset_cause = ESK_RESET_POWER;
    esk_mach_reset(m);
    return 1;
}

static u32 core_reg(struct esk_mach *m, u32 sel)
{
    dap_wr(m, DCRSR, sel);
    CHECK(dap_rd(m, DHCSR) & (1u << 16)); /* S_REGRDY */
    return dap_rd(m, DCRDR);
}

static void test_halt_step_resume(const char *model)
{
    esk_alloc a;
    struct esk_mach m;
    u64 n;
    u32 v;
    esk_alloc_init(&a, NULL);
    if (!machine(&m, &a, model))
        return;
    run(&m, 10);
    CHECK_EQ(dap_rd(&m, DHCSR) & 0x20003u, 0u); /* not debugged, running */
    dap_wr(&m, DHCSR, KEY | 3u);                /* C_DEBUGEN | C_HALT */
    v = dap_rd(&m, DHCSR);
    CHECK_EQ(v & 0x30003u, 0x30003u);           /* S_HALT, S_REGRDY, C_HALT, C_DEBUGEN */
    CHECK_EQ(dap_rd(&m, DFSR) & 1u, 1u);         /* HALTED */
    n = m.insns;
    run(&m, 10);
    CHECK_EQ(m.insns, n);                       /* nothing runs in Debug state */
    /* the registers through DCRSR and DCRDR */
    CHECK_EQ(core_reg(&m, 15), pc_of(&m));
    CHECK_EQ(core_reg(&m, 1), WATCHED);
    CHECK_EQ(core_reg(&m, 17), 0x20001000u);    /* MSP */
    CHECK_EQ(core_reg(&m, 16) >> 24 & 1u, 1u);  /* xPSR's T bit */
    dap_wr(&m, DCRDR, 1000u);
    dap_wr(&m, DCRSR, (1u << 16) | 0u);         /* r0 = 1000 */
    CHECK_EQ(core_reg(&m, 0), 1000u);
    dap_wr(&m, DCRDR, LOOP | 1u);
    dap_wr(&m, DCRSR, (1u << 16) | 15u);        /* DebugReturnAddress */
    CHECK_EQ(pc_of(&m), LOOP);
    /* one step: adds, then halted again */
    dap_wr(&m, DHCSR, KEY | 5u);                /* C_DEBUGEN | C_STEP, C_HALT cleared */
    run(&m, 10);
    CHECK_EQ(m.insns, n + 1);
    CHECK_EQ(pc_of(&m), LOOP + 2);
    CHECK_EQ(core_reg(&m, 0), 1001u);
    CHECK(dap_rd(&m, DHCSR) & (1u << 17));
    /* restarted, it runs on */
    dap_wr(&m, DHCSR, KEY | 1u);
    run(&m, 30);
    CHECK(m.insns > n + 20);
    CHECK_EQ(dap_rd(&m, DHCSR) & (1u << 17), 0u);
    /* C_DEBUGEN cleared: no longer debugged */
    dap_wr(&m, DHCSR, KEY);
    CHECK_EQ(dap_rd(&m, DHCSR) & 0xfu, 0u);
    /* a write without DBGKEY does nothing */
    dap_wr(&m, DHCSR, 3u);
    CHECK_EQ(dap_rd(&m, DHCSR) & 0x3u, 0u);
    esk_mach_free(&m);
    CHECK_EQ(a.live_bytes, 0);
}

static void test_halt_step_resume_m4(void) { test_halt_step_resume("cortex-m4"); }
static void test_halt_step_resume_m0plus(void) { test_halt_step_resume("cortex-m0plus"); }

/* an FPB comparator on `str`: the core halts before it, DFSR.BKPT */
static void test_fpb_breakpoint(void)
{
    esk_alloc a;
    struct esk_mach m;
    u32 at = LOOP + 2;
    esk_alloc_init(&a, NULL);
    if (!machine(&m, &a, "cortex-m4"))
        return;
    dap_wr(&m, DHCSR, KEY | 1u);
    CHECK_EQ(dap_rd(&m, FP_CTRL) & 0xf0u, 0x60u); /* six code comparators */
    dap_wr(&m, FP_CTRL, 3u);                      /* KEY | ENABLE */
    dap_wr(&m, FP_COMP0, (at & ~3u) | ((at & 2u) ? 2u << 30 : 1u << 30) | 1u);
    run(&m, 50);
    CHECK(dap_rd(&m, DHCSR) & (1u << 17));
    CHECK_EQ(pc_of(&m), at);
    CHECK_EQ(dap_rd(&m, DFSR) & 2u, 2u);
    /* removed, the core goes on past it */
    dap_wr(&m, DFSR, 0x1fu);
    dap_wr(&m, FP_COMP0, 0u);
    dap_wr(&m, DHCSR, KEY | 1u);
    run(&m, 5);
    CHECK(pc_of(&m) != at || m.insns > 5);
    CHECK_EQ(dap_rd(&m, DFSR), 0u);
    esk_mach_free(&m);
    CHECK_EQ(a.live_bytes, 0);
}

/* a DWT comparator watching writes of WATCHED: halted after the store,
 * DFSR.DWTTRAP, FUNCTION.MATCHED set and cleared by its read */
static void test_dwt_watchpoint(void)
{
    esk_alloc a;
    struct esk_mach m;
    esk_alloc_init(&a, NULL);
    if (!machine(&m, &a, "cortex-m4"))
        return;
    dap_wr(&m, DHCSR, KEY | 1u);
    dap_wr(&m, DEMCR, 1u << 24); /* TRCENA */
    CHECK_EQ(dap_rd(&m, 0xE0001000u) >> 28, 4u); /* NUMCOMP */
    dap_wr(&m, DWT_COMP0, WATCHED);
    dap_wr(&m, DWT_MASK0, 0u);
    dap_wr(&m, DWT_FUNC0, 6u); /* writes */
    run(&m, 50);
    CHECK(dap_rd(&m, DHCSR) & (1u << 17));
    CHECK_EQ(pc_of(&m), LOOP + 4); /* after the str */
    CHECK_EQ(dap_rd(&m, DFSR) & 4u, 4u);
    CHECK(dap_rd(&m, DWT_FUNC0) & (1u << 24));
    CHECK_EQ(dap_rd(&m, DWT_FUNC0) & (1u << 24), 0u);
    esk_mach_free(&m);
    CHECK_EQ(a.live_bytes, 0);
}

/* BKPT halts a debugged core at itself; the core cannot set C_DEBUGEN */
static void test_bkpt_and_software(void)
{
    esk_alloc a;
    struct esk_mach m;
    esk_alloc_init(&a, NULL);
    if (!machine(&m, &a, "cortex-m4"))
        return;
    /* the core's own write of DHCSR: C_DEBUGEN stays clear */
    CHECK_EQ(esk_bus_write(&m.bus, DHCSR, 4, KEY | 3u), 0);
    CHECK_EQ(dap_rd(&m, DHCSR) & 3u, 0u);
    CHECK_EQ(esk_bus_write(&m.bus, LOOP + 4, 2, 0xbe00u), 0); /* bkpt #0 in place of the b */
    dap_wr(&m, DHCSR, KEY | 1u);
    run(&m, 20);
    CHECK(dap_rd(&m, DHCSR) & (1u << 17));
    CHECK_EQ(pc_of(&m), LOOP + 4);
    CHECK_EQ(dap_rd(&m, DFSR) & 2u, 2u);
    esk_mach_free(&m);
    CHECK_EQ(a.live_bytes, 0);
}

/* VC_CORERESET: a system reset halts the core at its reset vector, with
 * S_RESET_ST and DFSR.VCATCH; a power-on clears the debug domain */
static void test_reset_vector_catch(void)
{
    esk_alloc a;
    struct esk_mach m;
    esk_alloc_init(&a, NULL);
    if (!machine(&m, &a, "cortex-m4"))
        return;
    run(&m, 20);
    dap_wr(&m, DHCSR, KEY | 1u);
    dap_wr(&m, DEMCR, 1u); /* VC_CORERESET */
    (void)dap_rd(&m, DHCSR); /* S_RESET_ST read away */
    m.reset_cause = ESK_RESET_SOFTWARE;
    esk_mach_reset(&m);
    CHECK_EQ(pc_of(&m), PC_RESET);
    CHECK(dap_rd(&m, DHCSR) & (1u << 17));
    run(&m, 10);
    CHECK_EQ(m.insns, 0u);
    CHECK_EQ(dap_rd(&m, DFSR) & 8u, 8u);
    m.reset_cause = ESK_RESET_SOFTWARE;
    esk_mach_reset(&m);
    CHECK(dap_rd(&m, DHCSR) & (1u << 25)); /* S_RESET_ST */
    CHECK_EQ(dap_rd(&m, DHCSR) & (1u << 25), 0u);
    m.reset_cause = ESK_RESET_POWER;
    esk_mach_reset(&m);
    CHECK_EQ(dap_rd(&m, DHCSR) & 0x20003u, 0u);
    CHECK_EQ(dap_rd(&m, DEMCR), 0u);
    run(&m, 10);
    CHECK_EQ(m.insns, 10u);
    esk_mach_free(&m);
    CHECK_EQ(a.live_bytes, 0);
}

TEST_MAIN_BEGIN
RUN(test_halt_step_resume_m4);
RUN(test_halt_step_resume_m0plus);
RUN(test_fpb_breakpoint);
RUN(test_dwt_watchpoint);
RUN(test_bkpt_and_software);
RUN(test_reset_vector_catch);
TEST_MAIN_END
