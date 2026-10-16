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
    if (port >= 0 && port < 8 && bit >= 0 && bit < 8)
        u->pin_drive[port][bit] = cfg->drive >= 0 ? cfg->drive : cfg->pull == 1 ? 2 : -1;
}

static int u_pin_volts(void *ctx, int port, int bit, double *v)
{
    uno *u = (uno *)ctx;
    (void)port;
    (void)bit;
    *v = u->volts;
    return 1;
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
    u->m.host.pin_volts = u_pin_volts;
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
RUN(test_disassembler);
TEST_MAIN_END
