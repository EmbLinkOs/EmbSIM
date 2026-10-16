/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* test_nrf52.c -- the nRF52840's models (SPEC-009) on the machine the
 * catalog's description builds: FICR, CLOCK, the GPIO ports and GPIOTE, the
 * UARTE's EasyDMA frames, TIMER and RTC. The program's accesses are made
 * from here, as a core's would be. */
#include "harness.h"
#include "mcu/build.h"
#include "mcu/nrf52/nrf52.h"
#include "util/json.h"

typedef struct nf {
    esk_alloc a;
    esk_json *mcu, *regs;
    struct esk_mach m;
    char tx[64];
    size_t ntx;
    u32 tx_bits;
    int ext[48];     /* what the board drives on each pin: -1 nothing */
    int warnings;
} nf;

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
    nf *f = (nf *)ctx;
    (void)unit;
    f->tx_bits = bit_cycles;
    if (f->ntx + 1 < sizeof f->tx)
        f->tx[f->ntx++] = (char)c;
}

static void r_diag(void *ctx, int sev, const char *code, const char *msg)
{
    nf *f = (nf *)ctx;
    (void)code;
    (void)msg;
    if (sev >= ES_SEV_WARNING)
        f->warnings++;
}

static int r_pin_in(void *ctx, int port, int bit, int *level)
{
    nf *f = (nf *)ctx;
    int n = 16 * port + bit;
    if (n < 0 || n >= 48 || f->ext[n] < 0)
        return 0;
    *level = f->ext[n];
    return 1;
}

static int nf_open(nf *f)
{
    char why[200];
    es_status st;
    int i;
    memset(f, 0, sizeof *f);
    for (i = 0; i < 48; i++)
        f->ext[i] = -1;
    esk_alloc_init(&f->a, NULL);
    f->mcu = load_json(&f->a, "catalog/mcus/nordic/nrf52840/mcu.json");
    f->regs = load_json(&f->a, "catalog/mcus/nordic/nrf52840/registers.json");
    CHECK(f->mcu != NULL && f->regs != NULL);
    esk_mach_init(&f->m, &f->a);
    f->m.host.ctx = f;
    f->m.host.uart_tx = r_uart;
    f->m.host.diag = r_diag;
    f->m.host.pin_in = r_pin_in;
    st = esk_mach_build(&f->m, f->mcu, f->regs, 32000000u, why, sizeof why);
    if (st != ES_OK) {
        FAIL(why);
        return 0;
    }
    CHECK_EQ(esk_bus_debug_write(&f->m.bus, 0, 4, 0x20001000u), 0);
    CHECK_EQ(esk_bus_debug_write(&f->m.bus, 4, 4, 0x101u), 0);
    CHECK_EQ(esk_bus_debug_write(&f->m.bus, 0x100, 4, 0xbf00e7feu), 0); /* b . */
    esk_mach_reset(&f->m);
    return 1;
}

static void nf_close(nf *f)
{
    esk_mach_free(&f->m);
    esk_json_free(&f->a, f->mcu);
    esk_json_free(&f->a, f->regs);
    CHECK_EQ(f->a.live_bytes, 0);
}

static u32 rd(nf *f, u32 a)
{
    u32 v = 0;
    CHECK_EQ(esk_bus_read(&f->m.bus, a, 4, &v), 0);
    return v;
}

static void wr(nf *f, u32 a, u32 v) { CHECK_EQ(esk_bus_write(&f->m.bus, a, 4, v), 0); }

/* ---- the tests ------------------------------------------------------------------ */

static void test_ficr(void)
{
    nf f;
    if (!nf_open(&f))
        return;
    CHECK_STR(esk_json_get_str(f.mcu, "name", ""), "nRF52840");
    CHECK_EQ(rd(&f, 0x10000100u), 0x52840u); /* INFO.PART */
    CHECK_EQ(rd(&f, 0x10000014u), 256u);     /* CODESIZE */
    CHECK_EQ(esk_mach_core_hz(&f.m), 64000000u);
    CHECK_EQ(f.warnings, 0);
    nf_close(&f);
}

static void test_clock(void)
{
    nf f;
    if (!nf_open(&f))
        return;
    wr(&f, 0x40000000u, 1); /* HFCLKSTART */
    CHECK_EQ(rd(&f, 0x40000100u), 0u);
    esk_mach_run(&f.m, 17000, 0); /* HFXODEBOUNCE 0x10: 256 us */
    CHECK_EQ(rd(&f, 0x40000100u), 1u); /* HFCLKSTARTED */
    CHECK_EQ(rd(&f, 0x4000040cu), 0x10001u); /* HFCLKSTAT: Xtal, running */
    wr(&f, 0x40000518u, 1);
    wr(&f, 0x40000008u, 1); /* LFCLKSTART */
    CHECK_EQ(rd(&f, 0x40000104u), 1u);
    CHECK_EQ(rd(&f, 0x40000418u), 0x10001u); /* LFCLKSTAT: Xtal, running */
    nf_close(&f);
}

static void test_gpio_and_gpiote(void)
{
    nf f;
    if (!nf_open(&f))
        return;
    wr(&f, 0x50000518u, 1u << 13);               /* P0 DIRSET 13 */
    CHECK_EQ(rd(&f, 0x50000734u) & 1u, 1u);      /* PIN_CNF[13].DIR */
    wr(&f, 0x50000508u, 1u << 13);               /* OUTSET */
    CHECK_EQ(rd(&f, 0x50000510u) >> 13 & 1u, 0u); /* IN: its input buffer disconnected (PIN_CNF's reset) */
    wr(&f, 0x50000734u, 1u);                     /* PIN_CNF[13]: output, input connected */
    CHECK_EQ(rd(&f, 0x50000510u) >> 13 & 1u, 1u); /* IN: the output */
    wr(&f, 0x5000030cu + 0x700u, 3u << 2);       /* P1 PIN_CNF[3]: pull-up */
    CHECK_EQ(rd(&f, 0x50000810u) >> 3 & 1u, 1u); /* P1 IN */
    wr(&f, 0x40006510u, 1u | (3u << 8) | (1u << 13) | (2u << 16)); /* GPIOTE CONFIG[0]: P1.03 HiToLo */
    f.ext[35] = 0;                               /* the board pulls P1.03 low */
    f.m.pins_changed(&f.m);
    CHECK_EQ(rd(&f, 0x40006100u), 1u);           /* IN[0] */
    wr(&f, 0x40006100u, 0);
    f.ext[35] = 1;
    f.m.pins_changed(&f.m);
    CHECK_EQ(rd(&f, 0x40006100u), 0u);           /* a rise: not its polarity */
    wr(&f, 0x50000738u, 0);                       /* PIN_CNF[14]: input connected */
    wr(&f, 0x40006514u, 3u | (14u << 8) | (3u << 16) | (1u << 20)); /* CONFIG[1]: task, P0.14, toggle, high */
    CHECK_EQ(rd(&f, 0x50000510u) >> 14 & 1u, 1u); /* OUTINIT drives it high */
    wr(&f, 0x40006004u, 1);                       /* OUT[1]: toggle */
    CHECK_EQ(rd(&f, 0x50000510u) >> 14 & 1u, 0u);
    nf_close(&f);
}

static void test_uarte_dma(void)
{
    nf f;
    if (!nf_open(&f))
        return;
    CHECK_EQ(esk_bus_debug_write(&f.m.bus, 0x20000100u, 2, 'h' | 'i' << 8), 0);
    wr(&f, 0x4000250cu, 6);            /* PSEL.TXD P0.06 */
    wr(&f, 0x40002524u, 0x01d7e000u);  /* 115200 */
    wr(&f, 0x40002500u, 8);            /* ENABLE: UARTE */
    wr(&f, 0x40002544u, 0x20000100u);
    wr(&f, 0x40002548u, 2);
    wr(&f, 0x40002008u, 1);            /* STARTTX */
    CHECK_EQ(rd(&f, 0x40002150u), 1u); /* TXSTARTED */
    CHECK_EQ(f.ntx, 1u);
    CHECK_EQ(f.tx_bits, 555u);         /* 2^32 / BAUDRATE cycles of 16 MHz, at 64 MHz */
    esk_mach_run(&f.m, 6000, 0);
    CHECK_EQ(f.ntx, 2u);
    CHECK_EQ(rd(&f, 0x40002120u), 0u);
    esk_mach_run(&f.m, 6000, 0);
    CHECK_EQ(rd(&f, 0x40002120u), 1u); /* ENDTX */
    CHECK_EQ(rd(&f, 0x4000254cu), 2u); /* TXD.AMOUNT */
    CHECK(f.tx[0] == 'h' && f.tx[1] == 'i');
    nf_close(&f);
}

static void test_timer_compare(void)
{
    nf f;
    if (!nf_open(&f))
        return;
    wr(&f, 0x40008510u, 4);           /* PRESCALER: 1 MHz */
    wr(&f, 0x40008508u, 3);           /* 32 bits */
    wr(&f, 0x40008540u, 100);         /* CC[0] */
    wr(&f, 0x40008304u, 1u << 16);    /* INTENSET COMPARE0 */
    wr(&f, 0x40008000u, 1);           /* START */
    esk_mach_run(&f.m, 6300, 0);
    CHECK_EQ(rd(&f, 0x40008140u), 0u);
    esk_mach_run(&f.m, 200, 0);
    CHECK_EQ(rd(&f, 0x40008140u), 1u); /* COMPARE[0] after 100 us */
    wr(&f, 0x40008040u, 1);            /* CAPTURE[0] */
    CHECK(rd(&f, 0x40008540u) >= 100u && rd(&f, 0x40008540u) <= 102u);
    nf_close(&f);
}

static void test_rtc_compare(void)
{
    nf f;
    if (!nf_open(&f))
        return;
    wr(&f, 0x40000008u, 1);           /* LFCLKSTART */
    wr(&f, 0x4000b540u, 10);          /* RTC0 CC[0] */
    wr(&f, 0x4000b344u, 1u << 16);    /* EVTENSET COMPARE0 */
    wr(&f, 0x4000b000u, 1);           /* START */
    esk_mach_run(&f.m, 19000, 0);     /* 10 counts are 19531 cycles */
    CHECK_EQ(rd(&f, 0x4000b140u), 0u);
    esk_mach_run(&f.m, 1000, 0);
    CHECK_EQ(rd(&f, 0x4000b140u), 1u);
    CHECK_EQ(rd(&f, 0x4000b504u), 10u);
    nf_close(&f);
}

TEST_MAIN_BEGIN
RUN(test_ficr);
RUN(test_clock);
RUN(test_gpio_and_gpiote);
RUN(test_uarte_dma);
RUN(test_timer_compare);
RUN(test_rtc_compare);
TEST_MAIN_END
