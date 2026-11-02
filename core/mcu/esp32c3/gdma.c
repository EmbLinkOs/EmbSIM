/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* gdma.c -- the ESP32-C3's GDMA controller (TRM v1.4 chapter 2): three
 * transmit (OUT) and three receive (IN) channels, each connected by
 * PERI_OUT_SEL / PERI_IN_SEL to a peripheral (0 SPI2, 2 UHCI0, 3 I2S, 6
 * AES, 7 SHA, 8 ADC; 63 none), walking linked lists of three-word
 * descriptors in internal RAM, their interrupts on matrix sources 44-46.
 *
 *   - A descriptor: DW0 size (11:0), length (23:12), err_eof (28), suc_eof
 *     (30), owner (31); DW1 the buffer; DW2 the next descriptor, 0 for the
 *     last. OUTLINK_ADDR and INLINK_ADDR hold the first's address's low 20
 *     bits (in 0x3FC00000's megabyte); OUTLINK_START and INLINK_START load
 *     it, RESTART carries on from the last one's DW2 (it may have been
 *     appended to), STOP stops; they clear themselves. With CHECK_OWNER an
 *     owner bit of 0, and any buffer outside 0x3FC80000-0x3FCDFFFF, is a
 *     descriptor error (OUT_DSCR_ERR, IN_DSCR_ERR) that halts the channel.
 *   - A transmit channel gives its peripheral the bytes of each descriptor's
 *     length in turn: OUT_DONE after each, OUT_EOF after one with suc_eof
 *     (the peripheral sees the EOF with its last byte), OUT_TOTAL_EOF when
 *     that was the last of the list; OUT_EOF_DES_ADDR is that descriptor.
 *     With OUT_AUTO_WRBACK the owner bit is cleared after use.
 *   - A receive channel stores its peripheral's bytes into each
 *     descriptor's buffer up to its size, then writes the length back with
 *     the owner bit cleared (IN_DONE) and goes on to the next; the
 *     peripheral's EOF writes the descriptor back with suc_eof
 *     (IN_SUC_EOF, IN_SUC_EOF_DES_ADDR); data with no descriptor left is
 *     lost (IN_DSCR_EMPTY).
 *   - MEM_TRANS_EN connects transmit channel n to receive channel n: once
 *     both run, the whole list is copied, its EOF ending the receive side.
 *   - OUT_RST and IN_RST return a channel to idle. INT_ST is INT_RAW and
 *     INT_ENA; INT_CLR clears.
 * Where it differs inside: bytes move as the peripheral asks for them or
 * gives them, at once, with no FIFO in between and no bus bandwidth taken
 * (the priorities and the arbitration change nothing); the FIFO status
 * registers read empty. Not yet: the in-link's auto return
 * (INLINK_AUTO_RET), the descriptors' burst modes (they change nothing
 * but alignment), err_eof (UHCI0's). */
#include <stdio.h>
#include <string.h>

#include "mcu/esp32c3/esp32c3.h"

#define NCH 3
#define SOURCE0 44
#define RAM_LO 0x3FC80000u
#define RAM_HI 0x3FCE0000u

/* INT_RAW */
#define I_IN_DONE (1u << 0)
#define I_IN_SUC_EOF (1u << 1)
#define I_OUT_DONE (1u << 3)
#define I_OUT_EOF (1u << 4)
#define I_IN_DSCR_ERR (1u << 5)
#define I_OUT_DSCR_ERR (1u << 6)
#define I_IN_DSCR_EMPTY (1u << 7)
#define I_OUT_TOTAL_EOF (1u << 8)

struct link {
    int on;      /* a descriptor loaded */
    int ended;   /* the list's end reached */
    u32 at;      /* the descriptor's address */
    u32 dw0, buf, next;
    u32 n;       /* bytes taken or stored */
    u32 last;    /* the last descriptor's address, for RESTART */
};

struct c3_gdma {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct {
        struct esk_rf_reg *raw, *st, *ena, *in_conf0, *in_conf1, *in_link, *in_state, *in_suc, *in_dscr, *in_bf0,
            *in_bf1, *in_sel, *out_conf0, *out_conf1, *out_link, *out_state, *out_eof, *out_eof_bfr, *out_dscr,
            *out_bf0, *out_bf1, *out_sel;
        struct link in, out;
        int level;
    } c[NCH];
};

static u32 reg(const struct esk_rf_reg *r) { return (u32)r->value; }

static struct c3_gdma *gdma_of(struct esk_mach *m)
{
    return (struct c3_gdma *)esk_rf_ctx_of(m->rf, &esk_esp32c3_gdma_ops);
}

static void update(struct c3_gdma *k, int n)
{
    u32 st = reg(k->c[n].raw) & reg(k->c[n].ena) & 0x1fffu;
    int level = st != 0;
    esk_rf_hw(k->c[n].st, st);
    if (level != k->c[n].level) {
        k->c[n].level = level;
        esk_esp32c3_source(k->s, SOURCE0 + n, level);
    }
}

static void raise(struct c3_gdma *k, int n, u32 bits)
{
    esk_rf_hw(k->c[n].raw, reg(k->c[n].raw) | bits);
    update(k, n);
}

static u32 rd32(struct c3_gdma *k, u32 a)
{
    u32 v = 0;
    if (esk_bus_read(&k->s->bus, a, 4, &v) != 0)
        return 0;
    return v;
}

static void wr32(struct c3_gdma *k, u32 a, u32 v) { (void)esk_bus_write(&k->s->bus, a, 4, v); }

static int in_ram(u32 a) { return a >= RAM_LO && a < RAM_HI; }

/* load the descriptor at `at`: 1, or 0 (an error raised, or none) */
static int load(struct c3_gdma *k, int n, int out, u32 at)
{
    struct link *l = out ? &k->c[n].out : &k->c[n].in;
    u32 conf1 = reg(out ? k->c[n].out_conf1 : k->c[n].in_conf1);
    l->on = 0;
    if (!at) {
        l->ended = 1;
        return 0;
    }
    if (!in_ram(at)) {
        raise(k, n, out ? I_OUT_DSCR_ERR : I_IN_DSCR_ERR);
        l->ended = 1;
        return 0;
    }
    l->at = at;
    l->last = at;
    l->dw0 = rd32(k, at);
    l->buf = rd32(k, at + 4u);
    l->next = rd32(k, at + 8u);
    l->n = 0;
    if (((conf1 & (1u << 12)) && !(l->dw0 >> 31)) || !in_ram(l->buf)) {
        raise(k, n, out ? I_OUT_DSCR_ERR : I_IN_DSCR_ERR);
        l->ended = 1;
        return 0;
    }
    l->on = 1;
    l->ended = 0;
    if (out) {
        esk_rf_hw(k->c[n].out_bf1, reg(k->c[n].out_bf0));
        esk_rf_hw(k->c[n].out_bf0, reg(k->c[n].out_dscr));
        esk_rf_hw(k->c[n].out_dscr, at);
        esk_rf_hw(k->c[n].out_state, (reg(k->c[n].out_state) & ~0x3ffffu) | (at & 0x3ffffu));
    } else {
        esk_rf_hw(k->c[n].in_bf1, reg(k->c[n].in_bf0));
        esk_rf_hw(k->c[n].in_bf0, reg(k->c[n].in_dscr));
        esk_rf_hw(k->c[n].in_dscr, at);
        esk_rf_hw(k->c[n].in_state, (reg(k->c[n].in_state) & ~0x3ffffu) | (at & 0x3ffffu));
    }
    return 1;
}

static int chan_of(struct c3_gdma *k, int peri, int out)
{
    int n;
    for (n = 0; n < NCH; n++)
        if ((reg(out ? k->c[n].out_sel : k->c[n].in_sel) & 0x3fu) == (u32)peri)
            return n;
    return -1;
}

/* ---- transmitting ------------------------------------------------------------- */

/* the next byte of channel n's list: 1 and the byte (and whether it ends
 * an EOF descriptor), or 0 when there is none */
static int out_byte(struct c3_gdma *k, int n, u8 *byte, int *eof)
{
    struct link *l = &k->c[n].out;
    u32 len, v = 0;
    *eof = 0;
    while (l->on) {
        len = (l->dw0 >> 12) & 0xfffu;
        if (l->n < len)
            break;
        /* the descriptor is used up: on to the next */
        if (load(k, n, 1, l->next) == 0 && !l->on)
            return 0;
    }
    if (!l->on)
        return 0;
    if (esk_bus_read(&k->s->bus, l->buf + l->n, 1, &v) != 0)
        v = 0;
    *byte = (u8)(v & 0xffu);
    l->n++;
    len = (l->dw0 >> 12) & 0xfffu;
    if (l->n >= len) { /* its last byte */
        int suc = (int)((l->dw0 >> 30) & 1u);
        if (reg(k->c[n].out_conf0) & (1u << 2)) /* OUT_AUTO_WRBACK */
            wr32(k, l->at, l->dw0 & ~(1u << 31));
        raise(k, n, I_OUT_DONE);
        if (suc) {
            *eof = 1;
            esk_rf_hw(k->c[n].out_eof, l->at);
            raise(k, n, I_OUT_EOF);
            if (!l->next)
                raise(k, n, I_OUT_TOTAL_EOF);
        }
    }
    return 1;
}

int esk_esp32c3_dma_out(struct esk_mach *m, int peri, u8 *byte, int *eof)
{
    struct c3_gdma *k = gdma_of(m);
    int n;
    if (!k || (n = chan_of(k, peri, 1)) < 0)
        return 0;
    return out_byte(k, n, byte, eof);
}

/* ---- receiving ------------------------------------------------------------------ */

static void in_writeback(struct c3_gdma *k, int n, int suc)
{
    struct link *l = &k->c[n].in;
    u32 dw0 = (l->dw0 & ~(0xfffu << 12 | 1u << 31 | 1u << 30)) | (l->n & 0xfffu) << 12;
    if (suc)
        dw0 |= 1u << 30;
    if (!(reg(k->c[n].in_conf0) & (1u << 1))) /* IN_LOOP_TEST keeps the owner */
        dw0 &= ~(1u << 31);
    else
        dw0 |= l->dw0 & (1u << 31);
    wr32(k, l->at, dw0);
    l->dw0 = dw0;
}

static int in_byte(struct c3_gdma *k, int n, u8 byte)
{
    struct link *l = &k->c[n].in;
    if (l->on && l->n >= (l->dw0 & 0xfffu)) { /* full: written back, the next */
        in_writeback(k, n, 0);
        raise(k, n, I_IN_DONE);
        load(k, n, 0, l->next);
    }
    if (!l->on) {
        if (l->ended)
            raise(k, n, I_IN_DSCR_EMPTY);
        return 0;
    }
    (void)esk_bus_write(&k->s->bus, l->buf + l->n, 1, byte);
    l->n++;
    return 1;
}

static void in_eof(struct c3_gdma *k, int n)
{
    struct link *l = &k->c[n].in;
    if (!l->on)
        return;
    in_writeback(k, n, 1);
    esk_rf_hw(k->c[n].in_suc, l->at);
    raise(k, n, I_IN_DONE | I_IN_SUC_EOF);
    load(k, n, 0, l->next);
}

int esk_esp32c3_dma_in(struct esk_mach *m, int peri, u8 byte)
{
    struct c3_gdma *k = gdma_of(m);
    int n;
    if (!k || (n = chan_of(k, peri, 0)) < 0)
        return 0;
    return in_byte(k, n, byte);
}

int esk_esp32c3_dma_in_ready(struct esk_mach *m, int peri)
{
    struct c3_gdma *k = gdma_of(m);
    int n;
    if (!k || (n = chan_of(k, peri, 0)) < 0)
        return 0;
    return k->c[n].in.on;
}

void esk_esp32c3_dma_in_eof(struct esk_mach *m, int peri)
{
    struct c3_gdma *k = gdma_of(m);
    int n;
    if (k && (n = chan_of(k, peri, 0)) >= 0)
        in_eof(k, n);
}

/* memory to memory: the transmit list into the receive list, at once */
static void mem_trans(struct c3_gdma *k, int n)
{
    u8 b;
    int eof = 0, guard = 0;
    if (!(reg(k->c[n].in_conf0) & (1u << 4)) || !k->c[n].out.on || !k->c[n].in.on)
        return;
    while (guard++ < (1 << 22) && out_byte(k, n, &b, &eof)) {
        in_byte(k, n, b);
        if (eof)
            in_eof(k, n);
    }
}

/* ---- registers --------------------------------------------------------------------- */

static u32 gd_read(void *ctx, u32 off, int n)
{
    struct c3_gdma *k = (struct c3_gdma *)ctx;
    return esk_rf_read(k->p, off, n);
}

static void gd_write(void *ctx, u32 off, int nb, u32 v)
{
    struct c3_gdma *k = (struct c3_gdma *)ctx;
    u32 w = off & ~3u;
    int n;
    for (n = 0; n < NCH; n++) {
        if (w == esk_rf_off(k->c[n].raw) + 0x0Cu) { /* INT_CLR */
            esk_rf_hw(k->c[n].raw, reg(k->c[n].raw) & ~v);
            update(k, n);
            return;
        }
        if (w == esk_rf_off(k->c[n].st))
            return;
    }
    esk_rf_write(k->p, off, nb, v);
    for (n = 0; n < NCH; n++) {
        if (w == esk_rf_off(k->c[n].raw) || w == esk_rf_off(k->c[n].ena))
            update(k, n);
        if (w == esk_rf_off(k->c[n].in_conf0) && (v & 1u)) /* IN_RST */
            memset(&k->c[n].in, 0, sizeof k->c[n].in);
        if (w == esk_rf_off(k->c[n].out_conf0) && (v & 1u)) /* OUT_RST */
            memset(&k->c[n].out, 0, sizeof k->c[n].out);
        if (w == esk_rf_off(k->c[n].in_link)) {
            u32 addr = 0x3FC00000u | (v & 0xfffffu);
            if (v & (1u << 21)) /* INLINK_STOP */
                k->c[n].in.on = 0;
            if (v & (1u << 22)) /* INLINK_START */
                load(k, n, 0, addr);
            if ((v & (1u << 23)) && (!k->c[n].in.on || k->c[n].in.ended)) /* RESTART */
                load(k, n, 0, rd32(k, k->c[n].in.last + 8u));
            esk_rf_hw(k->c[n].in_link, reg(k->c[n].in_link) & ~(7u << 21));
            mem_trans(k, n);
        }
        if (w == esk_rf_off(k->c[n].out_link)) {
            u32 addr = 0x3FC00000u | (v & 0xfffffu);
            if (v & (1u << 20))
                k->c[n].out.on = 0;
            if (v & (1u << 21))
                load(k, n, 1, addr);
            if ((v & (1u << 22)) && (!k->c[n].out.on || k->c[n].out.ended))
                load(k, n, 1, rd32(k, k->c[n].out.last + 8u));
            esk_rf_hw(k->c[n].out_link, reg(k->c[n].out_link) & ~(7u << 20));
            mem_trans(k, n);
        }
    }
}

static void gd_reset(void *ctx)
{
    struct c3_gdma *k = (struct c3_gdma *)ctx;
    int n;
    for (n = 0; n < NCH; n++) {
        memset(&k->c[n].in, 0, sizeof k->c[n].in);
        memset(&k->c[n].out, 0, sizeof k->c[n].out);
        k->c[n].level = 0;
        esk_esp32c3_source(k->s, SOURCE0 + n, 0);
    }
}

const struct esk_dev_ops esk_esp32c3_gdma_ops = {
    "esp32c3-gdma", gd_read, gd_write, gd_reset, 0, 0,
};

void *esk_esp32c3_gdma_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct c3_gdma *k = (struct c3_gdma *)esk_mach_alloc(s, sizeof *k);
    static const char *const names[] = {
        "INT_RAW_CH%d",        "INT_ST_CH%d",      "INT_ENA_CH%d",         "IN_CONF0_CH%d",
        "IN_CONF1_CH%d",       "IN_LINK_CH%d",     "IN_STATE_CH%d",        "IN_SUC_EOF_DES_ADDR_CH%d",
        "IN_DSCR_CH%d",        "IN_DSCR_BF0_CH%d", "IN_DSCR_BF1_CH%d",     "IN_PERI_SEL_CH%d",
        "OUT_CONF0_CH%d",      "OUT_CONF1_CH%d",   "OUT_LINK_CH%d",        "OUT_STATE_CH%d",
        "OUT_EOF_DES_ADDR_CH%d", "OUT_EOF_BFR_DES_ADDR_CH%d", "OUT_DSCR_CH%d", "OUT_DSCR_BF0_CH%d",
        "OUT_DSCR_BF1_CH%d",   "OUT_PERI_SEL_CH%d",
    };
    char name[40];
    int n;
    size_t i;
    if (!k)
        return 0;
    memset(k, 0, sizeof *k);
    k->s = s;
    k->p = p;
    for (n = 0; n < NCH; n++) {
        struct esk_rf_reg **slot[22];
        slot[0] = &k->c[n].raw;
        slot[1] = &k->c[n].st;
        slot[2] = &k->c[n].ena;
        slot[3] = &k->c[n].in_conf0;
        slot[4] = &k->c[n].in_conf1;
        slot[5] = &k->c[n].in_link;
        slot[6] = &k->c[n].in_state;
        slot[7] = &k->c[n].in_suc;
        slot[8] = &k->c[n].in_dscr;
        slot[9] = &k->c[n].in_bf0;
        slot[10] = &k->c[n].in_bf1;
        slot[11] = &k->c[n].in_sel;
        slot[12] = &k->c[n].out_conf0;
        slot[13] = &k->c[n].out_conf1;
        slot[14] = &k->c[n].out_link;
        slot[15] = &k->c[n].out_state;
        slot[16] = &k->c[n].out_eof;
        slot[17] = &k->c[n].out_eof_bfr;
        slot[18] = &k->c[n].out_dscr;
        slot[19] = &k->c[n].out_bf0;
        slot[20] = &k->c[n].out_bf1;
        slot[21] = &k->c[n].out_sel;
        for (i = 0; i < sizeof names / sizeof names[0]; i++) {
            snprintf(name, sizeof name, names[i], n);
            if (!(*slot[i] = esk_rf_reg(p, name)))
                return 0;
        }
    }
    return k;
}
