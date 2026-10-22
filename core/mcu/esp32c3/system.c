/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* system.c -- the ESP32-C3's SYSTEM registers (TRM v1.4 chapters 6 and 16):
 *
 *   - CPU_CLK (6.2.4.1, tables 6.2-1 and 6.2-2): SYSCLK_CONF's SOC_CLK_SEL
 *     picks XTAL_CLK (40 MHz) or RC_FAST_CLK (17.5 MHz), divided by
 *     PRE_DIV_CNT + 1, or PLL_CLK, at 80 MHz (CPUPERIOD_SEL 0) or 160 MHz
 *     (1) whether the PLL runs at 480 MHz or 320 MHz (PLL_FREQ_SEL); out of
 *     reset XTAL_CLK / 2, 20 MHz. APB_CLK is 80 MHz from the PLL, CPU_CLK
 *     otherwise (table 6.2-4);
 *   - the peripherals' clock enables (PERIP_CLK_EN0/1, CPU_PERI_CLK_EN): a
 *     peripheral whose bit is clear reads 0 and ignores writes, with a
 *     diagnostic; their resets (PERIP_RST_EN0/1, CPU_PERI_RST_EN): setting
 *     a bit returns the peripheral to its reset state;
 *   - CPU_INTR_FROM_CPU_0-3: the software interrupts, matrix sources 50-53,
 *     at the level written.
 * Not yet: the PLL's own power and lock (selecting it is enough), a
 * peripheral's registers held while its reset bit stays set, the RTC fast
 * memory's CRC, the PVT monitors, and the low-power clock's dividers. */
#include <stdio.h>
#include <string.h>

#include "mcu/esp32c3/esp32c3.h"

#define XTAL_HZ 40000000u
#define RC_FAST_HZ 17500000u

struct gate {
    const char *periph, *reg; /* the peripheral, SYSTEM's enable register */
    int bit;
};

/* SYSTEM's clock enable bits, by peripheral; the reset bits are at the same
 * place in the matching _RST_EN register */
static const struct gate gates[] = {
    { "UART0", "PERIP_CLK_EN0", 2 },         { "UART1", "PERIP_CLK_EN0", 5 },
    { "SPI0", "PERIP_CLK_EN0", 1 },          { "SPI1", "PERIP_CLK_EN0", 1 },
    { "I2S0", "PERIP_CLK_EN0", 4 },          { "SPI2", "PERIP_CLK_EN0", 6 },
    { "I2C0", "PERIP_CLK_EN0", 7 },          { "UHCI0", "PERIP_CLK_EN0", 8 },
    { "RMT", "PERIP_CLK_EN0", 9 },           { "LEDC", "PERIP_CLK_EN0", 11 },
    { "UHCI1", "PERIP_CLK_EN0", 12 },        { "TIMG0", "PERIP_CLK_EN0", 13 },
    { "EFUSE", "PERIP_CLK_EN0", 14 },        { "TIMG1", "PERIP_CLK_EN0", 15 },
    { "TWAI0", "PERIP_CLK_EN0", 19 },        { "USB_DEVICE", "PERIP_CLK_EN0", 23 },
    { "APB_SARADC", "PERIP_CLK_EN0", 28 },   { "SYSTIMER", "PERIP_CLK_EN0", 29 },
    { "AES", "PERIP_CLK_EN1", 1 },           { "SHA", "PERIP_CLK_EN1", 2 },
    { "RSA", "PERIP_CLK_EN1", 3 },           { "DS", "PERIP_CLK_EN1", 4 },
    { "HMAC", "PERIP_CLK_EN1", 5 },          { "DMA", "PERIP_CLK_EN1", 6 },
    { "ASSIST_DEBUG", "CPU_PERI_CLK_EN", 6 },
};
#define NGATES ((int)(sizeof gates / sizeof gates[0]))

struct c3_system {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *sysclk, *per_conf, *intr[4];
    struct esk_rf_reg *rst[NGATES];
    struct esk_rf_periph *q[NGATES];
    u32 hz;
};

static u32 reg(const struct esk_rf_reg *r) { return (u32)r->value; }

static struct c3_system *system_of(struct esk_mach *m)
{
    int i;
    for (i = 0; m->rf; i++) {
        struct esk_rf_periph *p = esk_rf_periph_at(m->rf, i);
        if (!p)
            break;
        if (p->ops == &esk_esp32c3_system_ops)
            return (struct c3_system *)p->ctx;
    }
    return NULL;
}

static u32 cpu_hz(const struct c3_system *k)
{
    u32 c = reg(k->sysclk), sel = (c >> 10) & 3u, div = (c & 0x3ffu) + 1u;
    if (sel == 1)
        return (reg(k->per_conf) & 3u) == 1u ? 160000000u : 80000000u;
    return (sel == 2 ? RC_FAST_HZ : XTAL_HZ) / div;
}

static u32 core_hz(struct esk_mach *m)
{
    struct c3_system *k = (struct c3_system *)m->clock_ctx;
    return k ? k->hz : 0;
}

u32 esk_esp32c3_apb_hz(struct esk_mach *m)
{
    struct c3_system *k = system_of(m);
    if (!k)
        return esk_mach_core_hz(m) > 80000000u ? 80000000u : esk_mach_core_hz(m);
    return ((reg(k->sysclk) >> 10) & 3u) == 1u ? 80000000u : k->hz;
}

static void clock_changed(struct c3_system *k)
{
    u32 hz = cpu_hz(k);
    if (hz != k->hz) {
        k->hz = hz;
        k->s->yield = 1; /* the runtime sees the core's new clock now */
    }
}

static u32 sy_read(void *ctx, u32 off, int n)
{
    struct c3_system *k = (struct c3_system *)ctx;
    return esk_rf_read(k->p, off, n);
}

static void sy_write(void *ctx, u32 off, int n, u32 v)
{
    struct c3_system *k = (struct c3_system *)ctx;
    u32 was[NGATES];
    int i;
    for (i = 0; i < NGATES; i++)
        was[i] = k->rst[i] ? reg(k->rst[i]) : 0;
    esk_rf_write(k->p, off, n, v);
    for (i = 0; i < NGATES; i++)
        if (k->rst[i] && k->q[i] && off == esk_rf_off(k->rst[i]) &&
            (reg(k->rst[i]) & ~was[i] & (1u << gates[i].bit)))
            esk_rf_periph_reset(k->q[i]);
    for (i = 0; i < 4; i++)
        if (off == esk_rf_off(k->intr[i]))
            esk_esp32c3_source(k->s, 50 + i, (int)(reg(k->intr[i]) & 1u));
    clock_changed(k);
}

static void sy_reset(void *ctx)
{
    struct c3_system *k = (struct c3_system *)ctx;
    int i;
    for (i = 0; i < 4; i++)
        esk_esp32c3_source(k->s, 50 + i, 0);
    k->hz = cpu_hz(k);
}

const struct esk_dev_ops esk_esp32c3_system_ops = {
    "esp32c3-system", sy_read, sy_write, sy_reset, 0, 0,
};

void *esk_esp32c3_system_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct c3_system *k = (struct c3_system *)esk_mach_alloc(s, sizeof *k);
    char name[32];
    int i;
    if (!k)
        return 0;
    memset(k, 0, sizeof *k);
    k->s = s;
    k->p = p;
    k->sysclk = esk_rf_reg(p, "SYSCLK_CONF");
    k->per_conf = esk_rf_reg(p, "CPU_PER_CONF");
    for (i = 0; i < 4; i++) {
        snprintf(name, sizeof name, "CPU_INTR_FROM_CPU_%d", i);
        if (!(k->intr[i] = esk_rf_reg(p, name)))
            return 0;
    }
    if (!k->sysclk || !k->per_conf)
        return 0;
    for (i = 0; i < NGATES; i++) {
        struct esk_rf_reg *en = esk_rf_reg(p, gates[i].reg);
        struct esk_rf_periph *q = esk_rf_periph(s->rf, gates[i].periph);
        size_t l = strlen(gates[i].reg);
        char rst[32], *g;
        if (!en || l < 6 || l + 1 > sizeof rst)
            return 0;
        /* PERIP_CLK_EN0 -> PERIP_RST_EN0, CPU_PERI_CLK_EN -> CPU_PERI_RST_EN */
        memcpy(rst, gates[i].reg, l + 1);
        memcpy(strstr(rst, "CLK"), "RST", 3);
        k->rst[i] = esk_rf_reg(p, rst);
        k->q[i] = q;
        if (!q)
            continue;
        l = strlen(p->name) + strlen(gates[i].reg) + 24;
        g = (char *)esk_mach_alloc(s, l);
        if (!g)
            return 0;
        snprintf(g, l, "%s.%s bit %d", p->name, gates[i].reg, gates[i].bit);
        q->gate = en;
        q->gate_mask = 1ull << gates[i].bit;
        q->gate_name = g;
    }
    /* the temperature sensor's registers in APB_SARADC run on their own
     * clock, PERIP_CLK_EN1's TSENS_CLK_EN (ESP-IDF turns on only that one
     * to use the sensor, and the PHY reads it with APB_SARADC's off) */
    {
        static const char *const tsens[] = { "APB_TSENS_CTRL", "TSENS_CTRL2" };
        struct esk_rf_periph *sar = esk_rf_periph(s->rf, "APB_SARADC");
        struct esk_rf_reg *en1 = esk_rf_reg(p, "PERIP_CLK_EN1");
        for (i = 0; sar && en1 && i < 2; i++) {
            struct esk_rf_reg *r = esk_rf_reg(sar, tsens[i]);
            if (r) {
                r->gate = en1;
                r->gate_mask = 1ull << 10;
                r->gate_name = "SYSTEM.PERIP_CLK_EN1 bit 10";
            }
        }
    }
    /* the analog blocks' bus and the radio's registers, which the register
     * file does not have */
    if (esk_esp32c3_regi2c_create(s) != 0 || esk_esp32c3_radio_create(s) != 0 ||
        esk_esp32c3_wifi_create(s) != 0)
        return 0;
    k->hz = cpu_hz(k);
    s->clock_ctx = k;
    s->core_hz = core_hz;
    return k;
}
