/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* radio.c -- the ESP32-C3's radio register blocks that neither the TRM nor
 * the SVD describe: FE2 at 0x60005000, FE at 0x60006000 and NRX at
 * 0x6001CC00 (ESP-IDF's soc/reg_base.h). ESP-IDF writes their power-up
 * bits at every start (rtc_sleep_pu: FE_GEN_CTRL, FE2_TX_INTERP_CTRL,
 * NRXPD_CTRL) and reads them back; they keep what is written and read 0
 * after a reset.
 * Not modelled: the radio they control (Wi-Fi and Bluetooth), their reset
 * values (unpublished). */
#include <string.h>

#include "mcu/esp32c3/esp32c3.h"

struct c3_radio {
    u32 size;
    u32 *word;
};

static u32 rd_read(void *ctx, u32 off, int n)
{
    struct c3_radio *k = (struct c3_radio *)ctx;
    (void)n;
    return k->word[(off / 4u) % (k->size / 4u)] >> (8u * (off & 3u));
}

static void rd_write(void *ctx, u32 off, int n, u32 v)
{
    struct c3_radio *k = (struct c3_radio *)ctx;
    u32 *w = &k->word[(off / 4u) % (k->size / 4u)], sh = 8u * (off & 3u);
    u32 mask = (n >= 4 ? 0xffffffffu : ((1u << (8 * n)) - 1u)) << sh;
    *w = (*w & ~mask) | (v << sh & mask);
}

static void rd_reset(void *ctx)
{
    struct c3_radio *k = (struct c3_radio *)ctx;
    memset(k->word, 0, k->size);
}

static const struct esk_dev_ops radio_ops = {
    "esp32c3-radio", rd_read, rd_write, rd_reset, 0, 0,
};

int esk_esp32c3_radio_create(struct esk_mach *s)
{
    static const struct {
        u32 base, size;
    } blocks[] = { { 0x60005000u, 0x1000u }, { 0x60006000u, 0x1000u }, { 0x6001CC00u, 0x400u } };
    size_t i;
    for (i = 0; i < sizeof blocks / sizeof blocks[0]; i++) {
        struct c3_radio *k = (struct c3_radio *)esk_mach_alloc(s, sizeof *k);
        if (!k || !(k->word = (u32 *)esk_mach_alloc(s, blocks[i].size)))
            return -1;
        k->size = blocks[i].size;
        memset(k->word, 0, k->size);
        if (esk_mach_add_dev(s, blocks[i].base, blocks[i].size, &radio_ops, k) != ES_OK)
            return -1;
    }
    return 0;
}
