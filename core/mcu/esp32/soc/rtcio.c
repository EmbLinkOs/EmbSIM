/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* rtcio.c -- the ESP32's RTC IO MUX (RTC_IO, TRM v5 chapter 4.11) as far as
 * its two DACs (chapter 29.5): DAC1 on GPIO25 (PAD_DAC1), DAC2 on GPIO26
 * (PAD_DAC2).
 *
 *   - While the pad is the RTC IO MUX's (MUX_SEL) and its DAC powered
 *     (XPD_DAC, with DAC_XPD_FORCE as ESP-IDF sets it), the pad is the DAC's
 *     output: VDD3P3_RTC x DAC / 255 for the eight-bit code in PDACn_DAC,
 *     the relation ESP-IDF's dac_oneshot documents; the GPIO model gives the
 *     pad that voltage (gpio.c).
 *   - Writes to the registers settle the pads again.
 * Where it differs inside: the output is a stiff source (the datasheet gives
 * no output impedance). Not yet: the cosine-wave generator (SENS's
 * SAR_DAC_CTRL1/2: SW_TONE_EN, DAC_CW_EN1/2, the scale, phase and offset)
 * and the DMA path (DAC_DIG_FORCE, I2S0), whose outputs read as the code
 * written; the other RTC pads' functions, pulls and hold. */
#include <string.h>

#include "mcu/esp32/soc/esp32.h"

#define PAD_DAC1 0x84u
#define PAD_DAC2 0x88u
#define XPD_DAC (1u << 18)
#define MUX_SEL (1u << 17)

struct e32_rtcio {
    struct esk_mach *s;
    struct esk_rf_periph *p;
};

static struct e32_rtcio *rtcio_of(struct esk_mach *m)
{
    return m->rf ? (struct e32_rtcio *)esk_rf_ctx_of(m->rf, &esk_esp32_rtcio_ops) : 0;
}

int esk_esp32_dac_out(struct esk_mach *m, int gpio, double *volts, double *rout)
{
    struct e32_rtcio *k = gpio == 25 || gpio == 26 ? rtcio_of(m) : 0;
    u32 v;
    double vdd;
    if (!k)
        return 0;
    v = esk_rf_read(k->p, gpio == 25 ? PAD_DAC1 : PAD_DAC2, 4);
    if (!(v & XPD_DAC) || !(v & MUX_SEL))
        return 0;
    vdd = m->vdd > 0.0 ? m->vdd : 3.3;
    *volts = vdd * (double)((v >> 19) & 0xffu) / 255.0;
    *rout = 1.0;
    return 1;
}

static u32 rio_read(void *ctx, u32 off, int n)
{
    struct e32_rtcio *k = (struct e32_rtcio *)ctx;
    return esk_rf_read(k->p, off, n);
}

static void rio_write(void *ctx, u32 off, int n, u32 v)
{
    struct e32_rtcio *k = (struct e32_rtcio *)ctx;
    esk_rf_write(k->p, off, n, v);
    if ((off & ~3u) == PAD_DAC1 || (off & ~3u) == PAD_DAC2)
        esk_esp32_pins_changed(k->s);
}

static void rio_reset(void *ctx) { (void)ctx; }

const struct esk_dev_ops esk_esp32_rtcio_ops = {
    "esp32-rtcio", rio_read, rio_write, rio_reset, 0, 0,
};

void *esk_esp32_rtcio_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct e32_rtcio *k = (struct e32_rtcio *)esk_mach_alloc(s, sizeof *k);
    if (!k)
        return 0;
    memset(k, 0, sizeof *k);
    k->s = s;
    k->p = p;
    return k;
}
