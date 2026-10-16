/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* test_riscv.c -- the RISC-V core and the FE310's models (SPEC-009): hand-
 * assembled RV32 programs at the reset address, run on the machine the
 * catalog's description builds. */
#include "harness.h"
#include "mcu/build.h"
#include "mcu/riscv/riscv.h"
#include "util/json.h"

#define BASE 0x20010000u

/* ---- encodings ------------------------------------------------------------ */

static u32 i_type(u32 op, u32 f3, u32 rd, u32 rs1, s32 imm)
{
    return ((u32)imm & 0xfffu) << 20 | rs1 << 15 | f3 << 12 | rd << 7 | op;
}
static u32 addi(u32 rd, u32 rs1, s32 imm) { return i_type(0x13, 0, rd, rs1, imm); }
static u32 lui(u32 rd, u32 imm20) { return imm20 << 12 | rd << 7 | 0x37; }
static u32 r_type(u32 f7, u32 f3, u32 rd, u32 rs1, u32 rs2) { return f7 << 25 | rs2 << 20 | rs1 << 15 | f3 << 12 | rd << 7 | 0x33; }
static u32 sw(u32 rs2, u32 rs1, s32 imm)
{
    u32 m = (u32)imm;
    return ((m >> 5) & 0x7f) << 25 | rs2 << 20 | rs1 << 15 | 2u << 12 | (m & 31) << 7 | 0x23;
}
static u32 csrrw(u32 rd, u32 csr, u32 rs1) { return csr << 20 | rs1 << 15 | 1u << 12 | rd << 7 | 0x73; }
static u32 csrrs(u32 rd, u32 csr, u32 rs1) { return csr << 20 | rs1 << 15 | 2u << 12 | rd << 7 | 0x73; }
static u32 csrrsi(u32 rd, u32 csr, u32 z) { return csr << 20 | z << 15 | 6u << 12 | rd << 7 | 0x73; }
static u32 lw(u32 rd, u32 rs1, s32 imm) { return i_type(0x03, 2, rd, rs1, imm); }
#define ECALL 0x00000073u
#define MRET 0x30200073u
#define WFI 0x10500073u
#define JAL_SELF 0x0000006fu

/* ---- the machine -------------------------------------------------------------- */

typedef struct fe {
    esk_alloc a;
    esk_json *mcu, *regs;
    struct esk_mach m;
    char tx[64];
    size_t ntx;
    u32 tx_bits;
    int bus_errors; /* mcu.bus_error diagnostics */
} fe;

static unsigned char *slurp(const char *rel, size_t *len)
{
    char path[1024];
    FILE *f;
    unsigned char *buf;
    long n;
    snprintf(path, sizeof path, "%s/%s", EMBSIM_SOURCE_DIR, rel);
    f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (unsigned char *)malloc(n > 0 ? (size_t)n : 1);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    *len = (size_t)n;
    return buf;
}

static esk_json *load_json(esk_alloc *a, const char *rel)
{
    size_t len = 0;
    unsigned char *text = slurp(rel, &len);
    esk_json *j;
    int line = 0;
    const char *err = NULL;
    if (!text)
        return NULL;
    j = esk_json_parse(a, (const char *)text, len, &line, &err);
    free(text);
    return j;
}

static void f_uart(void *ctx, const char *unit, int c, u32 bit_cycles)
{
    fe *f = (fe *)ctx;
    (void)unit;
    f->tx_bits = bit_cycles;
    if (f->ntx + 1 < sizeof f->tx)
        f->tx[f->ntx++] = (char)c;
}

static void f_diag(void *ctx, int sev, const char *code, const char *msg)
{
    fe *f = (fe *)ctx;
    (void)sev;
    (void)msg;
    if (!strcmp(code, "mcu.bus_error"))
        f->bus_errors++;
}

/* an FE310 out of reset (HFROSC: 14.4 MHz) with `prog` at the reset pc and
 * `handler` at BASE + 0x100 */
static int fe_open(fe *f, const u32 *prog, size_t n, const u32 *handler, size_t nh)
{
    char why[200];
    es_status st;
    size_t i;
    memset(f, 0, sizeof *f);
    esk_alloc_init(&f->a, NULL);
    f->mcu = load_json(&f->a, "catalog/mcus/sifive/fe310_g002/mcu.json");
    f->regs = load_json(&f->a, "catalog/mcus/sifive/fe310_g002/registers.json");
    CHECK(f->mcu != NULL && f->regs != NULL);
    esk_mach_init(&f->m, &f->a);
    f->m.host.ctx = f;
    f->m.host.uart_tx = f_uart;
    f->m.host.diag = f_diag;
    st = esk_mach_build(&f->m, f->mcu, f->regs, 16000000u, why, sizeof why);
    if (st != ES_OK) {
        FAIL(why);
        return 0;
    }
    for (i = 0; i < n; i++)
        CHECK_EQ(esk_bus_debug_write(&f->m.bus, BASE + (u32)(4 * i), 4, prog[i]), 0);
    for (i = 0; i < nh; i++)
        CHECK_EQ(esk_bus_debug_write(&f->m.bus, BASE + 0x100u + (u32)(4 * i), 4, handler[i]), 0);
    esk_mach_reset(&f->m);
    return 1;
}

static void fe_close(fe *f)
{
    esk_mach_free(&f->m);
    esk_json_free(&f->a, f->mcu);
    esk_json_free(&f->a, f->regs);
    CHECK_EQ(f->a.live_bytes, 0);
}

static u32 x(fe *f, int n)
{
    u8 b[8] = { 0 };
    f->m.cpu->ops->reg_read(f->m.cpu, n, b);
    return (u32)b[0] | (u32)b[1] << 8 | (u32)b[2] << 16 | (u32)b[3] << 24;
}

/* ---- the tests ------------------------------------------------------------------ */

static void test_alu_and_cycles(void)
{
    /* c.li x10, 5 sits in the low half of a word with a c.nop above it */
    const u32 prog[] = { addi(5, 0, 7), addi(6, 0, 6), r_type(1, 0, 7, 5, 6), addi(8, 0, -3), r_type(1, 4, 9, 7, 8),
                         (u32)(0x0001u << 16 | (2u << 13 | 10u << 7 | 5u << 2 | 1u)), JAL_SELF };
    fe f;
    if (!fe_open(&f, prog, sizeof prog / sizeof prog[0], NULL, 0))
        return;
    CHECK_EQ(esk_mach_core_hz(&f.m), 14400000u);
    esk_mach_run(&f.m, 0xffffff, 7);
    CHECK_EQ(x(&f, 7), 42u);
    CHECK_EQ(x(&f, 9), (u32)-14);
    CHECK_EQ(x(&f, 10), 5u);
    /* addi 1, addi 1, mul 1, addi 1, div 16, c.li 1, c.nop 1 */
    CHECK_EQ(f.m.cycles, 22u);
    esk_mach_run(&f.m, 100, 0);
    CHECK(f.m.asleep); /* the jump to itself with nothing enabled */
    fe_close(&f);
}

/* a store at a negative offset (S-type imm[11:5] negative): GCC's stack
 * frames are full of them */
static void test_store_negative_offset(void)
{
    const u32 prog[] = { lui(1, 0x80001), addi(2, 0, 0x5a), sw(2, 1, -64), lw(3, 1, -64),
                         sw(2, 1, -2048), JAL_SELF };
    fe f;
    u32 v = 0;
    if (!fe_open(&f, prog, sizeof prog / sizeof prog[0], NULL, 0))
        return;
    esk_mach_run(&f.m, 0xffffff, 5);
    CHECK_EQ(esk_bus_debug_read(&f.m.bus, 0x80000fc0u, 4, &v), 0);
    CHECK_EQ(v, 0x5au);
    CHECK_EQ(x(&f, 3), 0x5au);
    CHECK_EQ(esk_bus_debug_read(&f.m.bus, 0x80000800u, 4, &v), 0);
    CHECK_EQ(v, 0x5au);
    fe_close(&f);
}

static void test_ecall_trap(void)
{
    const u32 prog[] = { lui(1, BASE >> 12), addi(1, 1, 0x100), csrrw(0, 0x305, 1), ECALL, JAL_SELF };
    const u32 handler[] = { csrrs(11, 0x342, 0), csrrs(12, 0x341, 0), JAL_SELF };
    fe f;
    if (!fe_open(&f, prog, sizeof prog / sizeof prog[0], handler, 3))
        return;
    esk_mach_run(&f.m, 0xffffff, 6);
    CHECK_EQ(x(&f, 11), 11u);           /* ECALL from M-mode */
    CHECK_EQ(x(&f, 12), BASE + 12u);    /* mepc: the ecall */
    CHECK_EQ(x(&f, 32), BASE + 0x108u); /* pc in the handler */
    fe_close(&f);
}

static void test_clint_timer(void)
{
    /* mtimecmp = 32768 (one second of the RTC), MTIE and MIE, then WFI */
    const u32 prog[] = { lui(1, BASE >> 12), addi(1, 1, 0x100), csrrw(0, 0x305, 1), lui(5, 0x02004), lui(6, 8),
                         sw(0, 5, 4), sw(6, 5, 0), addi(7, 0, 0x80), csrrs(0, 0x304, 7), csrrsi(0, 0x300, 8), WFI,
                         JAL_SELF };
    const u32 handler[] = { csrrs(11, 0x342, 0), JAL_SELF };
    fe f;
    int i;
    if (!fe_open(&f, prog, sizeof prog / sizeof prog[0], handler, 2))
        return;
    for (i = 0; i < 20000 && x(&f, 11) == 0; i++)
        esk_mach_run(&f.m, 1000, 0);
    CHECK_EQ(x(&f, 11), 0x80000007u);
    /* 32768 RTC ticks at 14.4 MHz are 14,400,000 cycles exactly; the
     * interrupt is taken at the first instruction after it (the run stops
     * at most a chunk and the handler's csrr later) */
    CHECK(f.m.cycles >= 14400000u && f.m.cycles <= 14400000u + 1000u);
    fe_close(&f);
}

static void test_uart_frame(void)
{
    /* UART0: div 124 (125 cycles a bit), txen, then 'R' */
    const u32 prog[] = { lui(5, 0x10013), addi(6, 0, 124), sw(6, 5, 0x18), addi(6, 0, 1), sw(6, 5, 8),
                         addi(6, 0, 'R'), sw(6, 5, 0), JAL_SELF };
    fe f;
    if (!fe_open(&f, prog, sizeof prog / sizeof prog[0], NULL, 0))
        return;
    esk_mach_run(&f.m, 0xffffff, 7);
    CHECK_EQ(f.ntx, 1u);
    CHECK_EQ(f.tx[0], 'R');
    CHECK_EQ(f.tx_bits, 125u);
    fe_close(&f);
}

static void test_plic_reserved(void)
{
    /* the PLIC's registers past the FE310's 52 sources, which the PLIC
     * specification has: they read 0 and ignore writes, without a bus
     * error; source 0's priority reads 0; past them, a bus error */
    static const u32 prog[] = { JAL_SELF };
    fe f;
    u32 v = 1;
    if (!fe_open(&f, prog, 1, 0, 0))
        return;
    CHECK_EQ(esk_bus_write(&f.m.bus, 0x0c000000u, 4, 7), 0); /* source 0's priority */
    CHECK_EQ(esk_bus_read(&f.m.bus, 0x0c000000u, 4, &v), 0);
    CHECK_EQ(v, 0u);
    CHECK_EQ(esk_bus_write(&f.m.bus, 0x0c002008u, 4, 0xffffffffu), 0); /* enables of 64 to 95 */
    v = 1;
    CHECK_EQ(esk_bus_read(&f.m.bus, 0x0c002008u, 4, &v), 0);
    CHECK_EQ(v, 0u);
    v = 1;
    CHECK_EQ(esk_bus_read(&f.m.bus, 0x0c000ffcu, 4, &v), 0); /* source 1023's priority */
    CHECK_EQ(v, 0u);
    CHECK_EQ(esk_bus_read(&f.m.bus, 0x0c001004u, 4, &v), 0); /* PENDING1: a register */
    CHECK_EQ(f.bus_errors, 0);
    (void)esk_bus_read(&f.m.bus, 0x0c002080u, 4, &v); /* context 1's enables: not the FE310's */
    CHECK_EQ(f.bus_errors, 1);
    fe_close(&f);
}

/* ---- the PMP ---------------------------------------------------------------- */

static void test_pmp_locked_napot(void)
{
    /* entry 0: NAPOT over 64 bytes at 0x80000100, read-only and locked, so
     * it binds machine mode: a load passes, a store faults; the locked
     * entry ignores writes; entries 8 and up (pmpcfg2) are not the FE310's */
    const u32 prog[] = { lui(1, BASE >> 12), addi(1, 1, 0x100), csrrw(0, 0x305, 1),
                         lui(5, 0x20000), addi(5, 5, 0x47), csrrw(0, 0x3b0, 5),
                         addi(6, 0, 0x99), csrrw(0, 0x3a0, 6),
                         csrrw(0, 0x3a0, 0), csrrs(13, 0x3a0, 0), csrrw(0, 0x3b0, 0), csrrs(14, 0x3b0, 0),
                         csrrw(0, 0x3a2, 6), csrrs(15, 0x3a2, 0),
                         lui(7, 0x80000), addi(7, 7, 0x100), lw(8, 7, 0), sw(6, 7, 0), JAL_SELF };
    const u32 handler[] = { csrrs(11, 0x342, 0), csrrs(12, 0x343, 0), JAL_SELF };
    fe f;
    u32 v = 1;
    if (!fe_open(&f, prog, sizeof prog / sizeof prog[0], handler, 3))
        return;
    esk_mach_run(&f.m, 0xffffff, 21);
    CHECK_EQ(x(&f, 13), 0x99u);
    CHECK_EQ(x(&f, 14), 0x20000047u);
    CHECK_EQ(x(&f, 15), 0u);
    CHECK_EQ(x(&f, 11), 7u);           /* a store access fault */
    CHECK_EQ(x(&f, 12), 0x80000100u);  /* at the protected word */
    CHECK_EQ(x(&f, 32), BASE + 0x108u);
    CHECK_EQ(esk_bus_debug_read(&f.m.bus, 0x80000100u, 4, &v), 0);
    CHECK_EQ(v, 0u);                    /* not written */
    fe_close(&f);
}

/* an mret to user mode at BASE + 0x80, which holds an ecall; with
 * `open`, entry 0 first covers everything with RWX (NAPOT, all ones) */
static u32 user_cause(int open)
{
    u32 prog[40];
    const u32 head[] = { lui(1, BASE >> 12), addi(1, 1, 0x100), csrrw(0, 0x305, 1),
                         lui(5, BASE >> 12), addi(5, 5, 0x80), csrrw(0, 0x341, 5),
                         addi(6, 0, -1), csrrw(0, 0x3b0, 6), addi(7, 0, 0x1f), csrrw(0, 0x3a0, 7), MRET };
    const u32 handler[] = { csrrs(11, 0x342, 0), JAL_SELF };
    fe f;
    size_t i;
    u32 cause;
    for (i = 0; i < 40; i++)
        prog[i] = JAL_SELF;
    for (i = 0; i < sizeof head / sizeof head[0]; i++)
        prog[i] = head[i];
    if (!open)
        prog[9] = addi(0, 0, 0); /* no entry is turned on */
    prog[32] = ECALL;
    if (!fe_open(&f, prog, 40, handler, 2))
        return 0;
    esk_mach_run(&f.m, 0xffffff, 14);
    cause = x(&f, 11);
    fe_close(&f);
    return cause;
}

static void test_pmp_user_mode(void)
{
    /* user mode is allowed only what an entry gives it */
    CHECK_EQ(user_cause(0), 1u); /* an instruction access fault */
    CHECK_EQ(user_cause(1), 8u); /* the ecall, from user mode */
}

/* ---- the always-on domain ------------------------------------------------------ */

#define AON 0x10000000u
#define PLIC_PENDING0 0x0c001000u

static u32 rd(fe *f, u32 a)
{
    u32 v = 0;
    CHECK_EQ(esk_bus_read(&f->m.bus, a, 4, &v), 0);
    return v;
}

static void wd_write(fe *f, u32 off, u32 v)
{
    CHECK_EQ(esk_bus_write(&f->m.bus, AON + 0x1c, 4, 0x51F15Eu), 0); /* wdogkey */
    CHECK_EQ(esk_bus_write(&f->m.bus, AON + off, 4, v), 0);
}

static void test_aon_rtc(void)
{
    /* rtccmp0 at 32768 counts of the 32.768 kHz clock: one second, 14.4M
     * cycles at the HFROSC's 14.4 MHz; then rtcip0 and PLIC source 2 */
    static const u32 prog[] = { JAL_SELF };
    fe f;
    if (!fe_open(&f, prog, 1, 0, 0))
        return;
    CHECK_EQ(rd(&f, AON + 0x60), 0xffffffffu); /* rtccmp0 out of reset: rtcip0 low */
    CHECK_EQ(esk_bus_write(&f.m.bus, AON + 0x60, 4, 32768), 0);
    CHECK_EQ(esk_bus_write(&f.m.bus, AON + 0x40, 4, 0x1000), 0); /* rtcenalways */
    esk_mach_run(&f.m, 14400000u - 200u, 0); /* 32767.5 clocks */
    CHECK_EQ(rd(&f, AON + 0x48), 32767u);
    CHECK_EQ(rd(&f, AON + 0x40) >> 28 & 1, 0u);
    CHECK_EQ(rd(&f, PLIC_PENDING0) >> 2 & 1, 0u);
    esk_mach_run(&f.m, 400, 0); /* 32768.4 */
    CHECK_EQ(rd(&f, AON + 0x50), 32768u);         /* rtcs */
    CHECK_EQ(rd(&f, AON + 0x40) >> 28 & 1, 1u);   /* rtcip0 */
    CHECK_EQ(rd(&f, PLIC_PENDING0) >> 2 & 1, 1u);
    CHECK_EQ(esk_bus_write(&f.m.bus, AON + 0x60, 4, 40000), 0);
    CHECK_EQ(rd(&f, AON + 0x40) >> 28 & 1, 0u);   /* a level: low again */
    fe_close(&f);
}

static void test_aon_watchdog(void)
{
    /* writes without the key are ignored; wdogcmp0 100 with wdogzerocmp:
     * the comparator fires at count 100 and the count is back to 0 a clock
     * later, so after 150 clocks it is 49; wdogip0 is sticky and is PLIC
     * source 1; the food zeroes the count */
    static const u32 prog[] = { JAL_SELF };
    fe f;
    if (!fe_open(&f, prog, 1, 0, 0))
        return;
    CHECK_EQ(esk_bus_write(&f.m.bus, AON + 0x00, 4, 0x1000), 0);
    CHECK_EQ(rd(&f, AON + 0x00), 0u);
    wd_write(&f, 0x20, 100);
    wd_write(&f, 0x00, 0x1200); /* wdogenalways, wdogzerocmp, scale 0 */
    CHECK_EQ(rd(&f, AON + 0x1c), 0u); /* locked again */
    esk_mach_run(&f.m, 65918, 0); /* 150 clocks of 32.768 kHz at 14.4 MHz */
    CHECK_EQ(rd(&f, AON + 0x08), 49u);
    CHECK_EQ(rd(&f, AON + 0x00) >> 28 & 1, 1u);
    CHECK_EQ(rd(&f, PLIC_PENDING0) >> 1 & 1, 1u);
    CHECK_EQ(esk_bus_write(&f.m.bus, AON + 0x18, 4, 0xD09F00Du), 0); /* no key: ignored */
    CHECK(rd(&f, AON + 0x08) != 0u);
    wd_write(&f, 0x18, 0xD09F00Du);
    CHECK_EQ(rd(&f, AON + 0x08), 0u);
    wd_write(&f, 0x00, 0x1200); /* wdogip0 cleared */
    CHECK_EQ(rd(&f, AON + 0x00) >> 28 & 1, 0u);
    fe_close(&f);
}

static void test_aon_watchdog_reset(void)
{
    /* with wdogrsten the comparator resets the chip; the always-on domain
     * keeps its registers, and pmucause says the watchdog did it */
    static const u32 prog[] = { JAL_SELF };
    fe f;
    if (!fe_open(&f, prog, 1, 0, 0))
        return;
    CHECK_EQ(rd(&f, AON + 0x144), 0u);
    CHECK_EQ(esk_bus_write(&f.m.bus, AON + 0x80, 4, 0x1234), 0); /* backup0 */
    wd_write(&f, 0x20, 10);
    wd_write(&f, 0x00, 0x1100); /* wdogenalways, wdogrsten */
    esk_mach_run(&f.m, 14400000u, 0);
    CHECK(f.m.reset_req);
    esk_mach_reset(&f.m);
    CHECK_EQ(rd(&f, AON + 0x80), 0x1234u);
    CHECK_EQ(rd(&f, AON + 0x144), 0x200u);
    CHECK_EQ(rd(&f, AON + 0x00) & 0x1100u, 0x1100u);
    esk_mach_reset(&f.m); /* not the watchdog's: everything resets */
    CHECK_EQ(rd(&f, AON + 0x80), 0u);
    CHECK_EQ(rd(&f, AON + 0x144), 0u);
    fe_close(&f);
}

static void test_disassembler(void)
{
    char t[96];
    CHECK_EQ(esk_riscv_disasm(0, addi(5, 0, 7), t, sizeof t), 4);
    CHECK_STR(t, "addi t0, zero, 7");
    CHECK_EQ(esk_riscv_disasm(0, 2u << 13 | 10u << 7 | 5u << 2 | 1u, t, sizeof t), 2);
    CHECK_STR(t, "c.addi a0, zero, 5");
    esk_riscv_disasm(0, WFI, t, sizeof t);
    CHECK_STR(t, "wfi");
}

TEST_MAIN_BEGIN
RUN(test_alu_and_cycles);
RUN(test_store_negative_offset);
RUN(test_ecall_trap);
RUN(test_clint_timer);
RUN(test_uart_frame);
RUN(test_plic_reserved);
RUN(test_pmp_locked_napot);
RUN(test_pmp_user_mode);
RUN(test_aon_rtc);
RUN(test_aon_watchdog);
RUN(test_aon_watchdog_reset);
RUN(test_disassembler);
TEST_MAIN_END
