/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* radio.c -- the ESP32-C3's radio register blocks, which neither the TRM
 * nor the SVD describes: ESP-IDF's soc/reg_base.h names FE2, FE, NRX and
 * BB, and its closed PHY and Wi-Fi libraries (libphy, libpp) reach the rest
 * (docs/engineering/c3-radios.md):
 *
 *   - FE2 at 0x60005000 and FE at 0x60006000, the RF front end;
 *   - the receive gain table at 0x60011000-0x60012FFF (libphy's
 *     set_rx_gain_table);
 *   - the AGC and NRX at 0x6001C000-0x6001CFFF (the ROM's rom_agc_reg_init,
 *     NRX at +0xC00);
 *   - BB at 0x6001D000, the baseband (the SVD names only BBPD_CTRL, +0x54).
 *
 * The Wi-Fi MAC after them, at 0x60033000, is wifi.c's.
 *
 * Their registers keep what is written and read 0 after a reset. ESP-IDF
 * writes their power-up bits at every start (rtc_sleep_pu: FE_GEN_CTRL,
 * FE2_TX_INTERP_CTRL, NRXPD_CTRL) and phy_init programs them all. The
 * PHY's calibrations answer as an ideal radio: FE's IQ estimator
 * (rom_iq_est_enable waits for +0x174 bit 16) reads done, its estimate that
 * of a balanced front end.
 * Not modelled: their reset values (unpublished); the Bluetooth blocks are
 * not here (the plan's step 6). */
#include <string.h>

#include "mcu/esp32c3/esp32c3.h"

#define FE_BASE       0x60006000u
#define FE_IQ_EST     0x174u /* bit 16: the IQ estimate is done */

struct c3_radio {
    u32 base, size;
    u32 *word;
};

static u32 *word_at(struct c3_radio *k, u32 off)
{
    return &k->word[(off / 4u) % (k->size / 4u)];
}

static u32 rd_read(void *ctx, u32 off, int n)
{
    struct c3_radio *k = (struct c3_radio *)ctx;
    u32 *w = word_at(k, off);
    (void)n;
    if (k->base == FE_BASE && (off & ~3u) == FE_IQ_EST)
        *w |= 1u << 16;
    return *w >> (8u * (off & 3u));
}

static void rd_write(void *ctx, u32 off, int n, u32 v)
{
    struct c3_radio *k = (struct c3_radio *)ctx;
    u32 *w = word_at(k, off), sh = 8u * (off & 3u);
    u32 mask = (n >= 4 ? 0xffffffffu : ((1u << (8 * n)) - 1u)) << sh;
    *w = (*w & ~mask) | (v << sh & mask);
}

static void rd_reset(void *ctx)
{
    struct c3_radio *k = (struct c3_radio *)ctx;
    memset(k->word, 0, k->size);
}

/* the chip's one radio, which the Wi-Fi MAC and the Bluetooth LE core share:
 * the air asks each for what it listens on and gives it its frames; a
 * frame's end goes back to the one that sent it (Bluetooth's tokens have
 * bit 31 set) */
struct c3_air {
    struct esk_radio_port part[2]; /* ESK_RADIO_WIFI, ESK_RADIO_BLE */
};

static struct esk_radio_port *part_of(struct c3_air *a, int kind)
{
    return kind == ESK_RADIO_BLE ? &a->part[1] : &a->part[0];
}

static u32 air_tuned(void *ctx, int kind)
{
    struct esk_radio_port *p = part_of((struct c3_air *)ctx, kind);
    return p->tuned ? p->tuned(p->ctx, kind) : 0u;
}

static int air_rx(void *ctx, const u8 *frame, u32 len, u32 mhz, int rssi, u32 kbps, int kind)
{
    struct esk_radio_port *p = part_of((struct c3_air *)ctx, kind);
    return p->rx ? p->rx(p->ctx, frame, len, mhz, rssi, kbps, kind) : 0;
}

static void air_tx_done(void *ctx, u32 token, int acked)
{
    struct esk_radio_port *p = part_of((struct c3_air *)ctx, (token & 0x80000000u) ? ESK_RADIO_BLE : ESK_RADIO_WIFI);
    if (p->tx_done)
        p->tx_done(p->ctx, token, acked);
}

int esk_esp32c3_radio_join(struct esk_mach *s, int kind, const struct esk_radio_port *part)
{
    struct c3_air *a = (struct c3_air *)s->radio.ctx;
    if (!a) {
        if (!(a = (struct c3_air *)esk_mach_alloc(s, sizeof *a)))
            return -1;
        memset(a, 0, sizeof *a);
        s->radio.ctx = a;
        s->radio.tuned = air_tuned;
        s->radio.rx = air_rx;
        s->radio.tx_done = air_tx_done;
    }
    *part_of(a, kind) = *part;
    s->radio.kinds |= kind;
    return 0;
}

static const struct esk_dev_ops radio_ops = {
    "esp32c3-radio", rd_read, rd_write, rd_reset, 0, 0,
};

int esk_esp32c3_radio_create(struct esk_mach *s)
{
    static const struct {
        u32 base, size;
    } blocks[] = { { 0x60005000u, 0x1000u }, { FE_BASE, 0x1000u },  { 0x60011000u, 0x2000u },
                   { 0x6001C000u, 0x1000u }, { 0x6001D000u, 0x1000u } };
    size_t i;
    for (i = 0; i < sizeof blocks / sizeof blocks[0]; i++) {
        struct c3_radio *k = (struct c3_radio *)esk_mach_alloc(s, sizeof *k);
        if (!k || !(k->word = (u32 *)esk_mach_alloc(s, blocks[i].size)))
            return -1;
        k->base = blocks[i].base;
        k->size = blocks[i].size;
        memset(k->word, 0, k->size);
        if (esk_mach_add_dev(s, blocks[i].base, blocks[i].size, &radio_ops, k) != ES_OK)
            return -1;
    }
    return 0;
}
