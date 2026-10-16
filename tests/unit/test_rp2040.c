/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* test_rp2040.c -- the RP2040's models (SPEC-009) on the machine the
 * catalog's description builds: the boot through boot2, the bus rules, the
 * resets, the clocks, the timer on the watchdog's tick, the SIO's divider
 * and the UART's bit time. The program's accesses are made from here, as a
 * core's would be. */
#include "harness.h"
#include "mcu/build.h"
#include "mcu/rp2040/rp2040.h"
#include "util/json.h"

typedef struct rp {
    esk_alloc a;
    esk_json *mcu, *regs;
    struct esk_mach m;
    char tx[64];
    size_t ntx;
    u32 tx_bits;
    int no_boot2;
    int held;
} rp;

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

static void r_uart(void *ctx, const char *unit, int c, u32 bit_cycles)
{
    rp *f = (rp *)ctx;
    (void)unit;
    f->tx_bits = bit_cycles;
    if (f->ntx + 1 < sizeof f->tx)
        f->tx[f->ntx++] = (char)c;
}

static void r_diag(void *ctx, int sev, const char *code, const char *msg)
{
    rp *f = (rp *)ctx;
    (void)sev;
    (void)msg;
    if (!strcmp(code, "mcu.no_boot2"))
        f->no_boot2++;
    if (!strcmp(code, "mcu.held_in_reset"))
        f->held++;
}

/* boot2 as the example's (examples/pico-blink-serial/firmware/boot2.c): the
 * vector table at 0x10000100 into VTOR, MSP, and its reset handler */
static const u32 boot2[6] = { 0x49044803u, 0xc8036008u, 0x8808f380u, 0xbf004708u, 0x10000100u, 0xe000ed08u };

static void put(rp *f, u32 a, u32 v) { CHECK_EQ(esk_bus_debug_write(&f->m.bus, a, 4, v), 0); }

/* an RP2040 out of reset with boot2 (its CRC `crc`), a vector table and a
 * branch to itself at 0x10000200 in flash */
static int rp_open(rp *f, int good_crc)
{
    char why[200];
    es_status st;
    u8 b[252];
    u32 i;
    memset(f, 0, sizeof *f);
    esk_alloc_init(&f->a, NULL);
    f->mcu = load_json(&f->a, "catalog/mcus/raspberrypi/rp2040/mcu.json");
    f->regs = load_json(&f->a, "catalog/mcus/raspberrypi/rp2040/registers.json");
    CHECK(f->mcu != NULL && f->regs != NULL);
    esk_mach_init(&f->m, &f->a);
    f->m.host.ctx = f;
    f->m.host.uart_tx = r_uart;
    f->m.host.diag = r_diag;
    st = esk_mach_build(&f->m, f->mcu, f->regs, 12000000u, why, sizeof why);
    if (st != ES_OK) {
        FAIL(why);
        return 0;
    }
    memset(b, 0, sizeof b);
    for (i = 0; i < 6; i++) {
        put(f, 0x10000000u + 4u * i, boot2[i]);
        b[4 * i] = (u8)boot2[i];
        b[4 * i + 1] = (u8)(boot2[i] >> 8);
        b[4 * i + 2] = (u8)(boot2[i] >> 16);
        b[4 * i + 3] = (u8)(boot2[i] >> 24);
    }
    for (i = 24; i < 252; i += 4)
        put(f, 0x10000000u + i, 0);
    put(f, 0x100000fcu, esk_rp2040_crc32(b, 252) ^ (good_crc ? 0u : 1u));
    put(f, 0x10000100u, 0x20001000u); /* the stack */
    put(f, 0x10000104u, 0x10000201u); /* the reset handler */
    put(f, 0x10000200u, 0xbf00e7feu); /* b . */
    esk_mach_reset(&f->m);
    return 1;
}

static void rp_close(rp *f)
{
    esk_mach_free(&f->m);
    esk_json_free(&f->a, f->mcu);
    esk_json_free(&f->a, f->regs);
    CHECK_EQ(f->a.live_bytes, 0);
}

static u32 rd(rp *f, u32 a)
{
    u32 v = 0;
    CHECK_EQ(esk_bus_read(&f->m.bus, a, 4, &v), 0);
    return v;
}

static void wr(rp *f, u32 a, u32 v) { CHECK_EQ(esk_bus_write(&f->m.bus, a, 4, v), 0); }

static u32 reg(rp *f, int n)
{
    u8 b[8] = { 0 };
    f->m.cpu->ops->reg_read(f->m.cpu, n, b);
    return (u32)b[0] | (u32)b[1] << 8 | (u32)b[2] << 16 | (u32)b[3] << 24;
}

/* the SDK's clock set-up: the crystal, PLL_SYS at 125 MHz, clk_peri */
static void clocks_125(rp *f)
{
    wr(f, 0x4000c000u + 0x3000u, 1u << 12); /* PLL_SYS out of reset */
    wr(f, 0x40024000u, 0xaa0u);
    wr(f, 0x4002400cu, 47);
    wr(f, 0x40024000u + 0x2000u, 0xfabu << 12);
    esk_mach_run(&f->m, 20000, 0); /* the crystal starts */
    CHECK(rd(f, 0x40024004u) & (1u << 31));
    wr(f, 0x40008030u, 2); /* clk_ref: the crystal */
    wr(f, 0x40028000u, 1);
    wr(f, 0x40028008u, 125);
    wr(f, 0x40028004u + 0x3000u, (1u << 0) | (1u << 5));
    CHECK(rd(f, 0x40028000u) & (1u << 31)); /* LOCK */
    wr(f, 0x4002800cu, (6u << 16) | (2u << 12));
    wr(f, 0x40028004u + 0x3000u, 1u << 3);
    wr(f, 0x4000803cu, 1); /* clk_sys: PLL_SYS */
    wr(f, 0x40008048u, 1u << 11);
}

/* ---- the tests ------------------------------------------------------------------ */

static void test_boot(void)
{
    rp f;
    if (!rp_open(&f, 1))
        return;
    CHECK_STR(esk_json_get_str(f.mcu, "name", ""), "RP2040");
    CHECK_EQ(f.no_boot2, 0);
    CHECK_EQ(rd(&f, 0x20041f00u), boot2[0]); /* boot2's copy in SRAM */
    CHECK_EQ(rd(&f, 0x10u) & 0xffffffu, 0x01754du); /* the ROM's 'M', 'u', 1 */
    esk_mach_run(&f.m, 1000, 20);
    CHECK_EQ(reg(&f, 15), 0x10000200u);     /* the program's reset handler */
    CHECK_EQ(reg(&f, 13), 0x20001000u);     /* its stack */
    CHECK_EQ(rd(&f, 0xe000ed08u), 0x10000100u); /* VTOR */
    /* the bootrom leaves PADS_BANK0 and the QSPI bank out of reset */
    CHECK_EQ(rd(&f, 0x4000c008u) & ((1u << 8) | (1u << 6) | (1u << 9)), (1u << 8) | (1u << 6) | (1u << 9));
    CHECK_EQ(rd(&f, 0x40000000u), 0x20002927u); /* SYSINFO.CHIP_ID: a B2 (held in reset, its reset value) */
    rp_close(&f);
}

static void test_no_boot2(void)
{
    rp f;
    if (!rp_open(&f, 0))
        return;
    CHECK_EQ(f.no_boot2, 1);
    esk_mach_run(&f.m, 1000, 20);
    CHECK(f.m.asleep);        /* the USB boot's wait */
    CHECK(reg(&f, 15) < 0x4000u); /* in the ROM */
    rp_close(&f);
}

static void test_bus_rules(void)
{
    rp f;
    const u32 s0 = 0x4005800cu; /* WATCHDOG.SCRATCH0 */
    if (!rp_open(&f, 1))
        return;
    wr(&f, s0, 0xf0);
    wr(&f, s0 + 0x1000u, 0xff); /* XOR */
    CHECK_EQ(rd(&f, s0), 0x0fu);
    wr(&f, s0 + 0x2000u, 0x100); /* set */
    CHECK_EQ(rd(&f, s0), 0x10fu);
    wr(&f, s0 + 0x3000u, 0x0f); /* clear */
    CHECK_EQ(rd(&f, s0), 0x100u);
    CHECK_EQ(rd(&f, s0 + 0x2000u), 0x100u); /* a read through an alias reads the register */
    CHECK_EQ(esk_bus_write(&f.m.bus, s0 + 4u, 1, 0xab), 0); /* a byte to SCRATCH1: every lane */
    CHECK_EQ(rd(&f, s0 + 4u), 0xababababu);
    rp_close(&f);
}

static void test_resets(void)
{
    rp f;
    if (!rp_open(&f, 1))
        return;
    wr(&f, 0x40034024u, 67); /* UART0.UARTIBRD while UART0 is held */
    CHECK_EQ(rd(&f, 0x40034024u), 0u);
    CHECK_EQ(f.held, 1);
    wr(&f, 0x4000c000u + 0x3000u, 1u << 22);
    CHECK(rd(&f, 0x4000c008u) & (1u << 22)); /* RESET_DONE.UART0 */
    wr(&f, 0x40034024u, 67);
    CHECK_EQ(rd(&f, 0x40034024u), 67u);
    wr(&f, 0x4000c000u + 0x2000u, 1u << 22); /* into reset again: its registers reset */
    wr(&f, 0x4000c000u + 0x3000u, 1u << 22);
    CHECK_EQ(rd(&f, 0x40034024u), 0u);
    rp_close(&f);
}

static void test_clocks(void)
{
    rp f;
    if (!rp_open(&f, 1))
        return;
    CHECK_EQ(esk_mach_core_hz(&f.m), RP_ROSC_HZ);
    clocks_125(&f);
    CHECK_EQ(esk_mach_core_hz(&f.m), 125000000u);
    CHECK_EQ(rd(&f, 0x40008044u), 2u); /* CLK_SYS_SELECTED: the aux source */
    wr(&f, 0x40008094u, 9);            /* FC0 on clk_sys */
    CHECK(rd(&f, 0x40008098u) & (1u << 4));
    CHECK_EQ(rd(&f, 0x4000809cu) >> 5, 125000u);
    rp_close(&f);
}

static void test_timer_alarm(void)
{
    rp f;
    if (!rp_open(&f, 1))
        return;
    clocks_125(&f);
    wr(&f, 0x4000c000u + 0x3000u, 1u << 21); /* TIMER out of reset */
    wr(&f, 0x4005802cu, (1u << 9) | 12);     /* the tick: 1 us of the crystal */
    wr(&f, 0x40054038u, 1);                  /* INTE.ALARM_0 */
    wr(&f, 0x40054010u, rd(&f, 0x40054028u) + 1000u);
    CHECK_EQ(rd(&f, 0x40054020u), 1u);       /* ARMED */
    esk_mach_run(&f.m, 124000, 0);           /* 992 us */
    CHECK_EQ(rd(&f, 0x40054034u), 0u);
    esk_mach_run(&f.m, 2000, 0);
    CHECK_EQ(rd(&f, 0x40054034u), 1u);       /* INTR.ALARM_0 */
    CHECK_EQ(rd(&f, 0x40054020u), 0u);       /* disarmed */
    CHECK(rd(&f, 0x40054028u) >= 1000u && rd(&f, 0x40054028u) <= 1008u);
    wr(&f, 0x40054034u, 1);                  /* cleared by a one */
    CHECK_EQ(rd(&f, 0x40054034u), 0u);
    rp_close(&f);
}

static void test_divider(void)
{
    rp f;
    if (!rp_open(&f, 1))
        return;
    wr(&f, 0xd0000060u, 100);
    wr(&f, 0xd0000064u, 7);
    CHECK_EQ(rd(&f, 0xd0000078u) & 1u, 0u); /* not READY yet */
    esk_mach_run(&f.m, 20, 0);
    CHECK_EQ(rd(&f, 0xd0000078u), 3u);      /* READY, DIRTY */
    CHECK_EQ(rd(&f, 0xd0000074u), 2u);
    CHECK_EQ(rd(&f, 0xd0000070u), 14u);
    CHECK_EQ(rd(&f, 0xd0000078u), 1u);      /* reading the quotient clears DIRTY */
    wr(&f, 0xd0000068u, (u32)-7);
    wr(&f, 0xd000006cu, 2);
    CHECK_EQ(rd(&f, 0xd0000070u), (u32)-3);
    CHECK_EQ(rd(&f, 0xd0000074u), (u32)-1);
    rp_close(&f);
}

static void test_uart_bit_time(void)
{
    rp f;
    if (!rp_open(&f, 1))
        return;
    clocks_125(&f);
    wr(&f, 0x4000c000u + 0x3000u, 1u << 22);
    wr(&f, 0x40034024u, 67); /* 115200 baud at 125 MHz */
    wr(&f, 0x40034028u, 52);
    wr(&f, 0x4003402cu, 0x70); /* 8 bits, FIFOs */
    wr(&f, 0x40034030u, 0x301);
    wr(&f, 0x40034000u, 'P');
    CHECK_EQ(f.ntx, 1u);
    CHECK_EQ(f.tx[0], 'P');
    CHECK_EQ(f.tx_bits, 1085u); /* 16 x 67.8125 cycles of clk_peri = clk_sys */
    CHECK(rd(&f, 0x40034018u) & (1u << 3)); /* BUSY while the frame is on the line */
    esk_mach_run(&f.m, 10900, 0);
    CHECK_EQ(rd(&f, 0x40034018u) & (1u << 3), 0u);
    rp_close(&f);
}

TEST_MAIN_BEGIN
RUN(test_boot);
RUN(test_no_boot2);
RUN(test_bus_rules);
RUN(test_resets);
RUN(test_clocks);
RUN(test_timer_alarm);
RUN(test_divider);
RUN(test_uart_bit_time);
TEST_MAIN_END
