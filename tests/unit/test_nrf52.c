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

/* the analog inputs: 1.0 V on P0.04 (AIN2) */
static int r_pin_volts(void *ctx, int port, int bit, double *v)
{
    (void)ctx;
    if (16 * port + bit == 4) {
        *v = 1.0;
        return 1;
    }
    return 0;
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
    f->m.host.pin_volts = r_pin_volts;
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

static void test_spim_twim(void)
{
    nf f;
    u32 v = 0;
    if (!nf_open(&f))
        return;
    /* SPIM2 at 8 MHz (4 cycles a half bit): one byte out, two in, the
     * second clocking out ORC; MISO (P1.14) held high */
    f.ext[46] = 1;
    CHECK_EQ(esk_bus_debug_write(&f.m.bus, 0x20000200u, 1, 0xa5u), 0);
    wr(&f, 0x40023508u, 0x2fu);        /* PSEL.SCK P1.15 */
    wr(&f, 0x4002350cu, 0x2du);        /* PSEL.MOSI P1.13 */
    wr(&f, 0x40023510u, 0x2eu);        /* PSEL.MISO P1.14 */
    wr(&f, 0x40023524u, 0x80000000u);  /* 8 Mbps */
    wr(&f, 0x400235c0u, 0x3cu);        /* ORC */
    wr(&f, 0x40023544u, 0x20000200u);
    wr(&f, 0x40023548u, 1);
    wr(&f, 0x40023534u, 0x20000300u);
    wr(&f, 0x40023538u, 2);
    wr(&f, 0x40023500u, 7);            /* ENABLE: SPIM */
    CHECK_EQ(esk_nrf_pin_level(&f.m, 47), 0); /* SCK idles at CPOL 0 */
    wr(&f, 0x40023010u, 1);            /* START */
    CHECK_EQ(rd(&f, 0x4002314cu), 1u); /* STARTED */
    esk_mach_run(&f.m, 80, 0);
    CHECK_EQ(rd(&f, 0x40023120u), 1u); /* ENDTX after the first byte, 64 cycles */
    CHECK_EQ(rd(&f, 0x40023118u), 0u);
    esk_mach_run(&f.m, 60, 0);
    CHECK_EQ(rd(&f, 0x40023118u), 1u); /* END after 16 bits, 128 cycles */
    CHECK_EQ(rd(&f, 0x40023110u), 1u); /* ENDRX */
    CHECK_EQ(rd(&f, 0x4002354cu), 1u); /* TXD.AMOUNT */
    CHECK_EQ(rd(&f, 0x4002353cu), 2u); /* RXD.AMOUNT */
    CHECK_EQ(esk_bus_debug_read(&f.m.bus, 0x20000300u, 2, &v), 0);
    CHECK_EQ(v, 0xffffu);
    CHECK_EQ(esk_nrf_pin_level(&f.m, 45), 0); /* MOSI: ORC's last bit */

    /* TWIM0 at 400 kHz: nobody answers 0x50 (SDA, P0.26, pulled high) */
    f.ext[26] = f.ext[27] = 1; /* the pull-ups */
    CHECK_EQ(esk_bus_debug_write(&f.m.bus, 0x20000400u, 1, 0x07u), 0);
    wr(&f, 0x40003508u, 27);           /* PSEL.SCL */
    wr(&f, 0x4000350cu, 26);           /* PSEL.SDA */
    wr(&f, 0x40003524u, 0x06400000u);  /* 400 kbps: 80 cycles a half period */
    wr(&f, 0x40003588u, 0x50);         /* ADDRESS */
    wr(&f, 0x40003544u, 0x20000400u);
    wr(&f, 0x40003548u, 1);
    wr(&f, 0x40003534u, 0x20000500u);
    wr(&f, 0x40003538u, 2);
    wr(&f, 0x40003200u, (1u << 7) | (1u << 12)); /* LASTTX_STARTRX, LASTRX_STOP */
    CHECK_EQ(rd(&f, 0x40003200u), (1u << 7) | (1u << 12));
    wr(&f, 0x40003500u, 6);            /* ENABLE: TWIM */
    wr(&f, 0x40003008u, 1);            /* STARTTX */
    CHECK_EQ(rd(&f, 0x40003150u), 1u); /* TXSTARTED */
    esk_mach_run(&f.m, 3000, 0);
    CHECK_EQ(rd(&f, 0x40003124u), 1u); /* ERROR */
    CHECK_EQ(rd(&f, 0x400034c4u), 2u); /* ERRORSRC.ANACK */
    CHECK_EQ(rd(&f, 0x40003104u), 0u); /* held until STOP */
    CHECK_EQ(esk_nrf_pin_level(&f.m, 27), 0);
    wr(&f, 0x40003014u, 1);            /* STOP */
    esk_mach_run(&f.m, 400, 0);
    CHECK_EQ(rd(&f, 0x40003104u), 1u); /* STOPPED */
    CHECK_EQ(esk_nrf_pin_level(&f.m, 27), 1);
    wr(&f, 0x400034c4u, 2);            /* ERRORSRC written 1: cleared */
    CHECK_EQ(rd(&f, 0x400034c4u), 0u);

    /* a slave that pulls SDA low whenever it is let go: everything is
     * ACKed and reads 0; one byte written, two read after a repeated
     * START (LASTTX_STARTRX), then the STOP (LASTRX_STOP) */
    f.ext[26] = 0;
    wr(&f, 0x40003104u, 0);
    wr(&f, 0x40003124u, 0);
    CHECK_EQ(esk_bus_debug_write(&f.m.bus, 0x20000500u, 2, 0x5555u), 0);
    wr(&f, 0x40003008u, 1);            /* STARTTX */
    esk_mach_run(&f.m, 12000, 0);      /* 4 + 27 bits and the STARTs, 160 cycles each */
    CHECK_EQ(rd(&f, 0x40003160u), 1u); /* LASTTX */
    CHECK_EQ(rd(&f, 0x4000314cu), 1u); /* RXSTARTED, by the short */
    CHECK_EQ(rd(&f, 0x4000315cu), 1u); /* LASTRX */
    CHECK_EQ(rd(&f, 0x40003104u), 1u); /* STOPPED */
    CHECK_EQ(rd(&f, 0x40003124u), 0u);
    CHECK_EQ(rd(&f, 0x4000354cu), 1u);
    CHECK_EQ(rd(&f, 0x4000353cu), 2u);
    CHECK_EQ(esk_bus_debug_read(&f.m.bus, 0x20000500u, 2, &v), 0);
    CHECK_EQ(v, 0u);
    nf_close(&f);
}

static void test_pwm_saadc(void)
{
    nf f;
    u32 v = 0;
    if (!nf_open(&f))
        return;
    /* PWM0 at 1 MHz (64 cycles a count), COUNTERTOP 100: OUT0 (P1.04) high
     * for 25 counts (bit 15, FallingEdge), OUT1 (P1.06) for 50 */
    CHECK_EQ(esk_bus_debug_write(&f.m.bus, 0x20000600u, 4, (0x8000u | 25u) | (0x8000u | 50u) << 16), 0);
    CHECK_EQ(esk_bus_debug_write(&f.m.bus, 0x20000604u, 4, 0), 0);
    wr(&f, 0x4001c560u, 0x24u);        /* PSEL.OUT[0] P1.04 */
    wr(&f, 0x4001c564u, 0x26u);        /* PSEL.OUT[1] P1.06 */
    wr(&f, 0x4001c50cu, 4);            /* PRESCALER: 1 MHz */
    wr(&f, 0x4001c508u, 100);          /* COUNTERTOP */
    wr(&f, 0x4001c510u, 2);            /* DECODER: Individual */
    wr(&f, 0x4001c520u, 0x20000600u);  /* SEQ[0].PTR */
    wr(&f, 0x4001c524u, 4);            /* SEQ[0].CNT */
    wr(&f, 0x4001c500u, 1);            /* ENABLE */
    wr(&f, 0x4001c008u, 1);            /* SEQSTART[0] */
    CHECK_EQ(rd(&f, 0x4001c108u), 1u); /* SEQSTARTED[0] */
    CHECK_EQ(rd(&f, 0x4001c110u), 1u); /* SEQEND[0]: its one step is loaded */
    CHECK_EQ(esk_nrf_pin_level(&f.m, 36), 1);
    CHECK_EQ(esk_nrf_pin_level(&f.m, 38), 1);
    esk_mach_run(&f.m, 1500, 0);       /* 23 counts */
    CHECK_EQ(esk_nrf_pin_level(&f.m, 36), 1);
    esk_mach_run(&f.m, 200, 0);        /* past 25 */
    CHECK_EQ(esk_nrf_pin_level(&f.m, 36), 0);
    CHECK_EQ(esk_nrf_pin_level(&f.m, 38), 1);
    esk_mach_run(&f.m, 1700, 0);       /* past 50 */
    CHECK_EQ(esk_nrf_pin_level(&f.m, 38), 0);
    CHECK_EQ(rd(&f, 0x4001c118u), 0u);
    esk_mach_run(&f.m, 3200, 0);       /* past 100: the period's end */
    CHECK_EQ(rd(&f, 0x4001c118u), 1u); /* PWMPERIODEND */
    CHECK_EQ(esk_nrf_pin_level(&f.m, 36), 1); /* the step holds: LOOP 0 */
    wr(&f, 0x4001c004u, 1);            /* STOP */
    CHECK_EQ(rd(&f, 0x4001c104u), 0u);
    esk_mach_run(&f.m, 6500, 0);
    CHECK_EQ(rd(&f, 0x4001c104u), 1u); /* STOPPED at the period's end */

    /* SAADC, 12 bits, gain 1/6, the 0.6 V reference, 10 us acquisitions:
     * AIN2's 1.0 V and VDD's 3.0 V */
    wr(&f, 0x40007510u, 3);            /* CH[0].PSELP: AIN2 */
    wr(&f, 0x40007518u, 2u << 16);     /* CH[0].CONFIG: gain 1/6, TACQ 10 us */
    wr(&f, 0x4000751cu, 1000u << 16 | 0xff9cu); /* CH[0].LIMIT: -100 to 1000 */
    wr(&f, 0x40007520u, 9);            /* CH[1].PSELP: VDD */
    wr(&f, 0x40007528u, 2u << 16);
    wr(&f, 0x400075f0u, 2);            /* RESOLUTION: 12 bits */
    wr(&f, 0x4000762cu, 0x20000700u);
    wr(&f, 0x40007630u, 2);
    wr(&f, 0x40007500u, 1);            /* ENABLE */
    CHECK_EQ(rd(&f, 0x40007640u), 0u); /* reserved: anomaly 212's registers */
    wr(&f, 0x40007ffcu, 1);
    wr(&f, 0x4000700cu, 1);            /* CALIBRATEOFFSET */
    esk_mach_run(&f.m, 1300, 0);
    CHECK_EQ(rd(&f, 0x40007110u), 1u); /* CALIBRATEDONE after 20 us */
    wr(&f, 0x40007000u, 1);            /* START */
    CHECK_EQ(rd(&f, 0x40007100u), 1u); /* STARTED */
    wr(&f, 0x40007004u, 1);            /* SAMPLE */
    CHECK_EQ(rd(&f, 0x40007400u), 1u); /* STATUS: busy */
    esk_mach_run(&f.m, 800, 0);        /* 12 us a conversion: 768 cycles */
    CHECK_EQ(rd(&f, 0x40007108u), 1u); /* DONE */
    CHECK_EQ(rd(&f, 0x40007118u), 1u); /* CH[0].LIMITH: 1137 > 1000 */
    CHECK_EQ(rd(&f, 0x40007104u), 0u);
    esk_mach_run(&f.m, 800, 0);
    CHECK_EQ(rd(&f, 0x40007104u), 1u); /* END */
    CHECK_EQ(rd(&f, 0x40007634u), 2u); /* RESULT.AMOUNT */
    CHECK_EQ(rd(&f, 0x40007400u), 0u);
    CHECK_EQ(esk_bus_debug_read(&f.m.bus, 0x20000700u, 4, &v), 0);
    CHECK_EQ(v & 0xffffu, 1137u);      /* floor(1.0 / 6 / 0.6 * 4096) */
    CHECK_EQ(v >> 16, 3413u);          /* VDD, 3.0 V */
    CHECK_EQ(f.warnings, 0);
    nf_close(&f);
}

TEST_MAIN_BEGIN
RUN(test_ficr);
RUN(test_clock);
RUN(test_gpio_and_gpiote);
RUN(test_uarte_dma);
RUN(test_timer_compare);
RUN(test_rtc_compare);
RUN(test_spim_twim);
RUN(test_pwm_saadc);
TEST_MAIN_END
