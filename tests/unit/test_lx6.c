/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* test_lx6.c -- the ESP32's Xtensa LX6 cores (mcu/xtensa/lx6.c, lx6fp.c)
 * on the machine the catalog's ESP32 description builds: programs
 * assembled by Espressif's assembler (lx6_programs.h, gen_lx6_programs.py)
 * run from IRAM, their results checked against the Xtensa ISA reference
 * worked by hand. The window spills and fills of the recursive program go
 * through the ESP32 ROM's own window vectors. */
#include "harness.h"
#include "lx6_programs.h"
#include "mcu/build.h"
#include "mcu/xtensa/lx6.h"
#include "util/json.h"

#define IRAM 0x40080000u
#define GR_PC 0
#define GR_WB 69
#define GR_WS 70
#define GR_PS 73
#define GR_A 105
#define J_SELF 0xffff06u

typedef struct t32 {
    esk_alloc a;
    esk_json *mcu, *regs;
    struct esk_mach m;
} t32;

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

static void put32(u8 *b, u32 v)
{
    b[0] = (u8)v;
    b[1] = (u8)(v >> 8);
    b[2] = (u8)(v >> 16);
    b[3] = (u8)(v >> 24);
}

static u32 reg(t32 *t, int n)
{
    u8 b[8] = { 0 };
    t->m.cpu->ops->reg_read(t->m.cpu, n, b);
    return (u32)b[0] | (u32)b[1] << 8 | (u32)b[2] << 16 | (u32)b[3] << 24;
}

static void set_reg(t32 *t, int n, u32 v)
{
    u8 b[4];
    put32(b, v);
    CHECK_EQ(t->m.cpu->ops->reg_write(t->m.cpu, n, b), 4);
}

static u32 a(t32 *t, int n) { return reg(t, GR_A + n); }

/* the ESP32 out of reset, `prog` in IRAM, the PRO_CPU at `entry` with
 * windowed calls on (PS.WOE), interrupts open, a stack in DRAM */
static int open32(t32 *t, const unsigned char *prog, size_t n, u32 entry)
{
    char why[200];
    es_status st;
    size_t i;
    memset(t, 0, sizeof *t);
    esk_alloc_init(&t->a, NULL);
    t->mcu = load_json(&t->a, "catalog/mcus/espressif/esp32/mcu.json");
    t->regs = load_json(&t->a, "catalog/mcus/espressif/esp32/registers.json");
    CHECK(t->mcu != NULL && t->regs != NULL);
    if (!t->mcu || !t->regs)
        return 0;
    esk_mach_init(&t->m, &t->a);
    st = esk_mach_build(&t->m, t->mcu, t->regs, 40000000u, why, sizeof why);
    if (st != ES_OK) {
        FAIL(why);
        return 0;
    }
    {
        size_t rlen = 0;
        unsigned char *rom = slurp("catalog/mcus/espressif/esp32/rom.bin", &rlen);
        CHECK(rom != NULL);
        if (!rom)
            return 0;
        CHECK_EQ(esk_mach_factory(&t->m, 0x40000000u, rom, rlen), ES_OK);
        free(rom);
    }
    esk_mach_reset(&t->m);
    CHECK(esk_lx6_is(t->m.cpu));
    CHECK_STR(t->m.cpu->ops->name, "xtensa");
    CHECK_EQ(esk_lx6_cores(&t->m), 2);
    CHECK_EQ(reg(t, GR_PC), 0x40000400u); /* the reset vector */
    for (i = 0; i < n; i++)
        CHECK_EQ(esk_bus_debug_write(&t->m.bus, IRAM + (u32)i, 1, prog[i]), 0);
    set_reg(t, GR_PC, entry);
    set_reg(t, GR_PS, 0x00040000u); /* WOE, INTLEVEL 0 */
    set_reg(t, GR_WB, 0);
    set_reg(t, GR_WS, 1);
    set_reg(t, GR_A + 1, 0x3FFB8000u);
    return 1;
}

static void close32(t32 *t)
{
    esk_mach_free(&t->m);
    esk_json_free(&t->a, t->mcu);
    esk_json_free(&t->a, t->regs);
    CHECK_EQ(t->a.live_bytes, 0);
}

/* runs until the program's final `j .`: 1 when it got there */
static int run_to_end(t32 *t, u64 max_insns)
{
    u32 w = 0;
    esk_mach_run(&t->m, 0xffffffffu, max_insns);
    if (esk_bus_debug_read(&t->m.bus, reg(t, GR_PC), 4, &w))
        return 0;
    return (w & 0xffffffu) == J_SELF;
}

#define OPEN(t, name, NAME) open32(&(t), prog_##name, sizeof prog_##name, PROG_##NAME##_ENTRY)

static void test_alu(void)
{
    t32 t;
    if (!OPEN(t, alu, ALU))
        return;
    CHECK(run_to_end(&t, 100));
    CHECK_EQ(a(&t, 4), 4u);
    CHECK_EQ(a(&t, 5), 25u);
    CHECK_EQ(a(&t, 6), 59u);
    CHECK_EQ(a(&t, 7), 3u);
    CHECK_EQ(a(&t, 8), 3u);
    CHECK_EQ(a(&t, 9), (u32)-3);
    CHECK_EQ(a(&t, 10), 0xfffffffdu);
    CHECK_EQ(a(&t, 11), (u32)-3);
    CHECK_EQ(a(&t, 12), 127u);
    CHECK_EQ(a(&t, 13), 29u);
    CHECK_EQ(a(&t, 14), 29u);
    CHECK_EQ(a(&t, 15), 0xffu);
    close32(&t);
}

static void test_shifts(void)
{
    t32 t;
    if (!OPEN(t, shifts, SHIFTS))
        return;
    CHECK(run_to_end(&t, 100));
    CHECK_EQ(a(&t, 3), 0x12300000u);
    CHECK_EQ(a(&t, 4), 0x00123000u);
    CHECK_EQ(a(&t, 6), (u32)-16);
    CHECK_EQ(a(&t, 7), 0x918u);
    CHECK_EQ(a(&t, 9), 0x12300u);
    CHECK_EQ(a(&t, 10), 0xffffffffu);
    CHECK_EQ(a(&t, 12), 0x21000001u);
    CHECK_EQ(a(&t, 13), 0u);
    CHECK_EQ(a(&t, 14), 8u);
    close32(&t);
}

static void test_muldiv(void)
{
    t32 t;
    if (!OPEN(t, muldiv, MULDIV))
        return;
    CHECK(run_to_end(&t, 100));
    CHECK_EQ(a(&t, 4), (u32)-21);
    CHECK_EQ(a(&t, 5), 2u);
    CHECK_EQ(a(&t, 6), 0xffffffffu);
    CHECK_EQ(a(&t, 7), (u32)-2);
    CHECK_EQ(a(&t, 8), (u32)-1);
    CHECK_EQ(a(&t, 9), 0x55555553u);
    CHECK_EQ(a(&t, 10), 0u);
    CHECK_EQ(a(&t, 11), 0x2ffebu);
    CHECK_EQ(a(&t, 12), (u32)-21);
    close32(&t);
}

/* the zero-overhead loops, one left by a branch taken to its end */
static void test_loops(void)
{
    t32 t;
    if (!OPEN(t, loops, LOOPS))
        return;
    CHECK(run_to_end(&t, 200));
    CHECK_EQ(a(&t, 2), 5u);
    CHECK_EQ(a(&t, 4), 0u);  /* LOOPNEZ of 0: skipped */
    CHECK_EQ(a(&t, 6), 10u); /* left by BEQZ to LEND, not looped again */
    CHECK_EQ(a(&t, 7), 0u);
    CHECK_EQ(a(&t, 9), 0u);  /* LOOPGTZ of -2: skipped */
    close32(&t);
}

static void test_branches(void)
{
    t32 t;
    if (!OPEN(t, branches, BRANCHES))
        return;
    CHECK(run_to_end(&t, 100));
    CHECK_EQ(a(&t, 2), 15u);
    close32(&t);
}

static void test_memory(void)
{
    t32 t;
    if (!OPEN(t, memory, MEMORY))
        return;
    CHECK(run_to_end(&t, 100));
    CHECK_EQ(a(&t, 4), 0xffff8081u);
    CHECK_EQ(a(&t, 5), 0x8081u);
    CHECK_EQ(a(&t, 6), 0x80u);
    CHECK_EQ(a(&t, 8), 0x22334400u);
    CHECK_EQ(a(&t, 11), 0x55u); /* S32C1I stored, and returned the old word */
    CHECK_EQ(a(&t, 12), 0x66u);
    CHECK_EQ(a(&t, 13), 0x66u); /* compare failed: nothing stored */
    CHECK_EQ(a(&t, 14), 0xcafef00du);
    close32(&t);
}

/* fib(12) by CALL8/ENTRY/RETW: twelve frames deep, spilled and filled by
 * the ROM's WindowOverflow8 and WindowUnderflow8 */
static void test_windows(void)
{
    t32 t;
    if (!OPEN(t, windows, WINDOWS))
        return;
    CHECK(run_to_end(&t, 200000));
    CHECK_EQ(a(&t, 15), 144u);
    CHECK_EQ(reg(&t, GR_WB), 0u);
    close32(&t);
}

static void test_syscall(void)
{
    t32 t;
    if (!OPEN(t, syscall, SYSCALL))
        return;
    CHECK(run_to_end(&t, 100));
    CHECK_EQ(a(&t, 4), 1u); /* EXCCAUSE: SyscallCause */
    CHECK_EQ(a(&t, 3), 1u); /* back after it */
    CHECK_EQ(reg(&t, GR_PS) & 0x10u, 0u); /* RFE cleared EXCM */
    close32(&t);
}

static void test_timer(void)
{
    t32 t;
    if (!OPEN(t, timer, TIMER))
        return;
    CHECK(run_to_end(&t, 2000));
    CHECK_EQ(a(&t, 6), 1u);
    CHECK_EQ(a(&t, 7), 4u);             /* Level1InterruptCause */
    CHECK_EQ(a(&t, 8) & 0x40u, 0x40u);  /* INTERRUPT: timer 0 */
    close32(&t);
}

static void test_float(void)
{
    t32 t;
    if (!OPEN(t, float, FLOAT))
        return;
    CHECK(run_to_end(&t, 100));
    CHECK_EQ(a(&t, 5), 13u);
    CHECK_EQ(a(&t, 6), 2u);
    CHECK_EQ(a(&t, 7), 5u);
    CHECK_EQ(a(&t, 8), 0x40f00000u);
    close32(&t);
}

static void test_mac16(void)
{
    t32 t;
    if (!OPEN(t, mac16, MAC16))
        return;
    CHECK(run_to_end(&t, 100));
    CHECK_EQ(a(&t, 4), 14u);
    CHECK_EQ(a(&t, 5), 0u);
    CHECK_EQ(a(&t, 6), 15u);
    close32(&t);
}

/* the APP_CPU's ROM keeps its own data: released, it runs the ROM's start,
 * which sets g_ticks_per_us_app (0x3FFE40F0) and leaves the PRO_CPU's
 * g_ticks_per_us_pro (0x3FFE01E0) as the PRO_CPU set it */
static void test_app_cpu_rom_data(void)
{
    static const unsigned char spin[3] = { 0x06, 0xff, 0xff }; /* j . */
    t32 t;
    u32 pro = 0, app = 0;
    if (!open32(&t, spin, sizeof spin, IRAM))
        return;
    CHECK_EQ(esk_bus_debug_write(&t.m.bus, 0x3FFE01E0u, 4, 80u), 0);
    CHECK_EQ(esk_bus_debug_write(&t.m.bus, 0x3FFE40F0u, 4, 0xdeadbeefu), 0);
    /* DPORT: the APP_CPU's clock on, out of reset (APPCPU_CTRL_B, _A) */
    CHECK_EQ(esk_bus_write(&t.m.bus, 0x3FF00030u, 4, 1u), 0);
    CHECK_EQ(esk_bus_write(&t.m.bus, 0x3FF0002Cu, 4, 0u), 0);
    esk_mach_run(&t.m, 0xffffffffu, 20000);
    CHECK_EQ(esk_bus_debug_read(&t.m.bus, 0x3FFE01E0u, 4, &pro), 0);
    CHECK_EQ(esk_bus_debug_read(&t.m.bus, 0x3FFE40F0u, 4, &app), 0);
    CHECK_EQ(pro, 80u);
    CHECK_EQ(app, 13u); /* the ROM's initial value, unpacked by the APP_CPU */
    close32(&t);
}

TEST_MAIN_BEGIN
RUN(test_alu);
RUN(test_shifts);
RUN(test_muldiv);
RUN(test_loops);
RUN(test_branches);
RUN(test_memory);
RUN(test_windows);
RUN(test_syscall);
RUN(test_timer);
RUN(test_float);
RUN(test_mac16);
RUN(test_app_cpu_rom_data);
TEST_MAIN_END
