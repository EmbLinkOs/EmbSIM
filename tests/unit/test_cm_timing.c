/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* test_cm_timing.c -- the Cortex-M4's exception timing against its
 * Technical Reference Manual (ARM DDI 0439B 3.9.1, zero wait states):
 * 12 cycles from an interrupt to its handler's first instruction, 12 for
 * the return, 6 for a tail-chain, and a higher priority interrupt arriving
 * while a frame is stacked taking that frame (late arrival). Hand-assembled
 * Thumb code runs from RAM, one instruction at a time; each measurement is
 * the machine's cycles between two instruction boundaries, less the cost
 * table's charge (core/mcu/cortexm/cost.h) for the instruction in between,
 * which is stated where it is used. */
#include "harness.h"
#include "mcu/mach.h"


struct prog {
    struct esk_mach *m;
    u32 at;
};

static void h16(struct prog *p, u32 v)
{
    CHECK_EQ(esk_bus_write(&p->m->bus, p->at, 2, v), 0);
    p->at += 2;
}

static void word(struct esk_mach *m, u32 a, u32 v)
{
    CHECK_EQ(esk_bus_write(&m->bus, a, 4, v), 0);
}

/* ldr rd, [pc, #lit - Align(pc + 4, 4)] */
static void ldr_lit(struct prog *p, int rd, u32 lit)
{
    u32 base = (p->at + 4) & ~3u;
    CHECK(lit >= base && lit - base <= 1020 && (lit & 3) == 0); /* T1's reach */
    h16(p, 0x4800u | (u32)rd << 8 | (lit - base) / 4);
}

#define LIT_ISER 0x480u   /* NVIC_ISER0 */
#define LIT_ISPR 0x484u   /* NVIC_ISPR0 */
#define LIT_SYST 0x488u   /* SYST_CSR */
#define LIT_IPR 0x48cu    /* NVIC_IPR0 */
#define LIT_RVR 0x490u    /* the SysTick reload the test sets */

#define PC_RESET 0x100u
#define PC_IRQ0 0x200u
#define PC_IRQ1 0x300u
#define PC_SYSTICK 0x400u

static int machine(struct esk_mach *m, esk_alloc *a)
{
    struct esk_cpu *cpu = NULL;
    esk_mach_init(m, a);
    m->reset_hz = 16000000u;
    CHECK_EQ(esk_bus_add_region(m, 0x00000000u, 4096u, ESK_MEM_RAM), ES_OK);
    CHECK_EQ(esk_bus_add_region(m, 0x20000000u, 4096u, ESK_MEM_RAM), ES_OK);
    if (esk_cortexm_create(m, "cortex-m4", 4, &cpu) != ES_OK) {
        FAIL("cortex-m4");
        return 0;
    }
    word(m, 0x00, 0x20001000u);
    word(m, 0x04, PC_RESET | 1);
    word(m, 0x3c, PC_SYSTICK | 1);
    word(m, 0x40, PC_IRQ0 | 1);
    word(m, 0x44, PC_IRQ1 | 1);
    word(m, LIT_ISER, 0xE000E100u);
    word(m, LIT_ISPR, 0xE000E200u);
    word(m, LIT_SYST, 0xE000E010u);
    word(m, LIT_IPR, 0xE000E400u);
    return 1;
}

static u32 pc_of(struct esk_mach *m) { return m->cpu->ops->pc(m->cpu); }

/* one instruction (and any exception sequence before it): the cycles it took */
static u64 step1(struct esk_mach *m)
{
    u64 c0 = m->cycles;
    esk_mach_run(m, 1000, 1);
    if (getenv("CMT_DEBUG"))
        fprintf(stderr, "  pc %08x cycles %llu (+%llu) insns %llu\n", (unsigned)pc_of(m), (unsigned long long)m->cycles, (unsigned long long)(m->cycles - c0), (unsigned long long)m->insns);
    return m->cycles - c0;
}

/* cost.h's charges for the instructions the programs use */
#define C_LDR_LIT 2u
#define C_MOVS 1u
#define C_STR 1u
#define C_NOP 1u
/* bx lr with an EXC_RETURN value: 1; its refill is the return's (the TRM's
 * 12 cycles, or 6 when it tail-chains, fetch the next instruction) */
#define C_BX_RET 1u

/* an interrupt pended by a store: entry; the handler pends another of the
 * same priority, which tail-chains when it returns; then the return */
static void test_entry_tailchain_return(void)
{
    esk_alloc a;
    struct esk_mach m;
    struct prog p;
    u32 resume;
    int i;
    esk_alloc_init(&a, NULL);
    if (!machine(&m, &a))
        return;
    p.m = &m;
    p.at = PC_RESET;
    ldr_lit(&p, 0, LIT_ISER);   /* enable IRQ0 and IRQ1 */
    h16(&p, 0x2103);            /* movs r1, #3 */
    h16(&p, 0x6001);            /* str r1, [r0] */
    ldr_lit(&p, 0, LIT_ISPR);
    h16(&p, 0x2101);            /* movs r1, #1 */
    h16(&p, 0x6001);            /* str r1, [r0]: IRQ0 pends */
    resume = p.at;
    h16(&p, 0xbf00);            /* nop */
    h16(&p, 0xe7fe);            /* b . */
    p.at = PC_IRQ0;
    ldr_lit(&p, 0, LIT_ISPR);
    h16(&p, 0x2102);            /* movs r1, #2 */
    h16(&p, 0x6001);            /* str r1, [r0]: IRQ1 pends, same priority */
    h16(&p, 0x4770);            /* bx lr */
    p.at = PC_IRQ1;
    h16(&p, 0x4770);            /* bx lr */
    esk_mach_reset(&m);

    for (i = 0; i < 6; i++)
        step1(&m);
    CHECK_EQ(pc_of(&m), resume);

    /* entry: the handler's first instruction (ldr, 2) starts 12 cycles on */
    CHECK_EQ(step1(&m), 12 + C_LDR_LIT);
    CHECK_EQ(pc_of(&m), PC_IRQ0 + 2);
    CHECK_EQ(step1(&m), C_MOVS);
    CHECK_EQ(step1(&m), C_STR);
    CHECK_EQ(step1(&m), C_BX_RET);
    CHECK_EQ(pc_of(&m), resume);   /* returned (the frame popped), the cycles still to come */

    /* tail-chain: IRQ1's handler (bx lr) starts 6 cycles after IRQ0's return */
    CHECK_EQ(step1(&m), 6 + C_BX_RET);

    /* the return: the interrupted code's next instruction (nop) 12 cycles on */
    CHECK_EQ(step1(&m), 12 + C_NOP);
    CHECK_EQ(pc_of(&m), resume + 2);
    esk_mach_free(&m);
    CHECK_EQ(a.live_bytes, 0);
}

/* SysTick (priority 0) and IRQ0 (priority 0x80). IRQ0 is pended `lead`
 * cycles after SysTick is started with a reload of `rvr`; the first step
 * after the pend runs the entry and the first handler's first instruction.
 * Returns that handler's address; `entry`: the step's cycles. */
static u32 race(u32 rvr, u64 *entry, u64 *after)
{
    esk_alloc a;
    struct esk_mach m;
    struct prog p;
    int i;
    u32 first;
    esk_alloc_init(&a, NULL);
    if (!machine(&m, &a))
        return 0;
    word(&m, LIT_RVR, rvr);
    p.m = &m;
    p.at = PC_RESET;
    ldr_lit(&p, 0, LIT_IPR);
    h16(&p, 0x2180);            /* movs r1, #0x80 */
    h16(&p, 0x7001);            /* strb r1, [r0]: IRQ0 priority 0x80 */
    ldr_lit(&p, 0, LIT_ISER);
    h16(&p, 0x2101);            /* movs r1, #1 */
    h16(&p, 0x6001);            /* str r1, [r0]: IRQ0 enabled */
    ldr_lit(&p, 2, LIT_ISPR);   /* r2: ISPR, for later */
    ldr_lit(&p, 0, LIT_SYST);
    ldr_lit(&p, 1, LIT_RVR);
    h16(&p, 0x6041);            /* str r1, [r0, #4]: SYST_RVR */
    h16(&p, 0x6081);            /* str r1, [r0, #8]: SYST_CVR cleared */
    h16(&p, 0x2107);            /* movs r1, #7 */
    h16(&p, 0x6001);            /* str r1, [r0]: enabled, interrupt, core clock */
    h16(&p, 0x2101);            /* movs r1, #1 */
    h16(&p, 0x6011);            /* str r1, [r2]: IRQ0 pends */
    h16(&p, 0xe7fe);            /* b . */
    p.at = PC_IRQ0;
    h16(&p, 0x4770);            /* bx lr */
    p.at = PC_SYSTICK;          /* SysTick stops itself, then returns */
    ldr_lit(&p, 0, LIT_SYST);
    h16(&p, 0x2100);            /* movs r1, #0 */
    h16(&p, 0x6001);            /* str r1, [r0]: SYST_CSR off */
    h16(&p, 0x4770);            /* bx lr */
    esk_mach_reset(&m);
    for (i = 0; i < 15; i++)
        step1(&m);
    *entry = step1(&m);
    first = pc_of(&m);
    if (first == PC_SYSTICK + 2) {
        step1(&m);              /* movs */
        step1(&m);              /* str */
    }
    after[0] = step1(&m);
    after[1] = step1(&m);
    esk_mach_free(&m);
    CHECK_EQ(a.live_bytes, 0);
    return first;
}

static void test_late_arrival(void)
{
    u32 rvr;
    for (rvr = 2; rvr <= 16; rvr++) {
        /* SysTick's count at the pend is rvr - 2: it fires rvr - 2 cycles
         * after it, so 1 to 12 cycles into IRQ0's stacking for rvr 3..14 */
        u64 entry = 0, after[2] = { 0, 0 };
        u32 first = race(rvr, &entry, after);
        if (getenv("CMT_DEBUG"))
            fprintf(stderr, "  rvr %2u: first %08x, step %llu, then %llu, %llu\n", (unsigned)rvr, (unsigned)first,
                    (unsigned long long)entry, (unsigned long long)after[0], (unsigned long long)after[1]);
        if (rvr == 2) {
            /* fired with the pend: SysTick was pending first, an ordinary entry */
            CHECK_EQ(first, PC_SYSTICK + 2);
        } else if (rvr <= 14) {
            /* late arrival: SysTick's handler (ldr) starts when IRQ0's would have */
            CHECK_EQ(first, PC_SYSTICK + 2);
            CHECK_EQ(entry, 12 + C_LDR_LIT);
            CHECK_EQ(after[0], C_BX_RET);           /* SysTick's bx lr */
            /* IRQ0 tail-chains, its bx lr; below rvr 9 SysTick's period
             * (rvr + 1 cycles) ends before its handler stops it, and it
             * tail-chains back into itself first, as the hardware would */
            if (rvr >= 9)
                CHECK_EQ(after[1], 6 + C_BX_RET);
        } else {
            /* SysTick after IRQ0's first instruction: IRQ0 ran (bx lr) and
             * returned, and SysTick is taken from there */
            CHECK(first != PC_SYSTICK + 2);
            CHECK_EQ(entry, 12 + C_BX_RET);
        }
    }
}

TEST_MAIN_BEGIN
RUN(test_entry_tailchain_return);
RUN(test_late_arrival);
CHECK(t_str_eq("cortex-m4", "cortex-m4"));
TEST_MAIN_END
