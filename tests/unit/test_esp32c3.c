/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* test_esp32c3.c -- the ESP32-C3's ESP-RV core and its interrupt matrix
 * (ESP32-C3 TRM v1.4, chapters 1 and 8): hand-assembled RV32 programs at
 * the reset pc, run on the machine the catalog's description builds. */
#include "harness.h"
#include "mcu/build.h"
#include "mcu/esp32c3/esp32c3.h"
#include "mcu/riscv/riscv.h"
#include "util/json.h"

#define BASE 0x40000000u    /* the reset pc */
#define VEC (BASE + 0x100u) /* mtvec: 256-byte aligned */
#define INTMTX 0x600C2000u
#define UART0_MAP (INTMTX + 4u * 21u)
#define CPU_INT_ENABLE (INTMTX + 0x104u)
#define CPU_INT_TYPE (INTMTX + 0x108u)
#define CPU_INT_CLEAR (INTMTX + 0x10Cu)
#define CPU_INT_EIP (INTMTX + 0x110u)
#define CPU_INT_PRI(n) (INTMTX + 0x114u + 4u * (n))
#define CPU_INT_THRESH (INTMTX + 0x194u)
#define INTR_STATUS_0 (INTMTX + 0xF8u)

/* ---- encodings ------------------------------------------------------------ */

static u32 i_type(u32 op, u32 f3, u32 rd, u32 rs1, s32 imm)
{
    return ((u32)imm & 0xfffu) << 20 | rs1 << 15 | f3 << 12 | rd << 7 | op;
}
static u32 addi(u32 rd, u32 rs1, s32 imm) { return i_type(0x13, 0, rd, rs1, imm); }
static u32 lui(u32 rd, u32 imm20) { return imm20 << 12 | rd << 7 | 0x37; }
static u32 lw(u32 rd, u32 rs1, s32 imm) { return i_type(0x03, 2, rd, rs1, imm); }
static u32 csrrw(u32 rd, u32 csr, u32 rs1) { return csr << 20 | rs1 << 15 | 1u << 12 | rd << 7 | 0x73; }
static u32 csrrs(u32 rd, u32 csr, u32 rs1) { return csr << 20 | rs1 << 15 | 2u << 12 | rd << 7 | 0x73; }
static u32 csrrsi(u32 rd, u32 csr, u32 z) { return csr << 20 | z << 15 | 6u << 12 | rd << 7 | 0x73; }
static u32 jal_back(int words) /* jal x0, -4 * words */
{
    u32 off = (u32)(-4 * words);
    return (off >> 20 & 1u) << 31 | (off >> 1 & 0x3ffu) << 21 | (off >> 11 & 1u) << 20 |
           (off >> 12 & 0xffu) << 12 | 0x6fu;
}
#define ECALL 0x00000073u
#define MRET 0x30200073u
#define JAL_SELF 0x0000006fu
#define AMOADD_W 0x00b5252fu /* amoadd.w a0, a1, (a0) */

/* ---- the machine ------------------------------------------------------------ */

typedef struct c3 {
    esk_alloc a;
    esk_json *mcu, *regs;
    struct esk_mach m;
} c3;

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

/* an ESP32-C3 out of reset with `prog` at the reset pc and `vec` at mtvec's
 * base (0x40000100): its word 0 is the exception entry, word n interrupt n's */
static int c3_open(c3 *f, const u32 *prog, size_t n, const u32 *vec, size_t nv)
{
    char why[200];
    es_status st;
    size_t i;
    memset(f, 0, sizeof *f);
    esk_alloc_init(&f->a, NULL);
    f->mcu = load_json(&f->a, "catalog/mcus/espressif/esp32c3/mcu.json");
    f->regs = load_json(&f->a, "catalog/mcus/espressif/esp32c3/registers.json");
    CHECK(f->mcu != NULL && f->regs != NULL);
    esk_mach_init(&f->m, &f->a);
    st = esk_mach_build(&f->m, f->mcu, f->regs, 40000000u, why, sizeof why);
    if (st != ES_OK) {
        FAIL(why);
        return 0;
    }
    for (i = 0; i < n; i++)
        CHECK_EQ(esk_bus_debug_write(&f->m.bus, BASE + (u32)(4 * i), 4, prog[i]), 0);
    for (i = 0; i < nv; i++)
        CHECK_EQ(esk_bus_debug_write(&f->m.bus, VEC + (u32)(4 * i), 4, vec[i]), 0);
    esk_mach_reset(&f->m);
    return 1;
}

static void c3_close(c3 *f)
{
    esk_mach_free(&f->m);
    esk_json_free(&f->a, f->mcu);
    esk_json_free(&f->a, f->regs);
    CHECK_EQ(f->a.live_bytes, 0);
}

static u32 x(c3 *f, int n)
{
    u8 b[8] = { 0 };
    f->m.cpu->ops->reg_read(f->m.cpu, n, b);
    return (u32)b[0] | (u32)b[1] << 8 | (u32)b[2] << 16 | (u32)b[3] << 24;
}

static u32 rd(c3 *f, u32 a)
{
    u32 v = 0;
    CHECK_EQ(esk_bus_read(&f->m.bus, a, 4, &v), 0);
    return v;
}

static void wr(c3 *f, u32 a, u32 v)
{
    CHECK_EQ(esk_bus_write(&f->m.bus, a, 4, v), 0);
}

/* the vector table: the exception entry saves mcause and mepc in x20 and
 * x21; interrupt 5's in x22 and x23, then returns */
#define NVEC5 8
static const u32 *vec5(void)
{
    static u32 v[NVEC5];
    v[0] = csrrs(20, 0x342, 0);
    v[1] = csrrs(21, 0x341, 0);
    v[2] = JAL_SELF;
    v[5] = csrrs(22, 0x342, 0);
    v[6] = csrrs(23, 0x341, 0);
    v[7] = MRET;
    return v;
}

/* the prologue: mtvec at VEC */
#define MTVEC_AT_VEC lui(1, BASE >> 12), addi(1, 1, 0x100), csrrw(0, 0x305, 1)

/* ---- the core ------------------------------------------------------------------ */

static void test_csrs(void)
{
    /* misa: RV32 C I M U, no A; the IDs; mtvec keeps only its 256-byte
     * base, vectored; then mie, not the ESP-RV's, is an illegal instruction */
    const u32 prog[] = { csrrs(10, 0x301, 0), csrrs(11, 0xf11, 0), csrrs(12, 0xf12, 0),
                         MTVEC_AT_VEC, addi(2, 1, 0x7f), csrrw(0, 0x305, 2), csrrs(13, 0x305, 0),
                         csrrw(0, 0x305, 1), csrrs(14, 0x304, 0), JAL_SELF };
    c3 f;
    if (!c3_open(&f, prog, sizeof prog / sizeof prog[0], vec5(), 3))
        return;
    CHECK_STR(f.m.cpu->ops->name, "riscv");
    CHECK_EQ(esk_mach_core_hz(&f.m), 40000000u);
    esk_mach_run(&f.m, 0xffffff, 13);
    CHECK_EQ(x(&f, 10), 0x40101104u);
    CHECK_EQ(x(&f, 11), 0x612u);
    CHECK_EQ(x(&f, 12), 0x80000001u);
    CHECK_EQ(x(&f, 13), VEC | 1u);
    CHECK_EQ(x(&f, 20), 2u);          /* illegal instruction */
    CHECK_EQ(x(&f, 21), BASE + 40u);  /* the csrrs of mie */
    CHECK_EQ(x(&f, 32), VEC + 8u);    /* at the exception entry */
    c3_close(&f);
}

static void test_no_atomics(void)
{
    const u32 prog[] = { MTVEC_AT_VEC, lui(10, 0x3fc80), AMOADD_W, JAL_SELF };
    c3 f;
    if (!c3_open(&f, prog, sizeof prog / sizeof prog[0], vec5(), 3))
        return;
    esk_mach_run(&f.m, 0xffffff, 7);
    CHECK_EQ(x(&f, 20), 2u); /* the A extension is not the ESP32-C3's */
    CHECK_EQ(x(&f, 21), BASE + 16u);
    c3_close(&f);
}

static void test_counter(void)
{
    /* mpccr counts instructions (mpcer.INST) while enabled, then cycles */
    const u32 prog[] = { lui(8, 0x3fc80), addi(5, 0, 2), csrrw(0, 0x7e0, 5), csrrw(0, 0x7e2, 0),
                         addi(6, 0, 1), addi(6, 6, 1), addi(6, 6, 1), csrrs(10, 0x7e2, 0),
                         addi(5, 0, 1), csrrw(0, 0x7e0, 5), csrrw(0, 0x7e2, 0), lw(7, 8, 0),
                         csrrs(11, 0x7e2, 0), csrrs(12, 0x7e1, 0), JAL_SELF };
    c3 f;
    if (!c3_open(&f, prog, sizeof prog / sizeof prog[0], NULL, 0))
        return;
    esk_mach_run(&f.m, 0xffffff, 14);
    CHECK_EQ(x(&f, 10), 4u); /* the write of mpccr itself, then three addi */
    CHECK_EQ(x(&f, 11), 3u); /* cycles: the write (1) and a load (2) */
    CHECK_EQ(x(&f, 12), 3u); /* COUNT_SAT and COUNT_EN out of reset */
    c3_close(&f);
}

static void test_trigger(void)
{
    /* trigger 0 on the execution of BASE + 36 in machine mode, with
     * tcontrol.MTE: a breakpoint exception before it runs, its hit bit set,
     * and MTE pushed into MPTE */
    const u32 prog[] = { MTVEC_AT_VEC, lui(5, BASE >> 12), addi(5, 5, 36), csrrw(0, 0x7a2, 5),
                         addi(6, 0, 0x44), csrrw(0, 0x7a1, 6), csrrsi(0, 0x7a5, 8),
                         addi(7, 0, 1), addi(7, 7, 1), JAL_SELF };
    const u32 vec[] = { csrrs(20, 0x342, 0), csrrs(21, 0x341, 0), csrrs(24, 0x7a1, 0), csrrs(25, 0x7a5, 0),
                        JAL_SELF };
    c3 f;
    if (!c3_open(&f, prog, sizeof prog / sizeof prog[0], vec, 5))
        return;
    esk_mach_run(&f.m, 0xffffff, 14);
    CHECK_EQ(x(&f, 20), 3u);              /* breakpoint */
    CHECK_EQ(x(&f, 21), BASE + 36u);
    CHECK_EQ(x(&f, 7), 0u);               /* not run */
    CHECK_EQ(x(&f, 24) & 0x00100000u, 0x00100000u); /* hit */
    CHECK_EQ(x(&f, 24) >> 28, 2u);        /* match type */
    CHECK_EQ(x(&f, 25), 0x80u);           /* MPTE 1, MTE 0 */
    c3_close(&f);
}

/* an mret to user mode at BASE + 0x80, which holds an ecall. Entry 0
 * covers BASE to BASE + 0x200 for reads only (TOR); entry 1, NAPOT over
 * 0x40000000-0x7FFFFFFF (1 GB, the most it may cover), grants RWX. With
 * the standard's static priority entry 0 would refuse the fetch; the
 * ESP32-C3's PMP lets any matching entry grant it. Without entry 1, the
 * fetch faults. */
static u32 user_cause(int second)
{
    u32 prog[40];
    const u32 head[] = { MTVEC_AT_VEC, lui(5, BASE >> 12), addi(5, 5, 0x80), csrrw(0, 0x341, 5),
                         lui(6, 0x10000), addi(6, 6, 0x80), csrrw(0, 0x3b0, 6),
                         lui(7, 0x18000), addi(7, 7, -1), csrrw(0, 0x3b1, 7),
                         lui(8, 0x2), addi(8, 8, -0xf7), csrrw(0, 0x3a0, 8), MRET };
    c3 f;
    size_t i;
    u32 cause;
    for (i = 0; i < 40; i++)
        prog[i] = JAL_SELF;
    for (i = 0; i < sizeof head / sizeof head[0]; i++)
        prog[i] = head[i];
    if (!second)
        prog[11] = addi(8, 0, 0x09); /* entry 0 alone */
    prog[32] = ECALL;
    if (!c3_open(&f, prog, 40, vec5(), 3))
        return 0;
    esk_mach_run(&f.m, 0xffffff, 20);
    cause = x(&f, 20);
    c3_close(&f);
    return cause;
}

static void test_pmp_no_priority(void)
{
    CHECK_EQ(user_cause(1), 8u); /* the ecall, from user mode */
    CHECK_EQ(user_cause(0), 1u); /* an instruction access fault */
}

/* ---- the interrupt matrix and controller ---------------------------------------- */

/* UART0's source (21) on CPU interrupt 5 at priority 3 above threshold 1;
 * the program counts in x5 with MIE set */
static int int_open(c3 *f, u32 type)
{
    const u32 prog[] = { MTVEC_AT_VEC, csrrsi(0, 0x300, 8), addi(5, 5, 1), jal_back(1) };
    if (!c3_open(f, prog, sizeof prog / sizeof prog[0], vec5(), NVEC5))
        return 0;
    wr(f, UART0_MAP, 5);
    wr(f, CPU_INT_PRI(5), 3);
    wr(f, CPU_INT_THRESH, 1);
    wr(f, CPU_INT_TYPE, type);
    wr(f, CPU_INT_ENABLE, 1u << 5);
    esk_mach_run(&f->m, 0xffffff, 10);
    return 1;
}

static void test_level_interrupt(void)
{
    c3 f;
    int i;
    u32 before;
    if (!int_open(&f, 0))
        return;
    CHECK_EQ(rd(&f, CPU_INT_EIP), 0u);
    esk_esp32c3_source(&f.m, 21, 1);
    CHECK_EQ(rd(&f, INTR_STATUS_0), 1u << 21);
    CHECK_EQ(rd(&f, CPU_INT_EIP), 1u << 5);
    /* taken 4 cycles after it shows: a few instructions of the loop first */
    before = x(&f, 5);
    for (i = 0; i < 8 && x(&f, 32) < VEC; i++)
        esk_mach_run(&f.m, 0xffffff, 1);
    CHECK(x(&f, 32) >= VEC);
    CHECK(x(&f, 5) > before);
    esk_mach_run(&f.m, 0xffffff, 2);
    CHECK_EQ(x(&f, 22), 0x80000005u);       /* interrupt 5 */
    CHECK(x(&f, 23) >= BASE + 16u && x(&f, 23) <= BASE + 20u);
    /* a level is cleared at its source; MRET clears MPIE */
    esk_esp32c3_source(&f.m, 21, 0);
    CHECK_EQ(rd(&f, CPU_INT_EIP), 0u);
    esk_mach_run(&f.m, 0xffffff, 1);
    {
        u8 b[8] = { 0 };
        f.m.cpu->ops->reg_read(f.m.cpu, 66 + 0x300, b);
        CHECK_EQ(b[0] & 0x88u, 0x08u);      /* MIE back on, MPIE cleared */
    }
    c3_close(&f);
}

static void test_edge_interrupt(void)
{
    c3 f;
    if (!int_open(&f, 1u << 5))
        return;
    /* a pulse is latched */
    esk_esp32c3_source(&f.m, 21, 1);
    esk_esp32c3_source(&f.m, 21, 0);
    CHECK_EQ(rd(&f, CPU_INT_EIP), 1u << 5);
    /* an unclaimed, enabled edge is not cleared by the toggle */
    wr(&f, CPU_INT_CLEAR, 1u << 5);
    wr(&f, CPU_INT_CLEAR, 0);
    CHECK_EQ(rd(&f, CPU_INT_EIP), 1u << 5);
    esk_mach_run(&f.m, 0xffffff, 8);
    CHECK_EQ(x(&f, 22), 0x80000005u);
    /* claimed now: the toggle clears it */
    wr(&f, CPU_INT_CLEAR, 1u << 5);
    wr(&f, CPU_INT_CLEAR, 0);
    CHECK_EQ(rd(&f, CPU_INT_EIP), 0u);
    c3_close(&f);
}

static void test_threshold(void)
{
    c3 f;
    if (!int_open(&f, 0))
        return;
    wr(&f, CPU_INT_THRESH, 4);
    esk_esp32c3_source(&f.m, 21, 1);
    CHECK_EQ(rd(&f, CPU_INT_EIP), 0u); /* priority 3 below the threshold */
    esk_mach_run(&f.m, 0xffffff, 20);
    CHECK_EQ(x(&f, 22), 0u);
    wr(&f, CPU_INT_THRESH, 3);
    CHECK_EQ(rd(&f, CPU_INT_EIP), 1u << 5);
    wr(&f, CPU_INT_PRI(5), 0);         /* priority 0 never interrupts */
    CHECK_EQ(rd(&f, CPU_INT_EIP), 0u);
    wr(&f, CPU_INT_PRI(5), 3);
    esk_mach_run(&f.m, 0xffffff, 10);
    CHECK_EQ(x(&f, 22), 0x80000005u);
    c3_close(&f);
}

static void test_wfi_wakes(void)
{
    /* WFI with MIE clear sleeps until an interrupt is pending, then runs on */
    const u32 prog[] = { MTVEC_AT_VEC, 0x10500073u, addi(5, 0, 7), JAL_SELF };
    c3 f;
    if (!c3_open(&f, prog, sizeof prog / sizeof prog[0], vec5(), NVEC5))
        return;
    wr(&f, UART0_MAP, 5);
    wr(&f, CPU_INT_PRI(5), 3);
    wr(&f, CPU_INT_ENABLE, 1u << 5);
    esk_mach_run(&f.m, 0xffffff, 4);
    CHECK(f.m.asleep);
    esk_esp32c3_source(&f.m, 21, 1);
    CHECK(!f.m.asleep);
    esk_mach_run(&f.m, 0xffffff, 1);
    CHECK_EQ(x(&f, 5), 7u);
    c3_close(&f);
}

TEST_MAIN_BEGIN
RUN(test_csrs);
RUN(test_no_atomics);
RUN(test_counter);
RUN(test_trigger);
RUN(test_pmp_no_priority);
RUN(test_level_interrupt);
RUN(test_edge_interrupt);
RUN(test_threshold);
RUN(test_wfi_wakes);
TEST_MAIN_END
