/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* test_rp2040.c -- the RP2040's models (SPEC-009) on the machine the
 * catalog's description builds: the boot through boot2, the bus rules, the
 * resets, the clocks, the timer on the watchdog's tick, the SIO's divider,
 * the UART's bit time and core 1. The program's accesses are made from here, as a
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

/* Core 1: waiting in its bootrom out of reset, having sent core 0 a 0;
 * launched by the SDK's sequence through the FIFO, each word echoed; its
 * CPUID; a WFE that core 0's SEV ends; powered off and on by the PSM. The
 * programs as llvm-mc assembles them:
 *   core 1 (0x20002000): ldr r0, =SIO; ldr r1, [r0]; ldr r2, =0x20003000;
 *                        str r1, [r2]; wfe; movs r1, #7; str r1, [r2, #4]; b .
 *   core 0 (0x10000200): ldr r0, =0x20003000; 1: ldr r1, [r0]; cmp r1, #1;
 *                        bne 1b; sev; b . */
static void test_core1(void)
{
    static const u32 c1[6] = { 0x68014803u, 0x60114a03u, 0x2107bf20u, 0xe7fe6051u, 0xd0000000u, 0x20003000u };
    static const u32 c0[4] = { 0x68014802u, 0xd1fc2901u, 0xe7febf40u, 0x20003000u };
    const u32 seq[6] = { 0, 0, 1, 0x10000100u, 0x20003800u, 0x20002001u };
    rp f;
    u32 i;
    if (!rp_open(&f, 1))
        return;
    CHECK_EQ(esk_cortexm_cores(&f.m), 2);
    CHECK(esk_cortexm_core_booting(&f.m, 1));
    CHECK_EQ(rd(&f, 0xd0000000u), 0u);           /* CPUID: core 0 */
    CHECK_EQ(rd(&f, 0xd0000050u) & 3u, 3u);      /* FIFO_ST: VLD (the bootrom's 0), RDY */
    CHECK_EQ(rd(&f, 0xd0000058u), 0u);
    CHECK_EQ(rd(&f, 0xd0000050u) & 1u, 0u);
    for (i = 0; i < 6; i++)
        put(&f, 0x20002000u + 4u * i, c1[i]);
    for (i = 0; i < 4; i++)
        put(&f, 0x10000200u + 4u * i, c0[i]);
    for (i = 0; i < 6; i++) {
        wr(&f, 0xd0000054u, seq[i]);
        CHECK_EQ(rd(&f, 0xd0000058u), seq[i]); /* the echo */
    }
    CHECK(esk_cortexm_core_running(&f.m, 1));
    esk_mach_run(&f.m, 4000, 0);
    CHECK_EQ(rd(&f, 0x20003000u), 1u); /* core 1's CPUID */
    CHECK_EQ(rd(&f, 0x20003004u), 7u); /* past its WFE: core 0's SEV */
    CHECK(f.m.asleep);                 /* both at their branch to themselves */
    /* the PSM: core 1 off, then on again in its bootrom */
    (void)rd(&f, 0xd0000058u);
    wr(&f, 0x40010004u + 0x2000u, 1u << 16);
    CHECK(!esk_cortexm_core_running(&f.m, 1) && !esk_cortexm_core_booting(&f.m, 1));
    wr(&f, 0x40010004u + 0x3000u, 1u << 16);
    CHECK(esk_cortexm_core_booting(&f.m, 1));
    CHECK_EQ(rd(&f, 0xd0000050u) & 1u, 1u); /* its 0 */
    rp_close(&f);
}

/* ---- the bootrom's functions (rom.c) ---------------------------------------------- */

static void set_reg(rp *f, int n, u32 v)
{
    u8 b[4] = { (u8)v, (u8)(v >> 8), (u8)(v >> 16), (u8)(v >> 24) };
    CHECK_EQ(f->m.cpu->ops->reg_write(f->m.cpu, n, b), 4);
}

static u16 rom16(rp *f, u32 a) { return (u16)(rd(f, a & ~3u) >> (8 * (a & 2u))); }

/* a call of the ROM function at `fn` with r0-r3, returning to the program's
 * loop (test_rom's, which never sleeps); *cycles gets the cycles it took */
static u32 rom_fn(rp *f, u32 fn, u32 a, u32 b, u32 c, u32 d, u32 *hi, u64 *cycles)
{
    u64 c0;
    int i;
    set_reg(f, 0, a);
    set_reg(f, 1, b);
    set_reg(f, 2, c);
    set_reg(f, 3, d);
    set_reg(f, 14, 0x10000201u);
    set_reg(f, 15, fn & ~1u);
    c0 = f->m.cycles;
    for (i = 0; i < 10 && reg(f, 15) != 0x10000200u; i++)
        esk_mach_run(&f->m, 100000000u, 1);
    CHECK_EQ(reg(f, 15), 0x10000200u);
    if (hi)
        *hi = reg(f, 1);
    if (cycles)
        *cycles = f->m.cycles - c0;
    return reg(f, 0);
}

static u32 code(char a, char b) { return (u32)(u8)a | (u32)(u8)b << 8; }

static u32 rom_lookup(rp *f, u32 table, u32 c)
{
    return rom_fn(f, rom16(f, 0x18), table, c, 0, 0, 0, 0);
}

static u32 fbits(float x)
{
    u32 v;
    memcpy(&v, &x, 4);
    return v;
}

static void test_rom(void)
{
    rp f;
    u32 funcs, data, fn, sf, sd, hi, i, hz;
    u64 cyc;
    if (!rp_open(&f, 1))
        return;
    put(&f, 0x10000200u, 0xe7fdbf00u); /* nop; b to the nop: a loop that does not sleep */
    esk_mach_run(&f.m, 1000, 20);      /* into the program */
    CHECK_EQ(rd(&f, 0x10u) >> 24, 3u); /* version 3, B2 */
    funcs = rom16(&f, 0x14);
    data = rom16(&f, 0x16);
    CHECK_EQ(rom_lookup(&f, funcs, code('Z', 'Z')), 0u); /* no such function */
    fn = rom_lookup(&f, funcs, code('P', '3'));
    CHECK(fn != 0 && (fn & 1u));
    CHECK_EQ(rom_fn(&f, fn, 0xf0f0f0f1u, 0, 0, 0, 0, 0), 17u);
    CHECK_EQ(rom_fn(&f, rom_lookup(&f, funcs, code('L', '3')), 0, 0, 0, 0, 0, 0), 32u);
    CHECK_EQ(rom_fn(&f, rom_lookup(&f, funcs, code('T', '3')), 0x80u, 0, 0, 0, 0, 0), 7u);
    CHECK_EQ(rom_fn(&f, rom_lookup(&f, funcs, code('R', '3')), 1u, 0, 0, 0, 0, 0), 0x80000000u);
    /* memset and memcpy, through the bus */
    CHECK_EQ(rom_fn(&f, rom_lookup(&f, funcs, code('M', 'S')), 0x20004000u, 0x5a, 7, 0, 0, 0), 0x20004000u);
    CHECK_EQ(rd(&f, 0x20004004u) & 0xffffffu, 0x5a5a5au);
    CHECK_EQ(rom_fn(&f, rom_lookup(&f, funcs, code('M', 'C')), 0x20004100u, 0x20004000u, 8, 0, 0, 0),
             0x20004100u);
    CHECK_EQ(rd(&f, 0x20004100u), 0x5a5a5a5au);
    /* the flash: a sector erased, a page programmed (bits cleared only) */
    put(&f, 0x10010000u, 0x12345678u);
    rom_fn(&f, rom_lookup(&f, funcs, code('R', 'E')), 0x10000u, 0x1000u, 0x10000u, 0xd8u, 0, &cyc);
    CHECK_EQ(rd(&f, 0x10010000u), 0xffffffffu);
    hz = f.m.core_hz ? f.m.core_hz(&f.m) : f.m.reset_hz; /* the ROSC's, out of the boot */
    CHECK(cyc > (u64)hz * 40 / 1000); /* a sector's 45 ms, not a block's 150 */
    CHECK(cyc < (u64)hz * 100 / 1000);
    put(&f, 0x20004200u, 0x0f0f00ffu);
    rom_fn(&f, rom_lookup(&f, funcs, code('R', 'P')), 0x10000u, 0x20004200u, 4, 0, 0, 0);
    CHECK_EQ(rd(&f, 0x10010000u), 0x0f0f00ffu);
    put(&f, 0x20004200u, 0xffff0f0fu);
    rom_fn(&f, rom_lookup(&f, funcs, code('R', 'P')), 0x10000u, 0x20004200u, 4, 0, 0, 0);
    CHECK_EQ(rd(&f, 0x10010000u), 0x0f0f000fu);
    /* the float table: version 3's 32 entries */
    sf = rom_lookup(&f, data, code('S', 'F'));
    sd = rom_lookup(&f, data, code('S', 'D'));
    CHECK(sf != 0 && sd != 0);
    CHECK_EQ(rd(&f, (sf - 2) & ~3u) >> (8 * ((sf - 2) & 3u)) & 0xffu, 32u);
    CHECK_EQ(rom_fn(&f, rd(&f, sf + 0x00), fbits(1.5f), fbits(2.25f), 0, 0, 0, &cyc), fbits(3.75f));
    CHECK(cyc >= 50u);
    CHECK_EQ(rom_fn(&f, rd(&f, sf + 0x0c), fbits(1.0f), fbits(3.0f), 0, 0, 0, 0), 0x3eaaaaabu);
    CHECK_EQ(rom_fn(&f, rd(&f, sf + 0x18), fbits(2.0f), 0, 0, 0, 0, 0), 0x3fb504f3u);
    CHECK_EQ(rom_fn(&f, rd(&f, sf + 0x00), 1u, 0, 0, 0, 0, 0), 0u);              /* a denormal is 0 */
    CHECK_EQ(rom_fn(&f, rd(&f, sf + 0x08), fbits(1e-30f), fbits(1e-10f), 0, 0, 0, 0), 0u); /* flushed */
    CHECK_EQ(rom_fn(&f, rd(&f, sf + 0x04), 0x7f800000u, 0x7f800000u, 0, 0, 0, 0), 0x7f800000u); /* no NaN */
    CHECK_EQ(rom_fn(&f, rd(&f, sf + 0x1c), fbits(-1.5f), 0, 0, 0, 0, 0), (u32)-2); /* towards -inf */
    CHECK_EQ(rom_fn(&f, rd(&f, sf + 0x1c), fbits(3e9f), 0, 0, 0, 0, 0), 0x7fffffffu); /* saturated */
    CHECK_EQ(rom_fn(&f, rd(&f, sf + 0x24), fbits(-1.0f), 0, 0, 0, 0, 0), 0u);
    CHECK_EQ(rom_fn(&f, rd(&f, sf + 0x2c), 16777217u, 0, 0, 0, 0, 0), fbits(16777216.0f)); /* to even */
    CHECK_EQ(rom_fn(&f, rd(&f, sf + 0x30), 3u, 1u, 0, 0, 0, 0), fbits(1.5f));  /* fix2float */
    CHECK_EQ(rom_fn(&f, rd(&f, sf + 0x20), fbits(1.5f), 4u, 0, 0, 0, 0), 24u); /* float2fix */
    CHECK_EQ(rom_fn(&f, rd(&f, sf + 0x7c), fbits(1.5f), 0, 0, 0, &hi, 0), 0u);
    CHECK_EQ(hi, 0x3ff80000u);
    /* the double table */
    CHECK_EQ(rom_fn(&f, rd(&f, sd + 0x00), 0, 0x3ff00000u, 0, 0x40000000u, &hi, 0), 0u);
    CHECK_EQ(hi, 0x40080000u); /* 1 + 2 */
    CHECK_EQ(rom_fn(&f, rd(&f, sd + 0x18), 0, 0x40000000u, 0, 0, &hi, 0), 0x667f3bcdu);
    CHECK_EQ(hi, 0x3ff6a09eu); /* sqrt 2 */
    CHECK_EQ(rom_fn(&f, rd(&f, sd + 0x7c), 0x9999999au, 0x3fb99999u, 0, 0, 0, 0), 0x3dcccccdu); /* 0.1 */
    CHECK_EQ(rom_fn(&f, rd(&f, sd + 0x1c), 0, 0xc0040000u, 0, 0, 0, 0), (u32)-3); /* -2.5 to -3 */
    /* every entry of both tables is a function in the ROM */
    for (i = 0; i < 32; i++) {
        CHECK((rd(&f, sf + 4u * i) & 1u) && rd(&f, sf + 4u * i) < 0x4000u);
        CHECK((rd(&f, sd + 4u * i) & 1u) && rd(&f, sd + 4u * i) < 0x4000u);
    }
    rp_close(&f);
}

static int v_pin(void *ctx, int port, int bit, double *v)
{
    (void)ctx;
    if (port * 16 + bit == 26) {
        *v = 1.65;
        return 1;
    }
    if (port * 16 + bit == 28) {
        *v = 1.0;
        return 1;
    }
    return 0;
}

static void test_pwm_adc(void)
{
    rp f;
    u32 ctr;
    if (!rp_open(&f, 1))
        return;
    f.m.host.pin_volts = v_pin;
    clocks_125(&f);
    wr(&f, 0x4000c000u + 0x3000u, (1u << 0) | (1u << 5) | (1u << 8) | (1u << 14)); /* ADC, IO, PADS, PWM */
    /* slice 0: 125 MHz / 125 = 1 count a microsecond, TOP 999, A 250, B 750 */
    wr(&f, 0x40050004u, 125u << 4);
    wr(&f, 0x40050010u, 999);
    wr(&f, 0x4005000cu, 250u | 750u << 16);
    wr(&f, 0x400500a8u, 1);  /* INTE.CH0 */
    wr(&f, 0x40050000u, 1);  /* CSR.EN */
    CHECK_EQ(rd(&f, 0x400500a0u), 1u); /* EN mirrors it */
    CHECK_EQ(esk_rp2040_pwm_out(&f.m, 16), 1);
    CHECK_EQ(esk_rp2040_pwm_out(&f.m, 17), 1);
    esk_mach_run(&f.m, 30000, 0); /* 240 counts */
    CHECK_EQ(esk_rp2040_pwm_out(&f.m, 16), 1);
    esk_mach_run(&f.m, 2000, 0); /* past 250 */
    CHECK_EQ(esk_rp2040_pwm_out(&f.m, 16), 0);
    CHECK_EQ(esk_rp2040_pwm_out(&f.m, 17), 1);
    esk_mach_run(&f.m, 92000, 0); /* 992 counts */
    CHECK_EQ(rd(&f, 0x400500a4u), 0u);
    esk_mach_run(&f.m, 2000, 0); /* the wrap at 1000 counts */
    CHECK_EQ(rd(&f, 0x400500a4u), 1u);
    CHECK_EQ(rd(&f, 0x400500b0u), 1u); /* INTS */
    ctr = rd(&f, 0x40050008u);
    CHECK(ctr <= 16u);
    CHECK_EQ(esk_rp2040_pwm_out(&f.m, 16), 1);
    wr(&f, 0x400500a4u, 1); /* INTR written 1: cleared */
    CHECK_EQ(rd(&f, 0x400500a4u), 0u);
    /* CC is double-buffered while the slice runs */
    wr(&f, 0x4005000cu, 900u | 750u << 16);
    esk_mach_run(&f.m, 40000, 0); /* ~ 336 counts: still A 250 */
    CHECK_EQ(esk_rp2040_pwm_out(&f.m, 16), 0);
    esk_mach_run(&f.m, 100000, 0); /* past the next wrap: A 900 */
    CHECK_EQ(esk_rp2040_pwm_out(&f.m, 16), 1);

    /* the ADC: clk_adc from the 12 MHz crystal, 96 of its cycles a
     * conversion (8 us, 1000 cycles of clk_sys); GPIO26 at 1.65 V is 2048 */
    wr(&f, 0x4004c000u, 1); /* CS.EN, clk_adc stopped */
    CHECK(rd(&f, 0x4004c000u) & (1u << 8)); /* READY */
    wr(&f, 0x4004c000u, 1u | 1u << 2); /* START_ONCE */
    esk_mach_run(&f.m, 5000, 0);
    CHECK_EQ(rd(&f, 0x4004c000u) & (1u << 8), 0u); /* never done without its clock */
    wr(&f, 0x4004c000u, 0);
    wr(&f, 0x40008060u, (3u << 5) | (1u << 11)); /* CLK_ADC: the crystal, enabled */
    wr(&f, 0x4004c000u, 1);
    wr(&f, 0x4004c000u, 1u | 1u << 2);
    CHECK_EQ(rd(&f, 0x4004c000u) & (1u << 8), 0u);
    esk_mach_run(&f.m, 990, 0);
    CHECK_EQ(rd(&f, 0x4004c000u) & (1u << 8), 0u);
    esk_mach_run(&f.m, 20, 0);
    CHECK(rd(&f, 0x4004c000u) & (1u << 8));
    CHECK_EQ(rd(&f, 0x4004c004u), 2048u);
    /* the temperature sensor: 0.706 V is 876 */
    wr(&f, 0x4004c000u, 1u | 1u << 1 | 4u << 12 | 1u << 2);
    esk_mach_run(&f.m, 1100, 0);
    CHECK_EQ(rd(&f, 0x4004c004u), 876u);
    /* round robin over AINSEL 0 and 2 into the FIFO, THRESH 2 */
    wr(&f, 0x4004c008u, 1u | 2u << 24); /* FCS: EN, THRESH 2 */
    wr(&f, 0x4004c018u, 1);             /* INTE.FIFO */
    wr(&f, 0x4004c000u, 1u | 0u << 12 | 0x5u << 16 | 1u << 3); /* START_MANY, RROBIN 0 and 2 */
    esk_mach_run(&f.m, 1500, 0); /* one conversion */
    CHECK_EQ(rd(&f, 0x4004c008u) >> 16 & 15u, 1u);
    CHECK_EQ(rd(&f, 0x4004c014u), 0u);
    esk_mach_run(&f.m, 1000, 0); /* two */
    CHECK_EQ(rd(&f, 0x4004c014u), 1u); /* INTR.FIFO at THRESH */
    wr(&f, 0x4004c000u, 1); /* stop */
    esk_mach_run(&f.m, 2000, 0);
    CHECK_EQ(rd(&f, 0x4004c00cu), 2048u); /* AINSEL 0: GPIO26 */
    CHECK_EQ(rd(&f, 0x4004c00cu), 1241u); /* AINSEL 2: GPIO28 at 1.0 V */
    esk_mach_run(&f.m, 10, 0);
    while (rd(&f, 0x4004c008u) >> 16 & 15u)
        (void)rd(&f, 0x4004c00cu);
    CHECK(rd(&f, 0x4004c008u) & (1u << 8)); /* EMPTY */
    (void)rd(&f, 0x4004c00cu);
    CHECK(rd(&f, 0x4004c008u) & (1u << 10)); /* UNDER */
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
RUN(test_core1);
RUN(test_rom);
RUN(test_pwm_adc);
TEST_MAIN_END
