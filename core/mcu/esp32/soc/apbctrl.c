/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* apbctrl.c -- the ESP32's APB_CTRL (TRM v5 chapter 3.2) and the clock
 * tree it, RTC_CNTL and DPORT set together:
 *
 *   - CPU_CLK: RTC_CNTL CLK_CONF's SOC_CLK_SEL picks XTL_CLK (40 MHz),
 *     PLL_CLK, RC_FAST_CLK (8 MHz) or APLL_CLK; from XTL_CLK and
 *     RC_FAST_CLK the CPU runs at the source divided by SYSCLK_CONF's
 *     PRE_DIV_CNT + 1, from PLL_CLK at 80, 160 or 240 MHz as DPORT
 *     CPU_PER_CONF's CPUPERIOD_SEL says (TRM table 3.2-3);
 *   - APB_CLK: 80 MHz from the PLL, else CPU_CLK;
 *   - DATE: bit 31 set, as on chip revision v3 (ESP-IDF reads it with the
 *     eFuse's CHIP_VER_REV1 and REV2 for the major revision).
 * Not yet: APLL_CLK (taken as the PLL's 80 MHz setting), the REF_TICK
 * dividers (XTAL_TICK_CONF and the others keep what is written), the
 * SAR ADC controller's registers. */
#include "mcu/esp32/soc/esp32.h"

#define SYSCLK_CONF 0x00u
#define DATE 0x7Cu
#define RC_FAST_HZ 8000000u


struct e32_apbctrl {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    u32 hz;
};

static u32 cpu_hz(struct e32_apbctrl *k)
{
    u32 sel = esk_esp32_rtc_soc_clk_sel(k->s), div = (esk_rf_read(k->p, SYSCLK_CONF, 4) & 0x3ffu) + 1u;
    if (sel == 1 || sel == 3) {
        u32 per = esk_esp32_dport_cpuperiod(k->s);
        return per == 2 ? 240000000u : per == 1 ? 160000000u : 80000000u;
    }
    return (sel == 2 ? RC_FAST_HZ : ESP32_XTAL_HZ) / div;
}

static u32 core_hz(struct esk_mach *m)
{
    struct e32_apbctrl *k = (struct e32_apbctrl *)m->clock_ctx;
    if (!k)
        return ESP32_XTAL_HZ;
    k->hz = cpu_hz(k);
    return k->hz;
}

u32 esk_esp32_cpu_hz(struct esk_mach *m)
{
    return core_hz(m);
}

u32 esk_esp32_apb_hz(struct esk_mach *m)
{
    u32 sel = esk_esp32_rtc_soc_clk_sel(m);
    return sel == 1 || sel == 3 ? 80000000u : core_hz(m);
}

static u32 ap_read(void *ctx, u32 off, int n)
{
    struct e32_apbctrl *k = (struct e32_apbctrl *)ctx;
    return esk_rf_read(k->p, off, n);
}

static void ap_write(void *ctx, u32 off, int n, u32 v)
{
    struct e32_apbctrl *k = (struct e32_apbctrl *)ctx;
    esk_rf_write(k->p, off, n, v);
    if (off == SYSCLK_CONF)
        k->s->yield = 1; /* the runtime sees the core's new clock now */
}

static void ap_reset(void *ctx)
{
    struct e32_apbctrl *k = (struct e32_apbctrl *)ctx;
    k->hz = ESP32_XTAL_HZ;
}

const struct esk_dev_ops esk_esp32_apbctrl_ops = {
    "esp32-apbctrl", ap_read, ap_write, ap_reset, 0, 0,
};

void *esk_esp32_apbctrl_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct e32_apbctrl *k = (struct e32_apbctrl *)esk_mach_alloc(s, sizeof *k);
    struct esk_rf_reg *date = esk_rf_reg_at(p, DATE);
    if (!k)
        return 0;
    k->s = s;
    k->p = p;
    k->hz = ESP32_XTAL_HZ;
    if (date)
        esk_rf_set_reset(date, date->reset | 0x80000000u);
    s->core_hz = core_hz;
    s->clock_ctx = k;
    return k;
}
