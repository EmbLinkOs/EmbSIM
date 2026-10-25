/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* dac_f4.c -- the STM32F2/F4/F7's 12-bit DAC, two channels (RM0390 14),
 * over the register file, on PA4 (DAC_OUT1) and PA5 (DAC_OUT2).
 *
 *   - the data holding register takes a write of DHR12Rx (12 bits right),
 *     DHR12Lx (left), DHR8Rx (8 bits) or the dual registers DHR12RD,
 *     DHR12LD and DHR8RD (both channels at once); every view reads it back;
 *   - without TENx, DHR moves to DORx one APB1 clock after the write; with
 *     TENx, three APB1 clocks after the trigger TSELx picks (TIM6_TRGO,
 *     TIM8_TRGO, TIM7_TRGO, TIM5_TRGO, TIM2_TRGO, TIM4_TRGO, EXTI_9's
 *     edge, or SWTRIGRx, which clears at once): its rising edge;
 *   - WAVEx with a trigger: noise adds a 12-bit LFSR (X^12 + X^6 + X^4 +
 *     X + 1, from 0xAAA) masked by MAMPx to DHR; triangle adds a counter
 *     going up to 2^(MAMPx+1) - 1 and down to 0, a step a trigger; the sum
 *     keeps its 12 bits;
 *   - DMAENx with a trigger requests DAC_CHx to bring the next DHR; a
 *     trigger while that request is still waiting is an underrun: DMAUDRx
 *     sets (rc_w1), with DMAUDRIEx the TIM6_DAC interrupt is pending, and
 *     no request is made until DMAUDRx is cleared;
 *   - with ENx the pin carries VREF+ * DOR / 4096 (VREF+ the supply),
 *     whatever its GPIO mode, as on the part: through the output buffer
 *     (BOFFx 0: 0.2 V to VREF+ - 0.2 V, a low output impedance) or without
 *     it (15 kOhm, the full range), settling at 1 V/us.
 *
 * Not modelled: the buffer's offset and gain errors, its load regulation
 * beyond the output resistance, the conversion's integral and
 * differential non-linearity (the voltage is the ideal one). */
#include <stdio.h>
#include <string.h>

#include "mcu/stm32/stm32.h"

#define CR_EN (1u << 0)
#define CR_BOFF (1u << 1)
#define CR_TEN (1u << 2)
#define CR_DMAEN (1u << 12)
#define CR_DMAUDRIE (1u << 13)
#define SR_DMAUDR (1u << 13)

static const char *const dac_triggers[8] = {
    "TIM6_TRGO", "TIM8_TRGO", "TIM7_TRGO", "TIM5_TRGO", "TIM2_TRGO", "TIM4_TRGO", "EXTI9", "",
};

struct dac {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *cr, *swtrigr, *dhr12r[2], *dhr12l[2], *dhr8r[2], *dhr12rd, *dhr12ld, *dhr8rd;
    struct esk_rf_reg *dor[2], *sr;
    int irq, running;
    u32 dhr[2];
    u32 lfsr[2], tri[2];
    int tri_down[2];
    int pending[2];     /* a transfer to DOR is under way */
    u64 left[2], since;
    int dma_wait[2];    /* a request waits for the DMA */
    int trig_level[2];  /* the trigger's level, for its rising edge */
};

static u32 reg(const struct esk_rf_reg *r) { return r ? (u32)r->value : 0; }

/* channel ch's half of CR */
static u32 cr_of(const struct dac *d, int ch) { return reg(d->cr) >> (16 * ch) & 0xffffu; }

static void views(struct dac *d)
{
    int ch;
    for (ch = 0; ch < 2; ch++) {
        esk_rf_hw(d->dhr12r[ch], d->dhr[ch]);
        esk_rf_hw(d->dhr12l[ch], d->dhr[ch] << 4);
        esk_rf_hw(d->dhr8r[ch], d->dhr[ch] >> 4);
    }
    esk_rf_hw(d->dhr12rd, d->dhr[1] << 16 | d->dhr[0]);
    esk_rf_hw(d->dhr12ld, d->dhr[1] << 20 | d->dhr[0] << 4);
    esk_rf_hw(d->dhr8rd, (d->dhr[1] >> 4) << 8 | d->dhr[0] >> 4);
}

/* core cycles per APB1 clock */
static u64 apb1(const struct dac *d)
{
    u32 core = esk_mach_core_hz(d->s), pclk = esk_stm32_pclk(d->s, 1);
    return core && pclk && core >= pclk ? core / pclk : 1;
}

static void transfer_in(struct dac *d, int ch, u64 clocks)
{
    d->pending[ch] = 1;
    d->left[ch] = clocks * apb1(d);
    d->since = d->s->cycles;
    esk_rf_clock(d->p, &d->running, 1);
}

static void update(struct dac *d)
{
    int ch;
    for (ch = 0; ch < 2; ch++) {
        char sig[12];
        snprintf(sig, sizeof sig, "DAC_CH%d", ch + 1);
        esk_stm32_dma_request(d->s, sig, d->dma_wait[ch]);
    }
    if (((reg(d->sr) & SR_DMAUDR) && (cr_of(d, 0) & CR_DMAUDRIE)) ||
        ((reg(d->sr) >> 16 & SR_DMAUDR) && (cr_of(d, 1) & CR_DMAUDRIE)))
        esk_rf_interrupt(d->p, d->irq);
}

/* DHR (with the wave) into DOR */
static void to_dor(struct dac *d, int ch)
{
    u32 cr = cr_of(d, ch), wave = cr >> 6 & 3u, mamp = cr >> 8 & 15u, v = d->dhr[ch];
    if (cr & CR_TEN) {
        if (wave == 1) {
            u32 l = d->lfsr[ch], fb = (l ^ l >> 1 ^ l >> 4 ^ l >> 6) & 1u;
            d->lfsr[ch] = (l >> 1 | fb << 11) & 0xfffu;
            v += d->lfsr[ch] & ((2u << (mamp > 11 ? 11 : mamp)) - 1u);
        } else if (wave >= 2) {
            u32 top = (2u << (mamp > 11 ? 11 : mamp)) - 1u;
            if (!d->tri_down[ch]) {
                if (d->tri[ch] < top)
                    d->tri[ch]++;
                if (d->tri[ch] >= top)
                    d->tri_down[ch] = 1;
            } else {
                if (d->tri[ch])
                    d->tri[ch]--;
                if (!d->tri[ch])
                    d->tri_down[ch] = 0;
            }
            v += d->tri[ch];
        }
    }
    esk_rf_hw(d->dor[ch], v & 0xfffu);
    if (cr & CR_EN)
        esk_stm32f4_port_changed(d->s, 0);
}

static void trigger(struct dac *d, int ch)
{
    u32 cr = cr_of(d, ch);
    if (!(cr & CR_EN) || !(cr & CR_TEN))
        return;
    transfer_in(d, ch, 3);
    if (cr & CR_DMAEN) {
        if (d->dma_wait[ch]) {
            esk_rf_hw(d->sr, d->sr->value | (u64)SR_DMAUDR << (16 * ch)); /* an underrun */
            d->dma_wait[ch] = 0;
        } else if (!(reg(d->sr) >> (16 * ch) & SR_DMAUDR)) {
            d->dma_wait[ch] = 1;
        }
    }
    update(d);
}

static void dac_tick(void *ctx, u32 cycles)
{
    struct dac *d = (struct dac *)ctx;
    int ch;
    if (!d->running)
        return;
    cycles = esk_mach_after(d->s, cycles, d->since);
    d->since = 0;
    for (ch = 0; ch < 2; ch++) {
        if (!d->pending[ch])
            continue;
        if (d->left[ch] > cycles) {
            d->left[ch] -= cycles;
            continue;
        }
        d->pending[ch] = 0;
        to_dor(d, ch);
    }
    if (!d->pending[0] && !d->pending[1])
        esk_rf_clock(d->p, &d->running, 0);
}

static int dac_next(void *ctx, u32 *cycles)
{
    struct dac *d = (struct dac *)ctx;
    u64 best = ~(u64)0;
    int ch;
    if (!d->running)
        return 0;
    for (ch = 0; ch < 2; ch++)
        if (d->pending[ch] && d->left[ch] < best)
            best = d->left[ch];
    if (best == ~(u64)0)
        return 0;
    *cycles = best > 0xffffffffu ? 0xffffffffu : (u32)(best ? best : 1);
    return 1;
}

static u32 dac_read(void *ctx, u32 off, int n)
{
    struct dac *d = (struct dac *)ctx;
    return esk_rf_read(d->p, off, n);
}

static void dac_write(void *ctx, u32 off, int n, u32 v)
{
    struct dac *d = (struct dac *)ctx;
    int ch, wrote[2] = { 0, 0 };
    u32 was = reg(d->cr);
    if (off == esk_rf_off(d->sr)) {
        esk_rf_hw(d->sr, d->sr->value & ~(u64)(v & (SR_DMAUDR | SR_DMAUDR << 16))); /* rc_w1 */
        update(d);
        return;
    }
    esk_rf_write(d->p, off, n, v);
    for (ch = 0; ch < 2; ch++) {
        if (off == esk_rf_off(d->dhr12r[ch]))
            d->dhr[ch] = v & 0xfffu, wrote[ch] = 1;
        else if (off == esk_rf_off(d->dhr12l[ch]))
            d->dhr[ch] = v >> 4 & 0xfffu, wrote[ch] = 1;
        else if (off == esk_rf_off(d->dhr8r[ch]))
            d->dhr[ch] = (v & 0xffu) << 4, wrote[ch] = 1;
    }
    if (off == esk_rf_off(d->dhr12rd)) {
        d->dhr[0] = v & 0xfffu;
        d->dhr[1] = v >> 16 & 0xfffu;
        wrote[0] = wrote[1] = 1;
    } else if (off == esk_rf_off(d->dhr12ld)) {
        d->dhr[0] = v >> 4 & 0xfffu;
        d->dhr[1] = v >> 20 & 0xfffu;
        wrote[0] = wrote[1] = 1;
    } else if (off == esk_rf_off(d->dhr8rd)) {
        d->dhr[0] = (v & 0xffu) << 4;
        d->dhr[1] = (v >> 8 & 0xffu) << 4;
        wrote[0] = wrote[1] = 1;
    }
    if (wrote[0] || wrote[1]) {
        views(d);
        for (ch = 0; ch < 2; ch++) {
            if (!wrote[ch])
                continue;
            d->dma_wait[ch] = 0; /* the DMA brought it */
            if (!(cr_of(d, ch) & CR_TEN))
                transfer_in(d, ch, 1);
        }
        update(d);
        return;
    }
    if (off == esk_rf_off(d->swtrigr)) {
        for (ch = 0; ch < 2; ch++)
            if ((v >> ch & 1u) && (cr_of(d, ch) >> 3 & 7u) == 7u)
                trigger(d, ch);
        esk_rf_hw(d->swtrigr, 0);
        return;
    }
    if (off == esk_rf_off(d->cr)) {
        for (ch = 0; ch < 2; ch++) {
            u32 c = cr_of(d, ch), o = was >> (16 * ch) & 0xffffu;
            if ((c >> 6 & 3u) != (o >> 6 & 3u)) {
                d->lfsr[ch] = 0xaaau; /* WAVE written: the generators start again */
                d->tri[ch] = 0;
                d->tri_down[ch] = 0;
            }
            if (!(c & CR_DMAEN))
                d->dma_wait[ch] = 0;
        }
        if ((was ^ reg(d->cr)) & (CR_EN | CR_BOFF | (CR_EN | CR_BOFF) << 16))
            esk_stm32f4_port_changed(d->s, 0);
        update(d);
    }
}

static void dac_reset(void *ctx)
{
    struct dac *d = (struct dac *)ctx;
    int ch;
    d->running = 0;
    for (ch = 0; ch < 2; ch++) {
        d->dhr[ch] = 0;
        d->lfsr[ch] = 0xaaau;
        d->tri[ch] = 0;
        d->tri_down[ch] = 0;
        d->pending[ch] = d->dma_wait[ch] = d->trig_level[ch] = 0;
        d->left[ch] = 0;
    }
}

const struct esk_dev_ops esk_stm32f4_dac_ops = {
    "stm32f4-dac", dac_read, dac_write, dac_reset, dac_tick, dac_next,
};

static struct dac *dac_of(struct esk_mach *s)
{
    int i;
    struct esk_rf_periph *p;
    for (i = 0; s->rf && (p = esk_rf_periph_at(s->rf, i)) != 0; i++)
        if (p->ops == &esk_stm32f4_dac_ops && !esk_rf_gated(p))
            return (struct dac *)p->ctx;
    return 0;
}

void esk_stm32_dac_trigger(struct esk_mach *s, const char *sig, int level)
{
    struct dac *d = dac_of(s);
    int ch;
    if (!d)
        return;
    for (ch = 0; ch < 2; ch++) {
        u32 tsel = cr_of(d, ch) >> 3 & 7u;
        int was;
        if (tsel == 7u || strcmp(dac_triggers[tsel], sig))
            continue;
        was = d->trig_level[ch];
        d->trig_level[ch] = level != 0;
        if (!was && level)
            trigger(d, ch);
    }
}

void esk_stm32_dac_dma_end(struct esk_mach *s, const char *sig)
{
    (void)s;
    (void)sig; /* the DAC's requests go on: a circular stream feeds it */
}

int esk_stm32f4_dac_out(struct esk_mach *s, int port, int bit, double *volts, double *rout)
{
    struct dac *d = port == 0 && (bit == 4 || bit == 5) ? dac_of(s) : 0;
    int ch = bit - 4;
    double vref = s->vdd > 0.0 ? s->vdd : 3.3, v;
    if (!d || !(cr_of(d, ch) & CR_EN))
        return 0;
    v = vref * (double)reg(d->dor[ch]) / 4096.0;
    if (!(cr_of(d, ch) & CR_BOFF)) {
        if (v < 0.2)
            v = 0.2;
        if (v > vref - 0.2)
            v = vref - 0.2;
        *rout = 1.0;
    } else {
        *rout = 15000.0;
    }
    *volts = v;
    return 1;
}

void *esk_stm32f4_dac_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct dac d, *c;
    int ch;
    memset(&d, 0, sizeof d);
    d.s = s;
    d.p = p;
    d.cr = esk_rf_reg(p, "CR");
    d.swtrigr = esk_rf_reg(p, "SWTRIGR");
    d.dhr12rd = esk_rf_reg(p, "DHR12RD");
    d.dhr12ld = esk_rf_reg(p, "DHR12LD");
    d.dhr8rd = esk_rf_reg(p, "DHR8RD");
    d.sr = esk_rf_reg(p, "SR");
    for (ch = 0; ch < 2; ch++) {
        char n[12];
        snprintf(n, sizeof n, "DHR12R%d", ch + 1);
        d.dhr12r[ch] = esk_rf_reg(p, n);
        snprintf(n, sizeof n, "DHR12L%d", ch + 1);
        d.dhr12l[ch] = esk_rf_reg(p, n);
        snprintf(n, sizeof n, "DHR8R%d", ch + 1);
        d.dhr8r[ch] = esk_rf_reg(p, n);
        snprintf(n, sizeof n, "DOR%d", ch + 1);
        d.dor[ch] = esk_rf_reg(p, n);
        if (!d.dhr12r[ch] || !d.dhr12l[ch] || !d.dhr8r[ch] || !d.dor[ch])
            return 0;
    }
    if (!d.cr || !d.swtrigr || !d.dhr12rd || !d.dhr12ld || !d.dhr8rd || !d.sr)
        return 0;
    d.irq = esk_rf_irq(p, "DAC");
    c = (struct dac *)esk_mach_alloc(s, sizeof *c);
    if (!c)
        return 0;
    *c = d;
    dac_reset(c);
    return c;
}
