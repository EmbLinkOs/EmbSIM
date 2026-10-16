/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* test_pic32.c -- the PIC32MX795F512L's models (SPEC-009 §6) on the machine
 * the catalog's description builds: the register file's CLR, SET and INV
 * registers, the oscillator from the configuration words, the EVIC with the
 * core in EIC mode, Timer1, the ports with the change notice, UART2 at its
 * baud rate; and the Explorer 16/32 example's image. */
#include "harness.h"
#include "mcu/build.h"
#include "mcu/mips/mips.h"
#include "mcu/pic32/pic32.h"
#include "util/json.h"

#define BOOT 0xbfc00000u

/* ---- the machine -------------------------------------------------------------- */

typedef struct px {
    esk_alloc a;
    esk_json *mcu, *regs;
    struct esk_mach m;
    char tx[256];
    size_t ntx;
    u32 tx_bits;
    char unit[16];
    int level[8][16]; /* the board's levels: -1 none */
    esk_mach_pin cfg[8][16];
} px;

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

static void p_uart(void *ctx, const char *unit, int c, u32 bit_cycles)
{
    px *t = (px *)ctx;
    t->tx_bits = bit_cycles;
    snprintf(t->unit, sizeof t->unit, "%s", unit);
    if (t->ntx + 1 < sizeof t->tx)
        t->tx[t->ntx++] = (char)c;
}

static void p_diag(void *ctx, int sev, const char *code, const char *msg)
{
    ES_UNUSED(ctx);
    ES_UNUSED(sev);
    ES_UNUSED(code);
    ES_UNUSED(msg);
}

static void p_cfg(void *ctx, int port, int bit, const esk_mach_pin *cfg)
{
    px *t = (px *)ctx;
    if (port >= 0 && port < 8 && bit >= 0 && bit < 16)
        t->cfg[port][bit] = *cfg;
}

static int p_in(void *ctx, int port, int bit, int *level)
{
    px *t = (px *)ctx;
    if (port < 0 || port >= 8 || bit < 0 || bit >= 16 || t->level[port][bit] < 0)
        return 0;
    *level = t->level[port][bit];
    return 1;
}

/* the part with the 8 MHz crystal, `prog` at the reset vector, the
 * configuration words `cfg` (DEVCFG3..DEVCFG0, or 0 for the erased ones) */
static int px_open(px *t, const u32 *prog, size_t n, const u32 *cfg)
{
    char why[200];
    es_status st;
    size_t i;
    memset(t, 0, sizeof *t);
    memset(t->level, -1, sizeof t->level);
    esk_alloc_init(&t->a, NULL);
    t->mcu = load_json(&t->a, "catalog/mcus/microchip/pic32mx795f512l/mcu.json");
    t->regs = load_json(&t->a, "catalog/mcus/microchip/pic32mx795f512l/registers.json");
    CHECK(t->mcu != NULL && t->regs != NULL);
    esk_mach_init(&t->m, &t->a);
    t->m.host.ctx = t;
    t->m.host.uart_tx = p_uart;
    t->m.host.diag = p_diag;
    t->m.host.pin_cfg = p_cfg;
    t->m.host.pin_in = p_in;
    st = esk_mach_build(&t->m, t->mcu, t->regs, 8000000u, why, sizeof why);
    if (st != ES_OK) {
        FAIL(why);
        return 0;
    }
    for (i = 0; i < n; i++)
        CHECK_EQ(esk_bus_debug_write(&t->m.bus, esk_mips_phys(BOOT) + (u32)(4 * i), 4, prog[i]), 0);
    for (i = 0; cfg && i < 4; i++)
        CHECK_EQ(esk_bus_debug_write(&t->m.bus, 0x1fc02ff0u + (u32)(4 * i), 4, cfg[i]), 0);
    esk_mach_reset(&t->m);
    return 1;
}

static void px_close(px *t)
{
    esk_mach_free(&t->m);
    esk_json_free(&t->a, t->mcu);
    esk_json_free(&t->a, t->regs);
    CHECK_EQ(t->a.live_bytes, 0);
}

static u32 reg(px *t, int n)
{
    u8 b[4] = { 0 };
    t->m.cpu->ops->reg_read(t->m.cpu, n, b);
    return (u32)b[0] | (u32)b[1] << 8 | (u32)b[2] << 16 | (u32)b[3] << 24;
}

static u32 rd(px *t, u32 a)
{
    u32 v = 0;
    CHECK_EQ(esk_bus_debug_read(&t->m.bus, a & 0x1fffffffu, 4, &v), 0);
    return v;
}

/* the core's own access, as a store or load of the program's */
static void wr(px *t, u32 a, u32 v) { CHECK_EQ(esk_bus_write(&t->m.bus, a & 0x1fffffffu, 4, v), 0); }
static u32 ld(px *t, u32 a)
{
    u32 v = 0;
    CHECK_EQ(esk_bus_read(&t->m.bus, a & 0x1fffffffu, 4, &v), 0);
    return v;
}

#define B_SELF 0x1000ffffu
#define NOP 0u
static const u32 idle[] = { B_SELF, NOP };

/* the example's configuration words: PRIPLL 8 MHz / 2 x 20 = 80 MHz, PBDIV 2 */
static const u32 cfg80[4] = { 0xffffffffu, 0xfff8ffd9u, 0xff7fdefbu, 0x7fffffffu };

#define LATA 0xbf886020u
#define TRISA 0xbf886000u
#define PORTD 0xbf8860d0u
#define CNCON 0xbf8861c0u
#define CNEN 0xbf8861d0u
#define CNPUE 0xbf8861e0u
#define OSCCON 0xbf80f000u
#define SYSKEY 0xbf80f230u
#define T1CON 0xbf800600u
#define PR1 0xbf800620u
#define IFS0 0xbf881030u
#define IFS1 0xbf881040u
#define IEC0 0xbf881060u
#define IEC1 0xbf881070u
#define IPC1 0xbf8810a0u
#define IPC8 0xbf881110u
#define U2MODE 0xbf806800u
#define U2STA 0xbf806810u
#define U2TXREG 0xbf806820u
#define U2BRG 0xbf806840u

static void test_clr_set_inv(void)
{
    px t;
    if (!px_open(&t, idle, 2, NULL))
        return;
    wr(&t, LATA, 0x00f0u);
    wr(&t, LATA + 8u, 0x000fu); /* SET */
    CHECK_EQ(ld(&t, LATA), 0x00ffu);
    wr(&t, LATA + 4u, 0x0011u); /* CLR */
    CHECK_EQ(ld(&t, LATA), 0x00eeu);
    wr(&t, LATA + 12u, 0x0003u); /* INV */
    CHECK_EQ(ld(&t, LATA), 0x00edu);
    CHECK_EQ(ld(&t, LATA + 8u), 0u); /* the alias reads 0 */
    wr(&t, LATA + 8u, 0x0100u);      /* RA8 is not on the part: stays 0 */
    CHECK_EQ(ld(&t, LATA), 0x00edu);
    /* TRISA all ones out of reset: inputs; cleared, the pins drive LATA */
    CHECK_EQ(ld(&t, TRISA) & 0xffu, 0xffu);
    CHECK_EQ(t.cfg[0][5].drive, -1);
    wr(&t, TRISA + 4u, 0x00ffu);
    CHECK_EQ(t.cfg[0][5].drive, 1);
    CHECK_EQ(t.cfg[0][4].drive, 0);
    px_close(&t);
}

static void test_oscillator(void)
{
    px t;
    u32 o;
    /* erased configuration words: FRCDIV, FRCDIV 1: 4 MHz, PBDIV 8 */
    if (!px_open(&t, idle, 2, NULL))
        return;
    CHECK_EQ(esk_mach_core_hz(&t.m), 4000000u);
    CHECK_EQ(esk_pic32_pb_div(&t.m), 8u);
    px_close(&t);
    if (!px_open(&t, idle, 2, cfg80))
        return;
    CHECK_EQ(esk_mach_core_hz(&t.m), 80000000u);
    CHECK_EQ(esk_pic32_pb_div(&t.m), 2u);
    o = ld(&t, OSCCON);
    CHECK_EQ(o >> 12 & 7u, 3u); /* COSC: POSC with the PLL */
    CHECK(o & 0x20u);           /* SLOCK */
    /* locked: a switch to FRC is ignored */
    wr(&t, OSCCON, (o & ~0x700u) | 1u);
    CHECK_EQ(esk_mach_core_hz(&t.m), 80000000u);
    /* unlocked: it switches at once */
    wr(&t, SYSKEY, 0xaa996655u);
    wr(&t, SYSKEY, 0x556699aau);
    CHECK_EQ(ld(&t, SYSKEY), 0u);
    t.m.yield = 0;
    wr(&t, OSCCON, (o & ~0x700u) | 1u);
    CHECK_EQ(esk_mach_core_hz(&t.m), 8000000u);
    CHECK(t.m.yield);
    CHECK_EQ(ld(&t, OSCCON) & 1u, 0u); /* OSWEN cleared */
    px_close(&t);
}

static void test_timer1(void)
{
    /* PR1 99 at 1:1, PBCLK = SYSCLK / 8: the flag 800 core cycles on */
    px t;
    if (!px_open(&t, idle, 2, NULL))
        return;
    wr(&t, PR1, 99u);
    wr(&t, IPC1, 3u << 2);
    wr(&t, IEC0, 1u << 4);
    wr(&t, T1CON, 1u << 15);
    esk_mach_run(&t.m, 780, 0);
    CHECK_EQ(ld(&t, IFS0) & 0x10u, 0u);
    esk_mach_run(&t.m, 40, 0);
    CHECK_EQ(ld(&t, IFS0) & 0x10u, 0x10u);
    CHECK(t.m.cycles >= 800u && t.m.cycles <= 830u);
    px_close(&t);
}

static void test_change_notice(void)
{
    /* RD6 (CN15): its pull-up, then a falling edge from the board */
    px t;
    if (!px_open(&t, idle, 2, NULL))
        return;
    wr(&t, CNPUE, 1u << 15);
    CHECK_EQ(ld(&t, PORTD) >> 6 & 1u, 1u); /* the pull-up */
    CHECK_EQ(t.cfg[3][6].pull, 1);
    wr(&t, CNEN, 1u << 15);
    wr(&t, CNCON, 1u << 15);
    (void)ld(&t, PORTD);
    CHECK_EQ(ld(&t, IFS1) & 1u, 0u);
    t.level[3][6] = 0;
    t.m.pins_changed(&t.m);
    CHECK_EQ(ld(&t, IFS1) & 1u, 1u); /* CNIF */
    CHECK_EQ(ld(&t, PORTD) >> 6 & 1u, 0u);
    px_close(&t);
}

static void test_uart2(void)
{
    /* 115200 baud from PBCLK 40 MHz: BRGH, U2BRG 86, 4 x 87 PBCLK a bit */
    px t;
    if (!px_open(&t, idle, 2, cfg80))
        return;
    wr(&t, U2BRG, 86u);
    wr(&t, U2MODE, 1u << 15 | 1u << 3);
    wr(&t, U2STA, 1u << 10);
    CHECK_EQ(t.cfg[5][5].drive, 1); /* RF5: U2TX, idle high */
    CHECK_EQ(t.cfg[5][5].af, 1);
    wr(&t, U2TXREG, 'Z');
    CHECK_EQ(t.ntx, 1u);
    CHECK_EQ(t.tx[0], 'Z');
    CHECK_STR(t.unit, "UART2");
    CHECK_EQ(t.tx_bits, 4u * 87u * 2u);
    CHECK_EQ(ld(&t, U2STA) & 0x100u, 0u); /* TRMT: sending */
    esk_mach_run(&t.m, 10u * 696u + 20u, 0);
    CHECK_EQ(ld(&t, U2STA) & 0x100u, 0x100u);
    /* the transmit interrupt is persistent while the FIFO has room */
    wr(&t, IPC8, 1u << 2);
    wr(&t, IEC1, 1u << 10);
    CHECK(ld(&t, IFS1) & (1u << 10));
    wr(&t, IFS1 + 4u, 1u << 10);
    CHECK(ld(&t, IFS1) & (1u << 10));
    px_close(&t);
}

/* ---- the example's image ----------------------------------------------------- */

static void test_example_image(void)
{
    size_t len = 0;
    unsigned char *img = slurp("examples/explorer16-pic32mx/firmware/blink-serial.elf", &len);
    char why[200];
    u32 entry = 0, lata = 0;
    int i, toggles = 0;
    px t;
    CHECK(img != NULL);
    if (!img || !px_open(&t, NULL, 0, NULL)) {
        free(img);
        return;
    }
    CHECK_EQ(esk_mach_load_image(&t.m, img, len, &entry, why, sizeof why), ES_OK);
    esk_mach_reset(&t.m);
    CHECK_EQ(esk_mach_core_hz(&t.m), 80000000u); /* from its configuration words */
    for (i = 0; i < 1200; i++) {                 /* 1.2 s */
        esk_mach_run(&t.m, 80000, 0);
        if ((rd(&t, LATA) & 1u) != lata) {
            lata ^= 1u;
            toggles++;
        }
    }
    CHECK_EQ(t.m.state, ESK_MACH_RUN);
    CHECK_EQ(reg(&t, 37) & 0xfff00000u, 0x9d000000u); /* waiting in main, in program flash */
    CHECK(strstr(t.tx, "explorer16: PIC32MX795F512L, SYSCLK 80000 kHz, PBCLK 40000 kHz, DEVID 0x54307053") != NULL);
    CHECK_EQ(toggles, 2); /* D3 at 0.5 s and 1 s */
    {
        struct esk_rx_port *rx = esk_mach_rx_find(&t.m, "UART2");
        CHECK(rx != NULL);
        if (rx) {
            rx->put(rx->ctx, 'o');
            rx->put(rx->ctx, 'k');
            t.ntx = 0;
            for (i = 0; i < 100; i++)
                esk_mach_run(&t.m, 1000, 0);
            CHECK_EQ(t.ntx, 2u);
            CHECK(!memcmp(t.tx, "OK", 2));
        }
    }
    px_close(&t);
    free(img);
}

TEST_MAIN_BEGIN
RUN(test_clr_set_inv);
RUN(test_oscillator);
RUN(test_timer1);
RUN(test_change_notice);
RUN(test_uart2);
RUN(test_example_image);
TEST_MAIN_END
