/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* test_stm32f4.c -- the STM32F446's clock tree and power controller
 * (rcc.c, pwr_f4.c), its GPIO ports, SYSCFG and EXTI (gpio_f4.c,
 * syscfg_f4.c, exti.c) on the machine the catalog's description builds, with
 * the NUCLEO-F446RE's clocks: an 8 MHz clock on OSC_IN from the ST-Link's
 * MCO (HSE bypass) and a 32.768 kHz crystal. The program's accesses are
 * made from here in the order STM32CubeF4's SystemClock_Config makes them
 * (RM0390 5.1.4 and 6.3). */
#include "harness.h"
#include "mcu/build.h"
#include "mcu/cortexm/cortexm.h"
#include "mcu/stm32/stm32.h"
#include "util/json.h"

#define RCC 0x40023800u
#define RCC_CR (RCC + 0x00u)
#define RCC_PLLCFGR (RCC + 0x04u)
#define RCC_CFGR (RCC + 0x08u)
#define RCC_APB1ENR (RCC + 0x40u)
#define RCC_BDCR (RCC + 0x70u)
#define RCC_CSR (RCC + 0x74u)
#define RCC_DCKCFGR (RCC + 0x8Cu)
#define PWR_CR 0x40007000u
#define PWR_CSR 0x40007004u
#define FLASH_ACR 0x40023C00u
#define RCC_AHB1RSTR (RCC + 0x10u)
#define RCC_AHB1ENR (RCC + 0x30u)
#define RCC_APB2ENR (RCC + 0x44u)
#define GPIOA 0x40020000u
#define GPIOC 0x40020800u
#define MODER 0x00u
#define OTYPER 0x04u
#define OSPEEDR 0x08u
#define PUPDR 0x0Cu
#define IDR 0x10u
#define ODR 0x14u
#define BSRR 0x18u
#define LCKR 0x1Cu
#define AFRL 0x20u
#define SYSCFG 0x40013800u
#define EXTI 0x40013C00u
#define NVIC_ISPR0 0xE000E200u
#define RCC_APB1ENR (RCC + 0x40u)
#define GPIOB 0x40020400u
#define AFRH 0x24u
#define TIM1 0x40010000u
#define TIM2 0x40000000u
#define TIM3 0x40000400u
#define T_CR1 0x00u
#define T_CR2 0x04u
#define T_SMCR 0x08u
#define T_DIER 0x0Cu
#define T_SR 0x10u
#define T_EGR 0x14u
#define T_CCMR1 0x18u
#define T_CCER 0x20u
#define T_CNT 0x24u
#define T_PSC 0x28u
#define T_ARR 0x2Cu
#define T_RCR 0x30u
#define T_CCR1 0x34u
#define T_CCR2 0x38u
#define T_BDTR 0x44u
#define USART2 0x40004400u
#define U_SR 0x00u
#define U_DR 0x04u
#define U_BRR 0x08u
#define U_CR1 0x0Cu
#define U_CR2 0x10u
#define U_CR3 0x14u
#define DMA1 0x40026000u
#define DMA2 0x40026400u
#define D_LISR 0x00u
#define D_HISR 0x04u
#define D_HIFCR 0x0Cu
#define D_S(s) (0x10u + 0x18u * (s))
#define S_CR 0x00u
#define S_NDTR 0x04u
#define S_PAR 0x08u
#define S_M0AR 0x0Cu
#define S_M1AR 0x10u
#define S_FCR 0x14u

/* PLLM 8, PLLN 360, PLLP /2, PLLQ 2, PLLR 2 from HSE: 1 MHz into the VCO,
 * 360 MHz out of it, 180 MHz SYSCLK (CubeF4's NUCLEO-F446RE templates) */
#define PLLCFGR_180 (8u | 360u << 6 | 1u << 22 | 2u << 24 | 2u << 28)

typedef struct f4 {
    esk_alloc a;
    esk_json *mcu, *regs;
    struct esk_mach m;
    int warnings;
    char codes[256];
    esk_mach_pin pin[8][16]; /* what each pin was last told to be */
    struct { int c, nbits, stop; u32 bits, bit_cycles; } frame[16]; /* frames sent */
    int nframe;
    int ext[8][16];          /* what the board drives: -1 nothing */
} f4;

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
    f4 *f = (f4 *)ctx;
    size_t n = strlen(f->codes);
    (void)msg;
    if (sev < ES_SEV_WARNING)
        return;
    f->warnings++;
    snprintf(f->codes + n, sizeof f->codes - n, "%s ", code);
}

static void r_pin_cfg(void *ctx, int port, int bit, const esk_mach_pin *cfg)
{
    f4 *f = (f4 *)ctx;
    if (port >= 0 && port < 8 && bit >= 0 && bit < 16)
        f->pin[port][bit] = *cfg;
}

static int r_pin_in(void *ctx, int port, int bit, int *level)
{
    f4 *f = (f4 *)ctx;
    if (port < 0 || port >= 8 || bit < 0 || bit > 15 || f->ext[port][bit] < 0)
        return 0;
    *level = f->ext[port][bit];
    return 1;
}

static void r_uart_frame(void *ctx, const char *unit, int c, u32 bits, int nbits, int stop, u32 bit_cycles)
{
    f4 *f = (f4 *)ctx;
    (void)unit;
    if (f->nframe < 16) {
        f->frame[f->nframe].c = c;
        f->frame[f->nframe].bits = bits;
        f->frame[f->nframe].nbits = nbits;
        f->frame[f->nframe].stop = stop;
        f->frame[f->nframe].bit_cycles = bit_cycles;
        f->nframe++;
    }
}

static int f4_open(f4 *f, int hse_clock)
{
    char why[200];
    es_status st;
    int i, j;
    memset(f, 0, sizeof *f);
    for (i = 0; i < 8; i++)
        for (j = 0; j < 16; j++)
            f->ext[i][j] = -1;
    esk_alloc_init(&f->a, NULL);
    f->mcu = load_json(&f->a, "catalog/mcus/st/stm32f446re/mcu.json");
    f->regs = load_json(&f->a, "catalog/mcus/st/stm32f446re/registers.json");
    CHECK(f->mcu != NULL && f->regs != NULL);
    esk_mach_init(&f->m, &f->a);
    f->m.host.ctx = f;
    f->m.host.diag = r_diag;
    f->m.host.pin_cfg = r_pin_cfg;
    f->m.host.pin_in = r_pin_in;
    f->m.host.uart_frame = r_uart_frame;
    f->m.hse_clock = hse_clock;
    f->m.lse_hz = 32768u;
    st = esk_mach_build(&f->m, f->mcu, f->regs, 8000000u, why, sizeof why);
    if (st != ES_OK) {
        FAIL(why);
        return 0;
    }
    CHECK_EQ(esk_bus_debug_write(&f->m.bus, 0, 4, 0x20001000u), 0);
    CHECK_EQ(esk_bus_debug_write(&f->m.bus, 4, 4, 0x101u), 0);
    CHECK_EQ(esk_bus_debug_write(&f->m.bus, 0x100, 4, 0xbf00e7feu), 0); /* b . */
    esk_mach_reset(&f->m);
    return 1;
}

static void f4_close(f4 *f)
{
    esk_mach_free(&f->m);
    esk_json_free(&f->a, f->mcu);
    esk_json_free(&f->a, f->regs);
    CHECK_EQ(f->a.live_bytes, 0);
}

static u32 rd(f4 *f, u32 a)
{
    u32 v = 0;
    CHECK_EQ(esk_bus_read(&f->m.bus, a, 4, &v), 0);
    return v;
}

static void wr(f4 *f, u32 a, u32 v) { CHECK_EQ(esk_bus_write(&f->m.bus, a, 4, v), 0); }

/* SystemClock_Config up to the PLL running and over-drive on: PWR's clock,
 * scale 1, HSE bypass, the PLL from it */
static void pll_from_hse(f4 *f, u32 pllcfgr)
{
    wr(f, RCC_APB1ENR, rd(f, RCC_APB1ENR) | 1u << 28); /* PWREN */
    wr(f, PWR_CR, (rd(f, PWR_CR) & ~(3u << 14)) | 3u << 14); /* VOS scale 1 */
    wr(f, RCC_CR, rd(f, RCC_CR) | 1u << 18);           /* HSEBYP */
    wr(f, RCC_CR, rd(f, RCC_CR) | 1u << 16);           /* HSEON */
    CHECK(rd(f, RCC_CR) & 1u << 17);                   /* HSERDY */
    wr(f, RCC_PLLCFGR, pllcfgr);
    wr(f, RCC_CR, rd(f, RCC_CR) | 1u << 24); /* PLLON */
    CHECK(rd(f, RCC_CR) & 1u << 25);         /* PLLRDY */
}

static void overdrive(f4 *f)
{
    wr(f, PWR_CR, rd(f, PWR_CR) | 1u << 16); /* ODEN */
    CHECK(rd(f, PWR_CSR) & 1u << 16);        /* ODRDY */
    wr(f, PWR_CR, rd(f, PWR_CR) | 1u << 17); /* ODSWEN */
    CHECK(rd(f, PWR_CSR) & 1u << 17);        /* ODSWRDY */
}

/* HAL_RCC_ClockConfig: wait states first, the APBs at /16 while HCLK
 * changes, then HPRE, SYSCLK and the APBs' own dividers */
static void to_pll(f4 *f, u32 latency, u32 ppre1, u32 ppre2)
{
    wr(f, FLASH_ACR, (rd(f, FLASH_ACR) & ~0xFu) | latency);
    wr(f, RCC_CFGR, rd(f, RCC_CFGR) | 7u << 10 | 7u << 13);
    wr(f, RCC_CFGR, rd(f, RCC_CFGR) & ~(0xFu << 4)); /* HPRE /1 */
    wr(f, RCC_CFGR, (rd(f, RCC_CFGR) & ~3u) | 2u);   /* SW PLL_P */
    wr(f, RCC_CFGR, (rd(f, RCC_CFGR) & ~(0x3Fu << 10)) | ppre1 << 10 | ppre2 << 13);
}

/* ---- the tests ------------------------------------------------------------------ */

static void test_reset(void)
{
    f4 f;
    if (!f4_open(&f, 1))
        return;
    CHECK_STR(esk_json_get_str(f.mcu, "name", ""), "STM32F446RE");
    CHECK_EQ(rd(&f, RCC_CR) & 0xFFFF0003u, 0x00000003u); /* HSION, HSIRDY */
    CHECK_EQ(rd(&f, RCC_PLLCFGR), 0x24003010u);
    CHECK_EQ(rd(&f, RCC_CFGR), 0u);
    CHECK_EQ(esk_mach_core_hz(&f.m), 16000000u);
    CHECK_EQ(esk_stm32_pclk(&f.m, 1), 16000000u);
    CHECK_EQ(esk_stm32_timclk(&f.m, 2), 16000000u);
    wr(&f, RCC_APB1ENR, 1u << 28);
    CHECK_EQ(rd(&f, PWR_CR), 0x0000C000u); /* scale 1 out of reset */
    CHECK_EQ(rd(&f, PWR_CSR), 0u);         /* VOSRDY: the PLL is off */
    CHECK_EQ(f.warnings, 0);
    f4_close(&f);
}

static void test_180mhz(void)
{
    f4 f;
    if (!f4_open(&f, 1))
        return;
    pll_from_hse(&f, PLLCFGR_180);
    CHECK(rd(&f, PWR_CSR) & 1u << 14); /* VOSRDY */
    overdrive(&f);
    to_pll(&f, 5, 5, 4); /* APB1 /4, APB2 /2 */
    CHECK_EQ(rd(&f, RCC_CFGR) & 0xFu, 0xAu); /* SWS PLL_P */
    CHECK_EQ(esk_mach_core_hz(&f.m), 180000000u);
    CHECK_EQ(esk_stm32_hclk(&f.m), 180000000u);
    CHECK_EQ(esk_stm32_pclk(&f.m, 1), 45000000u);
    CHECK_EQ(esk_stm32_pclk(&f.m, 2), 90000000u);
    CHECK_EQ(esk_stm32_timclk(&f.m, 1), 90000000u);
    CHECK_EQ(esk_stm32_timclk(&f.m, 2), 180000000u);
    wr(&f, RCC_DCKCFGR, 1u << 24); /* TIMPRE: APB1's timers at HCLK */
    CHECK_EQ(esk_stm32_timclk(&f.m, 1), 180000000u);
    /* what SYSCLK uses cannot be turned off */
    wr(&f, RCC_CR, 0);
    CHECK_EQ(rd(&f, RCC_CR) & (1u << 16 | 1u << 24), 1u << 16 | 1u << 24);
    CHECK_EQ(esk_mach_core_hz(&f.m), 180000000u);
    /* PLL_R as SYSCLK (the F446's): 360 / 2 */
    wr(&f, RCC_CFGR, (rd(&f, RCC_CFGR) & ~3u) | 3u);
    CHECK_EQ(rd(&f, RCC_CFGR) & 0xFu, 0xFu);
    CHECK_EQ(esk_mach_core_hz(&f.m), 180000000u);
    /* HSI went off with the write of 0; SW waits for it */
    CHECK_EQ(rd(&f, RCC_CR) & 3u, 0u);
    wr(&f, RCC_CFGR, rd(&f, RCC_CFGR) & ~3u);
    CHECK_EQ(rd(&f, RCC_CFGR) & 0xFu, 0xCu);
    /* back to HSI, then the PLL and HSE may go */
    wr(&f, RCC_CR, rd(&f, RCC_CR) | 1u);
    CHECK_EQ(esk_mach_core_hz(&f.m), 16000000u);
    wr(&f, RCC_CR, 1u); /* HSEBYP stays: HSE was on as it was written */
    CHECK_EQ(rd(&f, RCC_CR) & 0xFFFF0003u, 0x00040003u);
    CHECK_EQ(rd(&f, PWR_CSR) & (1u << 14 | 1u << 16 | 1u << 17), 0u);
    CHECK_EQ(f.warnings, 0);
    f4_close(&f);
}

/* the NUCLEO drives OSC_IN with a clock: HSE is ready only with HSEBYP, and
 * a board with a crystal (hse_clock 0) only without it */
static void test_hse_kind(void)
{
    f4 f;
    if (!f4_open(&f, 1))
        return;
    wr(&f, RCC_CR, rd(&f, RCC_CR) | 1u << 16);
    CHECK_EQ(rd(&f, RCC_CR) & 1u << 17, 0u);
    /* HSEBYP is written only while HSE is off */
    wr(&f, RCC_CR, rd(&f, RCC_CR) | 1u << 18);
    CHECK_EQ(rd(&f, RCC_CR) & (1u << 17 | 1u << 18), 0u);
    wr(&f, RCC_CR, rd(&f, RCC_CR) & ~(1u << 16));
    wr(&f, RCC_CR, rd(&f, RCC_CR) | 1u << 18);
    wr(&f, RCC_CR, rd(&f, RCC_CR) | 1u << 16);
    CHECK(rd(&f, RCC_CR) & 1u << 17);
    /* SW to HSE switches once it is ready: 8 MHz */
    wr(&f, RCC_CFGR, 1u);
    CHECK_EQ(rd(&f, RCC_CFGR) & 0xFu, 5u);
    CHECK_EQ(esk_mach_core_hz(&f.m), 8000000u);
    f4_close(&f);
    if (!f4_open(&f, 0))
        return;
    wr(&f, RCC_CR, rd(&f, RCC_CR) | 1u << 16 | 1u << 18);
    CHECK_EQ(rd(&f, RCC_CR) & 1u << 17, 0u);
    /* the PLL waits for its source; SW to it waits for the PLL */
    wr(&f, RCC_PLLCFGR, PLLCFGR_180);
    wr(&f, RCC_CR, rd(&f, RCC_CR) | 1u << 24);
    CHECK_EQ(rd(&f, RCC_CR) & 1u << 25, 0u);
    wr(&f, RCC_CFGR, 2u);
    CHECK_EQ(rd(&f, RCC_CFGR) & 0xFu, 2u);
    CHECK_EQ(esk_mach_core_hz(&f.m), 16000000u);
    wr(&f, RCC_CR, rd(&f, RCC_CR) & ~(1u << 16));
    wr(&f, RCC_CR, rd(&f, RCC_CR) & ~(1u << 18));
    wr(&f, RCC_CR, rd(&f, RCC_CR) | 1u << 16);
    CHECK(rd(&f, RCC_CR) & 1u << 25);
    CHECK_EQ(rd(&f, RCC_CFGR) & 0xFu, 0xAu);
    f4_close(&f);
}

static void test_out_of_spec(void)
{
    f4 f;
    if (!f4_open(&f, 1))
        return;
    /* PLLM 4: 2 MHz into the VCO is within; PLLM 2 is 4 MHz, PLLN 50 a
     * 200 MHz VCO, and no over-drive at 200 MHz / 2 = 100 MHz is fine */
    pll_from_hse(&f, 2u | 50u << 6 | 1u << 22 | 2u << 24 | 2u << 28);
    CHECK(strstr(f.codes, "mcu.clock_out_of_spec") != NULL);
    CHECK_EQ(f.warnings, 1);
    f4_close(&f);
    if (!f4_open(&f, 1))
        return;
    /* 180 MHz without over-drive, with APB1 at 90 MHz and no wait states */
    pll_from_hse(&f, PLLCFGR_180);
    to_pll(&f, 0, 4, 4);
    CHECK_EQ(esk_mach_core_hz(&f.m), 180000000u);
    CHECK(strstr(f.codes, "mcu.flash_wait_states") != NULL);
    CHECK_EQ(f.warnings, 3); /* SYSCLK above 168 MHz, PCLK1, wait states */
    /* said once */
    wr(&f, RCC_CFGR, rd(&f, RCC_CFGR) & ~3u);
    wr(&f, RCC_CFGR, rd(&f, RCC_CFGR) | 2u);
    CHECK_EQ(f.warnings, 3);
    f4_close(&f);
}

/* the backup domain: BDCR is written only while PWR's DBP is set */
static void test_backup_domain(void)
{
    f4 f;
    if (!f4_open(&f, 1))
        return;
    wr(&f, RCC_BDCR, 1u); /* LSEON */
    CHECK_EQ(rd(&f, RCC_BDCR), 0u);
    wr(&f, RCC_APB1ENR, 1u << 28);
    wr(&f, PWR_CR, rd(&f, PWR_CR) | 1u << 8); /* DBP */
    wr(&f, RCC_BDCR, 1u);
    CHECK_EQ(rd(&f, RCC_BDCR) & 3u, 3u); /* LSERDY */
    wr(&f, RCC_CSR, 1u);                 /* LSION */
    CHECK_EQ(rd(&f, RCC_CSR) & 3u, 3u);  /* LSIRDY */
    CHECK_EQ(f.warnings, 0);
    f4_close(&f);
}

/* GPIO out of reset: the debug port's pins in AF0 with their pulls, the
 * rest inputs */
static void test_gpio_reset(void)
{
    f4 f;
    if (!f4_open(&f, 1))
        return;
    CHECK_EQ(rd(&f, GPIOA + MODER), 0u); /* the port's clock is off */
    wr(&f, RCC_AHB1ENR, 0x7u);           /* GPIOAEN, GPIOBEN, GPIOCEN */
    CHECK_EQ(rd(&f, GPIOA + MODER), 0xA8000000u);
    CHECK_EQ(rd(&f, GPIOA + PUPDR), 0x64000000u);
    CHECK_EQ(rd(&f, GPIOA + OSPEEDR), 0x0C000000u);
    CHECK_EQ(rd(&f, 0x40020400u + MODER), 0x00000280u);
    CHECK_EQ(f.pin[0][13].pull, 1); /* JTMS-SWDIO pulled up */
    CHECK_EQ(f.pin[0][14].pull, 2); /* JTCK-SWCLK pulled down */
    CHECK_EQ(f.pin[0][13].af, 1);
    CHECK_EQ(f.pin[0][13].drive, -1); /* no debugger: let go */
    CHECK_EQ(f.pin[1][4].pull, 1);    /* NJTRST */
    CHECK_EQ(f.pin[0][5].drive, -1);
    CHECK_EQ(f.pin[0][5].pull, 0);
    CHECK_EQ(rd(&f, GPIOA + IDR) & 0xE000u, 0xA000u); /* PA13, PA15 high, PA14 low */
    CHECK_STR(esk_rf_af_signal(f.m.rf, 0, 13, 0, 0), "JTMS_SWDIO");
    CHECK_EQ(f.warnings, 1); /* the read with GPIOA's clock off */
    CHECK(strstr(f.codes, "mcu.clock_gated") != NULL);
    f4_close(&f);
}

static void test_gpio_modes(void)
{
    f4 f;
    if (!f4_open(&f, 1))
        return;
    wr(&f, RCC_AHB1ENR, 0x7u);
    /* PA5 a push-pull output (the NUCLEO's LD2), low speed */
    wr(&f, GPIOA + MODER, rd(&f, GPIOA + MODER) | 1u << 10);
    CHECK_EQ(f.pin[0][5].drive, 0);
    CHECK_EQ(f.pin[0][5].speed, 1);
    wr(&f, GPIOA + BSRR, 1u << 5);
    CHECK_EQ(f.pin[0][5].drive, 1);
    CHECK_EQ(rd(&f, GPIOA + ODR), 1u << 5);
    CHECK_EQ(rd(&f, GPIOA + IDR) & 1u << 5, 1u << 5);
    CHECK_EQ(rd(&f, GPIOA + BSRR), 0u); /* write-only */
    /* a set wins over a reset written with it; BSRRH as a halfword */
    wr(&f, GPIOA + BSRR, 1u << 21 | 1u << 5);
    CHECK_EQ(rd(&f, GPIOA + ODR), 1u << 5);
    CHECK_EQ(esk_bus_write(&f.m.bus, GPIOA + BSRR + 2, 2, 1u << 5), 0);
    CHECK_EQ(rd(&f, GPIOA + ODR), 0u);
    CHECK_EQ(esk_bus_write(&f.m.bus, GPIOA + BSRR, 2, 1u << 5), 0);
    CHECK_EQ(rd(&f, GPIOA + ODR), 1u << 5);
    /* very high speed */
    wr(&f, GPIOA + OSPEEDR, rd(&f, GPIOA + OSPEEDR) | 3u << 10);
    CHECK_EQ(f.pin[0][5].speed, 4);
    /* open-drain: a 1 lets go, the pin reads the circuit, else its pull */
    wr(&f, GPIOA + OTYPER, 1u << 5);
    CHECK_EQ(f.pin[0][5].drive, -1);
    CHECK_EQ(f.pin[0][5].open_drain, 1);
    CHECK_EQ(rd(&f, GPIOA + IDR) & 1u << 5, 0u);
    wr(&f, GPIOA + PUPDR, rd(&f, GPIOA + PUPDR) | 1u << 10);
    CHECK_EQ(f.pin[0][5].pull, 1); /* pulls work on outputs too */
    CHECK_EQ(rd(&f, GPIOA + IDR) & 1u << 5, 1u << 5);
    f.ext[0][5] = 0; /* something on the line holds it low */
    CHECK_EQ(rd(&f, GPIOA + IDR) & 1u << 5, 0u);
    wr(&f, GPIOA + BSRR, 1u << 21);
    CHECK_EQ(f.pin[0][5].drive, 0);
    /* PC13 an input with a pull-down, then driven by the board (B1) */
    wr(&f, GPIOC + PUPDR, 2u << 26);
    CHECK_EQ(f.pin[2][13].pull, 2);
    CHECK_EQ(rd(&f, GPIOC + IDR) & 1u << 13, 0u);
    f.ext[2][13] = 1;
    esk_stm32f4_pins_changed(&f.m);
    CHECK_EQ(rd(&f, GPIOC + IDR) & 1u << 13, 1u << 13);
    /* analog: the Schmitt trigger is off, the pull disconnected */
    wr(&f, GPIOC + MODER, 3u << 26);
    CHECK_EQ(f.pin[2][13].analog, 1);
    CHECK_EQ(f.pin[2][13].pull, 0);
    CHECK_EQ(rd(&f, GPIOC + IDR) & 1u << 13, 0u);
    /* PA2 on AF7: USART2_TX, its model not yet there, so let go */
    wr(&f, GPIOA + AFRL, 7u << 8);
    wr(&f, GPIOA + MODER, rd(&f, GPIOA + MODER) | 2u << 4);
    CHECK_EQ(f.pin[0][2].af, 1);
    CHECK_EQ(f.pin[0][2].drive, -1);
    CHECK_STR(esk_rf_af_signal(f.m.rf, 0, 2, 7, 0), "USART2_TX");
    {
        int af = -1;
        CHECK_EQ(esk_rf_af_pin(f.m.rf, "USART2_TX", 0, &af), 2);
        CHECK_EQ(af, 7);
        CHECK_EQ(esk_rf_af_pin(f.m.rf, "USART2_TX", 1, &af), -1); /* PD5: not on the LQFP64 */
        CHECK_EQ(esk_rf_af_pin(f.m.rf, "SPI1_SCK", 0, &af), 5);   /* PA5 */
        CHECK_EQ(af, 5);
    }
    /* the port's reset bit in AHB1RSTR */
    wr(&f, RCC_AHB1RSTR, 1u);
    wr(&f, RCC_AHB1RSTR, 0u);
    CHECK_EQ(rd(&f, GPIOA + MODER), 0xA8000000u);
    CHECK_EQ(rd(&f, GPIOA + ODR), 0u);
    CHECK_EQ(f.pin[0][5].drive, -1);
    CHECK_EQ(f.warnings, 0);
    f4_close(&f);
}

/* LCKR's key sequence (RM0390 7.4.8), as HAL_GPIO_LockPin writes it */
static void test_gpio_lock(void)
{
    f4 f;
    if (!f4_open(&f, 1))
        return;
    wr(&f, RCC_AHB1ENR, 1u);
    /* a wrong sequence (other LCK bits on the second write) does not lock */
    wr(&f, GPIOA + LCKR, 1u << 16 | 1u << 5);
    wr(&f, GPIOA + LCKR, 1u << 6);
    wr(&f, GPIOA + LCKR, 1u << 16 | 1u << 5);
    CHECK_EQ(rd(&f, GPIOA + LCKR) & 1u << 16, 0u);
    wr(&f, GPIOA + LCKR, 1u << 16 | 1u << 5);
    wr(&f, GPIOA + LCKR, 1u << 5);
    wr(&f, GPIOA + LCKR, 1u << 16 | 1u << 5);
    (void)rd(&f, GPIOA + LCKR);
    CHECK_EQ(rd(&f, GPIOA + LCKR), 1u << 16 | 1u << 5);
    /* PA5's configuration is frozen, PA6's is not */
    wr(&f, GPIOA + MODER, rd(&f, GPIOA + MODER) | 1u << 10 | 1u << 12);
    CHECK_EQ(rd(&f, GPIOA + MODER) & (3u << 10 | 3u << 12), 1u << 12);
    wr(&f, GPIOA + AFRL, 0xFFu << 20);
    CHECK_EQ(rd(&f, GPIOA + AFRL), 0xFu << 24);
    wr(&f, GPIOA + OTYPER, 0x60u);
    CHECK_EQ(rd(&f, GPIOA + OTYPER), 0x40u);
    /* until reset, the lock stays */
    wr(&f, GPIOA + LCKR, 0);
    CHECK_EQ(rd(&f, GPIOA + LCKR), 1u << 16 | 1u << 5);
    wr(&f, RCC_AHB1RSTR, 1u);
    wr(&f, RCC_AHB1RSTR, 0u);
    CHECK_EQ(rd(&f, GPIOA + LCKR), 0u);
    wr(&f, GPIOA + MODER, rd(&f, GPIOA + MODER) | 1u << 10);
    CHECK_EQ(rd(&f, GPIOA + MODER) & 3u << 10, 1u << 10);
    f4_close(&f);
}

/* EXTI line 13 on PC13 through SYSCFG's EXTICR4, as B1's interrupt is set
 * up; a software trigger on an internal line; an event */
static void test_exti(void)
{
    f4 f;
    struct esk_cm_state *cs;
    if (!f4_open(&f, 1))
        return;
    cs = (struct esk_cm_state *)f.m.cpu;
    wr(&f, RCC_AHB1ENR, 0x4u);       /* GPIOCEN */
    wr(&f, RCC_APB2ENR, 1u << 14);   /* SYSCFGEN */
    f.ext[2][13] = 1;
    esk_stm32f4_pins_changed(&f.m);
    wr(&f, SYSCFG + 0x14u, 2u << 4); /* EXTICR4: EXTI13 on port C */
    CHECK_EQ(esk_stm32f4_exti_port(&f.m, 13), 2);
    wr(&f, EXTI + 0x0Cu, 1u << 13);  /* FTSR */
    wr(&f, EXTI + 0x00u, 1u << 13);  /* IMR */
    f.ext[2][13] = 0;                /* the button pressed */
    esk_stm32f4_pins_changed(&f.m);
    CHECK_EQ(rd(&f, EXTI + 0x14u), 1u << 13); /* PR13 */
    CHECK(rd(&f, NVIC_ISPR0 + 4) & 1u << 8);  /* EXTI15_10, IRQ 40 */
    wr(&f, EXTI + 0x14u, 1u << 13);           /* rc_w1 */
    CHECK_EQ(rd(&f, EXTI + 0x14u), 0u);
    /* a rising edge it does not select: nothing */
    f.ext[2][13] = 1;
    esk_stm32f4_pins_changed(&f.m);
    CHECK_EQ(rd(&f, EXTI + 0x14u), 0u);
    /* PA13's edge is not line 13's while EXTICR4 picks port C */
    wr(&f, RCC_AHB1ENR, 0x5u);
    f.ext[0][13] = 0;
    esk_stm32f4_pins_changed(&f.m);
    CHECK_EQ(rd(&f, EXTI + 0x14u), 0u);
    /* line 22 (RTC wake-up) by SWIER: RTC_WKUP, IRQ 3 */
    wr(&f, EXTI + 0x00u, 1u << 22);
    wr(&f, EXTI + 0x10u, 1u << 22);
    CHECK_EQ(rd(&f, EXTI + 0x14u), 1u << 22);
    CHECK(rd(&f, NVIC_ISPR0) & 1u << 3);
    wr(&f, EXTI + 0x14u, 1u << 22);
    CHECK_EQ(rd(&f, EXTI + 0x10u), 0u); /* clearing PR clears SWIER */
    /* EMR: an event to the core */
    cs->event = 0;
    wr(&f, EXTI + 0x04u, 1u << 0);
    wr(&f, EXTI + 0x10u, 1u << 0);
    CHECK_EQ(cs->event, 1);
    CHECK_EQ(f.warnings, 0);
    f4_close(&f);
}

/* SYSCFG: what answers at 0, and the compensation cell */
static void test_syscfg(void)
{
    f4 f;
    u32 v = 0;
    if (!f4_open(&f, 1))
        return;
    CHECK_EQ(esk_bus_debug_write(&f.m.bus, 0x20000000u, 4, 0x5A5A1234u), 0);
    CHECK_EQ(esk_bus_debug_write(&f.m.bus, 0x08000000u, 4, 0x20001000u), 0);
    wr(&f, RCC_APB2ENR, 1u << 14);
    CHECK_EQ(rd(&f, 0x0u), 0x20001000u); /* the flash, BOOT0 low */
    wr(&f, SYSCFG + 0x00u, 3u);           /* MEM_MODE: SRAM1 */
    CHECK_EQ(rd(&f, 0x0u), 0x5A5A1234u);
    wr(&f, 0x4u, 0xCAFEF00Du);            /* it is RAM there */
    CHECK_EQ(esk_bus_debug_read(&f.m.bus, 0x20000004u, 4, &v), 0);
    CHECK_EQ(v, 0xCAFEF00Du);
    CHECK_EQ(esk_bus_read(&f.m.bus, 0x1BFFCu, 4, &v), 0); /* SRAM1's last word */
    CHECK(esk_bus_read(&f.m.bus, 0x1C000u, 4, &v) != 0);  /* SRAM2 is not aliased */
    wr(&f, SYSCFG + 0x00u, 2u); /* FMC bank 1: nothing on the bus */
    CHECK(esk_bus_read(&f.m.bus, 0x0u, 4, &v) != 0);
    wr(&f, SYSCFG + 0x00u, 0u);
    CHECK_EQ(rd(&f, 0x0u), 0x20001000u);
    wr(&f, SYSCFG + 0x20u, 1u); /* CMP_PD */
    CHECK_EQ(rd(&f, SYSCFG + 0x20u), 0x101u);
    wr(&f, SYSCFG + 0x2Cu, 3u); /* CFGR: FMPI2C1's Fm+ drive */
    CHECK_EQ(rd(&f, SYSCFG + 0x2Cu), 3u);
    /* a reset maps the flash again */
    wr(&f, SYSCFG + 0x00u, 3u);
    esk_mach_reset(&f.m);
    CHECK_EQ(esk_bus_debug_read(&f.m.bus, 0x0u, 4, &v), 0);
    CHECK_EQ(v, 0x20001000u);
    f4_close(&f);
}

/* ---- timers ------------------------------------------------------------------ */

/* the core kept busy (nop; b back) so that time passes instruction by
 * instruction, as firmware's main loop does */
static void busy(f4 *f)
{
    CHECK_EQ(esk_bus_debug_write(&f->m.bus, 0x100, 4, 0xe7fdbf00u), 0);
}

/* a pin of port `port` on alternate function `af` */
static void pin_af(f4 *f, u32 port, int pin, u32 af)
{
    u32 afr = port + (pin < 8 ? AFRL : AFRH);
    int sh = 4 * (pin & 7);
    wr(f, afr, (rd(f, afr) & ~(15u << sh)) | af << sh);
    wr(f, port + MODER, (rd(f, port + MODER) & ~(3u << (2 * pin))) | 2u << (2 * pin));
}

/* TIM2 CH1 PWM on PA5 at 1 kHz, 25 %: the HAL's TIM_PWMOutput, the
 * NUCLEO's LD2 dimmed */
static void test_tim_pwm(void)
{
    f4 f;
    int i, high = 0, edges = 0, last = 0;
    if (!f4_open(&f, 1))
        return;
    busy(&f);
    wr(&f, RCC_AHB1ENR, 1u);
    wr(&f, RCC_APB1ENR, 1u);         /* TIM2EN */
    pin_af(&f, GPIOA, 5, 1);         /* TIM2_CH1 */
    CHECK_EQ(f.pin[0][5].drive, -1); /* the channel is off: let go */
    wr(&f, TIM2 + T_PSC, 15);        /* 16 MHz / 16 = 1 MHz */
    wr(&f, TIM2 + T_ARR, 999);
    wr(&f, TIM2 + T_CCR1, 250);
    wr(&f, TIM2 + T_CCMR1, 6u << 4 | 1u << 3); /* PWM mode 1, OC1PE */
    wr(&f, TIM2 + T_CCER, 1u);                 /* CC1E */
    wr(&f, TIM2 + T_EGR, 1u);
    wr(&f, TIM2 + T_CR1, 1u | 1u << 7);        /* CEN, ARPE */
    for (i = 0; i < 2000; i++) {
        int d;
        esk_mach_run(&f.m, 16, 0);
        d = f.pin[0][5].drive;
        high += d == 1;
        edges += d != last;
        last = d;
    }
    CHECK(high >= 495 && high <= 505); /* 2 ms at 25 % */
    CHECK(edges >= 3 && edges <= 5);
    CHECK_EQ(f.pin[0][5].af, 1);
    CHECK(rd(&f, TIM2 + T_SR) & 1u); /* UIF */
    CHECK(rd(&f, TIM2 + T_SR) & 2u); /* CC1IF */
    /* CC1P inverts it */
    wr(&f, TIM2 + T_CCER, 3u);
    for (i = 0, high = 0; i < 1000; i++) {
        esk_mach_run(&f.m, 16, 0);
        high += f.pin[0][5].drive == 1;
    }
    CHECK(high >= 745 && high <= 755);
    CHECK_EQ(f.warnings, 0);
    f4_close(&f);
}

/* center-aligned counting, and counting down with the repetition counter */
static void test_tim_counting(void)
{
    f4 f;
    u32 c;
    if (!f4_open(&f, 1))
        return;
    busy(&f);
    wr(&f, RCC_APB1ENR, 2u); /* TIM3EN */
    wr(&f, TIM3 + T_ARR, 100);
    wr(&f, TIM3 + T_CR1, 1u << 5 | 1u); /* CMS 01, CEN */
    esk_mach_run(&f.m, 150, 0);
    c = rd(&f, TIM3 + T_CNT);
    CHECK(c >= 47 && c <= 53);                 /* up to 100, back down */
    CHECK(rd(&f, TIM3 + T_CR1) & 1u << 4);     /* DIR: down */
    CHECK(rd(&f, TIM3 + T_SR) & 1u);           /* the overflow's update */
    wr(&f, TIM3 + T_SR, 0);
    esk_mach_run(&f.m, 60, 0);
    CHECK_EQ(rd(&f, TIM3 + T_CR1) & 1u << 4, 0u); /* turned at 0 */
    CHECK(rd(&f, TIM3 + T_SR) & 1u);              /* the underflow's */
    wr(&f, TIM3 + T_CR1, 1u << 5 | 1u | 1u << 4); /* DIR is the hardware's */
    CHECK_EQ(rd(&f, TIM3 + T_CR1) & 1u << 4, 0u);
    /* TIM1 counting down, ARR 9, RCR 2: an update every 30 counts */
    wr(&f, RCC_APB2ENR, 1u); /* TIM1EN */
    wr(&f, TIM1 + T_ARR, 9);
    wr(&f, TIM1 + T_RCR, 2);
    wr(&f, TIM1 + T_CR1, 1u << 4 | 1u << 2); /* DIR, URS */
    wr(&f, TIM1 + T_EGR, 1u);                /* UG: RCR in, no UIF */
    CHECK_EQ(rd(&f, TIM1 + T_CNT), 9u);      /* from ARR counting down */
    CHECK_EQ(rd(&f, TIM1 + T_SR), 0u);
    wr(&f, TIM1 + T_CR1, 1u << 4 | 1u << 2 | 1u);
    esk_mach_run(&f.m, 24, 0);
    CHECK_EQ(rd(&f, TIM1 + T_SR) & 1u, 0u);
    esk_mach_run(&f.m, 10, 0);
    CHECK(rd(&f, TIM1 + T_SR) & 1u);
    f4_close(&f);
}

/* input capture on TIM2 CH2 (PA1): the edge, overcapture, reading CCR2,
 * and the filter dropping a pulse shorter than its 8 samples */
static void test_tim_capture(void)
{
    f4 f;
    u32 c1, c2;
    if (!f4_open(&f, 1))
        return;
    busy(&f);
    wr(&f, RCC_AHB1ENR, 1u);
    wr(&f, RCC_APB1ENR, 1u);
    pin_af(&f, GPIOA, 1, 1); /* TIM2_CH2 */
    wr(&f, TIM2 + T_ARR, 0xFFFFFFFFu);
    wr(&f, TIM2 + T_CCMR1, 1u << 8); /* CC2S 01: IC2 on TI2 */
    wr(&f, TIM2 + T_CCER, 1u << 4);  /* CC2E, rising */
    wr(&f, TIM2 + T_CR1, 1u);
    esk_mach_run(&f.m, 1000, 0);
    f.ext[0][1] = 1;
    esk_stm32f4_pins_changed(&f.m);
    CHECK(rd(&f, TIM2 + T_SR) & 1u << 2); /* CC2IF */
    CHECK(esk_bus_debug_read(&f.m.bus, TIM2 + T_CCR2, 4, &c1) == 0); /* a debugger's read clears nothing */
    CHECK(c1 >= 995 && c1 <= 1005);
    f.ext[0][1] = 0; /* a falling edge: not selected */
    esk_stm32f4_pins_changed(&f.m);
    esk_mach_run(&f.m, 500, 0);
    f.ext[0][1] = 1;
    esk_stm32f4_pins_changed(&f.m);
    CHECK(rd(&f, TIM2 + T_SR) & 1u << 10); /* CC2OF: CC2IF was still set */
    c2 = rd(&f, TIM2 + T_CCR2);            /* and the read clears CC2IF */
    CHECK(c2 - c1 >= 495 && c2 - c1 <= 505);
    CHECK_EQ(rd(&f, TIM2 + T_SR) & 1u << 2, 0u);
    /* both edges (CC2NP:CC2P 11) */
    wr(&f, TIM2 + T_CCER, 1u << 4 | 1u << 5 | 1u << 7);
    f.ext[0][1] = 0;
    esk_stm32f4_pins_changed(&f.m);
    CHECK(rd(&f, TIM2 + T_SR) & 1u << 2);
    (void)rd(&f, TIM2 + T_CCR2);
    /* IC2F 0011: 8 samples at fCK_INT; a 4-cycle pulse vanishes */
    wr(&f, TIM2 + T_CCMR1, 1u << 8 | 3u << 12);
    f.ext[0][1] = 1;
    esk_stm32f4_pins_changed(&f.m);
    esk_mach_run(&f.m, 4, 0);
    f.ext[0][1] = 0;
    esk_stm32f4_pins_changed(&f.m);
    esk_mach_run(&f.m, 50, 0);
    CHECK_EQ(rd(&f, TIM2 + T_SR) & 1u << 2, 0u);
    f.ext[0][1] = 1;
    esk_stm32f4_pins_changed(&f.m);
    c1 = rd(&f, TIM2 + T_CNT);
    esk_mach_run(&f.m, 50, 0);
    CHECK(rd(&f, TIM2 + T_SR) & 1u << 2);
    c2 = rd(&f, TIM2 + T_CCR2);
    if (!(c2 - c1 >= 7 && c2 - c1 <= 11))
        printf("filter: c1 %u c2 %u\n", c1, c2);
    CHECK(c2 - c1 >= 7 && c2 - c1 <= 11); /* taken once the filter passed it */
    f4_close(&f);
}

/* TIM1 CH1 and CH1N with a dead time, then a break: the complementary
 * outputs a half bridge needs */
static void test_tim_complementary(void)
{
    f4 f;
    int both_high = 0, o = 0, on = 0, total = 0;
    if (!f4_open(&f, 1))
        return;
    busy(&f);
    wr(&f, RCC_AHB1ENR, 3u);
    wr(&f, RCC_APB2ENR, 1u);
    pin_af(&f, GPIOA, 8, 1);  /* TIM1_CH1 */
    pin_af(&f, GPIOA, 7, 1);  /* TIM1_CH1N */
    pin_af(&f, GPIOB, 12, 1); /* TIM1_BKIN */
    f.ext[1][12] = 1;         /* the break input idle (high) */
    esk_stm32f4_pins_changed(&f.m);
    wr(&f, TIM1 + T_ARR, 99);
    wr(&f, TIM1 + T_CCR1, 50);
    wr(&f, TIM1 + T_CCMR1, 6u << 4);
    wr(&f, TIM1 + T_CCER, 1u | 1u << 2); /* CC1E, CC1NE */
    wr(&f, TIM1 + T_CR2, 1u << 8);       /* OIS1: OC1 high when idle */
    wr(&f, TIM1 + T_BDTR, 10u | 1u << 15 | 1u << 12 | 1u << 10); /* DTG 10, MOE, BKE, OSSI */
    wr(&f, TIM1 + T_CR1, 1u);
    esk_mach_run(&f.m, 300, 0);
    /* the gaps between one output falling and the other rising */
    {
        u64 fell = 0, gap_min = ~0ULL, gap_max = 0;
        int po = f.pin[0][8].drive, pon = f.pin[0][7].drive, gaps = 0;
        while (total < 500) {
            total += (int)esk_mach_run(&f.m, 1, 0);
            o = f.pin[0][8].drive;
            on = f.pin[0][7].drive;
            both_high += o == 1 && on == 1;
            if ((po == 1 && o == 0) || (pon == 1 && on == 0))
                fell = f.m.cycles;
            if (fell && ((po == 0 && o == 1) || (pon == 0 && on == 1))) {
                u64 g = f.m.cycles - fell;
                gap_min = g < gap_min ? g : gap_min;
                gap_max = g > gap_max ? g : gap_max;
                gaps++;
                fell = 0;
            }
            po = o;
            pon = on;
        }
        CHECK_EQ(both_high, 0);
        CHECK(gaps >= 9 && gaps <= 10); /* 5 periods, two edges each (the window may cut one) */
        if (!(gap_min >= 9 && gap_max <= 12))
            printf("dead time: gaps %u to %u cycles\n", (unsigned)gap_min, (unsigned)gap_max);
        CHECK(gap_min >= 9 && gap_max <= 12); /* DTG 10: 10 tDTS of 1 cycle */
    }
    /* the break input goes low (BKP 0: active low) */
    f.ext[1][12] = 0;
    esk_stm32f4_pins_changed(&f.m);
    CHECK_EQ(rd(&f, TIM1 + T_BDTR) & 1u << 15, 0u); /* MOE cleared */
    CHECK(rd(&f, TIM1 + T_SR) & 1u << 7);            /* BIF */
    CHECK_EQ(f.pin[0][8].drive, 1);                   /* OIS1 */
    CHECK_EQ(f.pin[0][7].drive, 0);                   /* OIS1N */
    wr(&f, TIM1 + T_BDTR, rd(&f, TIM1 + T_BDTR) | 1u << 15);
    CHECK_EQ(rd(&f, TIM1 + T_BDTR) & 1u << 15, 0u);   /* not while it is active */
    /* LOCK: the first write froze LOCK at 0; level 0 freezes nothing else */
    wr(&f, TIM1 + T_BDTR, (rd(&f, TIM1 + T_BDTR) & ~0xFFu) | 20u | 3u << 8);
    CHECK_EQ(rd(&f, TIM1 + T_BDTR) & 0x3FFu, 20u);
    f4_close(&f);
}

/* master and slave: TIM3's updates clock TIM2 (ITR2, external clock mode
 * 1); TIM3's in encoder mode on PA6 and PA7; one-pulse mode */
static void test_tim_slave(void)
{
    f4 f;
    static const int fwd[4][2] = { { 1, 0 }, { 1, 1 }, { 0, 1 }, { 0, 0 } };
    int i;
    if (!f4_open(&f, 1))
        return;
    busy(&f);
    wr(&f, RCC_AHB1ENR, 1u);
    wr(&f, RCC_APB1ENR, 3u);
    wr(&f, TIM3 + T_ARR, 9);
    wr(&f, TIM3 + T_CR2, 2u << 4);              /* MMS: update */
    wr(&f, TIM2 + T_ARR, 0xFFFFu);
    wr(&f, TIM2 + T_SMCR, 7u | 2u << 4);        /* external clock 1 on ITR2 (TIM3) */
    wr(&f, TIM2 + T_CR1, 1u);
    wr(&f, TIM3 + T_CR1, 1u);
    esk_mach_run(&f.m, 100, 0);
    CHECK(rd(&f, TIM2 + T_CNT) >= 9 && rd(&f, TIM2 + T_CNT) <= 10);
    CHECK(rd(&f, TIM2 + T_SR) & 1u << 6);       /* TIF */
    /* TIM3 as a quadrature decoder */
    wr(&f, TIM3 + T_CR1, 0);
    wr(&f, TIM3 + T_CR2, 0);
    pin_af(&f, GPIOA, 6, 2); /* TIM3_CH1 */
    pin_af(&f, GPIOA, 7, 2); /* TIM3_CH2 */
    f.ext[0][6] = f.ext[0][7] = 0;
    esk_stm32f4_pins_changed(&f.m);
    wr(&f, TIM3 + T_ARR, 0xFFFFu);
    wr(&f, TIM3 + T_CCMR1, 1u | 1u << 8); /* CC1S, CC2S 01 */
    wr(&f, TIM3 + T_SMCR, 3u);            /* encoder mode 3 */
    wr(&f, TIM3 + T_CNT, 100);
    wr(&f, TIM3 + T_CR1, 1u);
    for (i = 0; i < 8; i++) {
        f.ext[0][6] = fwd[i % 4][0];
        f.ext[0][7] = fwd[i % 4][1];
        esk_stm32f4_pins_changed(&f.m);
    }
    CHECK_EQ(rd(&f, TIM3 + T_CNT), 108u); /* two cycles, four counts each */
    CHECK_EQ(rd(&f, TIM3 + T_CR1) & 1u << 4, 0u);
    for (i = 3; i >= 0; i--) {
        f.ext[0][6] = fwd[(i + 3) % 4][0];
        f.ext[0][7] = fwd[(i + 3) % 4][1];
        esk_stm32f4_pins_changed(&f.m);
    }
    CHECK_EQ(rd(&f, TIM3 + T_CNT), 104u); /* one cycle back */
    CHECK(rd(&f, TIM3 + T_CR1) & 1u << 4); /* DIR: down */
    /* one-pulse: CEN cleared at the update */
    wr(&f, TIM3 + T_SMCR, 0);
    wr(&f, TIM3 + T_ARR, 20);
    wr(&f, TIM3 + T_CNT, 0);
    wr(&f, TIM3 + T_CR1, 1u | 1u << 3);
    esk_mach_run(&f.m, 60, 0);
    CHECK_EQ(rd(&f, TIM3 + T_CR1) & 1u, 0u);
    CHECK_EQ(rd(&f, TIM3 + T_CNT), 0u);
    f4_close(&f);
}

/* SysTick: the processor clock, or the STM32's external reference HCLK/8
 * (CLKSOURCE 0); CALIB with a reference, its TENMS not given */
static void test_systick(void)
{
    f4 f;
    u32 c;
    if (!f4_open(&f, 1))
        return;
    busy(&f);
    CHECK_EQ(rd(&f, 0xE000E01Cu), 0x40000000u); /* NOREF 0, SKEW 1, TENMS 0 */
    CHECK_EQ(rd(&f, 0xE000E010u), 0u);          /* CLKSOURCE 0 out of reset */
    wr(&f, 0xE000E014u, 999);
    wr(&f, 0xE000E018u, 0);
    wr(&f, 0xE000E010u, 1u); /* ENABLE, the reference clock */
    esk_mach_run(&f.m, 4000, 0);
    c = rd(&f, 0xE000E018u);
    CHECK(c >= 497 && c <= 502);              /* 500 counts of HCLK/8 */
    CHECK_EQ(rd(&f, 0xE000E010u) & 1u << 16, 0u);
    esk_mach_run(&f.m, 4100, 0);
    CHECK(rd(&f, 0xE000E010u) & 1u << 16);    /* COUNTFLAG, cleared by the read */
    CHECK_EQ(rd(&f, 0xE000E010u) & 1u << 16, 0u);
    wr(&f, 0xE000E018u, 0);
    wr(&f, 0xE000E010u, 5u); /* the processor clock */
    esk_mach_run(&f.m, 500, 0);
    c = rd(&f, 0xE000E018u);
    CHECK(c >= 496 && c <= 504);
    f4_close(&f);
}

/* ---- USART ---------------------------------------------------------------- */

static void usart2_on(f4 *f, u32 cr1)
{
    wr(f, RCC_AHB1ENR, 1u);
    wr(f, RCC_APB1ENR, 1u << 17); /* USART2EN */
    pin_af(f, GPIOA, 2, 7);       /* USART2_TX */
    pin_af(f, GPIOA, 3, 7);       /* USART2_RX */
    wr(f, USART2 + U_BRR, 0x8B);  /* 16 MHz / 115200: 139 clocks a bit */
    wr(f, USART2 + U_CR1, cr1 | 1u << 13);
}

/* the transmitter: frames, their format, TXE and TC, a break */
static void test_usart_tx(void)
{
    f4 f;
    if (!f4_open(&f, 1))
        return;
    busy(&f);
    usart2_on(&f, 1u << 3); /* TE: an idle frame first */
    CHECK_EQ(f.pin[0][2].drive, 1); /* TX idles high */
    CHECK_EQ(f.nframe, 0);
    wr(&f, USART2 + U_DR, 'A');     /* waits for the idle frame: TXE clears */
    CHECK_EQ(rd(&f, USART2 + U_SR) & 0xC0u, 0u);
    esk_mach_run(&f.m, 1400, 0);    /* the idle frame's 10 bits */
    CHECK_EQ(f.nframe, 1);
    CHECK_EQ(f.frame[0].c, 'A');
    CHECK_EQ(f.frame[0].nbits, 8);
    CHECK_EQ(f.frame[0].stop, 2);
    CHECK_EQ(f.frame[0].bit_cycles, 139u);
    CHECK_EQ(rd(&f, USART2 + U_SR) & 0xC0u, 0x80u); /* TXE, not TC: 'A' is on the line */
    wr(&f, USART2 + U_DR, 'B');
    esk_mach_run(&f.m, 1400, 0);
    CHECK_EQ(f.nframe, 2);
    CHECK_EQ(f.frame[1].c, 'B');
    CHECK_EQ(rd(&f, USART2 + U_SR) & 0xC0u, 0x80u);
    esk_mach_run(&f.m, 1400, 0);
    CHECK_EQ(rd(&f, USART2 + U_SR) & 0xC0u, 0xC0u); /* TC */
    /* 9 bits, odd parity, 2 stop bits: 'A' (2 ones) gets parity 1 */
    wr(&f, USART2 + U_CR1, 1u << 13 | 1u << 3 | 1u << 12 | 1u << 10 | 1u << 9);
    wr(&f, USART2 + U_CR2, 2u << 12);
    wr(&f, USART2 + U_DR, 'A');
    CHECK_EQ(f.frame[2].nbits, 9);
    CHECK_EQ(f.frame[2].stop, 4);
    CHECK_EQ(f.frame[2].bits, 0x141u); /* 'A' with the parity bit 8 set */
    esk_mach_run(&f.m, 2000, 0);
    /* 8 bits with even parity: the parity is bit 7 ('C' has 3 ones) */
    wr(&f, USART2 + U_CR1, 1u << 13 | 1u << 3 | 1u << 10);
    wr(&f, USART2 + U_CR2, 0);
    wr(&f, USART2 + U_DR, 'C');
    CHECK_EQ(f.frame[3].bits, 0xC3u);
    CHECK_EQ(f.frame[3].c, 'C');
    esk_mach_run(&f.m, 2000, 0);
    /* a break: 10 bits of 0 and a stop bit; SBK clears with it */
    wr(&f, USART2 + U_CR1, 1u << 13 | 1u << 3 | 1u);
    CHECK_EQ(f.frame[4].c, -1);
    CHECK_EQ(f.frame[4].bits, 0u);
    CHECK_EQ(f.frame[4].nbits, 9);
    esk_mach_run(&f.m, 2000, 0);
    CHECK_EQ(rd(&f, USART2 + U_CR1) & 1u, 0u);
    /* OVER8: BRR 0x45 is 4 x 8 + 5 = 37 clocks a bit */
    wr(&f, USART2 + U_CR1, 1u << 13 | 1u << 3 | 1u << 15);
    wr(&f, USART2 + U_BRR, 0x45);
    wr(&f, USART2 + U_DR, 'x');
    CHECK_EQ(f.frame[5].bit_cycles, 37u);
    f4_close(&f);
}

/* the receiver: RXNE, overrun, the errors and their clearing, IDLE, the
 * interrupt */
static void test_usart_rx(void)
{
    f4 f;
    struct esk_rx_port *rx;
    if (!f4_open(&f, 1))
        return;
    busy(&f);
    usart2_on(&f, 1u << 2 | 1u << 5 | 1u << 4); /* RE, RXNEIE, IDLEIE */
    rx = esk_mach_rx_find(&f.m, "USART2");
    CHECK(rx != NULL && rx->put_frame != NULL);
    if (!rx)
        return;
    CHECK_EQ(rx->format(rx->ctx), 8u | 2u << 8);
    rx->put_frame(rx->ctx, 'h', 0);
    CHECK_EQ(rd(&f, USART2 + U_SR) & 1u << 5, 1u << 5);
    CHECK(rd(&f, NVIC_ISPR0 + 4) & 1u << 6); /* USART2, IRQ 38 */
    rx->put_frame(rx->ctx, 'i', 0);          /* RXNE still set: lost */
    CHECK(rd(&f, USART2 + U_SR) & 1u << 3);  /* ORE */
    CHECK_EQ(rd(&f, USART2 + U_DR), (u32)'h');
    CHECK_EQ(rd(&f, USART2 + U_SR) & (1u << 3 | 1u << 5), 0u); /* SR then DR cleared ORE */
    /* a line idle for a frame after it: IDLE */
    esk_mach_run(&f.m, 1500, 0);
    CHECK(rd(&f, USART2 + U_SR) & 1u << 4);
    (void)rd(&f, USART2 + U_DR);
    CHECK_EQ(rd(&f, USART2 + U_SR) & 1u << 4, 0u);
    /* a framing error; a break with LINEN sets LBD */
    rx->put_frame(rx->ctx, 0x55, 1);
    CHECK(rd(&f, USART2 + U_SR) & 1u << 1);
    (void)rd(&f, USART2 + U_DR);
    wr(&f, USART2 + U_CR2, 1u << 14);
    rx->put_frame(rx->ctx, 0, 1);
    CHECK(rd(&f, USART2 + U_SR) & 1u << 8);
    (void)rd(&f, USART2 + U_DR);
    wr(&f, USART2 + U_SR, 0);
    wr(&f, USART2 + U_CR2, 0);
    /* even parity: 'C' with bit 7 clear is wrong */
    wr(&f, USART2 + U_CR1, 1u << 13 | 1u << 2 | 1u << 10);
    rx->put_frame(rx->ctx, 'C', 0);
    CHECK(rd(&f, USART2 + U_SR) & 1u);
    CHECK_EQ(rd(&f, USART2 + U_DR), (u32)'C');
    rx->put_frame(rx->ctx, 0xC3, 0);
    CHECK_EQ(rd(&f, USART2 + U_SR) & 1u, 0u);
    (void)rd(&f, USART2 + U_DR);
    /* the session's input: a byte, its parity added */
    rx->put(rx->ctx, 'C');
    CHECK_EQ(rd(&f, USART2 + U_DR), 0xC3u);
    /* mute until an address mark for node 5 */
    wr(&f, USART2 + U_CR2, 5u);
    wr(&f, USART2 + U_CR1, 1u << 13 | 1u << 2 | 1u << 11 | 1u << 1); /* WAKE, RWU */
    rx->put_frame(rx->ctx, 0x12, 0);
    rx->put_frame(rx->ctx, 0x83, 0); /* node 3: still mute */
    CHECK_EQ(rd(&f, USART2 + U_SR) & 1u << 5, 0u);
    rx->put_frame(rx->ctx, 0x85, 0); /* node 5: awake, and received */
    CHECK_EQ(rd(&f, USART2 + U_CR1) & 2u, 0u);
    CHECK_EQ(rd(&f, USART2 + U_DR), 0x85u);
    rx->put_frame(rx->ctx, 0x83, 0); /* another node's address: mute again */
    CHECK(rd(&f, USART2 + U_CR1) & 2u);
    f4_close(&f);
}

/* hardware flow control on PA0 (nCTS) and PA1 (nRTS), and the synchronous
 * clock on PA4 */
static void test_usart_flow_sync(void)
{
    f4 f;
    struct esk_rx_port *rx;
    int i, edges = 0, last;
    if (!f4_open(&f, 1))
        return;
    busy(&f);
    f.ext[0][0] = 1; /* nCTS: not ready */
    usart2_on(&f, 1u << 3 | 1u << 2);
    pin_af(&f, GPIOA, 0, 7); /* USART2_CTS */
    pin_af(&f, GPIOA, 1, 7); /* USART2_RTS */
    wr(&f, USART2 + U_CR3, 1u << 9 | 1u << 8); /* CTSE, RTSE */
    esk_mach_run(&f.m, 1500, 0);               /* the idle frame */
    CHECK_EQ(f.pin[0][1].drive, 0);            /* nRTS: ready */
    wr(&f, USART2 + U_DR, 'z');
    esk_mach_run(&f.m, 3000, 0);
    CHECK_EQ(f.nframe, 0);                     /* held by nCTS */
    f.ext[0][0] = 0;
    esk_stm32f4_pins_changed(&f.m);
    CHECK_EQ(f.nframe, 1);
    CHECK(rd(&f, USART2 + U_SR) & 1u << 9);    /* CTS */
    rx = esk_mach_rx_find(&f.m, "USART2");
    if (rx)
        rx->put_frame(rx->ctx, 'q', 0);
    CHECK_EQ(f.pin[0][1].drive, 1);            /* nRTS: full */
    (void)rd(&f, USART2 + U_DR);
    CHECK_EQ(f.pin[0][1].drive, 0);
    /* synchronous: CK on PA4, RX sampled at its edges */
    esk_mach_run(&f.m, 3000, 0);
    wr(&f, USART2 + U_CR3, 0);
    pin_af(&f, GPIOA, 4, 7); /* USART2_CK */
    wr(&f, USART2 + U_CR2, 1u << 11 | 1u << 8); /* CLKEN, LBCL */
    CHECK_EQ(f.pin[0][4].drive, 0);             /* CPOL 0 */
    f.ext[0][3] = 1;                            /* the slave answers 0xFF */
    wr(&f, USART2 + U_DR, 0x5A);
    last = f.pin[0][4].drive;
    for (i = 0; i < 1600; i++) {
        esk_mach_run(&f.m, 1, 0);
        edges += f.pin[0][4].drive != last;
        last = f.pin[0][4].drive;
    }
    CHECK_EQ(edges, 16); /* 8 pulses with LBCL */
    CHECK(rd(&f, USART2 + U_SR) & 1u << 5);
    CHECK_EQ(rd(&f, USART2 + U_DR), 0xFFu);
    f4_close(&f);
}

/* ---- DMA -------------------------------------------------------------------- */

static void ram_write(f4 *f, u32 a, const char *bytes, int n)
{
    int i;
    for (i = 0; i < n; i++)
        CHECK_EQ(esk_bus_debug_write(&f->m.bus, a + (u32)i, 1, (u8)bytes[i]), 0);
}

static u32 ram32(f4 *f, u32 a)
{
    u32 v = 0;
    CHECK_EQ(esk_bus_debug_read(&f->m.bus, a, 4, &v), 0);
    return v;
}

/* USART2's transmitter fed by DMA1 stream 6, channel 4 (HAL_UART_Transmit_DMA),
 * the request raised before the stream was enabled */
static void test_dma_usart_tx(void)
{
    f4 f;
    u32 s6 = DMA1 + D_S(6);
    if (!f4_open(&f, 1))
        return;
    busy(&f);
    ram_write(&f, 0x20000400u, "Hello", 5);
    usart2_on(&f, 1u << 3);
    esk_mach_run(&f.m, 1500, 0);               /* the idle frame */
    wr(&f, USART2 + U_CR3, 1u << 7);           /* DMAT: TXE requests already */
    wr(&f, RCC_AHB1ENR, rd(&f, RCC_AHB1ENR) | 1u << 21); /* DMA1EN */
    wr(&f, s6 + S_PAR, USART2 + U_DR);
    wr(&f, s6 + S_M0AR, 0x20000400u);
    wr(&f, s6 + S_NDTR, 5);
    wr(&f, s6 + S_CR, 4u << 25 | 1u << 10 | 1u << 6 | 1u << 4 | 1u); /* CH4, MINC, M2P, TCIE, EN */
    CHECK_EQ(f.nframe, 1);                      /* the first at once */
    CHECK_EQ(f.frame[0].c, 'H');
    CHECK_EQ(rd(&f, s6 + S_NDTR), 3u);           /* and the second waits in DR */
    CHECK_EQ(rd(&f, s6 + S_CR) & 1u << 0, 1u);   /* EN */
    wr(&f, s6 + S_NDTR, 9);                      /* ignored while enabled */
    CHECK_EQ(rd(&f, s6 + S_NDTR), 3u);
    esk_mach_run(&f.m, 7000, 0);
    CHECK_EQ(f.nframe, 5);
    CHECK_EQ(f.frame[4].c, 'o');
    CHECK_EQ(rd(&f, s6 + S_NDTR), 0u);
    CHECK_EQ(rd(&f, s6 + S_CR) & 1u, 0u);          /* EN cleared at the end */
    CHECK(rd(&f, DMA1 + D_HISR) & 1u << 21);       /* TCIF6 */
    CHECK(rd(&f, DMA1 + D_HISR) & 1u << 20);       /* HTIF6 */
    CHECK(rd(&f, NVIC_ISPR0) & 1u << 17);          /* DMA1_Stream6, IRQ 17 */
    wr(&f, DMA1 + D_HIFCR, 1u << 21 | 1u << 20);
    CHECK_EQ(rd(&f, DMA1 + D_HISR) & (3u << 20), 0u);
    f4_close(&f);
}

/* USART2's receiver into a circular buffer (DMA1 stream 5), then double
 * buffered */
static void test_dma_usart_rx(void)
{
    f4 f;
    struct esk_rx_port *rx;
    u32 s5 = DMA1 + D_S(5);
    if (!f4_open(&f, 1))
        return;
    busy(&f);
    usart2_on(&f, 1u << 2);
    wr(&f, USART2 + U_CR3, 1u << 6); /* DMAR */
    wr(&f, RCC_AHB1ENR, rd(&f, RCC_AHB1ENR) | 1u << 21);
    wr(&f, s5 + S_PAR, USART2 + U_DR);
    wr(&f, s5 + S_M0AR, 0x20000500u);
    wr(&f, s5 + S_NDTR, 4);
    wr(&f, s5 + S_CR, 4u << 25 | 1u << 10 | 1u << 8 | 1u); /* CH4, MINC, CIRC, P2M */
    rx = esk_mach_rx_find(&f.m, "USART2");
    if (!rx)
        return;
    rx->put_frame(rx->ctx, 'a', 0);
    rx->put_frame(rx->ctx, 'b', 0);
    CHECK_EQ(rd(&f, USART2 + U_SR) & 1u << 5, 0u); /* the DMA read DR */
    CHECK(rd(&f, DMA1 + D_HISR) & 1u << 10);        /* HTIF5 */
    rx->put_frame(rx->ctx, 'c', 0);
    rx->put_frame(rx->ctx, 'd', 0);
    rx->put_frame(rx->ctx, 'e', 0);                 /* wraps */
    CHECK(rd(&f, DMA1 + D_HISR) & 1u << 11);        /* TCIF5 */
    CHECK_EQ(ram32(&f, 0x20000500u), 0x64636265u);  /* "ebcd": 'e' over 'a' */
    CHECK_EQ(rd(&f, s5 + S_NDTR), 3u);
    CHECK_EQ(rd(&f, s5 + S_CR) & 1u, 1u);
    /* double buffer: CT switches to M1AR after a round */
    wr(&f, s5 + S_CR, 0);
    wr(&f, DMA1 + D_HIFCR, 0xFC0u);
    wr(&f, s5 + S_M1AR, 0x20000600u);
    wr(&f, s5 + S_NDTR, 2);
    wr(&f, s5 + S_CR, 4u << 25 | 1u << 10 | 1u << 18 | 1u); /* DBM */
    rx->put_frame(rx->ctx, 'x', 0);
    rx->put_frame(rx->ctx, 'y', 0);
    CHECK(rd(&f, s5 + S_CR) & 1u << 19);           /* CT: now M1AR */
    rx->put_frame(rx->ctx, 'z', 0);
    CHECK_EQ(ram32(&f, 0x20000600u) & 0xFFu, (u32)'z');
    CHECK_EQ(ram32(&f, 0x20000500u) & 0xFFFFu, 0x7978u); /* "xy" */
    f4_close(&f);
}

/* memory to memory on DMA2 stream 0 through the FIFO (bytes in, words out),
 * a FIFO configuration error, a bus error */
static void test_dma_m2m(void)
{
    f4 f;
    u32 s0 = DMA2 + D_S(0);
    if (!f4_open(&f, 1))
        return;
    busy(&f);
    ram_write(&f, 0x20000700u, "ABCDEFGH", 8);
    wr(&f, RCC_AHB1ENR, 1u << 22); /* DMA2EN */
    wr(&f, s0 + S_PAR, 0x20000700u);
    wr(&f, s0 + S_M0AR, 0x20000800u);
    wr(&f, s0 + S_NDTR, 8);
    wr(&f, s0 + S_FCR, 3u);  /* FTH full, direct mode: forced off by M2M */
    wr(&f, s0 + S_CR, 2u << 13 | 1u << 9 | 1u << 10 | 2u << 6 | 1u); /* MSIZE word, PINC, MINC, M2M */
    CHECK(rd(&f, s0 + S_FCR) & 1u << 2); /* DMDIS */
    esk_mach_run(&f.m, 100, 0);
    CHECK_EQ(rd(&f, s0 + S_NDTR), 0u);
    CHECK_EQ(ram32(&f, 0x20000800u), 0x44434241u);
    CHECK_EQ(ram32(&f, 0x20000804u), 0x48474645u);
    CHECK(rd(&f, DMA2 + D_LISR) & 1u << 5); /* TCIF0 */
    /* MBURST INCR16 of words cannot fit a half FIFO: FEIF, not enabled */
    wr(&f, DMA2 + 0x08u, 0x3Du);             /* LIFCR */
    wr(&f, s0 + S_NDTR, 8);
    wr(&f, s0 + S_FCR, 1u << 2 | 1u);        /* FIFO, FTH half */
    wr(&f, s0 + S_CR, 3u << 23 | 2u << 13 | 2u << 11 | 1u << 9 | 1u << 10 | 2u << 6 | 1u);
    CHECK_EQ(rd(&f, s0 + S_CR) & 1u, 0u);
    CHECK(rd(&f, DMA2 + D_LISR) & 1u);       /* FEIF0 */
    /* reading where nothing is: TEIF, EN cleared */
    wr(&f, DMA2 + 0x08u, 0x3Du);
    wr(&f, s0 + S_PAR, 0x30000000u);
    wr(&f, s0 + S_NDTR, 4);
    wr(&f, s0 + S_CR, 1u << 10 | 2u << 6 | 1u);
    esk_mach_run(&f.m, 50, 0);
    CHECK(rd(&f, DMA2 + D_LISR) & 1u << 3);  /* TEIF0 */
    CHECK_EQ(rd(&f, s0 + S_CR) & 1u, 0u);
    f4_close(&f);
}

/* TIM2's update request (DMA1 stream 1, channel 3) writing ARR through the
 * DMA burst register: a frequency sweep, as TIM_DMABurst does */
static void test_dma_tim_burst(void)
{
    f4 f;
    u32 s1 = DMA1 + D_S(1);
    if (!f4_open(&f, 1))
        return;
    busy(&f);
    CHECK_EQ(esk_bus_debug_write(&f.m.bus, 0x20000900u, 4, 49), 0);
    CHECK_EQ(esk_bus_debug_write(&f.m.bus, 0x20000904u, 4, 199), 0);
    wr(&f, RCC_APB1ENR, 1u);
    wr(&f, RCC_AHB1ENR, 1u << 21);
    wr(&f, s1 + S_PAR, TIM2 + 0x4Cu); /* DMAR */
    wr(&f, s1 + S_M0AR, 0x20000900u);
    wr(&f, s1 + S_NDTR, 2);
    wr(&f, s1 + S_CR, 3u << 25 | 2u << 13 | 2u << 11 | 1u << 10 | 1u << 6 | 1u); /* CH3, words, MINC, M2P */
    wr(&f, TIM2 + 0x48u, 11u);         /* DCR: DBA 11 (ARR), DBL 0 */
    wr(&f, TIM2 + T_ARR, 99);
    wr(&f, TIM2 + T_DIER, 1u << 8);    /* UDE */
    wr(&f, TIM2 + T_CR1, 1u);
    esk_mach_run(&f.m, 110, 0);         /* the first update */
    CHECK_EQ(rd(&f, TIM2 + T_ARR), 49u);
    esk_mach_run(&f.m, 60, 0);
    CHECK_EQ(rd(&f, TIM2 + T_ARR), 199u);
    CHECK_EQ(rd(&f, s1 + S_NDTR), 0u);
    CHECK(rd(&f, DMA1 + D_LISR) & 1u << 11); /* TCIF1 */
    f4_close(&f);
}

TEST_MAIN_BEGIN
RUN(test_reset);
RUN(test_180mhz);
RUN(test_hse_kind);
RUN(test_out_of_spec);
RUN(test_backup_domain);
RUN(test_gpio_reset);
RUN(test_gpio_modes);
RUN(test_gpio_lock);
RUN(test_exti);
RUN(test_syscfg);
RUN(test_tim_pwm);
RUN(test_tim_counting);
RUN(test_tim_capture);
RUN(test_tim_complementary);
RUN(test_tim_slave);
RUN(test_systick);
RUN(test_usart_tx);
RUN(test_usart_rx);
RUN(test_usart_flow_sync);
RUN(test_dma_usart_tx);
RUN(test_dma_usart_rx);
RUN(test_dma_m2m);
RUN(test_dma_tim_burst);
TEST_MAIN_END
