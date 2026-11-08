/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* adc.c -- the ESP32's SAR ADCs as the RTC controller drives them (SENS,
 * TRM v5 chapter 29): one conversion at a time on SAR ADC1 or ADC2, as
 * ESP-IDF's adc_oneshot and Arduino's analogRead use them.
 *
 *   - ADC1 reads GPIO36, 37, 38, 39, 32, 33, 34, 35 (channels 0-7), ADC2
 *     GPIO4, 0, 2, 15, 13, 12, 14, 27, 25, 26 (channels 0-9).
 *   - MEAS1_START_SAR (SAR_MEAS_START1) rising starts a conversion of the
 *     channel SAR1_EN_PAD selects (its lowest bit set), at SAR_ATTEN1's
 *     attenuation for it (two bits a channel: 0, 2.5, 6, 11 dB); MEAS1_DONE_SAR
 *     reads 0 until (SAR1_SAMPLE_CYCLE + bits + 1) SAR clocks have passed
 *     (RTC_FAST_CLK's 8 MHz divided by SAR1_CLK_DIV), then 1, with the code
 *     in MEAS1_DATA_SAR; a new start clears it. ADC2 the same with
 *     SAR_MEAS_START2, SAR_ATTEN2 and SAR_READ_CTRL2.
 *   - The code is SAR1_SAMPLE_BIT's 9 to 12 bits wide (the 12-bit code
 *     shifted down). The converter's own output is inverted; SAR1_DATA_INV
 *     (SAR2_DATA_INV) set, as ESP-IDF sets it, gives the code itself.
 *   - The transfer: the line ESP-IDF's Vref calibration fits for the chip
 *     (esp_adc's adc_cali_line_fitting.c, from Espressif's
 *     characterisation of the ESP32): V = a code / 65536 + b mV, with a =
 *     Vref x scale / 4096, scale and b per unit and attenuation (ADC1:
 *     57431, 76236, 105481, 196602 and 75, 78, 107, 142 mV; ADC2: 57236,
 *     76175, 105678, 197170 and 63, 66, 89, 128 mV), Vref the chip's eFuse
 *     ADC_VREF (1100 mV + 7 mV a step); a voltage below b reads 0, one past
 *     the range 4095. ESP-IDF's calibrated voltage is so the pin's within
 *     its rounding.
 * Where it differs inside: the RTC IO MUX's routing of a pad to the ADC
 * (RTC_IO's ADC pad and FUN_SEL bits) is not checked: the pad's voltage
 * is read whatever its function. Not yet: the nonlinearity above about
 * 2450 mV at 11 dB (ESP-IDF's LUT region) and a chip's spread from the
 * fitted line, noise, the ULP's and the digital controller's (DMA,
 * I2S) conversions, the Hall sensor, ADC2's arbitration with Wi-Fi, the
 * amplifier (SAR_MEAS_WAIT, the AMP FSM). */
#include <string.h>

#include "mcu/esp32/soc/esp32.h"

#define READ_CTRL 0x00u
#define ATTEN1 0x34u
#define ATTEN2 0x38u
#define MEAS_START1 0x54u
#define READ_CTRL2 0x90u
#define MEAS_START2 0x94u
#define START_SAR (1u << 17)
#define DONE_SAR (1u << 16)
#define RTC_FAST_HZ 8000000.0

struct e32_adc {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    int busy[2];
    u64 due[2]; /* the conversion's end, in core cycles */
    u32 code[2];
    int clocked;
};

static const int pads[2][10] = {
    { 36, 37, 38, 39, 32, 33, 34, 35, -1, -1 },
    { 4, 0, 2, 15, 13, 12, 14, 27, 25, 26 },
};
static const double scale[2][4] = { { 57431, 76236, 105481, 196602 }, { 57236, 76175, 105678, 197170 } };
static const double offset_mv[2][4] = { { 75, 78, 107, 142 }, { 63, 66, 89, 128 } };

static u32 rd(const struct e32_adc *k, u32 off) { return esk_rf_read(k->p, off, 4); }

static void hw(struct e32_adc *k, u32 off, u32 v)
{
    struct esk_rf_reg *r = esk_rf_reg_at(k->p, off);
    if (r)
        esk_rf_hw(r, v);
}

/* the chip's ADC Vref in mV, from the eFuse (BLOCK0 word 4's ADC_VREF:
 * sign and magnitude, 7 mV a step) */
static double vref_mv(struct esk_mach *m)
{
    u32 bits = (esk_esp32_efuse_word(m, 0, 4) >> 8) & 0x1fu;
    int steps = (int)(bits & 0xfu);
    return 1100.0 + (double)((bits & 0x10u) ? -steps : steps) * 7.0;
}

static u32 convert(struct e32_adc *k, int unit)
{
    struct esk_mach *s = k->s;
    u32 start = rd(k, unit ? MEAS_START2 : MEAS_START1), ctrl = rd(k, unit ? READ_CTRL2 : READ_CTRL);
    u32 pads_on = (start >> 19) & 0xfffu, bits = 9u + ((ctrl >> 16) & 3u), atten;
    int ch, gpio;
    double v = 0, a, d;
    u32 code;
    for (ch = 0; ch < 10 && !(pads_on & (1u << ch)); ch++)
        ;
    gpio = ch < 10 ? pads[unit][ch] : -1;
    atten = ch < 10 ? (rd(k, unit ? ATTEN2 : ATTEN1) >> (2 * ch)) & 3u : 3u;
    if (gpio >= 0 && s->host.pin_volts && !s->host.pin_volts(s->host.ctx, gpio / 16, gpio % 16, &v))
        v = 0;
    a = vref_mv(s) * scale[unit][atten] / 4096.0;
    d = (v * 1000.0 - offset_mv[unit][atten]) * 65536.0 / a + 0.5;
    code = d <= 0 ? 0u : d >= 4095.0 ? 4095u : (u32)d;
    code >>= 12u - bits;
    if (!(ctrl & (1u << (unit ? 29 : 28)))) /* the converter's own, inverted */
        code = ((1u << bits) - 1u) - code;
    return code;
}

static void clocking(struct e32_adc *k)
{
    esk_rf_clock(k->p, &k->clocked, k->busy[0] || k->busy[1]);
}

static void finish(struct e32_adc *k, int unit)
{
    u32 off = unit ? MEAS_START2 : MEAS_START1;
    k->busy[unit] = 0;
    hw(k, off, (rd(k, off) & ~0xffffu) | DONE_SAR | (k->code[unit] & 0xffffu));
}

static void start(struct e32_adc *k, int unit)
{
    u32 off = unit ? MEAS_START2 : MEAS_START1, ctrl = rd(k, unit ? READ_CTRL2 : READ_CTRL);
    u32 div = ctrl & 0xffu, cycles = ((ctrl >> 8) & 0xffu) + 9u + ((ctrl >> 16) & 3u) + 1u;
    double t = (double)cycles * (double)(div ? div : 1u) / RTC_FAST_HZ;
    hw(k, off, rd(k, off) & ~DONE_SAR);
    k->code[unit] = convert(k, unit);
    k->busy[unit] = 1;
    k->due[unit] = k->s->cycles + (u64)(t * (double)esk_mach_core_hz(k->s) + 0.5) + 1u;
}

static void adc_tick(void *ctx, u32 cycles)
{
    struct e32_adc *k = (struct e32_adc *)ctx;
    int u;
    (void)cycles;
    for (u = 0; u < 2; u++)
        if (k->busy[u] && k->s->cycles >= k->due[u])
            finish(k, u);
    clocking(k);
}

static int adc_next(void *ctx, u32 *cycles)
{
    struct e32_adc *k = (struct e32_adc *)ctx;
    u64 best = 0;
    int u, any = 0;
    for (u = 0; u < 2; u++) {
        u64 d;
        if (!k->busy[u])
            continue;
        d = k->due[u] > k->s->cycles ? k->due[u] - k->s->cycles : 1u;
        if (!any || d < best)
            best = d;
        any = 1;
    }
    if (!any)
        return 0;
    *cycles = best > 0xffffffffu ? 0xffffffffu : (u32)best;
    return 1;
}

static u32 adc_read(void *ctx, u32 off, int n)
{
    struct e32_adc *k = (struct e32_adc *)ctx;
    return esk_rf_read(k->p, off, n);
}

static void adc_write(void *ctx, u32 off, int n, u32 v)
{
    struct e32_adc *k = (struct e32_adc *)ctx;
    u32 w = off & ~3u;
    int unit = w == MEAS_START2 ? 1 : 0;
    u32 was = (w == MEAS_START1 || w == MEAS_START2) ? rd(k, w) : 0;
    esk_rf_write(k->p, off, n, v);
    if (w == MEAS_START1 || w == MEAS_START2) {
        u32 now = rd(k, w);
        /* DATA and DONE are the converter's: what was written there is not kept */
        hw(k, w, (now & ~(0xffffu | DONE_SAR)) | (was & (0xffffu | DONE_SAR)));
        if ((now & START_SAR) && !(was & START_SAR))
            start(k, unit);
    }
    clocking(k);
}

static void adc_reset(void *ctx)
{
    struct e32_adc *k = (struct e32_adc *)ctx;
    k->busy[0] = k->busy[1] = 0;
    k->clocked = 0;
    clocking(k);
}

const struct esk_dev_ops esk_esp32_adc_ops = {
    "esp32-sens", adc_read, adc_write, adc_reset, adc_tick, adc_next,
};

void *esk_esp32_adc_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct e32_adc *k = (struct e32_adc *)esk_mach_alloc(s, sizeof *k);
    if (!k)
        return 0;
    memset(k, 0, sizeof *k);
    k->s = s;
    k->p = p;
    return k;
}
