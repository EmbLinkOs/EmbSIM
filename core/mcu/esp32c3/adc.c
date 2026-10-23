/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* adc.c -- the ESP32-C3's SAR ADCs and temperature sensor (APB_SARADC, TRM
 * v1.4 chapter 34), their interrupt on matrix source 43.
 *
 *   - SAR ADC1 reads GPIO0-4 (channels 0-4), SAR ADC2 GPIO5 (channel 0):
 *     the pin's voltage V, through the attenuation (0, 2.5, 6 or 12 dB:
 *     k = 1, 0.75, 0.5, 0.25), as data = V k / Vref x 4095 with Vref 1100 mV
 *     (34.2.3.2), 0 to 4095; SARn_INV inverts it. The analog block's
 *     ENCAL_GND (regi2c 0x69, register 7, bit 5 for ADC1 and 7 for ADC2),
 *     which ESP-IDF's self-calibration sets, grounds the input.
 *   - One-time sampling: ONETIME_START with SARADC1_ONETIME_SAMPLE or
 *     SARADC2_ONETIME_SAMPLE, ONETIME_CHANNEL (the unit in bit 3) and
 *     ONETIME_ATTEN: 25 SAR_CLK cycles later the value is in
 *     SAR1DATA_STATUS or SAR2DATA_STATUS and ADCn_DONE is raised. SAR_CLK
 *     is ADC_CTRL_CLK (APB_CLK, divided by CLKM_DIV_NUM + 1 + DIV_A /
 *     DIV_B) divided by SAR_CLK_DIV.
 *   - Scanning: with TIMER_EN a sample every 2 x TIMER_TARGET ADC_CTRL_CLK
 *     cycles, from the pattern table's next entry (SAR_PATT_TAB1/2, six
 *     bits each: ADC, channel, attenuation; SAR_PATT_LEN + 1 of them); with
 *     APB_ADC_TRANS each goes to the GDMA (peripheral 8) as a word, data in
 *     bits 11:0, the channel in 15:13 and the ADC in 16, and every
 *     APB_ADC_EOF_NUM of them ends the receive list's frame.
 *   - Two filters (FILTER_CHANNELn: the ADC in bit 3, the channel below;
 *     FILTER_FACTORn the coefficient k): data = ((k - 1) prev + data) / k
 *     + 0.5; FILTER_RESET clears them. Two threshold monitors (THRESn_EN,
 *     THRESn_CHANNEL, THRESn_HIGH and _LOW) raise THRESn_HIGH and _LOW.
 *   - The temperature sensor: while TSENS_PU is set and SYSTEM's
 *     TSENS_CLK_EN, TSENS_OUT is (T + 20.52 + 27.88 offset) / 0.4386, T
 *     the world's temperature where the chip sits (25 degC without one),
 *     the offset the one regi2c's TSENS_DAC selects (ESP-IDF's table: 5
 *     -2, 7 -1, 15 0, 11 1, 10 2).
 *   - INT_ST is INT_RAW and INT_ENA; INT_CLR clears.
 * Where it differs inside: the conversion is the TRM's ideal one, without
 * the part's offset, gain error, nonlinearity and noise, and without the
 * eFuse calibration's effect (ESP-IDF's calibration reads what the eFuses
 * hold); the sensor reads the room's temperature at once, with no
 * self-heating and no settling. Not yet: ADC2's errata (its DIG
 * controller's readings), the arbiter's priorities (the PWDET controller
 * never asks), the initial code's offset (ADC_SAR1_INITIAL_CODE). */
#include <stdio.h>
#include <string.h>

#include "mcu/esp32c3/esp32c3.h"

#define SOURCE 43
#define PERI_ADC 8
#define VREF 1.1
#define FS_PER_S 1000000000000000ull

/* ONETIME_SAMPLE */
#define OT_START (1u << 29)
#define OT_SAR2 (1u << 30)
#define OT_SAR1 (1u << 31)
/* INT_RAW */
#define I_THRES1_LOW (1u << 26)
#define I_THRES0_LOW (1u << 27)
#define I_THRES1_HIGH (1u << 28)
#define I_THRES0_HIGH (1u << 29)
#define I_ADC2_DONE (1u << 30)
#define I_ADC1_DONE (1u << 31)

struct c3_adc {
    struct esk_mach *s;
    struct esk_rf_periph *p, *sys;
    struct esk_rf_reg *ctrl, *ctrl2, *fctrl1, *fctrl0, *patt[2], *onetime, *data[2], *thres[2], *thres_ctrl, *ena,
        *raw, *st, *dma_conf, *clkm, *tsens, *tsens2, *sys_en1;
    int clocked, level;
    /* the one-time conversion under way */
    int busy, unit, ch, atten;
    u64 left;             /* fs to its end */
    /* scanning */
    u64 tleft;            /* fs to the timer's next sample */
    u32 pr, nsent;
    double filt[2];
    int filt_on[2];
};

static u32 reg(const struct esk_rf_reg *r) { return (u32)r->value; }

static void update(struct c3_adc *k)
{
    u32 st = reg(k->raw) & reg(k->ena) & 0xfc000000u;
    int level = st != 0;
    esk_rf_hw(k->st, st);
    if (level != k->level) {
        k->level = level;
        esk_esp32c3_source(k->s, SOURCE, level);
    }
}

static void raise(struct c3_adc *k, u32 bits)
{
    esk_rf_hw(k->raw, reg(k->raw) | bits);
    update(k);
}

static u64 fs_per_cycle(const struct c3_adc *k)
{
    u32 hz = esk_mach_core_hz(k->s);
    return hz ? FS_PER_S / hz : 0;
}

/* ADC_CTRL_CLK's period in fs */
static double ctrl_fs(const struct c3_adc *k)
{
    u32 c = reg(k->clkm), num = c & 0xffu, b = (c >> 8) & 0x3fu, a = (c >> 14) & 0x3fu;
    u32 hz = esk_esp32c3_apb_hz(k->s);
    if (!hz)
        return 0;
    return ((double)num + 1.0 + (b ? (double)a / (double)b : 0.0)) * (double)FS_PER_S / (double)hz;
}

static double sar_fs(const struct c3_adc *k)
{
    u32 div = (reg(k->ctrl) >> 7) & 0xffu;
    return ctrl_fs(k) * (double)(div ? div : 1u);
}

/* the conversion of `unit`'s channel `ch` at attenuation `atten` */
static u32 convert(struct c3_adc *k, int unit, int ch, int atten)
{
    static const double kf[4] = { 1.0, 0.75, 0.5, 0.25 };
    struct esk_mach *s = k->s;
    int gpio = unit ? 5 + ch : ch, grounded;
    double v = 0, d;
    u32 data;
    grounded = (esk_esp32c3_regi2c_get(s, 0x69u, 7u) >> (unit ? 7 : 5)) & 1;
    if (!grounded && (unit ? ch == 0 : ch <= 4) && s->host.pin_volts)
        if (!s->host.pin_volts(s->host.ctx, gpio / 16, gpio % 16, &v))
            v = 0;
    if (v < 0)
        v = 0;
    d = v * kf[atten & 3] / VREF * 4095.0 + 0.5;
    data = d >= 4095.0 ? 4095u : (u32)d;
    if (reg(k->ctrl2) & (1u << (unit ? 10 : 9))) /* SARn_INV */
        data = 4095u - data;
    return data;
}

/* a sample through the filters and the monitors */
static u32 process(struct c3_adc *k, int unit, int ch, u32 data)
{
    u32 f0 = reg(k->fctrl0), f1 = reg(k->fctrl1), code = (u32)unit << 3 | (u32)ch;
    int i;
    for (i = 0; i < 2; i++) {
        u32 fch = (f0 >> (i ? 18 : 22)) & 0xfu, kq = (f1 >> (i ? 26 : 29)) & 7u;
        if (fch != code || !kq)
            continue;
        if (!k->filt_on[i]) {
            k->filt[i] = (double)data;
            k->filt_on[i] = 1;
        } else {
            k->filt[i] = ((double)kq - 1.0) * k->filt[i] / (double)kq + (double)data / (double)kq + 0.5;
        }
        data = k->filt[i] >= 4095.0 ? 4095u : (u32)k->filt[i];
    }
    for (i = 0; i < 2; i++) {
        u32 t = reg(k->thres[i]), en = reg(k->thres_ctrl);
        if (!(en & (1u << (i ? 30 : 31))) || (t & 0xfu) != code)
            continue;
        if (data > ((t >> 5) & 0x1fffu))
            raise(k, i ? I_THRES1_HIGH : I_THRES0_HIGH);
        if (data < ((t >> 18) & 0x1fffu))
            raise(k, i ? I_THRES1_LOW : I_THRES0_LOW);
    }
    return data;
}

static int scanning(const struct c3_adc *k) { return (reg(k->ctrl2) & (1u << 24)) && ctrl_fs(k) > 0; }

static void clocking(struct c3_adc *k)
{
    esk_rf_clock(k->p, &k->clocked, k->busy || scanning(k));
}

static void onetime_done(struct c3_adc *k)
{
    u32 data = process(k, k->unit, k->ch, convert(k, k->unit, k->ch, k->atten));
    k->busy = 0;
    esk_rf_hw(k->data[k->unit], data | (u32)k->ch << 13 | (u32)k->unit << 16);
    raise(k, k->unit ? I_ADC2_DONE : I_ADC1_DONE);
}

/* the timer's sample: the pattern table's entry pr */
static void scan_sample(struct c3_adc *k)
{
    u32 len = ((reg(k->ctrl) >> 15) & 7u) + 1u, e, word, data;
    int unit, ch, atten, i;
    e = (reg(k->patt[k->pr / 4u]) >> (18u - 6u * (k->pr % 4u))) & 0x3fu;
    unit = (int)((e >> 5) & 1u);
    ch = (int)((e >> 2) & 7u);
    atten = (int)(e & 3u);
    k->pr = k->pr + 1u >= len ? 0 : k->pr + 1u;
    data = process(k, unit, ch, convert(k, unit, ch, atten));
    esk_rf_hw(k->data[unit], data | (u32)ch << 13 | (u32)unit << 16);
    if (!(reg(k->dma_conf) & (1u << 31))) /* APB_ADC_TRANS */
        return;
    word = data | (u32)ch << 13 | (u32)unit << 16;
    for (i = 0; i < 4; i++)
        esk_esp32c3_dma_in(k->s, PERI_ADC, (u8)((word >> (8 * i)) & 0xffu));
    if (++k->nsent >= (reg(k->dma_conf) & 0xffffu)) {
        k->nsent = 0;
        esk_esp32c3_dma_in_eof(k->s, PERI_ADC);
    }
}

static void adc_tick(void *ctx, u32 cycles)
{
    struct c3_adc *k = (struct c3_adc *)ctx;
    u64 budget = (u64)cycles * fs_per_cycle(k);
    if (k->busy) {
        if (budget >= k->left)
            onetime_done(k);
        else
            k->left -= budget;
    }
    if (scanning(k)) {
        u64 period = (u64)(2.0 * (double)((reg(k->ctrl2) >> 12) & 0xfffu) * ctrl_fs(k) + 0.5);
        if (!period)
            period = 1;
        while (budget >= k->tleft) {
            budget -= k->tleft;
            k->tleft = period;
            scan_sample(k);
        }
        k->tleft -= budget;
    }
    clocking(k);
}

static int adc_next(void *ctx, u32 *cycles)
{
    struct c3_adc *k = (struct c3_adc *)ctx;
    u64 fpc = fs_per_cycle(k), best = 0;
    int any = 0;
    if (!fpc)
        return 0;
    if (k->busy) {
        best = (k->left + fpc - 1u) / fpc;
        any = 1;
    }
    if (scanning(k)) {
        u64 c = (k->tleft + fpc - 1u) / fpc;
        if (!any || c < best)
            best = c;
        any = 1;
    }
    if (!any)
        return 0;
    *cycles = best == 0 ? 1u : best > 0xffffffffu ? 0xffffffffu : (u32)best;
    return 1;
}

/* TSENS_OUT: the sensor's reading of the room */
static u32 tsens_out(struct c3_adc *k)
{
    static const struct {
        u32 dac;
        int offset;
    } table[] = { { 5, -2 }, { 7, -1 }, { 15, 0 }, { 11, 1 }, { 10, 2 } };
    double t = 25.0, v;
    u32 dac = esk_esp32c3_regi2c_get(k->s, 0x69u, 6u) & 0xfu;
    int offset = 0;
    size_t i;
    if (!(reg(k->tsens) & (1u << 22)) || !(reg(k->sys_en1) & (1u << 10)))
        return 0;
    for (i = 0; i < sizeof table / sizeof table[0]; i++)
        if (table[i].dac == dac)
            offset = table[i].offset;
    if (k->s->host.temperature)
        (void)k->s->host.temperature(k->s->host.ctx, &t);
    v = (t + 20.52 + 27.88 * (double)offset) / 0.4386 + 0.5;
    return v <= 0 ? 0u : v >= 255.0 ? 255u : (u32)v;
}

static u32 adc_read(void *ctx, u32 off, int n)
{
    struct c3_adc *k = (struct c3_adc *)ctx;
    if ((off & ~3u) == esk_rf_off(k->tsens))
        esk_rf_hw(k->tsens, (reg(k->tsens) & ~0xffu) | tsens_out(k));
    return esk_rf_read(k->p, off, n);
}

static void adc_write(void *ctx, u32 off, int n, u32 v)
{
    struct c3_adc *k = (struct c3_adc *)ctx;
    u32 w = off & ~3u, ot0 = reg(k->onetime), ctrl2_0 = reg(k->ctrl2);
    if (w == 0x4Cu) { /* INT_CLR */
        esk_rf_hw(k->raw, reg(k->raw) & ~v);
        update(k);
        return;
    }
    if (w == 0x48u || w == 0x44u)
        return; /* INT_ST, INT_RAW */
    esk_rf_write(k->p, off, n, v);
    if (w == 0x40u)
        update(k);
    if (w == esk_rf_off(k->onetime) && (v & OT_START) && !(ot0 & OT_START) && (v & (OT_SAR1 | OT_SAR2))) {
        u32 code = (v >> 25) & 0xfu;
        k->unit = (v & OT_SAR1) ? 0 : 1;
        k->ch = (int)(code & 7u);
        k->atten = (int)((v >> 23) & 3u);
        k->left = (u64)(25.0 * sar_fs(k) + 0.5);
        if (!k->left)
            k->left = 1;
        k->busy = 1;
    }
    if (w == esk_rf_off(k->ctrl2) && (v & (1u << 24)) && !(ctrl2_0 & (1u << 24))) {
        k->tleft = (u64)(2.0 * (double)((v >> 12) & 0xfffu) * ctrl_fs(k) + 0.5);
        k->pr = 0;
        k->nsent = 0;
    }
    if (w == esk_rf_off(k->ctrl) && (v & (1u << 23))) /* SAR_PATT_P_CLEAR */
        k->pr = 0;
    if (w == esk_rf_off(k->fctrl0) && (v & (1u << 31))) /* FILTER_RESET */
        k->filt_on[0] = k->filt_on[1] = 0;
    if (w == esk_rf_off(k->dma_conf) && (v & (1u << 30))) /* APB_ADC_RESET_FSM */
        k->pr = k->nsent = 0;
    clocking(k);
}

static void adc_reset(void *ctx)
{
    struct c3_adc *k = (struct c3_adc *)ctx;
    k->busy = 0;
    k->pr = k->nsent = 0;
    k->filt_on[0] = k->filt_on[1] = 0;
    k->level = 0;
    esk_esp32c3_source(k->s, SOURCE, 0);
    k->clocked = 0;
    clocking(k);
}

const struct esk_dev_ops esk_esp32c3_adc_ops = {
    "esp32c3-adc", adc_read, adc_write, adc_reset, adc_tick, adc_next,
};

void *esk_esp32c3_adc_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct c3_adc *k = (struct c3_adc *)esk_mach_alloc(s, sizeof *k);
    if (!k)
        return 0;
    memset(k, 0, sizeof *k);
    k->s = s;
    k->p = p;
    k->sys = esk_rf_periph(s->rf, "SYSTEM");
    k->ctrl = esk_rf_reg(p, "CTRL");
    k->ctrl2 = esk_rf_reg(p, "CTRL2");
    k->fctrl1 = esk_rf_reg(p, "FILTER_CTRL1");
    k->fctrl0 = esk_rf_reg(p, "FILTER_CTRL0");
    k->patt[0] = esk_rf_reg(p, "SAR_PATT_TAB1");
    k->patt[1] = esk_rf_reg(p, "SAR_PATT_TAB2");
    k->onetime = esk_rf_reg(p, "ONETIME_SAMPLE");
    k->data[0] = esk_rf_reg(p, "SAR1DATA_STATUS");
    k->data[1] = esk_rf_reg(p, "SAR2DATA_STATUS");
    k->thres[0] = esk_rf_reg(p, "THRES0_CTRL");
    k->thres[1] = esk_rf_reg(p, "THRES1_CTRL");
    k->thres_ctrl = esk_rf_reg(p, "THRES_CTRL");
    k->ena = esk_rf_reg(p, "INT_ENA");
    k->raw = esk_rf_reg(p, "INT_RAW");
    k->st = esk_rf_reg(p, "INT_ST");
    k->dma_conf = esk_rf_reg(p, "DMA_CONF");
    k->clkm = esk_rf_reg(p, "CLKM_CONF");
    k->tsens = esk_rf_reg(p, "APB_TSENS_CTRL");
    k->tsens2 = esk_rf_reg(p, "TSENS_CTRL2");
    k->sys_en1 = k->sys ? esk_rf_reg(k->sys, "PERIP_CLK_EN1") : 0;
    if (!k->ctrl || !k->ctrl2 || !k->fctrl1 || !k->fctrl0 || !k->patt[0] || !k->patt[1] || !k->onetime ||
        !k->data[0] || !k->data[1] || !k->thres[0] || !k->thres[1] || !k->thres_ctrl || !k->ena || !k->raw ||
        !k->st || !k->dma_conf || !k->clkm || !k->tsens || !k->tsens2 || !k->sys_en1)
        return 0;
    return k;
}
