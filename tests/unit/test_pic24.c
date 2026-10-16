/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* test_pic24.c -- the PIC24 core and the PIC24FJ1024GB610's models (SPEC-009
 * §2, §6) on the machine the catalog's description builds: instructions and
 * their cycles (REPEAT and the iterative divide), program memory's layout,
 * an address error trap, an interrupt taken from PWRSAV and returned from,
 * the oscillator from the configuration words and OSCCON's unlock, Timer1,
 * the interrupt-on-change, UART2 on the pins the PPS gives it, the
 * disassembler; and the Explorer 16/32 example's XC16 image with its line
 * table. The programs'
 * words are as xc16-as v2.10 assembles them. */
#include "harness.h"
#include "mcu/build.h"
#include "mcu/dwarf.h"
#include "mcu/pic24/pic24.h"
#include "util/json.h"

/* ---- the machine -------------------------------------------------------------- */

typedef struct pt {
    esk_alloc a;
    esk_json *mcu, *regs;
    struct esk_mach m;
    char tx[256];
    size_t ntx;
    u32 tx_bits;
    char unit[16];
    int level[8][16]; /* the board's levels: -1 none */
    esk_mach_pin cfg[8][16];
    int traps;
    char last_code[32];
} pt;

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
    pt *t = (pt *)ctx;
    t->tx_bits = bit_cycles;
    snprintf(t->unit, sizeof t->unit, "%s", unit);
    if (t->ntx + 1 < sizeof t->tx)
        t->tx[t->ntx++] = (char)c;
}

static void p_diag(void *ctx, int sev, const char *code, const char *msg)
{
    pt *t = (pt *)ctx;
    ES_UNUSED(sev);
    ES_UNUSED(msg);
    snprintf(t->last_code, sizeof t->last_code, "%s", code);
    if (!strcmp(code, "mcu.trap"))
        t->traps++;
}

static void p_cfg(void *ctx, int port, int bit, const esk_mach_pin *cfg)
{
    pt *t = (pt *)ctx;
    if (port >= 0 && port < 8 && bit >= 0 && bit < 16)
        t->cfg[port][bit] = *cfg;
}

static int p_in(void *ctx, int port, int bit, int *level)
{
    pt *t = (pt *)ctx;
    if (port < 0 || port >= 8 || bit < 0 || bit >= 16 || t->level[port][bit] < 0)
        return 0;
    *level = t->level[port][bit];
    return 1;
}

/* a word of program memory at program address p */
static void prog(pt *t, u32 p, u32 w)
{
    CHECK_EQ(esk_bus_debug_write(&t->m.bus, 2 * p, 4, w & 0xffffffu), 0);
}

#define MAIN 0x200u
#define FOSCSEL 0xabf18u

/* the part with the 8 MHz crystal; at reset a GOTO to `code` (n words at
 * 0x200); FOSCSEL's word `foscsel`, or 0 for the erased one */
static int pt_open(pt *t, const u32 *code, size_t n, u32 foscsel)
{
    char why[200];
    es_status st;
    size_t i;
    memset(t, 0, sizeof *t);
    memset(t->level, -1, sizeof t->level);
    esk_alloc_init(&t->a, NULL);
    t->mcu = load_json(&t->a, "catalog/mcus/microchip/pic24fj1024gb610/mcu.json");
    t->regs = load_json(&t->a, "catalog/mcus/microchip/pic24fj1024gb610/registers.json");
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
    if (code) {
        prog(t, 0, 0x040000u | MAIN); /* goto MAIN */
        prog(t, 2, 0);
        for (i = 0; i < n; i++)
            prog(t, MAIN + 2u * (u32)i, code[i]);
    }
    if (foscsel)
        prog(t, FOSCSEL, foscsel);
    esk_mach_reset(&t->m);
    return 1;
}

static void pt_close(pt *t)
{
    esk_mach_free(&t->m);
    esk_json_free(&t->a, t->mcu);
    esk_json_free(&t->a, t->regs);
    CHECK_EQ(t->a.live_bytes, 0);
}

static u32 reg(pt *t, int n)
{
    u8 b[4] = { 0 };
    t->m.cpu->ops->reg_read(t->m.cpu, n, b);
    return (u32)b[0] | (u32)b[1] << 8 | (u32)b[2] << 16 | (u32)b[3] << 24;
}

/* a register's data address, by the description */
static u32 A(pt *t, const char *periph, const char *name)
{
    struct esk_rf_periph *p = esk_rf_periph(t->m.rf, periph);
    struct esk_rf_reg *r = p ? esk_rf_reg(p, name) : 0;
    CHECK(r != NULL);
    return r ? r->addr - PIC24_DATA : 0;
}

/* the program's own access, a word at data address ea */
static void wr(pt *t, u32 ea, u32 v) { CHECK_EQ(esk_bus_write(&t->m.bus, PIC24_DATA + ea, 2, v), 0); }
static void wrb(pt *t, u32 ea, u32 v) { CHECK_EQ(esk_bus_write(&t->m.bus, PIC24_DATA + ea, 1, v), 0); }
static u32 ld(pt *t, u32 ea)
{
    u32 v = 0;
    CHECK_EQ(esk_bus_read(&t->m.bus, PIC24_DATA + ea, 2, &v), 0);
    return v & 0xffffu;
}

/* encodings */
#define MOV_LIT(k, d) (0x200000u | ((u32)(k) & 0xffffu) << 4 | (u32)(d))
#define MOV_WF(s, f) (0x880000u | (u32)(f) << 3 | (u32)(s)) /* mov Ws, f (f even) */
#define MOV_FW(f, d) (0x800000u | (u32)(f) << 3 | (u32)(d)) /* mov f, Wd */
#define BRA(n) (0x370000u | ((u32)(n) & 0xffffu))         /* to PC + 2 + 2n */
#define NOP 0x000000u
#define INC_W1 0xe80081u
#define PWRSAV1 0xfe4001u
#define RETFIE 0x064000u

/* ---- the core ---------------------------------------------------------------- */

static void test_instructions(void)
{
    /* the words xc16-as gives: mov #0x1234, w0; mov #10, w1; add w0, w1,
     * w2; mov w2, 0x900; mov 0x900, w3; mov #100, w4; mov #7, w5; repeat
     * #17; div.u w4, w5; bra . */
    static const u32 code[] = { 0x212340u, 0x2000a1u, 0x400101u, 0x884802u, 0x804803u, 0x200644u,
                                0x200075u, 0x090011u, 0xd88205u, 0x37ffffu };
    pt t;
    char text[64];
    /* the encoders against the words above (held in locals: MSVC's C4127
     * takes a comparison of two constants for a mistake) */
    u32 enc[3];
    enc[0] = MOV_LIT(0x1234, 0);
    enc[1] = MOV_WF(2, 0x900);
    enc[2] = MOV_FW(0x900, 3);
    CHECK_EQ(enc[0], 0x212340u);
    CHECK_EQ(enc[1], 0x884802u);
    CHECK_EQ(enc[2], 0x804803u);
    if (!pt_open(&t, code, sizeof code / sizeof code[0], 0))
        return;
    CHECK_EQ(reg(&t, 17), 0u);     /* the PC at the reset GOTO */
    CHECK_EQ(reg(&t, 15), 0x800u); /* W15 at the RAM's start */
    esk_mach_run(&t.m, 1000, 0);
    CHECK_EQ(t.m.state, ESK_MACH_RUN);
    CHECK_EQ(reg(&t, 2), 0x123eu);
    CHECK_EQ(reg(&t, 3), 0x123eu);
    CHECK_EQ(ld(&t, 0x900), 0x123eu);
    CHECK_EQ(reg(&t, 0), 14u); /* 100 / 7 */
    CHECK_EQ(reg(&t, 1), 2u);
    CHECK_EQ(reg(&t, 17), MAIN + 18u);
    /* GOTO 2, seven instructions, REPEAT, the divide's 18 passes, the
     * branch to itself taken (2): then the core waits */
    CHECK_EQ(t.m.cycles, 30u);
    CHECK(t.m.asleep);
    /* program memory: the word at 2P, its phantom byte 0 */
    CHECK_EQ(esk_pic24_prog(&t.m, MAIN), 0x212340u);
    CHECK_EQ(esk_pic24_prog(&t.m, 0x800u), 0xffffffu); /* erased */
    CHECK_EQ(t.m.cpu->ops->disasm(t.m.cpu, MAIN, text, sizeof text), 2);
    CHECK(strstr(text, "mov") != NULL && strstr(text, "w0") != NULL);
    CHECK_EQ(t.m.cpu->ops->disasm(t.m.cpu, 0, text, sizeof text), 4); /* goto: two words */
    CHECK(strstr(text, "goto") != NULL);
    CHECK_STR(t.m.cpu->ops->reg_name(t.m.cpu, 18), "SR");
    pt_close(&t);
}

static void test_address_trap(void)
{
    /* mov #0x901, w6; mov [w6], w7: a word read at an odd address; the
     * trap's vector (0x000006) holds 0x300, where the handler waits */
    static const u32 code[] = { 0x209016u, 0x780396u, 0x37ffffu };
    pt t;
    if (!pt_open(&t, code, 3, 0))
        return;
    prog(&t, 0x6, 0x300u);
    prog(&t, 0x300, 0x37ffffu);
    esk_mach_run(&t.m, 1000, 0);
    CHECK_EQ(reg(&t, 17), 0x300u);
    CHECK(ld(&t, A(&t, "INT", "INTCON1")) & 0x8u); /* ADDRERR */
    CHECK_EQ(t.traps, 1);
    CHECK_EQ(reg(&t, 18) >> 5 & 7u, 5u); /* SR.IPL: the trap's priority 13, IPL3 set */
    pt_close(&t);
}

static void test_interrupt_pwrsav(void)
{
    /* Timer1 every 100 cycles at priority 1; main: pwrsav #1, then back to
     * it; the handler (0x300): inc w1, w1; bclr.b IFS0, #3; retfie */
    static const u32 code[] = { PWRSAV1, BRA(-2) };
    pt t;
    u32 ifs0;
    if (!pt_open(&t, code, 2, 0))
        return;
    ifs0 = A(&t, "INT", "IFS0");
    prog(&t, 0x14u + 2u * 3u, 0x300u); /* T1's vector */
    prog(&t, 0x300, INC_W1);
    prog(&t, 0x302, 0xa96000u | ifs0); /* bclr.b IFS0, #3 */
    prog(&t, 0x304, RETFIE);
    wr(&t, A(&t, "INT", "IPC0"), 1u << 12);
    wr(&t, A(&t, "INT", "IEC0"), 1u << 3);
    wr(&t, A(&t, "TMR", "PR1"), 99u);
    wr(&t, A(&t, "TMR", "T1CON"), 1u << 15);
    esk_mach_run(&t.m, 1050, 0);
    CHECK_EQ(t.m.state, ESK_MACH_RUN);
    CHECK_EQ(reg(&t, 1), 10u);
    CHECK_EQ(ld(&t, ifs0) & 0x8u, 0u);
    CHECK_EQ(ld(&t, A(&t, "INT", "INTTREG")), 1u << 8 | 11u); /* ILR 1, VECNUM 3 + 8 */
    CHECK(t.m.asleep);
    CHECK_EQ(reg(&t, 17), MAIN + 2u); /* after PWRSAV, asleep again */
    CHECK_EQ(reg(&t, 18) >> 5 & 7u, 0u);
    pt_close(&t);
}

/* ---- the models ---------------------------------------------------------------- */

static void test_oscillator(void)
{
    static const u32 idle[] = { 0x37ffffu };
    pt t;
    u32 osccon, o;
    /* erased configuration words: FNOSC 111, the FRC divided by RCDIV
     * (CLKDIV's reset 0x3000: by 1): 8 MHz, FCY 4 MHz */
    if (!pt_open(&t, idle, 1, 0))
        return;
    CHECK_EQ(esk_mach_core_hz(&t.m), 4000000u);
    pt_close(&t);
    /* FNOSC 011: the primary oscillator with the PLL, 32 MHz: FCY 16 MHz */
    if (!pt_open(&t, idle, 1, 0xffff0bu))
        return;
    osccon = A(&t, "OSC", "OSCCON");
    o = ld(&t, osccon);
    CHECK_EQ(o >> 12 & 7u, 3u); /* COSC */
    CHECK_EQ(o >> 8 & 7u, 3u);  /* NOSC */
    CHECK(o & 0x20u);           /* LOCK */
    CHECK_EQ(esk_mach_core_hz(&t.m), 16000000u);
    /* CPDIV 01: 16 MHz */
    wr(&t, A(&t, "OSC", "CLKDIV"), 0x3040u);
    CHECK_EQ(esk_mach_core_hz(&t.m), 8000000u);
    wr(&t, A(&t, "OSC", "CLKDIV"), 0x3000u);
    /* without the unlock sequence the switch to the FRC is ignored */
    wrb(&t, osccon + 1, 0x00);
    wrb(&t, osccon, 0x01);
    CHECK_EQ(esk_mach_core_hz(&t.m), 16000000u);
    CHECK_STR(t.last_code, "mcu.locked_write");
    /* with it, it switches at once */
    t.m.yield = 0;
    wrb(&t, osccon + 1, 0x78);
    wrb(&t, osccon + 1, 0x9a);
    wrb(&t, osccon + 1, 0x00); /* NOSC 000 */
    wrb(&t, osccon, 0x46);
    wrb(&t, osccon, 0x57);
    wrb(&t, osccon, 0x01); /* OSWEN */
    o = ld(&t, osccon);
    CHECK_EQ(o >> 12 & 7u, 0u);
    CHECK_EQ(o & 1u, 0u);
    CHECK_EQ(esk_mach_core_hz(&t.m), 4000000u);
    CHECK(t.m.yield);
    pt_close(&t);
}

static void test_timer1(void)
{
    /* PR1 99 at 1:1: the flag 100 instruction cycles on; the core spins
     * (inc w1, w1; bra back) */
    static const u32 spin[] = { INC_W1, BRA(-2) };
    pt t;
    u32 ifs0;
    if (!pt_open(&t, spin, 2, 0))
        return;
    ifs0 = A(&t, "INT", "IFS0");
    wr(&t, A(&t, "TMR", "PR1"), 99u);
    wr(&t, A(&t, "TMR", "T1CON"), 1u << 15);
    esk_mach_run(&t.m, 95, 0);
    CHECK_EQ(ld(&t, ifs0) & 0x8u, 0u);
    esk_mach_run(&t.m, 10, 0);
    CHECK_EQ(ld(&t, ifs0) & 0x8u, 0x8u);
    /* 1:256 */
    wr(&t, ifs0, 0);
    wr(&t, A(&t, "TMR", "TMR1"), 0);
    wr(&t, A(&t, "TMR", "T1CON"), 1u << 15 | 3u << 4);
    esk_mach_run(&t.m, 256u * 99u, 0);
    CHECK_EQ(ld(&t, ifs0) & 0x8u, 0u);
    esk_mach_run(&t.m, 260u, 0);
    CHECK_EQ(ld(&t, ifs0) & 0x8u, 0x8u);
    pt_close(&t);
}

static void test_interrupt_on_change(void)
{
    /* RD6 with its pull-up; a falling edge from the board flags IOCFD6,
     * IOCSTAT's port D and the IOC interrupt (19: IFS1 bit 3) */
    static const u32 idle[] = { 0x37ffffu };
    pt t;
    u32 ifs1, iocfd;
    if (!pt_open(&t, idle, 1, 0))
        return;
    ifs1 = A(&t, "INT", "IFS1");
    iocfd = A(&t, "GPIO", "IOCFD");
    wr(&t, A(&t, "GPIO", "ANSD"), 0);
    wr(&t, A(&t, "GPIO", "IOCPUD"), 1u << 6);
    CHECK_EQ(ld(&t, A(&t, "GPIO", "PORTD")) >> 6 & 1u, 1u);
    CHECK_EQ(t.cfg[3][6].pull, 1);
    wr(&t, A(&t, "PAD", "PADCON"), 1u << 15);
    wr(&t, A(&t, "GPIO", "IOCND"), 1u << 6);
    t.level[3][6] = 0;
    t.m.pins_changed(&t.m);
    CHECK_EQ(ld(&t, A(&t, "GPIO", "PORTD")) >> 6 & 1u, 0u);
    CHECK_EQ(ld(&t, iocfd), 1u << 6);
    CHECK_EQ(ld(&t, A(&t, "PAD", "IOCSTAT")), 1u << 3);
    CHECK_EQ(ld(&t, ifs1) & 0x8u, 0x8u);
    /* the program cannot set IOCFx bits, only clear them */
    wr(&t, iocfd, 0xffffu);
    CHECK_EQ(ld(&t, iocfd), 1u << 6);
    wr(&t, iocfd, 0);
    CHECK_EQ(ld(&t, iocfd), 0u);
    CHECK_EQ(ld(&t, A(&t, "PAD", "IOCSTAT")), 0u);
    /* a rising edge with only IOCND set flags nothing */
    t.level[3][6] = 1;
    t.m.pins_changed(&t.m);
    CHECK_EQ(ld(&t, iocfd), 0u);
    /* an analog pin's digital input reads 0 */
    wr(&t, A(&t, "GPIO", "ANSD"), 1u << 6);
    CHECK_EQ(ld(&t, A(&t, "GPIO", "PORTD")) >> 6 & 1u, 0u);
    pt_close(&t);
}

static void test_uart2_pps(void)
{
    /* the example's mapping: U2TX (5) on RP17 (RF5), U2RX from RP10 (RF4);
     * BRGH, U2BRG 34: 4 x 35 instruction cycles a bit */
    static const u32 spin[] = { INC_W1, BRA(-2) };
    pt t;
    u32 sta, ifs1;
    struct esk_rx_port *rx;
    if (!pt_open(&t, spin, 2, 0))
        return;
    sta = A(&t, "UART2", "U2STA");
    ifs1 = A(&t, "INT", "IFS1");
    CHECK_EQ(esk_pic24_rp_pin(&t.m, 17), 5 * 32 + 5);
    CHECK_EQ(esk_pic24_rp_pin(&t.m, 10), 5 * 32 + 4);
    wr(&t, A(&t, "UART2", "U2BRG"), 34u);
    wr(&t, A(&t, "UART2", "U2MODE"), 1u << 15 | 1u << 3);
    wr(&t, sta, 1u << 12 | 1u << 10);
    CHECK_EQ(t.cfg[5][5].af, 0); /* no pin yet: the PPS maps none */
    wr(&t, A(&t, "PPS", "RPOR8"), 5u << 8);
    wr(&t, A(&t, "PPS", "RPINR19"), 0x3f00u | 10u);
    CHECK_EQ(esk_pic24_pps_out_rp(&t.m, 5), 17);
    CHECK_EQ(esk_pic24_pps_in_rp(&t.m, "U2RXR"), 10);
    CHECK_EQ(t.cfg[5][5].drive, 1); /* RF5: U2TX, idle high */
    CHECK_EQ(t.cfg[5][5].af, 1);
    CHECK_EQ(t.cfg[5][4].af, 1); /* RF4: U2RX */
    {
        int port = -1, bit = -1;
        CHECK_EQ(t.m.uart_pin(&t.m, "UART2", 1, &port, &bit), 0);
        CHECK_EQ(port * 32 + bit, 5 * 32 + 5);
    }
    CHECK(ld(&t, ifs1) & (1u << 15)); /* U2TX: enabling the transmitter */
    wr(&t, A(&t, "UART2", "U2TXREG"), 'Z');
    CHECK_EQ(t.ntx, 1u);
    CHECK_EQ(t.tx[0], 'Z');
    CHECK_STR(t.unit, "UART2");
    CHECK_EQ(t.tx_bits, 140u);
    CHECK_EQ(ld(&t, sta) & 0x100u, 0u); /* TRMT: sending */
    esk_mach_run(&t.m, 10u * 140u + 10u, 0);
    CHECK_EQ(ld(&t, sta) & 0x100u, 0x100u);
    /* the receiver: a byte from the host's input */
    rx = esk_mach_rx_find(&t.m, "UART2");
    CHECK(rx != NULL);
    if (rx) {
        CHECK(rx->room(rx->ctx));
        rx->put(rx->ctx, 'q');
        CHECK_EQ(ld(&t, sta) & 1u, 1u);       /* URXDA */
        CHECK(ld(&t, ifs1) & (1u << 14));     /* U2RX */
        CHECK_EQ(ld(&t, A(&t, "UART2", "U2RXREG")), (u32)'q');
        CHECK_EQ(ld(&t, sta) & 1u, 0u);
        CHECK_EQ(rx->bit_cycles(rx->ctx), 140u);
    }
    /* IOLOCK set: the PPS's registers take no write */
    {
        u32 osccon = A(&t, "OSC", "OSCCON");
        wrb(&t, osccon, 0x46);
        wrb(&t, osccon, 0x57);
        wrb(&t, osccon, 0x40);
        wr(&t, A(&t, "PPS", "RPOR8"), 0);
        CHECK_EQ(esk_pic24_pps_out_rp(&t.m, 5), 17);
    }
    pt_close(&t);
}

/* ---- the example's image ----------------------------------------------------- */

static void test_example_image(void)
{
    size_t len = 0;
    unsigned char *img = slurp("examples/explorer16-pic24/firmware/blink-serial.elf", &len);
    char why[200];
    u32 entry = 0, lata = 0, ea;
    int i, toggles = 0;
    pt t;
    CHECK(img != NULL);
    if (!img || !pt_open(&t, NULL, 0, 0)) {
        free(img);
        return;
    }
    CHECK_EQ(esk_mach_load_image(&t.m, img, len, &entry, why, sizeof why), ES_OK);
    CHECK_EQ(entry, 0x100u);
    {
        /* XC16's DWARF, its sections two bytes a byte: the T1 handler's line */
        esk_dwarf d;
        const char *file = NULL;
        u32 at = 0, line = 0;
        CHECK(esk_elf_symbol(img, len, "__T1Interrupt", &at));
        CHECK_EQ(esk_dwarf_load(&d, &t.a, img, len), ES_OK);
        CHECK(esk_dwarf_line_at(&d, at, &file, &line));
        CHECK(file && strstr(file, "main.c") != NULL);
        CHECK_EQ(line, 78u);
        esk_dwarf_free(&d);
    }
    esk_mach_reset(&t.m);
    CHECK_EQ(esk_mach_core_hz(&t.m), 16000000u); /* from its configuration words */
    ea = A(&t, "GPIO", "LATA");
    for (i = 0; i < 1200; i++) { /* 1.2 s */
        esk_mach_run(&t.m, 16000, 0);
        if ((ld(&t, ea) & 1u) != lata) {
            lata ^= 1u;
            toggles++;
        }
    }
    CHECK_EQ(t.m.state, ESK_MACH_RUN);
    CHECK(t.m.asleep); /* idle in main */
    CHECK(strstr(t.tx, "explorer16: PIC24FJ1024GB610, FOSC 32000 kHz, FCY 16000 kHz, FOSCSEL 0xff0b") != NULL);
    CHECK_EQ(toggles, 2); /* D3 at 0.5 s and 1 s */
    CHECK_EQ(t.traps, 0);
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
    pt_close(&t);
    free(img);
}

TEST_MAIN_BEGIN
RUN(test_instructions);
RUN(test_address_trap);
RUN(test_interrupt_pwrsav);
RUN(test_oscillator);
RUN(test_timer1);
RUN(test_interrupt_on_change);
RUN(test_uart2_pps);
RUN(test_example_image);
TEST_MAIN_END
