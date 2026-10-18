/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* test_avr.c -- the AVR core and the ATmega328P's models (SPEC-009): hand-
 * assembled programs in flash, run on the machine the catalog's description
 * builds, against the datasheet's flags and cycle counts. */
#include "harness.h"
#include "mcu/avr/avr.h"
#include "mcu/build.h"
#include "util/json.h"

/* ---- encodings ------------------------------------------------------------ */

static u16 ldi(int d, int k) { return (u16)(0xE000 | ((k & 0xF0) << 4) | ((d - 16) << 4) | (k & 0xF)); }
static u16 out(int a, int r) { return (u16)(0xB800 | ((a & 0x30) << 5) | (r << 4) | (a & 0xF)); }
static u16 in_(int r, int a) { return (u16)(0xB000 | ((a & 0x30) << 5) | (r << 4) | (a & 0xF)); }
static u16 add(int d, int r) { return (u16)(0x0C00 | ((r & 0x10) << 5) | (d << 4) | (r & 0xF)); }
static u16 dec(int d) { return (u16)(0x940A | (d << 4)); }
static u16 brne(int k) { return (u16)(0xF401 | ((k & 0x7F) << 3)); }
static u16 rjmp(int k) { return (u16)(0xC000 | (k & 0xFFF)); }
static u16 sts0(int r) { return (u16)(0x9200 | (r << 4)); } /* then the address word */
static u16 lds0(int r) { return (u16)(0x9000 | (r << 4)); }
static u16 mul(int d, int r) { return (u16)(0x9C00 | ((r & 0x10) << 5) | (d << 4) | (r & 0xF)); }
#define NOP 0x0000
#define SEI 0x9478
#define SLEEP 0x9588
#define RETI 0x9518

/* ---- the machine -------------------------------------------------------------- */

typedef struct uno {
    esk_alloc a;
    esk_json *mcu, *regs;
    struct esk_mach m;
    char tx[256];
    size_t ntx;
    u32 tx_bit_cycles;
    int pin_drive[8][8];
    double volts;
    int loopback;       /* MISO (PB4) reads MOSI (PB3) */
    int ss_low;         /* something holds SS (PB2) low */
    int sda_low;        /* a slave holds SDA (PC4) low */
    int edges[8][8];    /* changes of a pin's drive */
    u64 scl_at[64];     /* when SCL (PC5) changed */
    int nscl;
    int use_pv;         /* pv gives each pin its own voltage */
    double pv[8][8];
    int ext[8][8];      /* a level something outside drives: 1 + level, 0 none */
    int watch_on[8][8]; /* the comparator's thresholds on the pins */
    double watch_v[8][8];
} uno;

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

static void u_uart(void *ctx, const char *unit, int c, u32 bit_cycles)
{
    uno *u = (uno *)ctx;
    (void)unit;
    u->tx_bit_cycles = bit_cycles;
    if (u->ntx + 1 < sizeof u->tx)
        u->tx[u->ntx++] = (char)c;
}

static void u_pin_cfg(void *ctx, int port, int bit, const esk_mach_pin *cfg)
{
    uno *u = (uno *)ctx;
    int d = cfg->drive >= 0 ? cfg->drive : cfg->pull == 1 ? 2 : -1;
    if (port < 0 || port >= 8 || bit < 0 || bit >= 8)
        return;
    if (d != u->pin_drive[port][bit]) {
        u->edges[port][bit]++;
        if (port == 2 && bit == 5 && u->nscl < 64)
            u->scl_at[u->nscl++] = u->m.cycles;
    }
    u->pin_drive[port][bit] = d;
}

static int u_pin_in(void *ctx, int port, int bit, int *level)
{
    uno *u = (uno *)ctx;
    if (port == 1 && bit == 4 && u->loopback) {
        *level = u->pin_drive[1][3] == 1;
        return 1;
    }
    if (port == 1 && bit == 2 && u->ss_low) {
        *level = 0;
        return 1;
    }
    if (port == 2 && bit == 4 && u->sda_low) {
        *level = 0;
        return 1;
    }
    if (port >= 0 && port < 8 && bit >= 0 && bit < 8 && u->ext[port][bit]) {
        *level = u->ext[port][bit] - 1;
        return 1;
    }
    return 0;
}

static int u_pin_volts(void *ctx, int port, int bit, double *v)
{
    uno *u = (uno *)ctx;
    *v = u->use_pv && port >= 0 && port < 8 && bit >= 0 && bit < 8 ? u->pv[port][bit] : u->volts;
    return 1;
}

static int u_pin_watch(void *ctx, int port, int bit, int on, double v)
{
    uno *u = (uno *)ctx;
    if (port < 0 || port >= 8 || bit < 0 || bit >= 8)
        return 0;
    u->watch_on[port][bit] = on;
    u->watch_v[port][bit] = v;
    return on;
}

static void u_diag(void *ctx, int sev, const char *code, const char *msg)
{
    (void)ctx;
    (void)sev;
    (void)code;
    (void)msg;
}

/* an ATmega328P at 16 MHz with `prog` (words) at 0 */
static int uno_open(uno *u, const u16 *prog, size_t n)
{
    char why[200];
    es_status st;
    size_t i;
    memset(u, 0, sizeof *u);
    esk_alloc_init(&u->a, NULL);
    u->mcu = load_json(&u->a, "catalog/mcus/microchip/atmega328p/mcu.json");
    u->regs = load_json(&u->a, "catalog/mcus/microchip/atmega328p/registers.json");
    CHECK(u->mcu != NULL && u->regs != NULL);
    esk_mach_init(&u->m, &u->a);
    u->m.host.ctx = u;
    u->m.host.uart_tx = u_uart;
    u->m.host.pin_cfg = u_pin_cfg;
    u->m.host.pin_in = u_pin_in;
    u->m.host.pin_volts = u_pin_volts;
    u->m.host.pin_watch = u_pin_watch;
    u->m.host.diag = u_diag;
    st = esk_mach_build(&u->m, u->mcu, u->regs, 16000000u, why, sizeof why);
    if (st != ES_OK) {
        FAIL(why);
        return 0;
    }
    for (i = 0; i < n; i++)
        CHECK_EQ(esk_bus_debug_write(&u->m.bus, (u32)(2 * i), 2, prog[i]), 0);
    esk_mach_reset(&u->m);
    return 1;
}

static void uno_close(uno *u)
{
    esk_mach_free(&u->m);
    esk_json_free(&u->a, u->mcu);
    esk_json_free(&u->a, u->regs);
    CHECK_EQ(u->a.live_bytes, 0);
}

static u8 reg(uno *u, int n)
{
    u8 b[4] = { 0 };
    u->m.cpu->ops->reg_read(u->m.cpu, n, b);
    return b[0];
}

static u8 data(uno *u, u32 a)
{
    u32 v = 0;
    (void)esk_bus_debug_read(&u->m.bus, ESK_AVR_DATA + a, 1, &v);
    return (u8)v;
}

/* ---- the tests ------------------------------------------------------------------ */

static void test_flags_and_cycles(void)
{
    /* ldi r16,0xFF; ldi r17,1; add r16,r17 -> 0 with H, Z, C; then a counted loop */
    const u16 prog[] = { ldi(16, 0xFF), ldi(17, 1), add(16, 17), ldi(18, 5), dec(18), brne(-2), NOP,
                         ldi(20, 7), ldi(21, 6), mul(20, 21), rjmp(-1) };
    uno u;
    if (!uno_open(&u, prog, sizeof prog / sizeof prog[0]))
        return;
    esk_mach_run(&u.m, 0xffffff, 3);
    CHECK_EQ(reg(&u, 16), 0);
    CHECK_EQ(reg(&u, 32) & 0x23, 0x23); /* H, Z, C */
    CHECK_EQ(u.m.cycles, 3);
    /* ldi 1, then four times dec + brne taken (1 + 2), then dec + brne not taken, nop */
    esk_mach_run(&u.m, 0xffffff, 1 + 10 + 1);
    CHECK_EQ(u.m.cycles, 3 + 1 + 4 * 3 + 2 + 1);
    CHECK_EQ(reg(&u, 18), 0);
    esk_mach_run(&u.m, 0xffffff, 3);
    CHECK_EQ(reg(&u, 0), 42); /* mul: 2 cycles */
    CHECK_EQ(u.m.cycles, 19 + 1 + 1 + 2);
    /* the jump to itself with I clear: the machine sleeps */
    esk_mach_run(&u.m, 100, 0);
    CHECK(u.m.asleep);
    uno_close(&u);
}

static void test_timer0_interrupt(void)
{
    /* vector 14 (TIMER0_COMPA, word 28) increments r24 and returns; main
     * sets Timer0 to CTC /64 with OCR0A 249 (1 ms at 16 MHz) and sleeps */
    u16 prog[64];
    size_t n = 0, i;
    uno u;
    memset(prog, 0, sizeof prog);
    prog[0] = rjmp(39); /* to word 40 */
    prog[28] = 0x9583;  /* inc r24 */
    prog[29] = RETI;
    n = 40;
    prog[n++] = ldi(16, 0x02);
    prog[n++] = out(0x24, 16); /* TCCR0A: CTC */
    prog[n++] = ldi(16, 249);
    prog[n++] = out(0x27, 16); /* OCR0A */
    prog[n++] = ldi(16, 0x02);
    prog[n++] = sts0(16);
    prog[n++] = 0x6E; /* TIMSK0: OCIE0A */
    prog[n++] = ldi(16, 0x03);
    prog[n++] = out(0x25, 16); /* TCCR0B: /64 */
    prog[n++] = ldi(16, 0x01);
    prog[n++] = out(0x33, 16); /* SMCR: idle sleep */
    prog[n++] = SEI;
    prog[n++] = SLEEP;
    prog[n++] = rjmp(-2);
    if (!uno_open(&u, prog, n))
        return;
    for (i = 0; i < 100 && u.m.cycles < 16000u * 10u + 2000u; i++)
        esk_mach_run(&u.m, 16000u * 10u + 2000u - u.m.cycles, 0);
    /* ten milliseconds: ten compare matches */
    CHECK_EQ(reg(&u, 24), 10);
    uno_close(&u);
}

static void test_usart_frame(void)
{
    /* 9600 baud (UBRR0 103), TXEN0, then 'A' to UDR0 and wait for TXC0 */
    const u16 prog[] = {
        ldi(16, 103), sts0(16), 0xC4, ldi(16, 0x08), sts0(16), 0xC1, ldi(16, 'A'), sts0(16), 0xC6,
        lds0(17), 0xC0, 0xFF16 /* sbrs r17,6 */, rjmp(-4), rjmp(-1),
    };
    uno u;
    u64 start;
    if (!uno_open(&u, prog, sizeof prog / sizeof prog[0]))
        return;
    esk_mach_run(&u.m, 0xffffff, 6);
    start = u.m.cycles;
    CHECK_EQ(u.ntx, 1);
    CHECK_EQ(u.tx[0], 'A');
    CHECK_EQ(u.tx_bit_cycles, 16u * 104u);
    /* TX on PD1 is the USART's, idle high */
    CHECK_EQ(u.pin_drive[3][1], 1);
    while (!(data(&u, 0xC0) & 0x40) && u.m.cycles < start + 20000)
        esk_mach_run(&u.m, 1, 0);
    CHECK(data(&u, 0xC0) & 0x40);
    /* the frame: start, 8 data, stop at 1664 cycles a bit (the last
     * instruction may overshoot by its own cycles) */
    CHECK(u.m.cycles - start >= 16640 && u.m.cycles - start <= 16640 + 4);
    uno_close(&u);
}

static void test_ports(void)
{
    /* DDRB5 out, PORTB5 high, PORTD2's pull-up; then a one to PINB toggles PB5 */
    const u16 prog[] = { ldi(16, 0x20), out(0x04, 16), out(0x05, 16), ldi(17, 0x04), out(0x0B, 17),
                         out(0x03, 16), in_(18, 0x09), rjmp(-1) };
    uno u;
    if (!uno_open(&u, prog, sizeof prog / sizeof prog[0]))
        return;
    esk_mach_run(&u.m, 0xffffff, 3);
    CHECK_EQ(u.pin_drive[1][5], 1);
    esk_mach_run(&u.m, 0xffffff, 2);
    CHECK_EQ(u.pin_drive[3][2], 2); /* pulled up */
    esk_mach_run(&u.m, 0xffffff, 1);
    CHECK_EQ(u.pin_drive[1][5], 0);
    esk_mach_run(&u.m, 0xffffff, 1);
    CHECK_EQ(reg(&u, 18) & 0x04, 0x04); /* PIND2 reads the pull-up */
    uno_close(&u);
}

static void test_adc(void)
{
    /* ADMUX AVCC, channel 0; ADCSRA ADEN | ADSC | /128; wait for ADSC to clear */
    const u16 prog[] = { ldi(16, 0x40), sts0(16), 0x7C, ldi(16, 0xC7), sts0(16), 0x7A,
                         lds0(17), 0x7A, 0xFD16 /* sbrc r17,6 */, rjmp(-4),
                         lds0(20), 0x78, lds0(21), 0x79, rjmp(-1) };
    uno u;
    int i;
    if (!uno_open(&u, prog, sizeof prog / sizeof prog[0]))
        return;
    u.volts = 2.5;
    for (i = 0; i < 2000 && reg(&u, 21) == 0 && reg(&u, 20) == 0; i++)
        esk_mach_run(&u.m, 64, 0);
    /* 2.5 V of 5 V: 512; the first conversion takes 25 ADC clocks of 128 cycles */
    CHECK_EQ(reg(&u, 20) | reg(&u, 21) << 8, 512);
    CHECK(u.m.cycles >= 25u * 128u);
    uno_close(&u);
}

static void test_timer0_fast_pwm_overflow(void)
{
    /* Timer0 in fast PWM at /1, then a run of nops: TOV0 sets as the
     * counter goes from TOP to BOTTOM, so a read of TCNT0 at 255 finds it
     * clear (Arduino's micros() counts on it) and at 0 finds it set */
    u16 prog[400];
    size_t n = 0, i;
    int tops = 0, wraps = 0;
    u8 last;
    uno u;
    memset(prog, 0, sizeof prog);
    prog[n++] = ldi(16, 0x03);
    prog[n++] = out(0x24, 16); /* TCCR0A: fast PWM, TOP 0xFF */
    prog[n++] = ldi(16, 0x01);
    prog[n++] = out(0x25, 16); /* TCCR0B: /1 */
    n = sizeof prog / sizeof prog[0] - 1;
    prog[n++] = rjmp(-1);
    if (!uno_open(&u, prog, n))
        return;
    esk_mach_run(&u.m, 0xffffff, 4);
    last = data(&u, 0x46);
    for (i = 0; i < 390; i++) {
        u8 cnt, tov;
        esk_mach_run(&u.m, 0xffffff, 1); /* a nop: one clock */
        cnt = data(&u, 0x46);            /* TCNT0 */
        tov = data(&u, 0x35) & 1;        /* TIFR0.TOV0 */
        CHECK_EQ(cnt, (u8)(last + 1));
        if (cnt == 255) {
            CHECK_EQ(tov, 0);
            tops++;
        }
        if (cnt == 0) {
            CHECK_EQ(tov, 1);
            wraps++;
            CHECK_EQ(esk_bus_debug_write(&u.m.bus, ESK_AVR_DATA + 0x35, 1, 1), 0);
        }
        last = cnt;
    }
    CHECK(tops >= 1 && wraps >= 1);
    uno_close(&u);
}

/* an I/O register by data address, as the core's ST and LD reach it */
static void poke(uno *u, u32 a, u8 v) { CHECK_EQ(esk_bus_write(&u->m.bus, ESK_AVR_DATA + a, 1, v), 0); }

static u8 peek(uno *u, u32 a)
{
    u32 v = 0;
    CHECK_EQ(esk_bus_read(&u->m.bus, ESK_AVR_DATA + a, 1, &v), 0);
    return (u8)v;
}

/* run the core (a loop of nops) until the register at `a` has `mask` set
 * (or clear, with `clear`): the cycles it took */
static u64 run_until(uno *u, u32 a, u8 mask, int clear, u64 limit)
{
    u64 start = u->m.cycles;
    while (u->m.cycles - start < limit && ((data(u, a) & mask) != 0) == (clear != 0))
        esk_mach_run(&u->m, 1, 0);
    return u->m.cycles - start;
}

#define SPCR 0x4C
#define SPSR 0x4D
#define SPDR 0x4E

static void test_spi(void)
{
    const u16 prog[] = { NOP, rjmp(-2) };
    uno u;
    u64 t;
    if (!uno_open(&u, prog, sizeof prog / sizeof prog[0]))
        return;
    u.loopback = 1;
    esk_mach_run(&u.m, 4, 0);
    poke(&u, 0x24, 0x2C); /* DDRB: SS, MOSI, SCK out */
    poke(&u, 0x25, 0x04); /* SS high */
    poke(&u, SPCR, 0x50); /* SPE | MSTR, mode 0, /4 */
    CHECK_EQ(u.pin_drive[1][5], 0); /* SCK idles low */
    CHECK_EQ(u.pin_drive[1][4], -1); /* MISO an input, no pull-up */
    u.edges[1][5] = 0;
    poke(&u, SPDR, 0xA5);
    t = run_until(&u, SPSR, 0x80, 0, 1000);
    /* eight bits of two half bits of two cycles; instructions of one and
     * two cycles may stretch an edge by one */
    CHECK(t >= 32 && t <= 40);
    CHECK_EQ(u.edges[1][5], 16);
    CHECK_EQ(peek(&u, SPSR) & 0x80, 0x80);
    CHECK_EQ(peek(&u, SPDR), 0xA5); /* looped back; the SPSR-then-SPDR read clears SPIF */
    CHECK_EQ(data(&u, SPSR) & 0x80, 0);
    CHECK_EQ(u.pin_drive[1][3], 1); /* MOSI holds the last bit */
    /* a write while a byte is under way is lost: WCOL */
    poke(&u, SPDR, 0x3C);
    esk_mach_run(&u.m, 4, 0);
    poke(&u, SPDR, 0xFF);
    CHECK_EQ(data(&u, SPSR) & 0x40, 0x40);
    run_until(&u, SPSR, 0x80, 0, 1000);
    CHECK_EQ(data(&u, SPDR), 0x3C);
    CHECK_EQ(peek(&u, SPSR) & 0xC0, 0xC0);
    (void)peek(&u, SPDR);
    CHECK_EQ(data(&u, SPSR) & 0xC0, 0);
    /* LSB first, mode 3, /128 halved by SPI2X: 32 cycles a half bit */
    poke(&u, SPCR, 0x7F);
    CHECK_EQ(u.pin_drive[1][5], 1); /* SCK idles high */
    poke(&u, SPSR, 0x01);
    poke(&u, SPDR, 0x81);
    t = run_until(&u, SPSR, 0x80, 0, 4000);
    CHECK(t >= 512 && t <= 516);
    CHECK_EQ(data(&u, SPDR), 0x81);
    /* SS an input held low: the master steps down, SPIF set */
    (void)peek(&u, SPSR);
    (void)peek(&u, SPDR);
    poke(&u, 0x24, 0x28);
    u.ss_low = 1;
    poke(&u, SPDR, 0x55);
    CHECK_EQ(data(&u, SPCR) & 0x10, 0);
    CHECK_EQ(data(&u, SPSR) & 0x80, 0x80);
    CHECK_EQ(u.pin_drive[1][5], 0); /* SCK back to PORTB5 */
    uno_close(&u);
}

#define TWBR 0xB8
#define TWSR 0xB9
#define TWDR 0xBB
#define TWCR 0xBC

static void test_twi(void)
{
    const u16 prog[] = { NOP, rjmp(-2) };
    uno u;
    u64 t;
    int i;
    if (!uno_open(&u, prog, sizeof prog / sizeof prog[0]))
        return;
    esk_mach_run(&u.m, 4, 0);
    CHECK_EQ(data(&u, TWSR), 0xF8);
    poke(&u, 0x28, 0x30); /* PORTC4, PORTC5: the pull-ups */
    poke(&u, TWBR, 72);   /* 100 kHz at 16 MHz */
    poke(&u, TWCR, 0x44); /* TWEN | TWEA */
    CHECK_EQ(u.pin_drive[2][4], 2);
    CHECK_EQ(u.pin_drive[2][5], 2);
    /* a START from a free bus */
    poke(&u, TWCR, 0xA4);
    poke(&u, TWDR, 0x12); /* TWINT clear: lost, TWWC */
    CHECK_EQ(data(&u, TWCR) & 0x08, 0x08);
    t = run_until(&u, TWCR, 0x80, 0, 1000);
    CHECK(t >= 160 && t <= 162);
    CHECK_EQ(data(&u, TWSR), 0x08);
    CHECK_EQ(u.pin_drive[2][5], 0); /* SCL held low */
    CHECK_EQ(u.pin_drive[2][4], 0);
    poke(&u, TWDR, 0xA0); /* TWINT set: TWWC clears */
    CHECK_EQ(data(&u, TWCR) & 0x08, 0);
    /* SLA+W to nobody: NACKed after nine SCL periods */
    u.nscl = 0;
    poke(&u, TWCR, 0x84);
    CHECK_EQ(data(&u, TWCR) & 0x80, 0);
    t = run_until(&u, TWCR, 0x80, 0, 4000);
    CHECK(t >= 1440 && t <= 1442);
    CHECK_EQ(data(&u, TWSR), 0x20);
    CHECK_EQ(data(&u, TWDR), 0xA0);
    CHECK_EQ(u.nscl, 18);
    for (i = 2; i + 2 < u.nscl; i += 2) /* a period of 160 cycles, rise to rise */
        CHECK(u.scl_at[i + 1] - u.scl_at[i - 1] >= 159 && u.scl_at[i + 1] - u.scl_at[i - 1] <= 161);
    /* the high half, give or take the cycle an instruction ran past an edge */
    CHECK(u.scl_at[1] - u.scl_at[0] >= 78 && u.scl_at[1] - u.scl_at[0] <= 82);
    /* a slave that pulls SDA low: a repeated START, SLA+R ACKed, a byte
     * ACKed, one NACKed */
    u.sda_low = 1;
    poke(&u, TWCR, 0xA4);
    run_until(&u, TWCR, 0x80, 0, 4000);
    CHECK_EQ(data(&u, TWSR), 0x10);
    poke(&u, TWDR, 0xA1);
    poke(&u, TWCR, 0x84);
    run_until(&u, TWCR, 0x80, 0, 4000);
    CHECK_EQ(data(&u, TWSR), 0x40);
    poke(&u, TWCR, 0xC4);
    run_until(&u, TWCR, 0x80, 0, 4000);
    CHECK_EQ(data(&u, TWSR), 0x50);
    CHECK_EQ(data(&u, TWDR), 0x00);
    poke(&u, TWCR, 0x84);
    run_until(&u, TWCR, 0x80, 0, 4000);
    CHECK_EQ(data(&u, TWSR), 0x58);
    /* the STOP: TWSTO holds until it is made, TWINT stays clear */
    u.sda_low = 0;
    poke(&u, TWCR, 0x94);
    CHECK_EQ(data(&u, TWCR) & 0x10, 0x10);
    t = run_until(&u, TWCR, 0x10, 1, 4000);
    CHECK(t >= 160 && t <= 162);
    CHECK_EQ(data(&u, TWCR) & 0x80, 0);
    CHECK_EQ(data(&u, TWSR), 0xF8);
    CHECK_EQ(u.pin_drive[2][4], 2);
    CHECK_EQ(u.pin_drive[2][5], 2);
    /* the prescaler: 16 + 2 * 72 * 4 cycles a period */
    poke(&u, TWSR, 0x01);
    CHECK_EQ(data(&u, TWSR), 0xF9);
    poke(&u, TWCR, 0xA4);
    t = run_until(&u, TWCR, 0x80, 0, 4000);
    CHECK(t >= 592 && t <= 594); /* the bus free, then the START: two high halves */
    /* TWEN off: the lines go */
    poke(&u, TWCR, 0x00);
    CHECK_EQ(u.pin_drive[2][4], 2); /* PORTC's pull-up, as GPIO */
    CHECK_EQ(data(&u, TWSR), 0xF9);
    uno_close(&u);
}

#define WDTCSR 0x60
#define MCUSR 0x54
#define WDR 0x95A8

/* run until the watchdog asks for a reset: the cycles it took */
static u64 run_to_reset(uno *u, u64 limit)
{
    u64 start = u->m.cycles;
    while (!u->m.reset_req && u->m.cycles - start < limit)
        esk_mach_run(&u->m, (u32)(limit - (u->m.cycles - start)), 0);
    return u->m.cycles - start;
}

static void test_wdt(void)
{
    /* vector 6 (WDT, word 12) increments r24 and returns */
    u16 prog[64];
    uno u;
    u64 t;
    memset(prog, 0, sizeof prog);
    prog[0] = rjmp(39);
    prog[12] = 0x9583; /* inc r24 */
    prog[13] = RETI;
    prog[40] = NOP;
    prog[41] = rjmp(-2);
    if (!uno_open(&u, prog, 42))
        return;
    CHECK_EQ(data(&u, WDTCSR), 0);
    CHECK_EQ(data(&u, MCUSR), 0x01); /* PORF */
    /* interrupt mode, 16 ms: 2048 cycles of 128 kHz, 256,000 at 16 MHz */
    poke(&u, WDTCSR, 0x40);
    t = run_until(&u, WDTCSR, 0x80, 0, 300000);
    CHECK(t >= 256000 && t <= 256002);
    CHECK_EQ(reg(&u, 24), 0); /* I clear: not taken */
    poke(&u, WDTCSR, 0xC0);   /* WDIF cleared by a 1 */
    CHECK_EQ(data(&u, WDTCSR), 0x40);
    /* WDP changes only through the timed sequence; WDE can be set at any time */
    poke(&u, WDTCSR, 0x46);
    CHECK_EQ(data(&u, WDTCSR), 0x40);
    poke(&u, WDTCSR, 0x18);
    CHECK_EQ(data(&u, WDTCSR), 0x18); /* WDIE written 0 with it */
    poke(&u, WDTCSR, 0x00); /* within the window: off */
    CHECK_EQ(data(&u, WDTCSR), 0x00);
    poke(&u, WDTCSR, 0x08);
    CHECK_EQ(data(&u, WDTCSR), 0x08);
    poke(&u, WDTCSR, 0x00); /* without the sequence: WDE stays */
    CHECK_EQ(data(&u, WDTCSR), 0x08);
    poke(&u, WDTCSR, 0x18);
    esk_mach_run(&u.m, 8, 0); /* the window closes after four cycles */
    poke(&u, WDTCSR, 0x00);
    CHECK_EQ(data(&u, WDTCSR), 0x08);
    /* system reset mode: the count started when WDE was set */
    t = run_to_reset(&u, 600000);
    CHECK(u.m.reset_req);
    CHECK(t <= 256010);
    esk_mach_reset(&u.m);
    CHECK_EQ(data(&u, MCUSR), 0x09);  /* WDRF with PORF kept */
    CHECK_EQ(data(&u, WDTCSR), 0x08); /* still on, 16 ms */
    /* WDRF holds WDE even through the sequence */
    poke(&u, WDTCSR, 0x18);
    poke(&u, WDTCSR, 0x00);
    CHECK_EQ(data(&u, WDTCSR), 0x08);
    t = run_to_reset(&u, 600000);
    CHECK(t >= 255990 && t <= 256010);
    esk_mach_reset(&u.m);
    /* cleared WDRF, then the sequence: off for good */
    poke(&u, MCUSR, 0x00);
    poke(&u, WDTCSR, 0x18);
    poke(&u, WDTCSR, 0x00);
    CHECK_EQ(data(&u, WDTCSR), 0x00);
    t = run_to_reset(&u, 600000);
    CHECK(!u.m.reset_req);
    /* interrupt and system reset mode, I set: the first time-out runs the
     * vector, which clears WDIE; the second resets */
    poke(&u, WDTCSR, 0x18);
    poke(&u, WDTCSR, 0x48);
    CHECK_EQ(data(&u, WDTCSR), 0x48);
    u.m.cpu->ops->reg_write(u.m.cpu, 32, (const u8[]){ 0x80, 0, 0, 0 });
    t = run_to_reset(&u, 1000000);
    CHECK(u.m.reset_req);
    CHECK(t >= 511900 && t <= 512100);
    CHECK_EQ(reg(&u, 24), 1);
    CHECK_EQ(data(&u, WDTCSR) & 0xC0, 0);
    uno_close(&u);
    /* WDR keeps it from timing out: a loop of 3 * 255 + 4 cycles */
    {
        const u16 kick[] = { 0x2722 /* clr r18 */, dec(18), brne(-2), WDR, rjmp(-5) };
        if (!uno_open(&u, kick, sizeof kick / sizeof kick[0]))
            return;
        poke(&u, WDTCSR, 0x08);
        t = run_to_reset(&u, 2000000);
        CHECK(!u.m.reset_req);
        CHECK(t >= 2000000);
        uno_close(&u);
    }
}

#define ACSR 0x50
#define ADCSRA 0x7A
#define ADCSRB 0x7B
#define ADMUX 0x7C

static void test_ac(void)
{
    /* vector 23 (ANALOG_COMP, word 46) increments r24 and returns */
    u16 prog[64];
    uno u;
    memset(prog, 0, sizeof prog);
    prog[0] = rjmp(59);
    prog[46] = 0x9583; /* inc r24 */
    prog[47] = RETI;
    prog[60] = NOP;
    prog[61] = rjmp(-2);
    if (!uno_open(&u, prog, 62))
        return;
    u.use_pv = 1;
    /* out of reset it is on, AIN0 (PD6) against AIN1 (PD7), both 0 V */
    CHECK_EQ(data(&u, ACSR), 0);
    u.pv[3][6] = 2.0;
    u.pv[3][7] = 1.0;
    esk_avr_pins_changed(&u.m);
    CHECK_EQ(data(&u, ACSR), 0x30); /* ACO, and ACI on the toggle */
    /* each input's threshold sits at the other's voltage */
    CHECK(u.watch_on[3][6] && u.watch_v[3][6] == 1.0);
    CHECK(u.watch_on[3][7] && u.watch_v[3][7] == 2.0);
    poke(&u, ACSR, 0x10); /* ACI cleared by a 1 */
    CHECK_EQ(data(&u, ACSR), 0x20);
    /* rising edges only, with the interrupt */
    poke(&u, ACSR, 0x0B);
    u.pv[3][6] = 0.5;
    esk_avr_pins_changed(&u.m);
    CHECK_EQ(data(&u, ACSR), 0x0B); /* ACO falls, no ACI */
    CHECK(u.watch_v[3][7] == 0.5);
    u.m.cpu->ops->reg_write(u.m.cpu, 32, (const u8[]){ 0x80, 0, 0, 0 });
    u.pv[3][6] = 1.5;
    esk_avr_pins_changed(&u.m);
    esk_mach_run(&u.m, 40, 0);
    CHECK_EQ(reg(&u, 24), 1);              /* the vector ran */
    CHECK_EQ(data(&u, ACSR) & 0x30, 0x20); /* entering it cleared ACI */
    /* the bandgap in place of AIN0: no threshold on PD6, PD7's at 1.1 V */
    poke(&u, ACSR, 0x40);
    CHECK_EQ(data(&u, ACSR) & 0x20, 0x20); /* 1.1 V > 1.0 V */
    CHECK(!u.watch_on[3][6]);
    CHECK(u.watch_on[3][7] && u.watch_v[3][7] == 1.1);
    /* ACME with the ADC off: the multiplexer's ADC3 (PC3) is the negative input */
    u.pv[2][3] = 1.5;
    poke(&u, ADMUX, 0x03);
    poke(&u, ADCSRB, 0x40);
    CHECK_EQ(data(&u, ACSR) & 0x20, 0); /* 1.1 V < 1.5 V */
    CHECK(!u.watch_on[3][7]);
    CHECK(u.watch_on[2][3] && u.watch_v[2][3] == 1.1);
    poke(&u, ADCSRA, 0x80); /* the ADC on: back to AIN1 */
    CHECK_EQ(data(&u, ACSR) & 0x20, 0x20);
    CHECK(!u.watch_on[2][3] && u.watch_on[3][7]);
    /* ACD switches it off */
    poke(&u, ACSR, 0x80);
    CHECK_EQ(data(&u, ACSR) & 0x20, 0);
    CHECK(!u.watch_on[3][6] && !u.watch_on[3][7]);
    uno_close(&u);
}

#define TCCR1B 0x81
#define TCNT1L 0x84
#define ICR1L 0x86
#define TIFR1 0x36
#define TIMSK1 0x6F

/* PB0 (ICP1) driven to `level` from outside, as the runtime reports it */
static void icp1(uno *u, int level)
{
    u->ext[1][0] = 1 + level;
    esk_avr_pins_changed(&u->m);
}

static u16 tc1(uno *u, u32 low)
{
    u8 lo = peek(u, low); /* the low byte first: TEMP latches the high one */
    return (u16)(peek(u, low + 1) << 8 | lo);
}

static void test_input_capture(void)
{
    /* vector 10 (TIMER1_CAPT, word 20) increments r24 and returns */
    u16 prog[64];
    uno u;
    u16 cnt;
    memset(prog, 0, sizeof prog);
    prog[0] = rjmp(39);
    prog[20] = 0x9583; /* inc r24 */
    prog[21] = RETI;
    prog[40] = NOP;
    prog[41] = rjmp(-2);
    if (!uno_open(&u, prog, 42))
        return;
    icp1(&u, 0);
    poke(&u, TCCR1B, 0x41); /* ICES1 (rising), clk/1 */
    esk_mach_run(&u.m, 100, 0);
    cnt = tc1(&u, TCNT1L);
    icp1(&u, 1);
    CHECK_EQ(data(&u, TIFR1) & 0x20, 0x20); /* ICF1 */
    CHECK_EQ(tc1(&u, ICR1L), cnt);          /* TCNT1 at the edge */
    poke(&u, TIFR1, 0x20);
    CHECK_EQ(data(&u, TIFR1) & 0x20, 0);
    esk_mach_run(&u.m, 50, 0);
    icp1(&u, 0); /* a falling edge: not the one ICES1 asks for */
    CHECK_EQ(data(&u, TIFR1) & 0x20, 0);
    CHECK_EQ(tc1(&u, ICR1L), cnt);
    poke(&u, TCCR1B, 0x01); /* falling edges */
    icp1(&u, 1);
    esk_mach_run(&u.m, 30, 0);
    cnt = tc1(&u, TCNT1L);
    icp1(&u, 0);
    CHECK_EQ(data(&u, TIFR1) & 0x20, 0x20);
    CHECK_EQ(tc1(&u, ICR1L), cnt);
    poke(&u, TIFR1, 0x20);
    /* the noise canceler: a pulse under four cycles is ignored; one that
     * holds is captured four cycles after its edge */
    poke(&u, TCCR1B, 0xC1);
    icp1(&u, 1);
    esk_mach_run(&u.m, 2, 0);
    icp1(&u, 0);
    esk_mach_run(&u.m, 20, 0);
    CHECK_EQ(data(&u, TIFR1) & 0x20, 0);
    cnt = tc1(&u, TCNT1L);
    icp1(&u, 1);
    esk_mach_run(&u.m, 20, 0);
    CHECK_EQ(data(&u, TIFR1) & 0x20, 0x20);
    CHECK_EQ(tc1(&u, ICR1L), (u16)(cnt + 4));
    poke(&u, TIFR1, 0x20);
    icp1(&u, 0);
    /* ICR1 as TOP (CTC, WGM 12): no capture, and ICR1 takes writes */
    poke(&u, TCCR1B, 0x59);
    poke(&u, ICR1L + 1, 0x12);
    poke(&u, ICR1L, 0x34);
    CHECK_EQ(tc1(&u, ICR1L), 0x1234);
    icp1(&u, 1);
    CHECK_EQ(tc1(&u, ICR1L), 0x1234);
    poke(&u, TCCR1B, 0x41); /* back to normal mode: ICR1 no longer takes writes */
    poke(&u, ICR1L + 1, 0x00);
    poke(&u, ICR1L, 0x00);
    CHECK_EQ(tc1(&u, ICR1L), 0x1234);
    poke(&u, TIFR1, 0x20);
    icp1(&u, 0);
    /* the comparator as the source (ACIC): its rising output captures, ICP1 does not */
    u.use_pv = 1;
    u.pv[3][7] = 2.0;
    poke(&u, ACSR, 0x04);
    esk_mach_run(&u.m, 10, 0);
    icp1(&u, 1);
    CHECK_EQ(data(&u, TIFR1) & 0x20, 0);
    cnt = tc1(&u, TCNT1L);
    u.pv[3][6] = 3.0;
    esk_avr_pins_changed(&u.m);
    CHECK_EQ(data(&u, TIFR1) & 0x20, 0x20);
    CHECK_EQ(tc1(&u, ICR1L), cnt);
    /* its vector with ICIE1 and I set; entering it clears ICF1 */
    poke(&u, TIMSK1, 0x20);
    u.m.cpu->ops->reg_write(u.m.cpu, 32, (const u8[]){ 0x80, 0, 0, 0 });
    esk_mach_run(&u.m, 40, 0);
    CHECK_EQ(reg(&u, 24), 1);
    CHECK_EQ(data(&u, TIFR1) & 0x20, 0);
    uno_close(&u);
}

static void test_disassembler(void)
{
    char t[64];
    CHECK_EQ(esk_avr_disasm(0, ldi(16, 0x2A), 0, t, sizeof t), 2);
    CHECK_STR(t, "ldi r16, 0x2A");
    CHECK_EQ(esk_avr_disasm(0, 0x940E, 0x0034, t, sizeof t), 4);
    CHECK_STR(t, "call 0x68");
    CHECK_EQ(esk_avr_disasm(0x10, out(0x25, 16), 0, t, sizeof t), 2);
    CHECK_STR(t, "out 0x25, r16");
    esk_avr_disasm(0x10, brne(-2), 0, t, sizeof t);
    CHECK_STR(t, "brne .-4 ; 0xe");
    esk_avr_disasm(0, RETI, 0, t, sizeof t);
    CHECK_STR(t, "reti");
}

TEST_MAIN_BEGIN
RUN(test_flags_and_cycles);
RUN(test_timer0_interrupt);
RUN(test_timer0_fast_pwm_overflow);
RUN(test_usart_frame);
RUN(test_ports);
RUN(test_adc);
RUN(test_spi);
RUN(test_twi);
RUN(test_wdt);
RUN(test_ac);
RUN(test_input_capture);
RUN(test_disassembler);
TEST_MAIN_END
