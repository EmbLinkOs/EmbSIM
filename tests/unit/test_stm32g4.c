/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* test_stm32g4.c -- the STM32G474RE on the machine the catalog's
 * description builds, with the NUCLEO-G474RE's clocks (a 24 MHz crystal on
 * OSC_IN/OSC_OUT, the 32.768 kHz LSE crystal): its clock tree and power
 * control (rcc_g4.c, pwr_g4.c), flash interface (flash_g4.c), GPIO, SYSCFG
 * and EXTI (gpio_f4.c, syscfg_f4.c, exti.c), its USARTs and LPUART
 * (usart_v2.c), the programmable CRC (crc.c) and the watchdogs (wdg.c).
 * The program's accesses are made from here in the order STM32CubeG4's HAL
 * makes them (RM0440). */
#include "harness.h"
#include "mcu/build.h"
#include "mcu/cortexm/cortexm.h"
#include "mcu/stm32/stm32.h"
#include "util/json.h"

#define RCC 0x40021000u
#define RCC_CR (RCC + 0x00u)
#define RCC_CFGR (RCC + 0x08u)
#define RCC_PLLCFGR (RCC + 0x0Cu)
#define RCC_CIER (RCC + 0x18u)
#define RCC_CIFR (RCC + 0x1Cu)
#define RCC_CICR (RCC + 0x20u)
#define RCC_AHB1ENR (RCC + 0x48u)
#define RCC_AHB2ENR (RCC + 0x4Cu)
#define RCC_APB1ENR1 (RCC + 0x58u)
#define RCC_APB1ENR2 (RCC + 0x5Cu)
#define RCC_APB2ENR (RCC + 0x60u)
#define RCC_CCIPR (RCC + 0x88u)
#define RCC_BDCR (RCC + 0x90u)
#define RCC_CSR (RCC + 0x94u)
#define RCC_CRRCR (RCC + 0x98u)
#define PWR 0x40007000u
#define PWR_CR1 (PWR + 0x00u)
#define PWR_CR3 (PWR + 0x08u)
#define PWR_SR1 (PWR + 0x10u)
#define PWR_SR2 (PWR + 0x14u)
#define PWR_SCR (PWR + 0x18u)
#define PWR_CR5 (PWR + 0x80u)
#define FLASH 0x40022000u
#define FLASH_ACR (FLASH + 0x00u)
#define FLASH_KEYR (FLASH + 0x08u)
#define FLASH_OPTKEYR (FLASH + 0x0Cu)
#define FLASH_SR (FLASH + 0x10u)
#define FLASH_CR (FLASH + 0x14u)
#define FLASH_OPTR (FLASH + 0x20u)
#define FLASH_WRP1AR (FLASH + 0x2Cu)
#define GPIOA 0x48000000u
#define GPIOC 0x48000800u
#define MODER 0x00u
#define PUPDR 0x0Cu
#define IDR 0x10u
#define ODR 0x14u
#define BSRR 0x18u
#define AFRL 0x20u
#define BRR 0x28u
#define SYSCFG 0x40010000u
#define EXTI 0x40010400u
#define LPUART1 0x40008000u
#define USART1 0x40013800u
#define U_CR1 0x00u
#define U_CR2 0x04u
#define U_CR3 0x08u
#define U_BRR 0x0Cu
#define U_RQR 0x18u
#define U_ISR 0x1Cu
#define U_ICR 0x20u
#define U_RDR 0x24u
#define U_TDR 0x28u
#define CRC 0x40023000u
#define IWDG 0x40003000u
#define WWDG 0x40002C00u
#define DBGMCU 0xE0042000u
#define NVIC_ISPR1 0xE000E204u
#define NVIC_ISPR2 0xE000E208u

typedef struct g4 {
    esk_alloc a;
    esk_json *mcu, *regs;
    struct esk_mach m;
    int warnings;
    char codes[256];
    esk_mach_pin pin[7][16];
    int ext[7][16];
    struct { int c, nbits, stop; u32 bits, bit_cycles; char unit[12]; } frame[32];
    int nframe;
} g4;

static unsigned char *slurp(const char *rel, size_t *len)
{
    char path[1024];
    FILE *f;
    unsigned char *buf;
    long n;
    snprintf(path, sizeof path, "%s/%s", EMBSIM_SOURCE_DIR, rel);
    f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (unsigned char *)malloc(n > 0 ? (size_t)n : 1);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    *len = (size_t)n;
    return buf;
}

static esk_json *load_json(esk_alloc *a, const char *rel)
{
    size_t len = 0;
    unsigned char *text = slurp(rel, &len);
    esk_json *j;
    int line = 0;
    const char *err = NULL;
    if (!text)
        return NULL;
    j = esk_json_parse(a, (const char *)text, len, &line, &err);
    free(text);
    return j;
}

static void r_diag(void *ctx, int sev, const char *code, const char *msg)
{
    g4 *f = (g4 *)ctx;
    size_t n = strlen(f->codes);
    (void)msg;
    if (sev < ES_SEV_WARNING)
        return;
    f->warnings++;
    snprintf(f->codes + n, sizeof f->codes - n, "%s ", code);
}

static void r_pin_cfg(void *ctx, int port, int bit, const esk_mach_pin *cfg)
{
    g4 *f = (g4 *)ctx;
    if (port >= 0 && port < 7 && bit >= 0 && bit < 16)
        f->pin[port][bit] = *cfg;
}

static int r_pin_in(void *ctx, int port, int bit, int *level)
{
    g4 *f = (g4 *)ctx;
    if (port < 0 || port >= 7 || bit < 0 || bit > 15 || f->ext[port][bit] < 0)
        return 0;
    *level = f->ext[port][bit];
    return 1;
}

static void r_uart_frame(void *ctx, const char *unit, int c, u32 bits, int nbits, int stop, u32 bit_cycles)
{
    g4 *f = (g4 *)ctx;
    if (f->nframe < 32) {
        f->frame[f->nframe].c = c;
        f->frame[f->nframe].bits = bits;
        f->frame[f->nframe].nbits = nbits;
        f->frame[f->nframe].stop = stop;
        f->frame[f->nframe].bit_cycles = bit_cycles;
        snprintf(f->frame[f->nframe].unit, sizeof f->frame[f->nframe].unit, "%s", unit);
        f->nframe++;
    }
}

static int g4_open(g4 *f)
{
    char why[200];
    es_status st;
    int i, j;
    memset(f, 0, sizeof *f);
    for (i = 0; i < 7; i++)
        for (j = 0; j < 16; j++)
            f->ext[i][j] = -1;
    esk_alloc_init(&f->a, NULL);
    f->mcu = load_json(&f->a, "catalog/mcus/st/stm32g474re/mcu.json");
    f->regs = load_json(&f->a, "catalog/mcus/st/stm32g474re/registers.json");
    CHECK(f->mcu != NULL && f->regs != NULL);
    esk_mach_init(&f->m, &f->a);
    f->m.host.ctx = f;
    f->m.host.diag = r_diag;
    f->m.host.pin_cfg = r_pin_cfg;
    f->m.host.pin_in = r_pin_in;
    f->m.host.uart_frame = r_uart_frame;
    f->m.hse_clock = 0;   /* X3: a 24 MHz crystal */
    f->m.lse_hz = 32768u; /* X2 */
    st = esk_mach_build(&f->m, f->mcu, f->regs, 24000000u, why, sizeof why);
    if (st != ES_OK) {
        FAIL(why);
        return 0;
    }
    CHECK_EQ(esk_bus_debug_write(&f->m.bus, 0x08000000u, 4, 0x20001000u), 0);
    CHECK_EQ(esk_bus_debug_write(&f->m.bus, 0x08000004u, 4, 0x08000101u), 0);
    CHECK_EQ(esk_bus_debug_write(&f->m.bus, 0x08000100u, 4, 0xbf00e7feu), 0); /* b . */
    for (i = 0; i < f->m.bus.nrg; i++) /* as an image's load leaves them: stores reach the flash interface */
        f->m.bus.rg[i].rom = f->m.bus.rg[i].kind == ESK_MEM_FLASH || f->m.bus.rg[i].kind == ESK_MEM_ROM;
    esk_mach_reset(&f->m);
    return 1;
}

static void g4_close(g4 *f)
{
    esk_mach_free(&f->m);
    esk_json_free(&f->a, f->mcu);
    esk_json_free(&f->a, f->regs);
    CHECK_EQ(f->a.live_bytes, 0);
}

static u32 rd(g4 *f, u32 a)
{
    u32 v = 0;
    CHECK_EQ(esk_bus_read(&f->m.bus, a, 4, &v), 0);
    return v;
}

static void wr(g4 *f, u32 a, u32 v) { CHECK_EQ(esk_bus_write(&f->m.bus, a, 4, v), 0); }
static void wr8(g4 *f, u32 a, u32 v) { CHECK_EQ(esk_bus_write(&f->m.bus, a, 1, v), 0); }

/* the core runs (its program a loop in place) for `us` microseconds */
static void run_us(g4 *f, double us)
{
    u32 hz = esk_mach_core_hz(&f->m);
    u64 end = f->m.cycles + (u64)((double)(hz ? hz : 16000000u) * us / 1e6);
    while (f->m.cycles < end && !f->m.reset_req)
        if (!esk_mach_run(&f->m, end - f->m.cycles, 0) && f->m.asleep)
            esk_mach_idle(&f->m, end - f->m.cycles);
}

/* the microseconds until the bits at `a` are all set (or all clear), or -1 */
static long wait_bits(g4 *f, u32 a, u32 mask, int set, long max_us)
{
    double us = 0;
    while (us <= (double)max_us) {
        u32 hz = esk_mach_core_hz(&f->m);
        u64 from = f->m.cycles;
        u32 v = rd(f, a) & mask;
        if (set ? v == mask : v == 0)
            return (long)(us + 0.5);
        run_us(f, max_us > 100000 ? 100.0 : 0.25);
        us += (double)(f->m.cycles - from) * 1e6 / (double)(hz ? hz : 16000000u);
    }
    return -1;
}

/* SystemClock_Config of CubeG4's NUCLEO-G474RE examples: range 1 boost,
 * HSI16 through the PLL (M /4, N 85, R /2) to 170 MHz, four wait states,
 * the AHB at /2 for the switch (RM0440 7.2.7) and back to /1 */
static void clock_170(g4 *f, int from_hse)
{
    wr(f, RCC_APB1ENR1, rd(f, RCC_APB1ENR1) | 1u << 28); /* PWREN */
    wr(f, PWR_CR5, rd(f, PWR_CR5) & ~(1u << 8));         /* R1MODE 0: boost */
    if (from_hse) {
        wr(f, RCC_CR, rd(f, RCC_CR) | 1u << 16); /* HSEON */
        CHECK(wait_bits(f, RCC_CR, 1u << 17, 1, 5000) > 1000); /* HSERDY: the crystal's 2 ms */
        /* M /6, N 85, R /2: 24 / 6 x 85 / 2 = 170 MHz */
        wr(f, RCC_PLLCFGR, 3u | 5u << 4 | 85u << 8 | 1u << 24);
    } else {
        wr(f, RCC_PLLCFGR, 2u | 3u << 4 | 85u << 8 | 1u << 24); /* HSI16 /4 x 85 /2, PLLREN */
    }
    wr(f, RCC_CR, rd(f, RCC_CR) | 1u << 24); /* PLLON */
    CHECK(wait_bits(f, RCC_CR, 1u << 25, 1, 100) >= 10); /* PLLRDY: its 15 us lock */
    wr(f, FLASH_ACR, (rd(f, FLASH_ACR) & ~0xFu) | 4u);  /* LATENCY 4 */
    wr(f, RCC_CFGR, (rd(f, RCC_CFGR) & ~0xF0u) | 0x80u); /* HPRE /2 */
    wr(f, RCC_CFGR, (rd(f, RCC_CFGR) & ~3u) | 3u);       /* SW PLL */
    CHECK_EQ(rd(f, RCC_CFGR) >> 2 & 3u, 3u);             /* SWS */
    run_us(f, 1.0);
    wr(f, RCC_CFGR, rd(f, RCC_CFGR) & ~0xF0u); /* HPRE /1 */
}

/* ---- the tests -------------------------------------------------------------- */

static void test_reset(void)
{
    g4 f;
    if (!g4_open(&f))
        return;
    CHECK_STR(esk_json_get_str(f.mcu, "name", ""), "STM32G474RE");
    CHECK_EQ(rd(&f, RCC_CR), 0x00000500u);      /* HSION, HSIRDY */
    CHECK_EQ(rd(&f, RCC_CFGR), 0x00000005u);    /* SW, SWS: HSI16 */
    CHECK_EQ(rd(&f, RCC_PLLCFGR), 0x00001000u);
    CHECK_EQ(rd(&f, RCC_CSR) & 0xFE000000u, 0x0C000000u); /* BORRSTF, PINRSTF */
    CHECK_EQ(rd(&f, RCC_AHB1ENR), 0x00000100u); /* FLASHEN */
    CHECK_EQ(esk_mach_core_hz(&f.m), 16000000u);
    CHECK_EQ(esk_stm32_pclk(&f.m, 1), 16000000u);
    CHECK_EQ(esk_stm32_timclk(&f.m, 2), 16000000u);
    /* the option bytes as from the factory, their complements checked */
    CHECK_EQ(rd(&f, FLASH_OPTR), 0xFFEFF8AAu);
    CHECK_EQ(rd(&f, FLASH_SR) & (1u << 15), 0u);  /* no OPTVERR */
    CHECK_EQ(rd(&f, FLASH_CR), 0xC0000000u);      /* LOCK, OPTLOCK */
    CHECK_EQ(rd(&f, DBGMCU) & 0xFFFu, 0x469u);    /* DEV_ID */
    /* PWR's clock is off: its registers read 0 and say why once */
    CHECK_EQ(rd(&f, PWR_CR1), 0u);
    CHECK(strstr(f.codes, "mcu.clock_gated") != NULL);
    wr(&f, RCC_APB1ENR1, rd(&f, RCC_APB1ENR1) | 1u << 28);
    CHECK_EQ(rd(&f, PWR_CR1), 0x00000200u);       /* VOS range 1 */
    CHECK_EQ(rd(&f, PWR_CR5), 0x00000100u);       /* R1MODE: normal */
    CHECK_EQ(rd(&f, PWR_SR2) & (1u << 8), 1u << 8); /* REGLPS */
    g4_close(&f);
}

static void test_170mhz(void)
{
    g4 f;
    if (!g4_open(&f))
        return;
    f.warnings = 0;
    clock_170(&f, 0);
    CHECK_EQ(esk_mach_core_hz(&f.m), 170000000u);
    CHECK_EQ(esk_stm32_pclk(&f.m, 2), 170000000u);
    CHECK_EQ(esk_stm32_timclk(&f.m, 1), 170000000u);
    CHECK_EQ(f.warnings, 0);
    /* APB1 /2: the timers run at twice it */
    wr(&f, RCC_CFGR, (rd(&f, RCC_CFGR) & ~(7u << 8)) | 4u << 8);
    CHECK_EQ(esk_stm32_pclk(&f.m, 1), 85000000u);
    CHECK_EQ(esk_stm32_timclk(&f.m, 1), 170000000u);
    /* the PLL used as SYSCLK cannot be turned off, nor HSI16 under it */
    wr(&f, RCC_CR, rd(&f, RCC_CR) & ~(1u << 24 | 1u << 8));
    CHECK_EQ(rd(&f, RCC_CR) & (1u << 24 | 1u << 8), 1u << 24 | 1u << 8);
    g4_close(&f);
    /* the same from the board's 24 MHz crystal */
    if (!g4_open(&f))
        return;
    f.warnings = 0;
    clock_170(&f, 1);
    CHECK_EQ(esk_mach_core_hz(&f.m), 170000000u);
    CHECK_EQ(f.warnings, 0);
    g4_close(&f);
}

static void test_out_of_spec(void)
{
    g4 f;
    if (!g4_open(&f))
        return;
    /* range 1 normal (R1MODE left 1): 170 MHz is above its 150 MHz */
    wr(&f, RCC_APB1ENR1, rd(&f, RCC_APB1ENR1) | 1u << 28);
    wr(&f, RCC_PLLCFGR, 2u | 3u << 4 | 85u << 8 | 1u << 24);
    wr(&f, RCC_CR, rd(&f, RCC_CR) | 1u << 24);
    CHECK(wait_bits(&f, RCC_CR, 1u << 25, 1, 100) >= 0);
    f.codes[0] = 0;
    wr(&f, FLASH_ACR, (rd(&f, FLASH_ACR) & ~0xFu) | 2u); /* too few wait states too */
    wr(&f, RCC_CFGR, (rd(&f, RCC_CFGR) & ~3u) | 3u);
    CHECK(strstr(f.codes, "mcu.clock_out_of_spec") != NULL);
    CHECK(strstr(f.codes, "mcu.flash_wait_states") != NULL);
    g4_close(&f);
    /* a VCO input below 2.66 MHz */
    if (!g4_open(&f))
        return;
    wr(&f, RCC_PLLCFGR, 2u | 15u << 4 | 85u << 8 | 1u << 24); /* 16 / 16 = 1 MHz */
    f.codes[0] = 0;
    wr(&f, RCC_CR, rd(&f, RCC_CR) | 1u << 24);
    CHECK(strstr(f.codes, "mcu.clock_out_of_spec") != NULL);
    g4_close(&f);
}

static void test_startup_and_irq(void)
{
    g4 f;
    long t;
    if (!g4_open(&f))
        return;
    /* HSI48 ready after its start-up, LSI after 80 us, each flag with its
     * interrupt */
    wr(&f, RCC_CIER, 1u << 10 | 1u << 0);
    wr(&f, RCC_CRRCR, 1u);
    t = wait_bits(&f, RCC_CRRCR, 2u, 1, 50);
    CHECK(t >= 2 && t <= 4);
    CHECK(rd(&f, RCC_CIFR) & 1u << 10);
    CHECK(rd(&f, 0xE000E200u) & 1u << 5); /* RCC's interrupt, 5, pending */
    wr(&f, RCC_CICR, 1u << 10);
    CHECK_EQ(rd(&f, RCC_CIFR) & 1u << 10, 0u);
    wr(&f, RCC_CSR, 1u);
    t = wait_bits(&f, RCC_CSR, 2u, 1, 500);
    CHECK(t >= 75 && t <= 90);
    /* LSE: two seconds, written only with PWR's DBP */
    wr(&f, RCC_APB1ENR1, rd(&f, RCC_APB1ENR1) | 1u << 28);
    wr(&f, RCC_BDCR, 1u);
    CHECK_EQ(rd(&f, RCC_BDCR) & 1u, 0u);
    wr(&f, PWR_CR1, rd(&f, PWR_CR1) | 1u << 8);
    wr(&f, RCC_BDCR, 1u);
    t = wait_bits(&f, RCC_BDCR, 2u, 1, 3000000);
    CHECK(t >= 1990000 && t <= 2010000);
    g4_close(&f);
}

static void test_reset_flags(void)
{
    g4 f;
    if (!g4_open(&f))
        return;
    wr(&f, RCC_CSR, rd(&f, RCC_CSR) | 1u << 23); /* RMVF */
    CHECK_EQ(rd(&f, RCC_CSR) & 0xFE000000u, 0u);
    f.m.reset_cause = ESK_RESET_SOFTWARE;
    esk_mach_reset(&f.m);
    CHECK_EQ(rd(&f, RCC_CSR) & 0xFE000000u, 1u << 26 | 1u << 28); /* PINRSTF, SFTRSTF */
    f.m.reset_cause = ESK_RESET_WATCHDOG;
    esk_mach_reset(&f.m);
    CHECK_EQ(rd(&f, RCC_CSR) & 0xFE000000u, 1u << 26 | 1u << 28 | 1u << 29);
    g4_close(&f);
}

static void test_gpio_exti(void)
{
    g4 f;
    if (!g4_open(&f))
        return;
    CHECK_EQ(rd(&f, GPIOA + MODER), 0u); /* its clock off */
    wr(&f, RCC_AHB2ENR, 1u | 1u << 2); /* GPIOA, GPIOC */
    CHECK_EQ(rd(&f, GPIOA + MODER), 0xABFFFFFFu);
    CHECK_EQ(rd(&f, GPIOA + PUPDR), 0x64000000u);
    /* PA5 (LD2) an output: BSRR sets it, BRR clears it */
    wr(&f, GPIOA + MODER, (rd(&f, GPIOA + MODER) & ~(3u << 10)) | 1u << 10);
    wr(&f, GPIOA + BSRR, 1u << 5);
    CHECK_EQ(f.pin[0][5].drive, 1);
    CHECK_EQ(rd(&f, GPIOA + ODR) & 0x20u, 0x20u);
    wr(&f, GPIOA + BRR, 1u << 5);
    CHECK_EQ(f.pin[0][5].drive, 0);
    CHECK_EQ(rd(&f, GPIOA + BRR), 0u);
    /* PC13 (B1) an input: EXTI13 on port C, rising edge, its interrupt
     * EXTI15_10 (40) */
    wr(&f, RCC_APB2ENR, 1u); /* SYSCFGEN */
    wr(&f, GPIOC + MODER, rd(&f, GPIOC + MODER) & ~(3u << 26));
    wr(&f, SYSCFG + 0x14u, 2u << 4); /* EXTICR4: EXTI13 on PC */
    wr(&f, EXTI + 0x08u, 1u << 13);  /* RTSR1 */
    wr(&f, EXTI + 0x00u, rd(&f, EXTI + 0x00u) | 1u << 13); /* IMR1 */
    CHECK_EQ(rd(&f, EXTI + 0x00u) & 0x1F840000u, 0x1F840000u); /* the direct lines' */
    f.ext[2][13] = 0;
    esk_stm32f4_pins_changed(&f.m);
    f.ext[2][13] = 1;
    esk_stm32f4_pins_changed(&f.m);
    CHECK(rd(&f, EXTI + 0x14u) & 1u << 13); /* PR1 */
    CHECK(rd(&f, NVIC_ISPR1) & 1u << 8);    /* 40 */
    wr(&f, EXTI + 0x14u, 1u << 13);
    CHECK_EQ(rd(&f, EXTI + 0x14u) & 1u << 13, 0u);
    /* the second bank: SWIER2 sets PR2 (line 38, PVM1) */
    wr(&f, EXTI + 0x20u, 1u << 6);
    wr(&f, EXTI + 0x30u, 1u << 6);
    CHECK(rd(&f, EXTI + 0x34u) & 1u << 6);
    g4_close(&f);
}

/* LPUART1 on PA2/PA3 (AF12) at 115200 from PCLK1, the VCP's */
static void lpuart_setup(g4 *f, u32 pclk)
{
    wr(f, RCC_AHB2ENR, rd(f, RCC_AHB2ENR) | 1u);
    wr(f, RCC_APB1ENR2, rd(f, RCC_APB1ENR2) | 1u);
    wr(f, GPIOA + MODER, (rd(f, GPIOA + MODER) & ~(0xFu << 4)) | 0xAu << 4);
    wr(f, GPIOA + AFRL, (rd(f, GPIOA + AFRL) & ~(0xFFu << 8)) | 0xCCu << 8);
    /* UART_SetConfig: BRR = 256 x PCLK / baud, rounded */
    wr(f, LPUART1 + U_BRR, (u32)(((u64)pclk * 256u + 115200u / 2) / 115200u));
    wr(f, LPUART1 + U_CR1, 1u << 3 | 1u << 2); /* TE, RE */
    wr(f, LPUART1 + U_CR1, rd(f, LPUART1 + U_CR1) | 1u); /* UE */
}

static void test_lpuart(void)
{
    g4 f;
    struct esk_rx_port *rx;
    if (!g4_open(&f))
        return;
    clock_170(&f, 0);
    lpuart_setup(&f, 170000000u);
    /* UART_CheckIdleState waits for TEACK and REACK */
    CHECK_EQ(rd(&f, LPUART1 + U_ISR) & (3u << 21), 3u << 21);
    CHECK_EQ(f.pin[0][2].drive, 1); /* TX idles high */
    f.nframe = 0;
    wr(&f, LPUART1 + U_TDR, 'H');
    CHECK_EQ(rd(&f, LPUART1 + U_ISR) & (1u << 6), 0u); /* TC clear */
    CHECK(wait_bits(&f, LPUART1 + U_ISR, 1u << 6, 1, 1000) > 80); /* the idle frame, then 'H' */
    CHECK_EQ(f.nframe, 1);
    CHECK_EQ(f.frame[0].c, 'H');
    CHECK_STR(f.frame[0].unit, "LPUART1");
    CHECK_NEAR(f.frame[0].bit_cycles, 170e6 / 115200.0, 1.0);
    /* receiving: RXNE, then ORE once RDR holds a frame; ICR clears it */
    rx = esk_mach_rx_find(&f.m, "LPUART1");
    CHECK(rx != NULL);
    if (rx) {
        CHECK(rx->room(rx->ctx));
        rx->put(rx->ctx, 'a');
        CHECK(rd(&f, LPUART1 + U_ISR) & 1u << 5);
        rx->put(rx->ctx, 'b');
        CHECK(rd(&f, LPUART1 + U_ISR) & 1u << 3);
        CHECK_EQ(rd(&f, LPUART1 + U_RDR), (u32)'a');
        CHECK_EQ(rd(&f, LPUART1 + U_ISR) & 1u << 5, 0u);
        wr(&f, LPUART1 + U_ICR, 1u << 3);
        CHECK_EQ(rd(&f, LPUART1 + U_ISR) & 1u << 3, 0u);
        /* the FIFO: eight frames held, then RXFF */
        wr(&f, LPUART1 + U_CR1, rd(&f, LPUART1 + U_CR1) & ~1u);
        wr(&f, LPUART1 + U_CR1, rd(&f, LPUART1 + U_CR1) | 1u << 29 | 1u);
        {
            int k;
            for (k = 0; k < 8; k++)
                rx->put(rx->ctx, '0' + k);
            CHECK(rd(&f, LPUART1 + U_ISR) & 1u << 24); /* RXFF */
            CHECK_EQ(rd(&f, LPUART1 + U_ISR) & 1u << 3, 0u);
            for (k = 0; k < 8; k++)
                CHECK_EQ(rd(&f, LPUART1 + U_RDR), (u32)('0' + k));
            CHECK_EQ(rd(&f, LPUART1 + U_ISR) & 1u << 5, 0u);
        }
    }
    g4_close(&f);
}

static void test_usart_formats(void)
{
    g4 f;
    if (!g4_open(&f))
        return;
    /* USART1 on PC4/PC5 (AF7), 16 MHz PCLK2, 9600 baud, 7 data bits with
     * even parity, MSB first, its kernel clock from HSI16 */
    wr(&f, RCC_AHB2ENR, rd(&f, RCC_AHB2ENR) | 1u << 2);
    wr(&f, RCC_APB2ENR, rd(&f, RCC_APB2ENR) | 1u << 14);
    wr(&f, RCC_CCIPR, 2u); /* USART1SEL: HSI16 */
    wr(&f, GPIOC + MODER, (rd(&f, GPIOC + MODER) & ~(0xFu << 8)) | 0xAu << 8);
    wr(&f, GPIOC + AFRL, (rd(&f, GPIOC + AFRL) & ~(0xFFu << 16)) | 0x77u << 16);
    wr(&f, USART1 + U_BRR, 16000000u / 9600u);
    wr(&f, USART1 + U_CR2, 1u << 19); /* MSBFIRST */
    wr(&f, USART1 + U_CR1, 1u << 28 | 1u << 10 | 1u << 3); /* M1 (7 bits), PCE, TE */
    wr(&f, USART1 + U_CR1, rd(&f, USART1 + U_CR1) | 1u);
    f.nframe = 0;
    wr(&f, USART1 + U_TDR, 0x41u); /* 'A' */
    CHECK(wait_bits(&f, USART1 + U_ISR, 1u << 6, 1, 3000) > 0);
    CHECK_EQ(f.nframe, 1);
    CHECK_EQ(f.frame[0].nbits, 7);
    /* 6 data bits and the parity bit: 0x41's six (0b000001) MSB first are
     * 0b100000 on the line, even parity over them 1 */
    CHECK_EQ(f.frame[0].bits & 0x7Fu, 0x60u);
    CHECK_NEAR(f.frame[0].bit_cycles, 16e6 / 9600.0, 2.0);
    g4_close(&f);
}

static void test_crc(void)
{
    g4 f;
    static const u8 d16[9] = { 0x4D, 0x3C, 0x2B, 0x1A, 0xBE, 0x71, 0xC9, 0x8A, 0x5E };
    int k;
    if (!g4_open(&f))
        return;
    wr(&f, RCC_AHB1ENR, rd(&f, RCC_AHB1ENR) | 1u << 12); /* CRCEN */
    /* the default unit: CRC-32 of one word */
    wr(&f, CRC + 0x08u, 1u);
    wr(&f, CRC + 0x00u, 0x12345678u);
    CHECK_EQ(rd(&f, CRC + 0x00u), 0xDF8A8A2Bu);
    /* CubeG4's CRC_UserDefinedPolynomial: 8-bit polynomial 0x9B over the
     * word 0x1234, INIT 0xFF: 0xEF */
    wr(&f, CRC + 0x14u, 0x9Bu);
    wr(&f, CRC + 0x08u, 2u << 3 | 1u);
    wr(&f, CRC + 0x00u, 0x1234u);
    CHECK_EQ(rd(&f, CRC + 0x00u), 0xEFu);
    /* CRC_Data_Reversing_16bit_CRC: CCITT's 0x1021, INIT 0x5ABE, the
     * input reversed by words (a byte written reversed as a byte), the
     * output reversed: RBIT(0xC20A) >> 16 */
    wr(&f, CRC + 0x14u, 0x1021u);
    wr(&f, CRC + 0x10u, 0x5ABEu);
    wr(&f, CRC + 0x08u, 1u << 7 | 3u << 5 | 1u << 3 | 1u);
    for (k = 0; k < 8; k += 4)
        wr(&f, CRC + 0x00u, (u32)d16[k] << 24 | (u32)d16[k + 1] << 16 | (u32)d16[k + 2] << 8 | d16[k + 3]);
    wr8(&f, CRC + 0x00u, d16[8]);
    CHECK_EQ(rd(&f, CRC + 0x00u) & 0xFFFFu, 0x5043u);
    g4_close(&f);
}

static void flash_unlock(g4 *f)
{
    wr(f, FLASH_KEYR, 0x45670123u);
    wr(f, FLASH_KEYR, 0xCDEF89ABu);
    CHECK_EQ(rd(f, FLASH_CR) & 1u << 31, 0u);
}

static void test_flash(void)
{
    g4 f;
    u32 v = 0;
    long t;
    if (!g4_open(&f))
        return;
    flash_unlock(&f);
    /* HAL_FLASH_Program's double word at 0x08040000 (bank 2, page 0): PG,
     * the first word, the second; BSY for 81.7 us */
    wr(&f, FLASH_CR, 1u);
    wr(&f, 0x08040000u, 0x11223344u);
    CHECK_EQ(rd(&f, FLASH_SR) & 1u << 16, 0u);
    wr(&f, 0x08040004u, 0x55667788u);
    CHECK(rd(&f, FLASH_SR) & 1u << 16);
    t = wait_bits(&f, FLASH_SR, 1u << 16, 0, 1000);
    CHECK(t >= 80 && t <= 84); /* the program runs from bank 1: bank 2's work does not stall it */
    CHECK_EQ(esk_bus_debug_read(&f.m.bus, 0x08040004u, 4, &v), 0);
    CHECK_EQ(v, 0x55667788u);
    /* a halfword is SIZERR, a double word not erased PROGERR */
    CHECK_EQ(esk_bus_write(&f.m.bus, 0x08040008u, 2, 0x1234u), 0);
    CHECK(rd(&f, FLASH_SR) & 1u << 6);
    wr(&f, FLASH_SR, 0xFFFFu);
    wr(&f, 0x08040000u, 0x1u);
    wr(&f, 0x08040004u, 0x1u);
    CHECK(rd(&f, FLASH_SR) & 1u << 3);
    wr(&f, FLASH_SR, 0xFFFFu);
    /* a page erase: PER, PNB 0, BKER (bank 2), STRT; 22 ms */
    wr(&f, FLASH_CR, 1u << 1 | 1u << 11);
    wr(&f, FLASH_CR, rd(&f, FLASH_CR) | 1u << 16);
    t = wait_bits(&f, FLASH_SR, 1u << 16, 0, 100000);
    CHECK(t >= 21000 && t <= 23000);
    CHECK_EQ(esk_bus_debug_read(&f.m.bus, 0x08040000u, 4, &v), 0);
    CHECK_EQ(v, 0xFFFFFFFFu);
    /* the option bytes: write protection of bank 1's pages 2 to 3 */
    wr(&f, FLASH_CR, 1u << 31); /* LOCK */
    flash_unlock(&f);
    wr(&f, FLASH_OPTKEYR, 0x08192A3Bu);
    wr(&f, FLASH_OPTKEYR, 0x4C5D6E7Fu);
    CHECK_EQ(rd(&f, FLASH_CR) & 1u << 30, 0u);
    wr(&f, FLASH_WRP1AR, 2u | 3u << 16);
    wr(&f, FLASH_CR, rd(&f, FLASH_CR) | 1u << 17); /* OPTSTRT */
    CHECK_EQ(esk_bus_debug_read(&f.m.bus, 0x1FFF7818u, 4, &v), 0);
    CHECK_EQ(v & 0x007F007Fu, 0x00030002u);
    wr(&f, FLASH_CR, rd(&f, FLASH_CR) | 1u << 27); /* OBL_LAUNCH: a reset */
    CHECK(f.m.reset_req);
    f.m.reset_req = 0;
    esk_mach_reset(&f.m);
    CHECK(rd(&f, RCC_CSR) & 1u << 25); /* OBLRSTF */
    CHECK_EQ(rd(&f, FLASH_WRP1AR) & 0x007F007Fu, 0x00030002u);
    CHECK_EQ(rd(&f, FLASH_SR) & 1u << 15, 0u);
    flash_unlock(&f);
    wr(&f, FLASH_CR, 1u);
    wr(&f, 0x08001000u, 0u); /* page 2 */
    CHECK(rd(&f, FLASH_SR) & 1u << 4); /* WRPERR */
    g4_close(&f);
}

static void test_iwdg_window(void)
{
    g4 f;
    if (!g4_open(&f))
        return;
    /* HAL_IWDG_Init with a window: start, access, PR /4, RLR 1000, WINR 500;
     * refreshing above the window resets the part */
    wr(&f, IWDG + 0x00u, 0xCCCCu);
    CHECK(rd(&f, RCC_CSR) & 1u << 1 || wait_bits(&f, RCC_CSR, 2u, 1, 200) >= 0); /* LSI forced on */
    wr(&f, IWDG + 0x00u, 0x5555u);
    wr(&f, IWDG + 0x04u, 0u);
    wr(&f, IWDG + 0x08u, 1000u);
    wr(&f, IWDG + 0x10u, 500u);
    CHECK(rd(&f, IWDG + 0x0Cu) & 4u); /* WVU */
    CHECK(wait_bits(&f, IWDG + 0x0Cu, 7u, 0, 1000) >= 0);
    wr(&f, IWDG + 0x00u, 0xAAAAu); /* counter 1000 > 500: too early */
    CHECK(f.m.reset_req);
    CHECK_EQ(f.m.reset_cause, ESK_RESET_WATCHDOG);
    g4_close(&f);
}

static void test_wwdg_prescaler(void)
{
    g4 f;
    long t;
    if (!g4_open(&f))
        return;
    /* WDGTB at CFR[13:11]: /128 at 16 MHz, 4096 x 128 / 16 MHz = 32.77 ms a
     * count; EWIF once 0x7F has counted down to 0x40 */
    wr(&f, RCC_APB1ENR1, rd(&f, RCC_APB1ENR1) | 1u << 11);
    wr(&f, WWDG + 0x04u, 7u << 11 | 1u << 9 | 0x7Fu);
    wr(&f, WWDG + 0x00u, 0x7Fu);
    t = wait_bits(&f, WWDG + 0x08u, 1u, 1, 2500000);
    CHECK(t > 0);
    CHECK_NEAR((double)t, 63.0 * 4096.0 * 128.0 / 16.0, 33000.0);
    g4_close(&f);
}

static void test_stop(void)
{
    g4 f;
    if (!g4_open(&f))
        return;
    clock_170(&f, 0);
    /* Stop 1 (LPMS 001) then a wake-up: the part runs on HSI16 again */
    wr(&f, PWR_CR1, (rd(&f, PWR_CR1) & ~7u) | 1u);
    CHECK(f.m.deep_sleep != NULL);
    if (f.m.deep_sleep) {
        f.m.deep_sleep(&f.m);
        CHECK(f.m.clocks_stopped);
        CHECK_EQ(rd(&f, RCC_CR) & 1u << 24, 0u);
        CHECK_EQ(esk_mach_core_hz(&f.m), 16000000u);
        f.m.deep_wake(&f.m);
        CHECK(!f.m.clocks_stopped);
    }
    /* Standby (011) with SRAM2 kept (RRS): SRAM1 lost, SBF after the reset */
    CHECK_EQ(esk_bus_debug_write(&f.m.bus, 0x20000100u, 4, 0x12345678u), 0);
    CHECK_EQ(esk_bus_debug_write(&f.m.bus, 0x20014100u, 4, 0xCAFEF00Du), 0);
    wr(&f, PWR_CR3, rd(&f, PWR_CR3) | 1u << 8 | 1u); /* RRS, EWUP1 (PA0) */
    wr(&f, PWR_CR1, (rd(&f, PWR_CR1) & ~7u) | 3u);
    if (f.m.deep_sleep) {
        u32 v = 0;
        f.m.deep_sleep(&f.m);
        CHECK(f.m.powered_down);
        CHECK_EQ(esk_bus_debug_read(&f.m.bus, 0x20014100u, 4, &v), 0);
        CHECK_EQ(v, 0xCAFEF00Du);
        CHECK_EQ(esk_bus_debug_read(&f.m.bus, 0x20000100u, 4, &v), 0);
        CHECK(v != 0x12345678u);
        /* PA0 rises: WUF1, the part resets out of Standby */
        esk_stm32f4_pwr_pin_edge(&f.m, 0, 0, 0);
        esk_stm32f4_pwr_pin_edge(&f.m, 0, 0, 1);
        CHECK(f.m.reset_req);
        CHECK_EQ(f.m.reset_cause, ESK_RESET_STANDBY);
        f.m.reset_req = 0;
        f.m.powered_down = 0;
        esk_mach_reset(&f.m);
        wr(&f, RCC_APB1ENR1, rd(&f, RCC_APB1ENR1) | 1u << 28);
        CHECK_EQ(rd(&f, PWR_SR1) & (1u << 8 | 1u), 1u << 8 | 1u); /* SBF, WUF1 */
        wr(&f, PWR_SCR, 1u << 8 | 0x1Fu);
        CHECK_EQ(rd(&f, PWR_SR1) & 0x11Fu, 0u);
    }
    g4_close(&f);
}

/* halfwords from `a` (little-endian Thumb code) */
static void code(g4 *f, u32 a, const u16 *h, int n)
{
    int k;
    for (k = 0; k < n; k++)
        CHECK_EQ(esk_bus_debug_write(&f->m.bus, a + 2u * (u32)k, 2, h[k]), 0);
}

static u32 reg(g4 *f, int n)
{
    u8 b[4] = { 0, 0, 0, 0 };
    f->m.cpu->ops->reg_read(f->m.cpu, n, b);
    return (u32)b[0] | (u32)b[1] << 8 | (u32)b[2] << 16 | (u32)b[3] << 24;
}

/* ARMv7-M's ExceptionTaken clears CONTROL.SPSEL: a handler entered from a
 * thread on the process stack reads it 0, and the thread reads it 1 again
 * after the return (CubeG4's CORTEXM_ProcessStack checks both) */
static void test_spsel_in_handler(void)
{
    static const u16 thread[] = {
        0xf242, 0x0000, 0xf2c2, 0x0000, /* movw r0, #0x2000; movt r0, #0x2000 */
        0xf380, 0x8809,                 /* msr psp, r0 */
        0x2002,                         /* movs r0, #2 */
        0xf380, 0x8814,                 /* msr control, r0 */
        0xf3bf, 0x8f6f,                 /* isb */
        0xdf01,                         /* svc #1 */
        0xf3ef, 0x8214,                 /* mrs r2, control */
        0xe7fe,                         /* b . */
    };
    /* the handler stores CONTROL at 0x20003000 (its r0-r3 are the frame's again after the return) */
    static const u16 handler[] = {
        0xf243, 0x0000, 0xf2c2, 0x0000, /* movw r0, #0x3000; movt r0, #0x2000 */
        0xf3ef, 0x8114,                 /* mrs r1, control */
        0x6001,                         /* str r1, [r0] */
        0x4770,                         /* bx lr */
    };
    u32 seen = 0;
    g4 f;
    if (!g4_open(&f))
        return;
    code(&f, 0x08000100u, thread, (int)(sizeof thread / sizeof thread[0]));
    code(&f, 0x08000200u, handler, (int)(sizeof handler / sizeof handler[0]));
    CHECK_EQ(esk_bus_debug_write(&f.m.bus, 0x0800002Cu, 4, 0x08000201u), 0); /* SVCall's vector */
    esk_mach_reset(&f.m);
    CHECK_EQ(esk_bus_debug_write(&f.m.bus, 0x20003000u, 4, 0xdeadbeefu), 0);
    run_us(&f, 20.0);
    CHECK_EQ(esk_bus_debug_read(&f.m.bus, 0x20003000u, 4, &seen), 0);
    CHECK_EQ(seen, 0u);             /* in the handler: SPSEL 0, the main stack */
    CHECK_EQ(reg(&f, 2) & 2u, 2u);  /* back in the thread: the process stack */
    CHECK_EQ(reg(&f, 13), 0x20002000u); /* PSP as it was, the frame popped */
    g4_close(&f);
}

/* DMA1 and DMAMUX (RM0440 12, 13) */
#define DMA1 0x40020000u
#define DMA_ISR (DMA1 + 0x00u)
#define DMA_IFCR (DMA1 + 0x04u)
#define DMA_CCR(k) (DMA1 + 0x08u + 0x14u * ((k) - 1u))
#define DMA_CNDTR(k) (DMA1 + 0x0Cu + 0x14u * ((k) - 1u))
#define DMA_CPAR(k) (DMA1 + 0x10u + 0x14u * ((k) - 1u))
#define DMA_CMAR(k) (DMA1 + 0x14u + 0x14u * ((k) - 1u))
#define DMAMUX 0x40020800u
#define MUX_CCR(c) (DMAMUX + 0x04u * (c))
#define MUX_CSR (DMAMUX + 0x80u)
#define MUX_CFR (DMAMUX + 0x84u)
#define MUX_RGCR(c) (DMAMUX + 0x100u + 0x04u * (c))
#define NVIC_ISPR0 0xE000E200u

static void pc13(g4 *f, int level)
{
    f->ext[2][13] = level;
    esk_stm32f4_pins_changed(&f->m);
}

static void test_dma(void)
{
    g4 f;
    u32 v, k;
    if (!g4_open(&f))
        return;
    wr(&f, RCC_AHB1ENR, rd(&f, RCC_AHB1ENR) | 1u | 1u << 2); /* DMA1EN, DMAMUX1EN */
    /* memory to memory: eight words, both addresses stepping, TCIE */
    for (k = 0; k < 8; k++)
        CHECK_EQ(esk_bus_debug_write(&f.m.bus, 0x20000100u + 4u * k, 4, 0x11110000u + k), 0);
    wr(&f, DMA_CPAR(1), 0x20000100u);
    wr(&f, DMA_CMAR(1), 0x20000200u);
    wr(&f, DMA_CNDTR(1), 8u);
    wr(&f, DMA_CCR(1), 1u << 14 | 2u << 10 | 2u << 8 | 1u << 7 | 1u << 6 | 1u << 1); /* MEM2MEM, 32-bit, MINC, PINC, TCIE */
    wr(&f, DMA_CCR(1), rd(&f, DMA_CCR(1)) | 1u);
    /* while EN: the sizes and CNDTR keep their values */
    wr(&f, DMA_CCR(1), (rd(&f, DMA_CCR(1)) & ~(3u << 8)) | 0u << 8);
    CHECK_EQ(rd(&f, DMA_CCR(1)) >> 8 & 3u, 2u);
    run_us(&f, 20.0);
    for (k = 0; k < 8; k++) {
        CHECK_EQ(esk_bus_debug_read(&f.m.bus, 0x20000200u + 4u * k, 4, &v), 0);
        CHECK_EQ(v, 0x11110000u + k);
    }
    CHECK_EQ(rd(&f, DMA_CNDTR(1)), 0u);
    CHECK_EQ(rd(&f, DMA_ISR) & 0xFu, 0x7u);    /* GIF1, TCIF1, HTIF1 */
    CHECK_EQ(rd(&f, DMA_CCR(1)) & 1u, 1u);     /* not circular: EN stays, nothing more is served */
    CHECK(rd(&f, NVIC_ISPR0) & 1u << 11);       /* DMA1_CH1 */
    wr(&f, DMA_CNDTR(1), 5u);
    CHECK_EQ(rd(&f, DMA_CNDTR(1)), 0u);        /* read-only while EN */
    wr(&f, DMA_IFCR, 1u << 1);                 /* CTCIF1: HTIF1 is left, so GIF1 too */
    CHECK_EQ(rd(&f, DMA_ISR) & 0xFu, 0x5u);
    wr(&f, DMA_IFCR, 1u);                      /* CGIF1 */
    CHECK_EQ(rd(&f, DMA_ISR) & 0xFu, 0u);
    wr(&f, DMA_CCR(1), 0u);
    /* table 89: bytes read, half-words written zero-extended */
    CHECK_EQ(esk_bus_debug_write(&f.m.bus, 0x20000300u, 4, 0x44332211u), 0);
    wr(&f, DMA_CPAR(1), 0x20000300u);
    wr(&f, DMA_CMAR(1), 0x20000400u);
    wr(&f, DMA_CNDTR(1), 4u);
    wr(&f, DMA_CCR(1), 1u << 14 | 1u << 10 | 0u << 8 | 1u << 7 | 1u << 6 | 1u);
    run_us(&f, 10.0);
    CHECK_EQ(esk_bus_debug_read(&f.m.bus, 0x20000400u, 4, &v), 0);
    CHECK_EQ(v, 0x00220011u);
    CHECK_EQ(esk_bus_debug_read(&f.m.bus, 0x20000404u, 4, &v), 0);
    CHECK_EQ(v, 0x00440033u);
    wr(&f, DMA_CCR(1), 0u);
    wr(&f, DMA_IFCR, 0xFFFFFFFFu);
    /* LPUART1_TX (request 35) through DMAMUX channel 0 to DMA1 channel 1 */
    lpuart_setup(&f, 16000000u);
    CHECK(wait_bits(&f, LPUART1 + U_ISR, 1u << 21, 1, 100) >= 0); /* TEACK */
    CHECK_EQ(esk_bus_debug_write(&f.m.bus, 0x20000500u, 4, 0x00214948u), 0); /* "HI!" */
    wr(&f, MUX_CCR(0), 35u);
    wr(&f, DMA_CPAR(1), LPUART1 + U_TDR);
    wr(&f, DMA_CMAR(1), 0x20000500u);
    wr(&f, DMA_CNDTR(1), 3u);
    wr(&f, DMA_CCR(1), 1u << 7 | 1u << 4 | 1u); /* MINC, DIR (from memory), EN */
    f.nframe = 0;
    wr(&f, LPUART1 + U_CR3, rd(&f, LPUART1 + U_CR3) | 1u << 7); /* DMAT */
    run_us(&f, 400.0);
    CHECK_EQ(f.nframe, 3);
    CHECK_EQ(f.frame[0].c, 'H');
    CHECK_EQ(f.frame[2].c, '!');
    CHECK_EQ(rd(&f, DMA_CNDTR(1)), 0u);
    wr(&f, DMA_CCR(1), 0u);
    wr(&f, DMA_IFCR, 0xFFFFFFFFu);
    /* synchronised on EXTI13 (PC13's rising edge): two requests (NBREQ 1)
     * a sync event, an event each two; the event (input 16) triggers
     * generator 0, whose request (1) DMA1 channel 2 serves */
    wr(&f, RCC_AHB2ENR, rd(&f, RCC_AHB2ENR) | 1u << 2);
    wr(&f, RCC_APB2ENR, rd(&f, RCC_APB2ENR) | 1u); /* SYSCFG */
    wr(&f, SYSCFG + 0x14u, 2u << 4);               /* EXTI13 on PC */
    wr(&f, GPIOC + MODER, rd(&f, GPIOC + MODER) & ~(3u << 26)); /* PC13 an input */
    pc13(&f, 0);
    for (k = 0; k < 8; k++)
        CHECK_EQ(esk_bus_debug_write(&f.m.bus, 0x20000600u + k, 1, 'a' + k), 0);
    wr(&f, MUX_CCR(0), 35u | 1u << 9 | 1u << 16 | 1u << 17 | 1u << 19 | 13u << 24); /* EGE, SE, SPOL rising, NBREQ 1, SYNC_ID 13 */
    wr(&f, MUX_RGCR(0), 16u | 1u << 16 | 1u << 17);  /* SIG_ID 16 (channel 0's event), GE, rising */
    wr(&f, MUX_CCR(1), 1u);                          /* DMAMUX_REQ_GEN0 to DMA1 channel 2 */
    wr(&f, DMA_CPAR(2), 0x20000700u);
    wr(&f, DMA_CMAR(2), 0x20000800u);
    wr(&f, DMA_CNDTR(2), 4u);
    wr(&f, DMA_CCR(2), 2u << 10 | 2u << 8 | 1u << 7 | 1u); /* 32-bit, MINC: one word a generated request */
    wr(&f, DMA_CMAR(1), 0x20000600u);
    wr(&f, DMA_CNDTR(1), 8u);
    wr(&f, DMA_CCR(1), 1u << 7 | 1u << 4 | 1u);
    f.nframe = 0;
    run_us(&f, 400.0);
    CHECK_EQ(f.nframe, 0);                      /* no sync event yet */
    CHECK_EQ(rd(&f, DMA_CNDTR(2)), 4u);
    pc13(&f, 1);
    run_us(&f, 400.0);
    CHECK_EQ(f.nframe, 2);                      /* NBREQ + 1 */
    CHECK_EQ(f.frame[1].c, 'b');
    CHECK_EQ(rd(&f, DMA_CNDTR(2)), 3u);         /* the event's generated request */
    pc13(&f, 0);
    pc13(&f, 1);
    run_us(&f, 400.0);
    CHECK_EQ(f.nframe, 4);
    CHECK_EQ(f.frame[3].c, 'd');
    CHECK_EQ(rd(&f, DMA_CNDTR(2)), 2u);
    CHECK_EQ(rd(&f, MUX_CSR), 0u);
    /* four requests a sync event (NBREQ 3, written with SE and EGE clear):
     * an idle transmitter takes two at once, the others a frame apart, so
     * a sync event right after the first is an overrun: SOF0, DMAMUX_OVR
     * (94) with SOIE */
    wr(&f, MUX_CCR(0), 35u);
    wr(&f, MUX_CCR(0), 35u | 3u << 19);
    wr(&f, MUX_CCR(0), 35u | 3u << 19 | 1u << 8 | 1u << 16 | 1u << 17 | 13u << 24);
    run_us(&f, 400.0);
    pc13(&f, 0);
    pc13(&f, 1);
    pc13(&f, 0);
    pc13(&f, 1);
    CHECK_EQ(rd(&f, MUX_CSR), 1u);
    CHECK(rd(&f, NVIC_ISPR2) & 1u << 30);
    wr(&f, MUX_CFR, 1u);
    CHECK_EQ(rd(&f, MUX_CSR), 0u);
    wr(&f, MUX_CCR(0), 0u);
    wr(&f, DMA_CCR(1), 0u);
    wr(&f, DMA_CCR(2), 0u);
    /* a bus error: TEIF, EN cleared and refused until TEIF is */
    wr(&f, DMA_IFCR, 0xFFFFFFFFu);
    wr(&f, DMA_CPAR(1), 0x60000000u); /* no FMC bank fitted */
    wr(&f, DMA_CMAR(1), 0x20000900u);
    wr(&f, DMA_CNDTR(1), 2u);
    wr(&f, DMA_CCR(1), 1u << 14 | 1u << 3 | 1u);
    run_us(&f, 5.0);
    CHECK_EQ(rd(&f, DMA_ISR) & 0x9u, 0x9u); /* TEIF1, GIF1 */
    CHECK_EQ(rd(&f, DMA_CCR(1)) & 1u, 0u);
    wr(&f, DMA_CCR(1), rd(&f, DMA_CCR(1)) | 1u);
    CHECK_EQ(rd(&f, DMA_CCR(1)) & 1u, 0u);
    wr(&f, DMA_IFCR, 1u << 3);
    wr(&f, DMA_CCR(1), 0u);
    g4_close(&f);
}

/* SPI1, the FIFO SPI (RM0440 42): SCK PB3, MISO PB4, MOSI PB5 (AF5), NSS
 * PA4 (AF5) */
#define SPI1 0x40013000u
#define SPI_CR1 (SPI1 + 0x00u)
#define SPI_CR2 (SPI1 + 0x04u)
#define SPI_SR (SPI1 + 0x08u)
#define SPI_DR (SPI1 + 0x0Cu)
#define GPIOB 0x48000400u

static u32 rd8(g4 *f, u32 a)
{
    u32 v = 0;
    CHECK_EQ(esk_bus_read(&f->m.bus, a, 1, &v), 0);
    return v;
}

static void test_spi_fifo(void)
{
    g4 f;
    u32 v;
    int k;
    if (!g4_open(&f))
        return;
    wr(&f, RCC_AHB2ENR, rd(&f, RCC_AHB2ENR) | 1u | 2u);   /* GPIOA, GPIOB */
    wr(&f, RCC_APB2ENR, rd(&f, RCC_APB2ENR) | 1u << 12); /* SPI1EN */
    wr(&f, GPIOB + MODER, (rd(&f, GPIOB + MODER) & ~(0x3Fu << 6)) | 0x2Au << 6);
    wr(&f, GPIOB + AFRL, (rd(&f, GPIOB + AFRL) & ~(0xFFFu << 12)) | 0x555u << 12);
    wr(&f, GPIOA + MODER, (rd(&f, GPIOA + MODER) & ~(3u << 8)) | 2u << 8);
    wr(&f, GPIOA + AFRL, (rd(&f, GPIOA + AFRL) & ~(0xFu << 16)) | 5u << 16);
    f.ext[1][4] = 1; /* MISO held high: every frame received is ones */
    esk_stm32f4_pins_changed(&f.m);
    CHECK_EQ(rd(&f, SPI_CR2), 0x700u);     /* DS 8 bits out of reset */
    wr(&f, SPI_CR2, 2u << 8);              /* a DS not used: forced to 8 bits */
    CHECK_EQ(rd(&f, SPI_CR2) >> 8 & 15u, 7u);
    wr(&f, SPI_CR2, 7u << 8 | 1u << 12);   /* 8 bits, FRXTH: RXNE at a byte */
    wr(&f, SPI_CR1, 1u << 2 | 1u << 8 | 1u << 9); /* MSTR, SSI, SSM, PCLK / 2 */
    /* the transmit FIFO before SPE: TXE while it is at most half full */
    wr8(&f, SPI_DR, 0x11u);
    CHECK_EQ(rd(&f, SPI_SR) & 0x1802u, 1u << 11 | 2u); /* FTLVL 1/4, TXE */
    wr8(&f, SPI_DR, 0x22u);
    CHECK_EQ(rd(&f, SPI_SR) & 0x1802u, 2u << 11 | 2u); /* 1/2, TXE */
    wr8(&f, SPI_DR, 0x33u);
    CHECK_EQ(rd(&f, SPI_SR) & 0x1802u, 3u << 11);      /* 3/4 reads full, TXE clear */
    wr(&f, SPI_CR1, rd(&f, SPI_CR1) | 1u << 6);        /* SPE: three frames go */
    run_us(&f, 20.0);
    v = rd(&f, SPI_SR);
    CHECK_EQ(v & 0x1880u, 0u);              /* FTLVL empty, BSY clear */
    CHECK_EQ(v >> 9 & 3u, 3u);              /* FRLVL: three bytes */
    CHECK(v & 1u);                          /* RXNE */
    CHECK_EQ(rd(&f, SPI_DR) & 0xFFFFu, 0xFFFFu); /* a word read: two frames packed */
    CHECK_EQ(rd(&f, SPI_SR) >> 9 & 3u, 1u);
    CHECK_EQ(rd8(&f, SPI_DR), 0xFFu);
    CHECK_EQ(rd(&f, SPI_SR) & 0x601u, 0u);  /* empty, RXNE clear */
    /* FRXTH 0: RXNE at two bytes only */
    wr(&f, SPI_CR2, 7u << 8);
    wr8(&f, SPI_DR, 0x44u);
    run_us(&f, 10.0);
    CHECK_EQ(rd(&f, SPI_SR) & 0x601u, 1u << 9); /* a byte in, no RXNE */
    wr8(&f, SPI_DR, 0x55u);
    run_us(&f, 10.0);
    CHECK_EQ(rd(&f, SPI_SR) & 0x601u, 2u << 9 | 1u);
    /* five more frames unread: the FIFO full at four bytes, then OVR */
    for (k = 0; k < 3; k++) {
        wr8(&f, SPI_DR, 0x66u);
        run_us(&f, 10.0);
    }
    v = rd(&f, SPI_SR);
    CHECK_EQ(v >> 9 & 3u, 3u);
    CHECK(v & 1u << 6);                     /* OVR */
    (void)rd(&f, SPI_DR);
    (void)rd(&f, SPI_DR);
    v = rd(&f, SPI_SR);                     /* DR then SR: the read shows OVR and clears it */
    CHECK(v & 1u << 6);
    v = rd(&f, SPI_SR);
    CHECK_EQ(v & (1u << 6), 0u);
    /* NSSP: NSS pulses between frames and stays high after the last */
    wr(&f, SPI_CR1, rd(&f, SPI_CR1) & ~(1u << 6));
    wr(&f, SPI_CR1, (rd(&f, SPI_CR1) & ~(1u << 9 | 1u << 8)));      /* NSS by the hardware */
    wr(&f, SPI_CR2, 7u << 8 | 1u << 12 | 1u << 3 | 1u << 2);        /* NSSP, SSOE */
    wr(&f, SPI_CR1, rd(&f, SPI_CR1) | 1u << 6);
    CHECK_EQ(f.pin[0][4].drive, 0);         /* enabled: NSS low */
    wr(&f, SPI_DR, 0x7788u);                /* two frames back to back */
    run_us(&f, 20.0);
    CHECK_EQ(f.pin[0][4].drive, 1);         /* high after the transfer */
    g4_close(&f);
}

TEST_MAIN_BEGIN
RUN(test_reset);
RUN(test_170mhz);
RUN(test_out_of_spec);
RUN(test_startup_and_irq);
RUN(test_reset_flags);
RUN(test_gpio_exti);
RUN(test_lpuart);
RUN(test_usart_formats);
RUN(test_crc);
RUN(test_flash);
RUN(test_iwdg_window);
RUN(test_wwdg_prescaler);
RUN(test_stop);
RUN(test_spsel_in_handler);
RUN(test_dma);
RUN(test_spi_fifo);
TEST_MAIN_END
