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
TEST_MAIN_END
