/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* usbjtag.c -- the ESP32-C3's USB Serial/JTAG controller (USB_DEVICE, TRM
 * v1.4 chapter 30), its CDC-ACM side as the CPU sees it.
 *
 *   - Writing EP1 puts a byte in the 64-byte send buffer while
 *     EP1_CONF's SERIAL_IN_EP_DATA_FREE is 1; the 64th byte, or WR_DONE,
 *     flushes the buffer to the host, and it stays unavailable (DATA_FREE
 *     0) until the host has read it (30.3.2).
 *   - Reading EP1 takes a byte the host sent while
 *     SERIAL_OUT_EP_DATA_AVAIL is 1.
 *   - INT_RAW's SERIAL_IN_EMPTY (set out of reset) and the others are
 *     cleared by INT_CLR; INT_ST is INT_RAW and INT_ENA, and raises the
 *     matrix source (26).
 *   - No USB host is on the chip's D- and D+ (GPIO18 and GPIO19), as on
 *     an ESP32-C3-DevKitM-1, whose USB connector is the CP2102's: a flushed
 *     packet is never read, so the buffer stays unavailable, no data
 *     comes, and there are no SOF frames (FRAM_NUM stays 0). The TRM says
 *     firmware must time out in that case (30.4); the ROM and ESP-IDF's
 *     console do.
 * Not yet: a host on those pins (the CDC-ACM port on the computer, its
 * DTR and RTS resets into download mode, the JTAG side), which comes with
 * ADR-031's native USB devices; the pads' own pull-ups and pin swap
 * (CONF0) apart from what the IO MUX's reset gives GPIO19. */
#include <string.h>

#include "mcu/esp32c3/esp32c3.h"

#define SOURCE 26
#define OFF_EP1 0x00u
#define OFF_EP1_CONF 0x04u
#define OFF_INT_RAW 0x08u
#define OFF_INT_ST 0x0Cu
#define OFF_INT_CLR 0x14u
#define WR_DONE 1u
#define DATA_FREE 2u

struct c3_usbjtag {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *conf, *raw, *st, *ena;
    u8 in[64];
    int nin;
    int flushed; /* the buffer waits for the host */
    int level;
};

static void update(struct c3_usbjtag *k)
{
    u32 st = (u32)k->raw->value & (u32)k->ena->value;
    int level = st != 0;
    esk_rf_hw(k->st, st);
    esk_rf_hw(k->conf, (!k->flushed && k->nin < 64) ? DATA_FREE : 0u);
    if (level != k->level) {
        k->level = level;
        esk_esp32c3_source(k->s, SOURCE, level);
    }
}

static u32 uj_read(void *ctx, u32 off, int n)
{
    struct c3_usbjtag *k = (struct c3_usbjtag *)ctx;
    if ((off & ~3u) == OFF_EP1)
        return 0; /* nothing from a host */
    return esk_rf_read(k->p, off, n);
}

static void uj_write(void *ctx, u32 off, int n, u32 v)
{
    struct c3_usbjtag *k = (struct c3_usbjtag *)ctx;
    switch (off & ~3u) {
    case OFF_EP1:
        if (!k->flushed && k->nin < 64) {
            k->in[k->nin++] = (u8)(v & 0xffu);
            if (k->nin == 64)
                k->flushed = 1;
        }
        break;
    case OFF_EP1_CONF:
        if (v & WR_DONE)
            k->flushed = 1;
        break;
    case OFF_INT_CLR:
        esk_rf_hw(k->raw, (u32)k->raw->value & ~v);
        break;
    case OFF_INT_RAW:
    case OFF_INT_ST:
        break; /* read-only to the CPU */
    default:
        esk_rf_write(k->p, off, n, v);
        break;
    }
    update(k);
}

static void uj_reset(void *ctx)
{
    struct c3_usbjtag *k = (struct c3_usbjtag *)ctx;
    k->nin = 0;
    k->flushed = 0;
    k->level = 0;
    update(k);
}

const struct esk_dev_ops esk_esp32c3_usbjtag_ops = {
    "esp32c3-usbjtag", uj_read, uj_write, uj_reset, 0, 0,
};

void *esk_esp32c3_usbjtag_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct c3_usbjtag *k = (struct c3_usbjtag *)esk_mach_alloc(s, sizeof *k);
    if (!k)
        return 0;
    memset(k, 0, sizeof *k);
    k->s = s;
    k->p = p;
    k->conf = esk_rf_reg(p, "EP1_CONF");
    k->raw = esk_rf_reg(p, "INT_RAW");
    k->st = esk_rf_reg(p, "INT_ST");
    k->ena = esk_rf_reg(p, "INT_ENA");
    if (!k->conf || !k->raw || !k->st || !k->ena)
        return 0;
    return k;
}
