/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* test_mips.c -- the MIPS32 core (SPEC-009 §2): hand-assembled programs at
 * the boot vector of a bare machine (a PIC32MX's memory map: boot flash,
 * program flash, RAM), with the M4K (`pic32mx`, EIC mode) and the 24Kc
 * (compatibility-mode interrupts). */
#include "harness.h"
#include "mcu/mips/mips.h"

#define BOOT 0xbfc00000u /* kseg1: the reset vector */

/* ---- encodings ------------------------------------------------------------ */

enum { ZERO, AT, V0, V1, A0, A1, A2, A3, T0, T1, T2, T3, T4, T5, T6, T7, S0, S1,
       K0 = 26, K1 = 27, SP = 29, RA = 31 };

static u32 i_type(u32 op, u32 rs, u32 rt, u32 imm) { return op << 26 | rs << 21 | rt << 16 | (imm & 0xffffu); }
static u32 addiu(u32 rt, u32 rs, s32 imm) { return i_type(9, rs, rt, (u32)imm); }
static u32 ori(u32 rt, u32 rs, u32 imm) { return i_type(13, rs, rt, imm); }
static u32 lui(u32 rt, u32 imm) { return i_type(15, 0, rt, imm); }
static u32 lw(u32 rt, u32 base, s32 off) { return i_type(0x23, base, rt, (u32)off); }
static u32 sw(u32 rt, u32 base, s32 off) { return i_type(0x2b, base, rt, (u32)off); }
static u32 ll(u32 rt, u32 base, s32 off) { return i_type(0x30, base, rt, (u32)off); }
static u32 sc(u32 rt, u32 base, s32 off) { return i_type(0x38, base, rt, (u32)off); }
static u32 beq(u32 rs, u32 rt, s32 words) { return i_type(4, rs, rt, (u32)words); }
static u32 mul(u32 rd, u32 rs, u32 rt) { return 0x70000002u | rs << 21 | rt << 16 | rd << 11; }
static u32 mfc0(u32 rt, u32 rd, u32 sel) { return 0x40000000u | rt << 16 | rd << 11 | sel; }
static u32 mtc0(u32 rt, u32 rd, u32 sel) { return 0x40800000u | rt << 16 | rd << 11 | sel; }
static u32 jalx(u32 target) { return 0x74000000u | (target >> 2 & 0x03ffffffu); }
#define NOP 0u
#define SYSCALL 0x0000000cu
#define WAIT 0x42000020u
#define ERET 0x42000018u
#define SDBBP 0x7000003fu
#define B_SELF 0x1000ffffu /* beq zero, zero, . */

/* ---- the machine -------------------------------------------------------------- */

typedef struct mx {
    esk_alloc a;
    struct esk_mach m;
} mx;

static int mx_open(mx *t, const char *model, const u32 *prog, size_t n)
{
    struct esk_cpu *cpu = NULL;
    size_t i;
    memset(t, 0, sizeof *t);
    esk_alloc_init(&t->a, NULL);
    esk_mach_init(&t->m, &t->a);
    CHECK_EQ(esk_bus_add_region(&t->m, 0x1fc00000u, 0x3000u, ESK_MEM_FLASH), ES_OK);
    CHECK_EQ(esk_bus_add_region(&t->m, 0x1d000000u, 0x80000u, ESK_MEM_FLASH), ES_OK);
    CHECK_EQ(esk_bus_add_region(&t->m, 0x00000000u, 0x20000u, ESK_MEM_RAM), ES_OK);
    if (esk_mips_create(&t->m, model, &cpu) != ES_OK) {
        FAIL("the core cannot be created");
        return 0;
    }
    t->m.reset_hz = 8000000u;
    for (i = 0; i < n; i++)
        CHECK_EQ(esk_bus_debug_write(&t->m.bus, esk_mips_phys(BOOT) + (u32)(4 * i), 4, prog[i]), 0);
    esk_mach_reset(&t->m);
    return 1;
}

/* words at a kseg1 address of boot flash */
static void mx_put(mx *t, u32 va, const u32 *w, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++)
        CHECK_EQ(esk_bus_debug_write(&t->m.bus, esk_mips_phys(va) + (u32)(4 * i), 4, w[i]), 0);
}

static void mx_close(mx *t)
{
    esk_mach_free(&t->m);
    CHECK_EQ(t->a.live_bytes, 0);
}

static u32 reg(mx *t, int n)
{
    u8 b[4] = { 0 };
    t->m.cpu->ops->reg_read(t->m.cpu, n, b);
    return (u32)b[0] | (u32)b[1] << 8 | (u32)b[2] << 16 | (u32)b[3] << 24;
}

enum { GR_STATUS = 32, GR_LO, GR_HI, GR_BADVADDR, GR_CAUSE, GR_PC };

/* ---- the tests ------------------------------------------------------------------ */

static void test_alu_and_delay_slot(void)
{
    /* the branch's delay slot runs; the instruction after it does not */
    const u32 prog[] = { lui(T0, 0x1234), ori(T0, T0, 0x5678), addiu(T1, ZERO, -3), mul(T5, T1, T1),
                         beq(ZERO, ZERO, 2), addiu(T2, ZERO, 7), addiu(T3, ZERO, 1), addiu(T4, ZERO, 9),
                         B_SELF, NOP };
    mx t;
    if (!mx_open(&t, "pic32mx", prog, sizeof prog / sizeof prog[0]))
        return;
    CHECK_EQ(reg(&t, GR_PC), BOOT);
    CHECK(reg(&t, GR_STATUS) & 0x00400004u); /* BEV and ERL out of reset */
    esk_mach_run(&t.m, 1000, 0);
    CHECK_EQ(reg(&t, T0), 0x12345678u);
    CHECK_EQ(reg(&t, T1), 0xfffffffdu);
    CHECK_EQ(reg(&t, T5), 9u);
    CHECK_EQ(reg(&t, T2), 7u);
    CHECK_EQ(reg(&t, T3), 0u);
    CHECK_EQ(reg(&t, T4), 9u);
    CHECK(t.m.asleep); /* the loop to itself, with no interrupt to come */
    /* lui, ori, addiu 1 each, mul 2, beq and its slot 1 each, addiu, and the
     * b to itself, which sleeps before its slot */
    CHECK_EQ(t.m.cycles, 9u);
    mx_close(&t);
}

static void test_exception_and_delay_slot(void)
{
    /* a syscall in a branch's delay slot: EPC is the branch, Cause.BD set,
     * the vector BEV's general one (0xbfc00380) */
    const u32 prog[] = { addiu(T0, ZERO, 1), beq(ZERO, ZERO, 4), SYSCALL };
    const u32 handler[] = { mfc0(K0, 13, 0), mfc0(K1, 14, 0), mfc0(T7, 12, 0), B_SELF, NOP };
    mx t;
    if (!mx_open(&t, "pic32mx", prog, sizeof prog / sizeof prog[0]))
        return;
    mx_put(&t, BOOT + 0x380, handler, sizeof handler / sizeof handler[0]);
    esk_mach_run(&t.m, 1000, 0);
    CHECK_EQ(reg(&t, K0) >> 2 & 31u, 8u);      /* ExcCode: Sys */
    CHECK(reg(&t, K0) & 0x80000000u);          /* BD */
    CHECK_EQ(reg(&t, K1), BOOT + 4u);          /* EPC: the branch */
    CHECK(reg(&t, T7) & 2u);                   /* EXL */
    mx_close(&t);
}

static void test_eret_and_address_error(void)
{
    /* ERL cleared (BEV kept), as a startup does, then a misaligned load:
     * AdEL with BadVAddr; the handler steps past it */
    const u32 prog[] = { lui(T0, 0x0040), mtc0(T0, 12, 0), addiu(T0, ZERO, 0x101), lw(T1, T0, 0),
                         addiu(T2, ZERO, 5), B_SELF, NOP };
    const u32 handler[] = { mfc0(K0, 13, 0), mfc0(K1, 14, 0), addiu(K1, K1, 4), mtc0(K1, 14, 0),
                            mfc0(T7, 8, 0), ERET };
    mx t;
    if (!mx_open(&t, "pic32mx", prog, sizeof prog / sizeof prog[0]))
        return;
    mx_put(&t, BOOT + 0x380, handler, sizeof handler / sizeof handler[0]);
    esk_mach_run(&t.m, 1000, 0);
    CHECK_EQ(reg(&t, K0) >> 2 & 31u, 4u); /* AdEL */
    CHECK_EQ(reg(&t, T7), 0x101u);        /* BadVAddr */
    CHECK_EQ(reg(&t, T2), 5u);            /* back after the load */
    CHECK_EQ(reg(&t, GR_STATUS) & 2u, 0u);
    mx_close(&t);
}

static void test_core_timer_compat(void)
{
    /* the 24Kc: Compare 100 (200 cycles: Count counts every other one), IM7
     * and IE with BEV kept, then wait; the interrupt comes at 0xbfc00380 */
    const u32 prog[] = { addiu(T0, ZERO, 100), mtc0(T0, 11, 0), lui(T1, 0x0040), ori(T1, T1, 0x8001),
                         mtc0(T1, 12, 0), WAIT, B_SELF, NOP };
    const u32 handler[] = { mfc0(K0, 13, 0), mfc0(K1, 9, 0), B_SELF, NOP };
    mx t;
    int i;
    if (!mx_open(&t, "24kc", prog, sizeof prog / sizeof prog[0]))
        return;
    mx_put(&t, BOOT + 0x380, handler, sizeof handler / sizeof handler[0]);
    for (i = 0; i < 100 && reg(&t, K0) == 0; i++)
        esk_mach_run(&t.m, 50, 0);
    CHECK_EQ(reg(&t, K0) >> 2 & 31u, 0u); /* Int */
    CHECK(reg(&t, K0) & 0x40000000u);     /* TI */
    CHECK(reg(&t, K0) & 0x8000u);         /* IP7 */
    CHECK(reg(&t, K1) >= 100u && reg(&t, K1) <= 110u);
    CHECK(t.m.cycles >= 200u && t.m.cycles <= 230u);
    mx_close(&t);
}

/* an EIC that requests level 3 while the core's timer is asserted */
typedef struct eic_t {
    struct mips_eic e;
    struct esk_mach *m;
    int timer, taken;
    u64 at; /* the cycle it was taken */
} eic_t;

static void eic_core(void *ctx, int timer, int sw0, int sw1)
{
    eic_t *c = (eic_t *)ctx;
    ES_UNUSED(sw0);
    ES_UNUSED(sw1);
    c->timer = timer;
    c->e.ripl = timer ? 3 : 0;
}

static void eic_taken(void *ctx)
{
    eic_t *c = (eic_t *)ctx;
    c->taken++;
    c->at = c->m->cycles;
}
static int eic_timer_on(void *ctx) { ES_UNUSED(ctx); return 1; }

static void test_core_timer_eic(void)
{
    /* the M4K in EIC mode: EBase 0x9fc01000, IntCtl.VS 32 bytes, Cause.IV,
     * BEV and ERL cleared, IE; the controller's vector 2 is taken at
     * EBase + 0x200 + 2 * 32 */
    const u32 prog[] = { lui(T0, 0x9fc0), ori(T0, T0, 0x1000), mtc0(T0, 15, 1), addiu(T0, ZERO, 0x20),
                         mtc0(T0, 12, 1), lui(T0, 0x0080), mtc0(T0, 13, 0), addiu(T0, ZERO, 50),
                         mtc0(T0, 11, 0), addiu(T0, ZERO, 1), mtc0(T0, 12, 0), WAIT, B_SELF, NOP };
    const u32 handler[] = { mfc0(K0, 13, 0), mfc0(K1, 12, 0), mtc0(ZERO, 11, 0), B_SELF, NOP };
    eic_t c;
    mx t;
    int i;
    if (!mx_open(&t, "pic32mx", prog, sizeof prog / sizeof prog[0]))
        return;
    memset(&c, 0, sizeof c);
    c.e.core_irq = eic_core;
    c.e.taken = eic_taken;
    c.e.timer_on = eic_timer_on;
    c.e.ctx = &c;
    c.m = &t.m;
    c.e.spaced = 1;
    c.e.vector = 2;
    esk_mips_eic_attach(&t.m, &c.e);
    mx_put(&t, 0xbfc01240u, handler, sizeof handler / sizeof handler[0]);
    for (i = 0; i < 100 && !c.taken; i++)
        esk_mach_run(&t.m, 50, 0);
    esk_mach_run(&t.m, 20, 0);
    CHECK_EQ(c.taken, 1);
    CHECK(!c.timer);                       /* deasserted: the handler wrote Compare */
    CHECK_EQ(reg(&t, K0) >> 10 & 0xffu, 3u); /* Cause.RIPL */
    CHECK(reg(&t, K1) & 2u);               /* EXL */
    CHECK_EQ(reg(&t, GR_PC) & ~0x1fu, 0x9fc01240u); /* in the handler */
    CHECK(c.at >= 100u && c.at <= 110u); /* Compare 50: 100 cycles */
    mx_close(&t);
}

static void test_mips16(void)
{
    /* jalx to MIPS16e code that loads 42 into v0, and with EXTEND 0x1234
     * into v1, then jr ra (with its delay slot) back to MIPS32 */
    const u32 prog[] = { jalx(BOOT + 0x100), NOP, addiu(T0, ZERO, 1), B_SELF, NOP };
    mx t;
    if (!mx_open(&t, "pic32mx", prog, sizeof prog / sizeof prog[0]))
        return;
    /* li v0, 42; extend; li v1; jr ra; nop (move zero, s0), a halfword
     * each, the first at the lower address. EXTEND 0x1234: imm[15:11] = 0x02 in bits 4:0, imm[10:5] = 0x11 in
     * bits 10:5; the li's low five bits are imm[4:0] = 0x14 */
    {
        u32 ext = 0xf000u | (0x11u << 5) | 0x02u;
        u32 w[3];
        w[0] = 0x6a2au | ext << 16;
        w[1] = 0x6b14u | 0xe820u << 16;
        w[2] = 0x6500u;
        mx_put(&t, BOOT + 0x100, w, 3);
    }
    esk_mach_run(&t.m, 1000, 0);
    CHECK_EQ(reg(&t, V0), 42u);
    CHECK_EQ(reg(&t, V1), 0x1234u);
    CHECK_EQ(reg(&t, T0), 1u);           /* back in MIPS32 past the jalx */
    CHECK_EQ(reg(&t, RA), BOOT + 8u);    /* jalx's link */
    CHECK_EQ(reg(&t, GR_PC) & 1u, 0u);
    mx_close(&t);
}

static void test_ll_sc_and_memory(void)
{
    /* RAM through kseg1 (0xa0000000): ll, sc succeeds; a store between ll
     * and sc to the same word makes the second sc fail */
    const u32 prog[] = { lui(S0, 0xa000), addiu(T0, ZERO, 77), sw(T0, S0, 0x10), ll(T1, S0, 0x10),
                         addiu(T2, ZERO, 5), sc(T2, S0, 0x10), ll(T3, S0, 0x10), addiu(T4, ZERO, 9),
                         sw(T4, S0, 0x10), addiu(T5, ZERO, 6), sc(T5, S0, 0x10), lw(T6, S0, 0x10), B_SELF,
                         NOP };
    mx t;
    u32 v = 0;
    if (!mx_open(&t, "pic32mx", prog, sizeof prog / sizeof prog[0]))
        return;
    esk_mach_run(&t.m, 1000, 0);
    CHECK_EQ(reg(&t, T1), 77u);
    CHECK_EQ(reg(&t, T2), 1u); /* the first sc */
    CHECK_EQ(reg(&t, T3), 5u);
    CHECK_EQ(reg(&t, T5), 0u); /* the second: the word changed */
    CHECK_EQ(reg(&t, T6), 9u);
    CHECK_EQ(esk_bus_debug_read(&t.m.bus, 0x10u, 4, &v), 0);
    CHECK_EQ(v, 9u);
    mx_close(&t);
}

static void test_sdbbp_and_ids(void)
{
    const u32 prog[] = { mfc0(T0, 15, 0), mfc0(T1, 16, 0), SDBBP };
    mx t;
    if (!mx_open(&t, "pic32mx", prog, sizeof prog / sizeof prog[0]))
        return;
    esk_mach_run(&t.m, 1000, 0);
    CHECK_EQ(reg(&t, T0), 0x00018700u); /* PRId: the M4K */
    CHECK_EQ(reg(&t, T1) >> 7 & 7u, 3u); /* Config.MT: fixed mapping */
    CHECK_EQ(t.m.state, ESK_MACH_LOCKUP);
    mx_close(&t);
}

static void test_disassembler(void)
{
    char s[96];
    CHECK_EQ(esk_mips_disasm(0, addiu(T1, ZERO, -3), s, sizeof s), 4);
    CHECK_STR(s, "addiu t1, zero, -3");
    esk_mips_disasm(0, WAIT, s, sizeof s);
    CHECK_STR(s, "wait");
    esk_mips_disasm(0x9d000000u, beq(ZERO, ZERO, -1), s, sizeof s);
    CHECK_STR(s, "beq zero, zero, 0x9d000000");
    esk_mips_disasm(0, mtc0(T0, 12, 1), s, sizeof s);
    CHECK_STR(s, "mtc0 t0, $12, 1");
}

TEST_MAIN_BEGIN
RUN(test_alu_and_delay_slot);
RUN(test_exception_and_delay_slot);
RUN(test_eret_and_address_error);
RUN(test_core_timer_compat);
RUN(test_core_timer_eic);
RUN(test_mips16);
RUN(test_ll_sc_and_memory);
RUN(test_sdbbp_and_ids);
RUN(test_disassembler);
TEST_MAIN_END
