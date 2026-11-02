/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* test_rp2040.c -- the RP2040's models (SPEC-009) on the machine the
 * catalog's description builds: the boot through boot2, the bus rules, the
 * resets, the clocks, the timer on the watchdog's tick, the SIO's divider,
 * the UART's bit time and core 1; its serial wire's multi-drop debug ports
 * driven bit by bit as a probe drives them. The program's accesses are made
 * from here, as a core's would be. */
#include "harness.h"
#include "mcu/build.h"
#include "mcu/rp2040/rp2040.h"
#include "mcu/swd.h"
#include "util/num.h"
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

/* an I2C slave's SDA, as the circuit has it: released (1), or pulled low by
 * a slave that ACKs everything and reads as 0 */
static int sda_low;

static int i2c_pin(void *ctx, int port, int bit, int *level)
{
    (void)ctx;
    if (port * 16 + bit == 20 && sda_low) {
        *level = 0;
        return 1;
    }
    return 0; /* the pad's pull */
}

static void test_spi_i2c(void)
{
    rp f;
    int i, o = -1;
    if (!rp_open(&f, 1))
        return;
    f.m.host.pin_in = i2c_pin;
    sda_low = 0;
    clocks_125(&f);
    wr(&f, 0x4000c000u + 0x3000u, (1u << 3) | (1u << 5) | (1u << 8) | (1u << 16)); /* I2C0, IO, PADS, SPI0 */
    for (i = 4; i <= 7; i++)
        wr(&f, 0x40014004u + 8u * (u32)i, 1); /* FUNCSEL SPI */

    /* SPI0 in loopback: 125 MHz / (10 * 5) = 2.5 MHz, 25 cycles a half bit */
    wr(&f, 0x4003c010u, 10);              /* SSPCPSR */
    wr(&f, 0x4003c000u, 7u | 4u << 8);    /* SSPCR0: 8 bits, SCR 4, mode 0 */
    wr(&f, 0x4003c004u, (1u << 1) | 1u);  /* SSPCR1: SSE, LBM */
    CHECK_EQ(rd(&f, 0x4003c00cu), 3u);    /* TFE, TNF */
    CHECK_EQ(rd(&f, 0x4003c018u), 8u);    /* TXRIS */
    CHECK(esk_rp2040_spi_out(&f.m, 0, RP_SPI_CSN, &o) && o == 1);
    wr(&f, 0x4003c008u, 0xa5u);
    wr(&f, 0x4003c008u, 0x3cu);
    CHECK(rd(&f, 0x4003c00cu) & (1u << 4)); /* BSY */
    CHECK(esk_rp2040_spi_out(&f.m, 0, RP_SPI_CSN, &o) && o == 0);
    esk_mach_run(&f.m, 390, 0);             /* half a bit and 7.3 bits of the first frame */
    CHECK_EQ(rd(&f, 0x4003c00cu) & (1u << 2), 0u);
    esk_mach_run(&f.m, 500, 0);             /* the gap, and the second */
    CHECK_EQ(rd(&f, 0x4003c00cu), (1u << 0) | (1u << 1) | (1u << 2)); /* TFE, TNF, RNE */
    CHECK_EQ(rd(&f, 0x4003c008u), 0xa5u);
    CHECK_EQ(rd(&f, 0x4003c008u), 0x3cu);
    CHECK_EQ(rd(&f, 0x4003c00cu), 3u);
    CHECK(esk_rp2040_spi_out(&f.m, 0, RP_SPI_CSN, &o) && o == 1);
    CHECK(esk_rp2040_spi_out(&f.m, 0, RP_SPI_SCK, &o) && o == 0);
    CHECK(rd(&f, 0x40014000u + 8u * 6u) & (1u << 13)); /* GPIO6 OETOPAD: SCK driven */
    /* nine frames into an eight-entry receive FIFO: an overrun */
    wr(&f, 0x4003c014u, 1u << 0); /* RORIM */
    for (i = 0; i < 9; i++)
        wr(&f, 0x4003c008u, (u32)i);
    CHECK_EQ(rd(&f, 0x4003c00cu) & (1u << 1), 0u); /* TNF: full */
    esk_mach_run(&f.m, 9 * 460, 0);
    CHECK_EQ(rd(&f, 0x4003c018u), (1u << 0) | (1u << 2) | (1u << 3)); /* RORRIS, RXRIS, TXRIS */
    CHECK_EQ(rd(&f, 0x4003c01cu), 1u);                                /* RORMIS */
    CHECK(rd(&f, 0x4003c00cu) & (1u << 3));                           /* RFF */
    for (i = 0; i < 8; i++)
        CHECK_EQ(rd(&f, 0x4003c008u), (u32)i);
    wr(&f, 0x4003c020u, 1); /* RORIC */
    CHECK_EQ(rd(&f, 0x4003c018u), 8u);
    /* 16 bits, SPO 1, SPH 1: SCK idles high */
    wr(&f, 0x4003c000u, 15u | 4u << 8 | 3u << 6);
    CHECK(esk_rp2040_spi_out(&f.m, 0, RP_SPI_SCK, &o) && o == 1);
    wr(&f, 0x4003c008u, 0xbeefu);
    esk_mach_run(&f.m, 1000, 0);
    CHECK_EQ(rd(&f, 0x4003c008u), 0xbeefu);

    /* I2C0 on GPIO20 (SDA) and 21 (SCL), the pads' pull-ups */
    for (i = 20; i <= 21; i++) {
        wr(&f, 0x4001c004u + 4u * (u32)i, (1u << 6) | (1u << 3)); /* IE, PUE */
        wr(&f, 0x40014004u + 8u * (u32)i, 3);                     /* FUNCSEL I2C */
    }
    wr(&f, 0x40044000u, 0x65u | 1u << 8); /* IC_CON: master, fast, TX_EMPTY_CTRL */
    wr(&f, 0x4004401cu, 20);              /* FS_SCL_HCNT */
    wr(&f, 0x40044020u, 30);              /* FS_SCL_LCNT */
    wr(&f, 0x4004407cu, 3);               /* SDA_HOLD */
    wr(&f, 0x40044004u, 0x50);            /* IC_TAR */
    wr(&f, 0x4004406cu, 1);               /* IC_ENABLE */
    wr(&f, 0x40044004u, 0x51);            /* ignored: enabled */
    CHECK_EQ(rd(&f, 0x40044004u), 0x50u);
    CHECK_EQ(rd(&f, 0x4004409cu), 1u);
    CHECK(rd(&f, 0x40044034u) & (1u << 4)); /* TX_EMPTY */
    /* nobody answers: the address is NACKed */
    wr(&f, 0x40044010u, 0x12u | 1u << 9);
    CHECK(rd(&f, 0x40044070u) & (1u << 5)); /* MST_ACTIVITY */
    CHECK_EQ(rd(&f, 0x40044034u) & (1u << 4), 0u);
    esk_mach_run(&f.m, 3000, 0);
    CHECK_EQ(rd(&f, 0x40044034u) & ((1u << 4) | (1u << 6) | (1u << 9) | (1u << 10)),
             (1u << 4) | (1u << 6) | (1u << 9) | (1u << 10)); /* TX_EMPTY, TX_ABRT, STOP_DET, START_DET */
    CHECK_EQ(rd(&f, 0x40044080u), 1u);                        /* ABRT_7B_ADDR_NOACK */
    CHECK_EQ(rd(&f, 0x40044070u) & (1u << 5), 0u);
    CHECK(esk_rp2040_i2c_out(&f.m, 0, RP_I2C_SCL, &o) && o == 1);
    CHECK(esk_rp2040_i2c_out(&f.m, 0, RP_I2C_SDA, &o) && o == 1);
    CHECK_EQ(rd(&f, 0x40044054u), 1u); /* IC_CLR_TX_ABRT */
    CHECK_EQ(rd(&f, 0x40044080u), 0u);
    CHECK_EQ(rd(&f, 0x40044054u), 0u);
    CHECK_EQ(rd(&f, 0x40044060u), 1u); /* IC_CLR_STOP_DET */
    CHECK_EQ(rd(&f, 0x40044034u) & (1u << 9), 0u);

    /* a slave that ACKs: a write without STOP holds the bus, SCL low */
    sda_low = 1;
    wr(&f, 0x40044010u, 0x00u);
    esk_mach_run(&f.m, 3000, 0);
    CHECK_EQ(rd(&f, 0x40044080u), 0u);
    CHECK(rd(&f, 0x40044034u) & (1u << 4));  /* TX_EMPTY: the command is done */
    CHECK(rd(&f, 0x40044070u) & (1u << 5));  /* MST_ACTIVITY: the bus is held */
    CHECK(esk_rp2040_i2c_out(&f.m, 0, RP_I2C_SCL, &o) && o == 0);
    /* the SDK changes IC_TAR between them: disabled and enabled again at
     * once, the bus stays held */
    wr(&f, 0x4004406cu, 0);
    wr(&f, 0x40044004u, 0x51);
    wr(&f, 0x4004406cu, 1);
    esk_mach_run(&f.m, 1000, 0);
    CHECK(esk_rp2040_i2c_out(&f.m, 0, RP_I2C_SCL, &o) && o == 0);
    CHECK_EQ(rd(&f, 0x40044034u) & (1u << 9), 0u); /* no STOP */
    /* a read with RESTART and STOP: a repeated START, one byte, a STOP */
    rd(&f, 0x40044064u); /* IC_CLR_START_DET */
    wr(&f, 0x40044010u, 1u << 8 | 1u << 9 | 1u << 10);
    esk_mach_run(&f.m, 3000, 0);
    CHECK_EQ(rd(&f, 0x40044078u), 1u); /* IC_RXFLR */
    CHECK_EQ(rd(&f, 0x40044010u), 0u); /* the byte, all 0 */
    CHECK_EQ(rd(&f, 0x40044034u) & ((1u << 9) | (1u << 10)), (1u << 9) | (1u << 10));
    CHECK_EQ(rd(&f, 0x40044070u) & (1u << 5), 0u);
    /* the receive FIFO empty: RX_UNDER */
    CHECK_EQ(rd(&f, 0x40044010u), 0u);
    CHECK(rd(&f, 0x40044034u) & 1u);
    CHECK_EQ(rd(&f, 0x40044040u), 1u); /* IC_CLR_INTR */
    CHECK_EQ(rd(&f, 0x40044034u) & 0x7ebu, 0u);
    /* disabled while it holds the bus, and left so: a STOP */
    wr(&f, 0x40044010u, 0x00u);
    esk_mach_run(&f.m, 3000, 0);
    CHECK(esk_rp2040_i2c_out(&f.m, 0, RP_I2C_SCL, &o) && o == 0);
    wr(&f, 0x4004406cu, 0);
    esk_mach_run(&f.m, 3000, 0);
    CHECK(esk_rp2040_i2c_out(&f.m, 0, RP_I2C_SCL, &o) && o == 1);
    CHECK(rd(&f, 0x40044034u) & (1u << 9));
    CHECK_EQ(rd(&f, 0x4004409cu), 0u);
    sda_low = 0;
    rp_close(&f);
}



/* ---- the bus fabric's arbiters and BUSCTRL (busctrl.c) ---- */

#define BUSCTRL 0x40030000u

/* master `m` (2: the DMA's read port, 3: its write port) reads `a` at cycle `t` */
static void fabric_access(rp *f, int m, u32 a, u64 t)
{
    u32 v = 0;
    f->m.bus.master = m;
    f->m.bus.master_t = t;
    CHECK_EQ(esk_bus_read(&f->m.bus, a, 4, &v), 0);
    f->m.bus.master = -1;
}

/* two masters on one port in one cycle: one waits, counted as contested;
 * the striped banks and the SIO apart; BUS_PRIORITY's high level wins over
 * round robin; the counters' selection, clearing and BUS_PRIORITY_ACK */
static void test_busctrl(void)
{
    rp f;
    if (!rp_open(&f, 1))
        return;
    wr(&f, 0x4000c000u + 0x3000u, 1u << 1);               /* RESETS: BUSCTRL out of reset */
    CHECK_EQ(rd(&f, BUSCTRL + 0x0cu), 0x1fu);            /* PERFSEL0's reset */
    CHECK_EQ(rd(&f, BUSCTRL + 0x04u), 0u);               /* BUS_PRIORITY_ACK */
    wr(&f, BUSCTRL + 0x0cu, 6u);                          /* PERFCTR0: sram4_contested */
    wr(&f, BUSCTRL + 0x14u, 7u);                          /* PERFCTR1: sram4 */
    wr(&f, BUSCTRL + 0x1cu, 15u);                         /* PERFCTR2: sram0 */
    wr(&f, BUSCTRL + 0x24u, 14u);                         /* PERFCTR3: sram0_contested */
    fabric_access(&f, 2, 0x20040000u, 1000);
    fabric_access(&f, 3, 0x20040010u, 1000);             /* SRAM4 in the same cycle */
    CHECK_EQ(rd(&f, BUSCTRL + 0x08u), 1u);               /* one contested */
    CHECK_EQ(rd(&f, BUSCTRL + 0x10u), 2u);               /* two accesses */
    fabric_access(&f, 2, 0x20040000u, 1001);             /* the write took 1001: again */
    CHECK_EQ(rd(&f, BUSCTRL + 0x08u), 2u);
    fabric_access(&f, 2, 0x20040000u, 1005);
    CHECK_EQ(rd(&f, BUSCTRL + 0x08u), 2u);               /* alone */
    /* the striped banks: SRAM0 and SRAM1 are two ports */
    fabric_access(&f, 2, 0x20000000u, 2000);
    fabric_access(&f, 3, 0x20000004u, 2000);
    CHECK_EQ(rd(&f, BUSCTRL + 0x18u), 1u);
    CHECK_EQ(rd(&f, BUSCTRL + 0x20u), 0u);
    /* the SIO is not on the crossbar */
    fabric_access(&f, 2, 0xd0000000u, 3000);
    fabric_access(&f, 3, 0xd0000000u, 3000);
    CHECK_EQ(rd(&f, BUSCTRL + 0x20u), 0u);
    /* the DMA's write port at high priority wins whatever the round robin */
    wr(&f, BUSCTRL, 1u << 12);
    CHECK_EQ(rd(&f, BUSCTRL + 0x04u), 1u);               /* acknowledged */
    wr(&f, BUSCTRL + 0x08u, 0u);                          /* a write clears */
    CHECK_EQ(rd(&f, BUSCTRL + 0x08u), 0u);
    fabric_access(&f, 3, 0x20040000u, 4000);
    fabric_access(&f, 2, 0x20040000u, 4000);             /* low: waits */
    fabric_access(&f, 2, 0x20040000u, 4010);
    fabric_access(&f, 3, 0x20040000u, 4010);             /* high: the read port set back */
    CHECK_EQ(rd(&f, BUSCTRL + 0x08u), 2u);
    rp_close(&f);
}

/* ---- the serial wire: a probe's bits on the RP2040's SWDIO --------------------- */

typedef struct wire {
    rp *f;
    esk_swd *w;
    int rescue, rescues;
} wire;

static int w_mem(void *ctx, int core, u32 addr, int bytes, int write, u32 *v)
{
    wire *x = (wire *)ctx;
    int r;
    x->f->m.bus.dap = 1 + core;
    r = write ? esk_bus_write(&x->f->m.bus, addr, bytes, *v) : esk_bus_read(&x->f->m.bus, addr, bytes, v);
    x->f->m.bus.dap = 0;
    return r;
}

static int w_instance(void *ctx, int port)
{
    wire *x = (wire *)ctx;
    u32 pc = rd(x->f, 0x40004008u); /* SYSCFG.PROC_CONFIG */
    return port == 0 ? (int)((pc >> 24) & 15u) : port == 1 ? (int)(pc >> 28) : -1;
}

static void w_rescue(void *ctx, int hold)
{
    wire *x = (wire *)ctx;
    x->rescue = hold;
    x->rescues += hold;
}

static void sw_out(esk_swd *w, u64 v, int n)
{
    int i;
    for (i = 0; i < n; i++)
        (void)esk_swd_clock(w, 1, (int)((v >> i) & 1u));
}

static u32 sw_in(esk_swd *w, int n)
{
    u32 v = 0;
    int i;
    for (i = 0; i < n; i++)
        v |= (u32)esk_swd_clock(w, 0, 1) << i;
    return v;
}

static int par(u32 v)
{
    int p = 0;
    while (v) {
        p ^= (int)(v & 1u);
        v >>= 1;
    }
    return p;
}

/* a packet as CMSIS-DAP's SWD_Transfer makes it, with 8 idle cycles after:
 * the ACK (7: nobody answered), a read's data and parity check */
static u32 sw_xfer(esk_swd *w, int ap, int rnw, int a, u32 *data, int bad_parity)
{
    u32 req = 1u | (u32)ap << 1 | (u32)rnw << 2 | (u32)((a >> 2) & 3) << 3, ack, d;
    req |= (u32)par(req & 0x1Eu) << 5 | 1u << 7;
    sw_out(w, req, 8);
    (void)sw_in(w, 1);
    ack = sw_in(w, 3);
    if (ack == 1u && rnw) {
        d = sw_in(w, 32);
        CHECK_EQ((int)sw_in(w, 1), par(d));
        (void)sw_in(w, 1);
        *data = d;
    } else if (ack == 1u) {
        (void)sw_in(w, 1);
        sw_out(w, *data, 32);
        sw_out(w, (u64)(par(*data) ^ bad_parity), 1);
    } else {
        (void)sw_in(w, 1);
    }
    sw_out(w, 0, 8);
    return ack;
}

static void sw_line_reset(esk_swd *w)
{
    sw_out(w, 0x00FFFFFFFFFFFFFFull, 64); /* 56 high, 8 idle */
}

/* TARGETSEL: no ACK from anyone (CMSIS-DAP's SWD_Sequence) */
static void sw_targetsel(esk_swd *w, u32 id)
{
    sw_out(w, 0x99u, 8);
    (void)sw_in(w, 5);
    sw_out(w, id, 32);
    sw_out(w, (u64)par(id), 1);
    sw_out(w, 0, 8);
}

static u32 sw_dp_read(esk_swd *w, int a)
{
    u32 v = 0xDEADu;
    CHECK_EQ(sw_xfer(w, 0, 1, a, &v, 0), 1u);
    return v;
}

static void sw_write(esk_swd *w, int ap, int a, u32 v)
{
    CHECK_EQ(sw_xfer(w, ap, 0, a, &v, 0), 1u);
}

/* an AP read as CMSIS-DAP makes it: posted, then RDBUFF */
static u32 sw_ap_read(esk_swd *w, int a)
{
    u32 v = 0;
    CHECK_EQ(sw_xfer(w, 1, 1, a, &v, 0), 1u);
    return sw_dp_read(w, 0xC);
}

static void test_swd(void)
{
    /* OpenOCD's sequences (src/jtag/swd.h) */
    static const u8 to_dormant[9] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xbc, 0xe3 };
    static const u8 dormant_to_swd[28] = { 0xff, 0x92, 0xf3, 0x09, 0x62, 0x95, 0x2d, 0x85, 0x86, 0xe9,
                                           0xaf, 0xdd, 0xe3, 0xa2, 0x0e, 0xbc, 0x19, 0xa0, 0xf1, 0xff,
                                           0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00 };
    rp f;
    wire x;
    esk_swd_port_cfg cfg[3];
    esk_swd_target t;
    const esk_json *sw, *ports;
    uint64_t v64;
    u32 v;
    int i;
    if (!rp_open(&f, 1))
        return;
    memset(&x, 0, sizeof x);
    x.f = &f;
    /* the ports as the catalog's description gives them */
    sw = esk_json_get(esk_json_get(f.mcu, "core"), "swd");
    ports = esk_json_get(sw, "ports");
    CHECK_EQ((int)esk_json_len(ports), 3);
    memset(cfg, 0, sizeof cfg);
    for (i = 0; i < 3; i++) {
        const esk_json *pj = esk_json_at(ports, (size_t)i);
        cfg[i].version = (int)esk_json_get_int(sw, "version", 1);
        CHECK_EQ(esk_json_to_u64(esk_json_get(sw, "dpidr"), &v64), 0);
        cfg[i].dpidr = (u32)v64;
        CHECK_EQ(esk_json_to_u64(esk_json_get(sw, "targetid"), &v64), 0);
        cfg[i].targetid = (u32)v64;
        cfg[i].multidrop = esk_json_get_bool(sw, "multidrop", 0);
        cfg[i].ap_idr = 0x04770031u;
        cfg[i].ap_base = 0xE00FF003u;
        cfg[i].core = (int)esk_json_get_int(pj, "core", 0);
        cfg[i].instance = (int)esk_json_get_int(pj, "instance", 0);
        cfg[i].rescue = esk_json_get_bool(pj, "rescue", 0);
        if (cfg[i].rescue)
            cfg[i].core = -1;
    }
    t.ctx = &x;
    t.mem = w_mem;
    t.instance = w_instance;
    t.rescue = w_rescue;
    t.present = NULL;
    x.w = esk_swd_create(&f.a, cfg, 3, &t);
    CHECK(x.w != NULL);

    /* after power-on nothing answers before a line reset */
    v = 0;
    CHECK_EQ(sw_xfer(x.w, 0, 1, 0, &v, 0), 7u);
    /* dormant: a line reset alone wakes nobody */
    for (i = 0; i < 9; i++)
        sw_out(x.w, to_dormant[i], 8);
    sw_line_reset(x.w);
    CHECK_EQ(sw_xfer(x.w, 0, 1, 0, &v, 0), 7u);
    /* the selection alert and SWD's activation code, then a line reset */
    for (i = 0; i < 28; i++)
        sw_out(x.w, dormant_to_swd[i], 8);
    /* core 0's port selected: DPIDR, TARGETID, DLPIDR */
    sw_targetsel(x.w, 0x01002927u);
    CHECK_EQ(sw_dp_read(x.w, 0), 0x0BC12477u);
    sw_write(x.w, 0, 8, 2u); /* SELECT: DPBANKSEL 2 */
    CHECK_EQ(sw_dp_read(x.w, 4), 0x01002927u);
    sw_write(x.w, 0, 8, 3u);
    CHECK_EQ(sw_dp_read(x.w, 4), 0x00000001u); /* TINSTANCE 0, protocol 1 */
    sw_write(x.w, 0, 8, 0u);
    /* an AP access before the debug domain is powered up: STICKYERR, then FAULT */
    v = 0;
    CHECK_EQ(sw_xfer(x.w, 1, 1, 0, &v, 0), 1u);
    CHECK_EQ(sw_dp_read(x.w, 4) & 0x20u, 0x20u);
    CHECK_EQ(sw_xfer(x.w, 1, 1, 0, &v, 0), 4u);
    sw_write(x.w, 0, 0, 0x1Eu); /* ABORT: the sticky flags cleared */
    sw_write(x.w, 0, 4, 0x50000000u);
    CHECK_EQ(sw_dp_read(x.w, 4), 0xF0000000u); /* both acknowledged */
    /* the MEM-AP */
    sw_write(x.w, 0, 8, 0xF0u);
    CHECK_EQ(sw_ap_read(x.w, 0xC), 0x04770031u); /* IDR */
    CHECK_EQ(sw_ap_read(x.w, 0x8), 0xE00FF003u); /* BASE */
    sw_write(x.w, 0, 8, 0x00u);
    sw_write(x.w, 1, 0, 0x23000022u);            /* 32 bits, packed */
    CHECK_EQ(sw_ap_read(x.w, 0) & 0x77u, 0x42u);  /* packed reads back off, DeviceEn */
    sw_write(x.w, 1, 0, 0x23000012u);            /* single increment */
    sw_write(x.w, 1, 4, 0x20000100u);
    sw_write(x.w, 1, 0xC, 0x12345678u);
    sw_write(x.w, 1, 0xC, 0xCAFEBABEu);
    CHECK_EQ(rd(&f, 0x20000100u), 0x12345678u);
    CHECK_EQ(rd(&f, 0x20000104u), 0xCAFEBABEu);
    CHECK_EQ(sw_ap_read(x.w, 4), 0x20000108u); /* TAR moved on */
    /* posted reads: each AP read answers the one before */
    sw_write(x.w, 1, 4, 0x20000100u);
    v = 0;
    CHECK_EQ(sw_xfer(x.w, 1, 1, 0xC, &v, 0), 1u);
    CHECK_EQ(sw_xfer(x.w, 1, 1, 0xC, &v, 0), 1u);
    CHECK_EQ(v, 0x12345678u);
    CHECK_EQ(sw_dp_read(x.w, 0xC), 0xCAFEBABEu);
    CHECK_EQ(sw_dp_read(x.w, 8), 0xCAFEBABEu); /* RESEND */
    /* bytes on their lanes, the increment wrapping within 1 KiB */
    sw_write(x.w, 1, 0, 0x23000010u);
    sw_write(x.w, 1, 4, 0x20000101u);
    sw_write(x.w, 1, 0xC, 0x0000AB00u);
    CHECK_EQ(rd(&f, 0x20000100u), 0x1234AB78u);
    sw_write(x.w, 1, 0, 0x23000012u);
    sw_write(x.w, 1, 4, 0x200003FCu);
    sw_write(x.w, 1, 0xC, 1u);
    CHECK_EQ(sw_ap_read(x.w, 4), 0x20000000u);
    /* BD0-3 */
    sw_write(x.w, 1, 4, 0x20000100u);
    sw_write(x.w, 0, 8, 0x10u);
    CHECK_EQ(sw_ap_read(x.w, 4), 0xCAFEBABEu);
    sw_write(x.w, 0, 8, 0x00u);
    /* core 0 halted through its own debug */
    sw_write(x.w, 1, 0, 0x23000002u);            /* no increment */
    sw_write(x.w, 1, 4, 0xE000EDF0u);
    sw_write(x.w, 1, 0xC, 0xA05F0003u);
    CHECK_EQ(sw_ap_read(x.w, 0xC) & 0x20003u, 0x20003u);
    /* a bus error: STICKYERR, FAULT, cleared by ABORT */
    sw_write(x.w, 1, 4, 0x30000000u);
    v = 0;
    CHECK_EQ(sw_xfer(x.w, 1, 1, 0xC, &v, 0), 1u);
    CHECK_EQ(sw_xfer(x.w, 0, 1, 0xC, &v, 0), 4u);
    CHECK_EQ(sw_dp_read(x.w, 4) & 0x20u, 0x20u);
    sw_write(x.w, 0, 0, 0x4u);
    CHECK_EQ(sw_dp_read(x.w, 4) & 0x20u, 0u);
    /* a write with a bad parity: WDATAERR, nothing written */
    v = 0x20000100u;
    CHECK_EQ(sw_xfer(x.w, 1, 0, 4, &v, 1), 1u);
    CHECK_EQ(sw_dp_read(x.w, 4) & 0x80u, 0x80u);
    sw_write(x.w, 0, 0, 0x8u);

    /* core 1's port: its own debug, not halted */
    sw_line_reset(x.w);
    sw_targetsel(x.w, 0x11002927u);
    CHECK_EQ(sw_dp_read(x.w, 0), 0x0BC12477u);
    sw_write(x.w, 0, 8, 3u);
    CHECK_EQ(sw_dp_read(x.w, 4), 0x10000001u);
    sw_write(x.w, 0, 8, 0u);
    sw_write(x.w, 0, 4, 0x50000000u);
    sw_write(x.w, 1, 0, 0x23000002u);
    sw_write(x.w, 1, 4, 0xE000EDF0u);
    CHECK_EQ(sw_ap_read(x.w, 0xC) & 0x20003u, 0u);
    /* every port answers a first packet that is not TARGETSEL (core 0's
     * and core 1's DPIDR are the same, so the wire reads it) */
    sw_line_reset(x.w);
    CHECK_EQ(sw_dp_read(x.w, 0), 0x0BC12477u);
    /* no port has instance 2: all locked out until the next line reset */
    sw_line_reset(x.w);
    sw_targetsel(x.w, 0x21002927u);
    CHECK_EQ(sw_xfer(x.w, 0, 1, 0, &v, 0), 7u);
    /* SYSCFG.PROC_CONFIG moves core 0's port to instance 3 (SYSCFG out of
     * reset first: in reset its registers keep their reset values) */
    wr(&f, 0x4000C000u + 0x3000u, 1u << 18);
    wr(&f, 0x40004008u, (rd(&f, 0x40004008u) & ~0x0F000000u) | 0x03000000u);
    sw_line_reset(x.w);
    sw_targetsel(x.w, 0x01002927u);
    CHECK_EQ(sw_xfer(x.w, 0, 1, 0, &v, 0), 7u);
    sw_line_reset(x.w);
    sw_targetsel(x.w, 0x31002927u);
    CHECK_EQ(sw_dp_read(x.w, 0), 0x0BC12477u);

    /* the Rescue DP: CDBGPWRUPREQ holds the rescue, no CSYSPWRUPACK */
    sw_line_reset(x.w);
    sw_targetsel(x.w, 0xF1002927u);
    CHECK_EQ(sw_dp_read(x.w, 0), 0x0BC12477u);
    sw_write(x.w, 0, 4, 0x50000000u);
    CHECK_EQ(x.rescue, 1);
    CHECK_EQ(sw_dp_read(x.w, 4) & 0xF0000000u, 0x70000000u);
    sw_write(x.w, 0, 4, 0u);
    CHECK_EQ(x.rescue, 0);
    CHECK_EQ(x.rescues, 1);
    esk_swd_destroy(&f.a, x.w);

    /* the rescue's reset: the bootrom halts in its dead loop, CHIP_RESET
     * says HAD_PSM_RESTART */
    f.m.reset_cause = ESK_RESET_RESCUE;
    esk_mach_reset(&f.m);
    CHECK_EQ(rd(&f, 0x40064008u) & 0x01110100u, 0x00100000u);
    for (i = 0; i < 20; i++)
        f.m.cpu->ops->step(f.m.cpu);
    CHECK((reg(&f, 15) & ~3u) == 0x28u); /* wfi; b dead */
    rp_close(&f);
}

TEST_MAIN_BEGIN
RUN(test_boot);
RUN(test_busctrl);
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
RUN(test_spi_i2c);
RUN(test_swd);
TEST_MAIN_END
