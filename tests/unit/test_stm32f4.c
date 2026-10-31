/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* test_stm32f4.c -- the STM32F446's clock tree and power controller
 * (rcc.c, pwr_f4.c), its GPIO ports, SYSCFG and EXTI (gpio_f4.c,
 * syscfg_f4.c, exti.c), timers and SysTick (tim.c, systick.c), USARTs
 * (usart.c), DMA (dma_f4.c), SPIs (spi.c) and I2Cs (i2c.c) on the machine
 * the catalog's description builds, with the NUCLEO-F446RE's clocks: an
 * 8 MHz clock on OSC_IN from the ST-Link's MCO (HSE bypass) and a
 * 32.768 kHz crystal. The program's accesses are made from here in the
 * order STM32CubeF4's SystemClock_Config makes them (RM0390 5.1.4 and
 * 6.3). */
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
#define RCC_CIR (RCC + 0x0Cu)
#define CRC_DR 0x40023000u
#define CRC_IDR 0x40023004u
#define CRC_CR 0x40023008u
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
#define TIM5 0x40000C00u
#define T_CR1 0x00u
#define T_CR2 0x04u
#define T_SMCR 0x08u
#define T_DIER 0x0Cu
#define T_SR 0x10u
#define T_EGR 0x14u
#define T_CCMR1 0x18u
#define T_CCER 0x20u
#define T_CCR4 0x40u
#define T_CCMR2 0x1Cu
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
#define SPI1 0x40013000u
#define P_CR1 0x00u
#define P_CR2 0x04u
#define P_SR 0x08u
#define P_DR 0x0Cu
#define P_CRCPR 0x10u
#define P_RXCRCR 0x14u
#define P_TXCRCR 0x18u
#define NVIC_ISPR1 0xE000E204u
#define I2C1 0x40005400u
#define I2C3 0x40005C00u
#define C_CR1 0x00u
#define C_CR2 0x04u
#define C_OAR1 0x08u
#define C_OAR2 0x0Cu
#define C_DR 0x10u
#define C_SR1 0x14u
#define C_SR2 0x18u
#define C_CCR 0x1Cu
#define C_TRISE 0x20u
#define ADC1 0x40012000u
#define ADC2 0x40012100u
#define ADCC 0x40012300u
#define A_SR 0x00u
#define A_CR1 0x04u
#define A_CR2 0x08u
#define A_SMPR1 0x0Cu
#define A_SMPR2 0x10u
#define A_JOFR1 0x14u
#define A_HTR 0x24u
#define A_LTR 0x28u
#define A_SQR1 0x2Cu
#define A_SQR3 0x34u
#define A_JSQR 0x38u
#define A_JDR1 0x3Cu
#define A_DR 0x4Cu
#define AC_CSR 0x00u
#define AC_CCR 0x04u
#define AC_CDR 0x08u
#define DAC 0x40007400u
#define DAC_CR 0x00u
#define DAC_SWTRIGR 0x04u
#define DAC_DHR12R1 0x08u
#define DAC_DHR12L1 0x0Cu
#define DAC_DHR8R1 0x10u
#define DAC_DHR12RD 0x20u
#define DAC_DOR1 0x2Cu
#define DAC_DOR2 0x30u
#define DAC_SR 0x34u
#define TIM6 0x40001000u
#define IWDG 0x40003000u
#define WWDG 0x40002C00u
#define AIRCR 0xE000ED0Cu
#define FLASH_OPTCR 0x40023C14u
#define RTC 0x40002800u
#define R_TR 0x00u
#define R_DR 0x04u
#define R_CR 0x08u
#define R_ISR 0x0Cu
#define R_PRER 0x10u
#define R_WUTR 0x14u
#define R_ALRMAR 0x1Cu
#define R_WPR 0x24u
#define R_SSR 0x28u
#define R_TSTR 0x30u
#define R_TAFCR 0x40u
#define R_BKP0R 0x50u
#define EXTI_IMR (EXTI + 0x00u)
#define EXTI_RTSR (EXTI + 0x08u)

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
    struct {                 /* an SPI device in mode 0 on SPI1's PA5-PA7 */
        int loop;            /* MISO follows MOSI (1), inverted (2) */
        u64 mosi;            /* MOSI's bits at SCK's rising edges, the last in bit 0 */
        int n;               /* how many */
        u64 miso;            /* MISO's next bits, the next in bit 63 */
        int nss_at[8], nnss; /* the bit counts at which NSS (PA4) rose */
        int fall;            /* MOSI sampled at SCK's falling edges (the TI mode) */
    } dev;
    struct {                 /* an I2C device on I2C1's PB8 (SCL) and PB9 (SDA) */
        int on, addr;        /* its 7-bit address */
        int scl, sda;        /* the lines as last seen */
        int busy, n, first, addressed, rd, mack;
        u32 shift, out;
        u8 got[16];          /* the bytes written to it, its address first */
        int ngot, nstart, nstop;
        u8 data[8];          /* what it answers a read with */
        int k;
        int stretch;         /* it holds SCL low after each ACK bit */
    } idev;
    int wire[4][2];          /* pins wired together, as port * 16 + bit; -1 none */
    double volts[8][16];     /* the voltage on a pin's net, for the ADC */
    int has_volts[8][16];
    double world_c;          /* the world's temperature, when has_world */
    int has_world;
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

/* an I2C line on PB8 or PB9: low while either side pulls it, else the
 * pull-up's 1 */
static int i2c_line(f4 *f, int bit) { return f->pin[1][bit].drive == 0 ? 0 : f->ext[1][bit] != 0; }

static void idev_step(f4 *f)
{
    int scl = i2c_line(f, 8), sda = i2c_line(f, 9);
#define D f->idev
    if (scl == D.scl && sda == D.sda)
        return;
    if (scl && D.scl && sda != D.sda) { /* a START or STOP */
        if (!sda) {
            D.busy = 1;
            D.n = 0;
            D.first = 1;
            D.nstart++;
        } else {
            D.busy = D.addressed = 0;
            D.nstop++;
            f->ext[1][9] = 1;
        }
    } else if (scl && !D.scl && D.busy) {
        if (D.n < 8)
            D.shift = (D.shift << 1 | (u32)sda) & 0xffu;
        else
            D.mack = !sda;
        D.n++;
    } else if (!scl && D.scl && D.busy) {
        if (D.n == 8) { /* the ACK bit */
            if (D.first) {
                D.addressed = (int)(D.shift >> 1) == D.addr;
                D.rd = (int)(D.shift & 1u);
            }
            if (D.addressed && (D.first || !D.rd) && D.ngot < 16)
                D.got[D.ngot++] = (u8)D.shift;
            f->ext[1][9] = D.addressed && (D.first || !D.rd) ? 0 : 1;
        } else if (D.n == 9) {
            int first = D.first;
            D.n = 0;
            D.first = 0;
            f->ext[1][9] = 1;
            if (D.addressed && D.rd && (first || D.mack)) {
                D.out = D.data[D.k++ & 7];
                f->ext[1][9] = (int)(D.out >> 7 & 1u);
            }
            if (D.stretch && D.addressed)
                f->ext[1][8] = 0;
        } else if (D.addressed && D.rd) {
            f->ext[1][9] = (int)(D.out >> (7 - D.n) & 1u);
        }
    }
    D.scl = i2c_line(f, 8);
    D.sda = i2c_line(f, 9);
#undef D
}

/* the pin wired to (port, bit), as port * 16 + bit, or -1 */
static int wired(const f4 *f, int port, int bit)
{
    int i, at = port * 16 + bit;
    for (i = 0; i < 4; i++) {
        if (f->wire[i][0] == at)
            return f->wire[i][1];
        if (f->wire[i][1] == at)
            return f->wire[i][0];
    }
    return -1;
}

static void r_pin_cfg(void *ctx, int port, int bit, const esk_mach_pin *cfg)
{
    f4 *f = (f4 *)ctx;
    int was;
    if (port < 0 || port >= 8 || bit < 0 || bit >= 16)
        return;
    was = f->pin[port][bit].drive;
    f->pin[port][bit] = *cfg;
    if (was != cfg->drive && wired(f, port, bit) >= 0)
        esk_stm32f4_pins_changed(&f->m); /* the other end hears it */
    if (port == 1 && (bit == 8 || bit == 9) && f->idev.on)
        idev_step(f);
    if (port != 0 || was == cfg->drive)
        return;
    if (bit == 5 && was >= 0 && cfg->drive == !f->dev.fall) { /* MOSI sampled */
        f->dev.mosi = f->dev.mosi << 1 | (u64)(f->pin[0][7].drive == 1);
        f->dev.n++;
    }
    if (bit == 5 && was == 1 && cfg->drive == 0 && !f->dev.loop) { /* falls: MISO's next */
        f->ext[0][6] = (int)(f->dev.miso >> 63);
        f->dev.miso <<= 1;
    } else if (bit == 5) {
    } else if (bit == 7 && f->dev.loop) {
        f->ext[0][6] = (cfg->drive == 1) ^ (f->dev.loop == 2);
    } else if (bit == 4 && cfg->drive == 1 && f->dev.nnss < 8) {
        f->dev.nss_at[f->dev.nnss++] = f->dev.n;
    }
}

static int r_pin_in(void *ctx, int port, int bit, int *level)
{
    f4 *f = (f4 *)ctx;
    int w = port >= 0 && bit >= 0 ? wired(f, port, bit) : -1;
    if (w >= 0) { /* a wired open-drain line with its pull-up */
        *level = f->pin[w / 16][w % 16].drive != 0;
        return 1;
    }
    if (port < 0 || port >= 8 || bit < 0 || bit > 15 || f->ext[port][bit] < 0)
        return 0;
    *level = f->ext[port][bit];
    return 1;
}

static int r_pin_volts(void *ctx, int port, int bit, double *v)
{
    f4 *f = (f4 *)ctx;
    if (port < 0 || port >= 8 || bit < 0 || bit > 15 || !f->has_volts[port][bit])
        return 0;
    *v = f->volts[port][bit];
    return 1;
}

static int r_temperature(void *ctx, double *c)
{
    f4 *f = (f4 *)ctx;
    *c = f->world_c;
    return f->has_world;
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
    for (i = 0; i < 4; i++)
        f->wire[i][0] = f->wire[i][1] = -1;
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
    f->m.host.pin_volts = r_pin_volts;
    f->m.host.temperature = r_temperature;
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

/* the core runs until the bit at `a` sets, as a program polls it: the
 * microseconds that took, or -1 after `max_us` */
static long wait_bits(f4 *f, u32 a, u32 mask, int set, long max_us);
static long wait_set(f4 *f, u32 a, u32 mask, long max_us) { return wait_bits(f, a, mask, 1, max_us); }
static long wait_clr(f4 *f, u32 a, u32 mask, long max_us) { return wait_bits(f, a, mask, 0, max_us); }

static long wait_bits(f4 *f, u32 a, u32 mask, int set, long max_us)
{
    long step = max_us > 100000 ? 1000 : 1; /* polled every ms past 100 ms */
    double us = 0; /* the machine's time since the wait began, at the core clocks it ran */
    while (us <= (double)max_us) {
        u32 hz = esk_mach_core_hz(&f->m);
        u64 end, from;
        if (((rd(f, a) & mask) != 0) == set)
            return (long)(us + 0.5);
        from = f->m.cycles;
        end = from + (u64)(hz ? hz : 16000000u) / 1000000u * (u64)step;
        while (f->m.cycles < end)
            if (!esk_mach_run(&f->m, end - f->m.cycles, 0) && f->m.asleep)
                esk_mach_idle(&f->m, end - f->m.cycles); /* as the runtime does */
        us += (double)(f->m.cycles - from) * 1e6 / (double)(hz ? hz : 16000000u);
    }
    return -1;
}

/* SystemClock_Config up to the PLL running and over-drive on: PWR's clock,
 * scale 1, HSE bypass, the PLL from it */
static void pll_from_hse(f4 *f, u32 pllcfgr)
{
    wr(f, RCC_APB1ENR, rd(f, RCC_APB1ENR) | 1u << 28); /* PWREN */
    wr(f, PWR_CR, (rd(f, PWR_CR) & ~(3u << 14)) | 3u << 14); /* VOS scale 1 */
    wr(f, RCC_CR, rd(f, RCC_CR) | 1u << 18);           /* HSEBYP */
    wr(f, RCC_CR, rd(f, RCC_CR) | 1u << 16);           /* HSEON */
    CHECK(wait_set(f, RCC_CR, 1u << 17, 10) >= 0);     /* HSERDY */
    wr(f, RCC_PLLCFGR, pllcfgr);
    wr(f, RCC_CR, rd(f, RCC_CR) | 1u << 24); /* PLLON */
    CHECK(wait_set(f, RCC_CR, 1u << 25, 200) > 0); /* PLLRDY: its lock time */
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
    CHECK(wait_set(&f, RCC_CR, 2u, 10) > 0); /* HSIRDY: its start-up */
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
    CHECK(wait_set(&f, RCC_CR, 1u << 17, 10) >= 0);
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
    CHECK(wait_set(&f, RCC_CR, 1u << 17, 3000) > 1900); /* the crystal's 2 ms */
    CHECK(wait_set(&f, RCC_CR, 1u << 25, 200) > 0);
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
    CHECK_EQ(rd(&f, RCC_BDCR) & 3u, 1u);
    CHECK(wait_set(&f, RCC_BDCR, 2u, 2100000) >= 1999000); /* LSERDY after tsu(LSE) */
    wr(&f, RCC_CSR, 1u);                 /* LSION */
    CHECK(wait_set(&f, RCC_CSR, 2u, 40) >= 14); /* LSIRDY after tsu(LSI) */
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

/* ---- SPI ------------------------------------------------------------------------ */

/* SPI1 on PA4 (NSS), PA5 (SCK), PA6 (MISO), PA7 (MOSI), AF5 */
static void spi1_pins(f4 *f)
{
    int i;
    busy(f);
    wr(f, RCC_AHB1ENR, rd(f, RCC_AHB1ENR) | 1u);
    wr(f, RCC_APB2ENR, rd(f, RCC_APB2ENR) | 1u << 12); /* SPI1EN */
    for (i = 4; i <= 7; i++)
        pin_af(f, GPIOA, i, 5);
}

/* the device's next frame on MISO: `bits` bits of `v`, MSB first */
static void dev_answer(f4 *f, u32 v, int bits)
{
    u64 q = (u64)v << (64 - bits);
    f->ext[0][6] = (int)(q >> 63);
    f->dev.miso = q << 1;
}

static void spi_wait(f4 *f, u32 flag)
{
    int i;
    for (i = 0; i < 4000 && !(rd(f, SPI1 + P_SR) & flag); i++)
        esk_mach_run(&f->m, 4, 0);
    CHECK(rd(f, SPI1 + P_SR) & flag);
}

static void spi_idle(f4 *f)
{
    int i;
    for (i = 0; i < 4000 && (rd(f, SPI1 + P_SR) & 1u << 7); i++) /* BSY */
        esk_mach_run(&f->m, 4, 0);
    CHECK_EQ(rd(f, SPI1 + P_SR) & 1u << 7, 0u);
}

/* a master in mode 0 at PCLK2 / 8 (HAL_SPI_TransmitReceive, NSS soft):
 * the frame on the pins, RXNE, OVR and its clearing sequence */
static void test_spi_master(void)
{
    f4 f;
    if (!f4_open(&f, 1))
        return;
    spi1_pins(&f);
    CHECK_EQ(rd(&f, SPI1 + P_SR), 2u);       /* TXE */
    CHECK_EQ(rd(&f, SPI1 + P_CRCPR), 7u);
    wr(&f, SPI1 + P_CR1, 2u << 3 | 1u << 2 | 1u << 9 | 1u << 8); /* BR /8, MSTR, SSM, SSI */
    CHECK_EQ(f.pin[0][5].drive, -1);         /* off: let go */
    wr(&f, SPI1 + P_CR1, rd(&f, SPI1 + P_CR1) | 1u << 6); /* SPE */
    CHECK_EQ(f.pin[0][5].drive, 0);          /* SCK idles at CPOL */
    CHECK_EQ(f.pin[0][4].drive, -1);         /* NSS soft: the pin is free */
    CHECK_EQ(f.pin[0][6].drive, -1);         /* MISO in */
    dev_answer(&f, 0x3c, 8);
    wr(&f, SPI1 + P_DR, 0xa5);
    CHECK_EQ(rd(&f, SPI1 + P_SR) & 0x83u, 0x82u); /* TXE back at once, BSY */
    spi_wait(&f, 1u);
    CHECK_EQ(f.dev.n, 8);
    CHECK_EQ((u32)f.dev.mosi, 0xa5u);
    CHECK_EQ(rd(&f, SPI1 + P_DR), 0x3cu);
    CHECK_EQ(rd(&f, SPI1 + P_SR) & 1u, 0u);  /* reading DR cleared RXNE */
    spi_idle(&f);
    CHECK_EQ(f.pin[0][5].drive, 0);
    /* two frames back to back, DR not read: OVR, the second lost */
    dev_answer(&f, 0x11, 8);
    wr(&f, SPI1 + P_DR, 0x01);
    wr(&f, SPI1 + P_DR, 0x02);               /* waits in the buffer */
    CHECK_EQ(rd(&f, SPI1 + P_SR) & 2u, 0u);
    spi_wait(&f, 1u);
    dev_answer(&f, 0x22, 8);
    spi_idle(&f);
    CHECK_EQ(f.dev.n, 24);
    CHECK_EQ(rd(&f, SPI1 + P_SR) & 0x41u, 0x41u); /* RXNE, OVR */
    CHECK_EQ(rd(&f, SPI1 + P_DR), 0x11u);    /* the older data */
    CHECK(rd(&f, SPI1 + P_SR) & 0x40u);      /* DR then SR clears OVR... */
    CHECK_EQ(rd(&f, SPI1 + P_SR) & 0x40u, 0u);
    /* 16 bits, LSB first, mode 3 */
    wr(&f, SPI1 + P_CR1, 0);
    wr(&f, SPI1 + P_CR1, 1u << 11 | 1u << 7 | 3u | 2u << 3 | 1u << 2 | 1u << 9 | 1u << 8 | 1u << 6);
    CHECK_EQ(f.pin[0][5].drive, 1);          /* CPOL 1 */
    f.dev.n = 0;
    wr(&f, SPI1 + P_DR, 0x8001u);
    spi_idle(&f);
    CHECK_EQ(f.dev.n, 16);
    /* mode 3's rising edge is the trailing one: MOSI is valid on it */
    CHECK_EQ((u32)f.dev.mosi & 0xffffu, 0x8001u);
    CHECK_EQ(f.warnings, 0);
    f4_close(&f);
}

/* MODF: a master whose NSS input is low; SPE and MSTR cleared and kept so
 * until SR is read and CR1 written; SSOE drives NSS low instead */
static void test_spi_modf(void)
{
    f4 f;
    if (!f4_open(&f, 1))
        return;
    spi1_pins(&f);
    f.ext[0][4] = 0;                         /* another master pulls NSS low */
    esk_stm32f4_pins_changed(&f.m);
    wr(&f, SPI1 + P_CR2, 1u << 5);           /* ERRIE */
    wr(&f, SPI1 + P_CR1, 1u << 2 | 1u << 6); /* MSTR, SPE, hardware NSS input */
    CHECK_EQ(rd(&f, SPI1 + P_CR1) & 0x44u, 0u);
    CHECK(rd(&f, NVIC_ISPR1) & 1u << 3);     /* SPI1, IRQ 35 */
    CHECK_EQ(f.pin[0][5].drive, -1);
    wr(&f, SPI1 + P_CR1, 1u << 2 | 1u << 6); /* write-protected */
    CHECK_EQ(rd(&f, SPI1 + P_CR1) & 0x44u, 0u);
    CHECK(rd(&f, SPI1 + P_SR) & 1u << 5);    /* MODF read... */
    f.ext[0][4] = 1;
    esk_stm32f4_pins_changed(&f.m);
    wr(&f, SPI1 + P_CR1, 1u << 2 | 1u << 6); /* ...then CR1 written: cleared */
    CHECK_EQ(rd(&f, SPI1 + P_SR) & 1u << 5, 0u);
    CHECK_EQ(rd(&f, SPI1 + P_CR1) & 0x44u, 0x44u);
    CHECK_EQ(f.pin[0][5].drive, 0);
    f.ext[0][4] = 0;                         /* NSS falls while enabled */
    esk_stm32f4_pins_changed(&f.m);
    CHECK(rd(&f, SPI1 + P_SR) & 1u << 5);
    CHECK_EQ(rd(&f, SPI1 + P_CR1) & 0x44u, 0u);
    /* SSOE: NSS is the master's output, low while enabled */
    f.ext[0][4] = -1;
    (void)rd(&f, SPI1 + P_SR);
    wr(&f, SPI1 + P_CR2, 1u << 2);
    wr(&f, SPI1 + P_CR1, 1u << 2 | 1u << 6);
    CHECK_EQ(rd(&f, SPI1 + P_SR) & 1u << 5, 0u);
    CHECK_EQ(f.pin[0][4].drive, 0);
    wr(&f, SPI1 + P_CR1, 1u << 2);
    CHECK_EQ(f.pin[0][4].drive, -1);         /* let go with SPE */
    f4_close(&f);
}

/* the board clocks the slave: a level on SCK (PA5) and MOSI (PA7) */
static int slave_clock(f4 *f, int sck, int mosi)
{
    f->ext[0][7] = mosi;
    f->ext[0][5] = sck;
    esk_stm32f4_pins_changed(&f->m);
    return f->pin[0][6].drive;
}

/* one frame into a slave in mode 0, MSB first: what MISO gave */
static u32 slave_frame(f4 *f, u32 in, int from, int to)
{
    u32 got = 0;
    int i;
    for (i = from; i < to; i++) {
        int b = (int)(in >> (7 - i) & 1u);
        got = got << 1 | (u32)(slave_clock(f, 1, b) == 1); /* leading: sampled */
        (void)slave_clock(f, 0, b);
    }
    return got;
}

/* a slave (HAL_SPI_TransmitReceive as SPI_MODE_SLAVE, hardware NSS):
 * MISO only while selected, its data loaded at once, the frame received,
 * the bit count across NSS, the frame received sent back */
static void test_spi_slave(void)
{
    f4 f;
    u32 got;
    if (!f4_open(&f, 1))
        return;
    spi1_pins(&f);
    f.ext[0][4] = 1;
    f.ext[0][5] = 0;
    esk_stm32f4_pins_changed(&f.m);
    wr(&f, SPI1 + P_CR2, 1u << 6);           /* RXNEIE */
    wr(&f, SPI1 + P_CR1, 1u << 6);           /* SPE, slave, mode 0 */
    wr(&f, SPI1 + P_DR, 0x5a);
    CHECK(rd(&f, SPI1 + P_SR) & 2u);         /* into the shift register: TXE */
    wr(&f, SPI1 + P_DR, 0x96);               /* the next waits */
    CHECK_EQ(rd(&f, SPI1 + P_SR) & 2u, 0u);
    CHECK_EQ(f.pin[0][6].drive, -1);         /* not selected */
    f.ext[0][4] = 0;
    esk_stm32f4_pins_changed(&f.m);
    CHECK_EQ(f.pin[0][6].drive, 0);          /* 0x5a's MSB, before the first edge */
    got = slave_frame(&f, 0xc3, 0, 8);
    CHECK_EQ(got, 0x5au);
    CHECK_EQ(rd(&f, SPI1 + P_SR) & 3u, 3u);  /* RXNE, and TXE: 0x96 went in */
    CHECK(rd(&f, NVIC_ISPR1) & 1u << 3);
    CHECK_EQ(rd(&f, SPI1 + P_DR), 0xc3u);
    /* NSS rises after four bits: the count carries on */
    got = slave_frame(&f, 0x0f, 0, 4);
    f.ext[0][4] = 1;
    esk_stm32f4_pins_changed(&f.m);
    CHECK_EQ(f.pin[0][6].drive, -1);
    CHECK_EQ(slave_clock(&f, 1, 1), -1);     /* ignored, not selected */
    (void)slave_clock(&f, 0, 1);
    f.ext[0][4] = 0;
    esk_stm32f4_pins_changed(&f.m);
    got = got << 4 | slave_frame(&f, 0x0f, 4, 8);
    CHECK_EQ(got, 0x96u);
    CHECK_EQ(rd(&f, SPI1 + P_DR), 0x0fu);
    CHECK(rd(&f, SPI1 + P_SR) & 1u << 7 ? 0 : 1); /* not busy between frames */
    /* nothing written: the frame received goes back */
    got = slave_frame(&f, 0x44, 0, 8);
    CHECK_EQ(got, 0x0fu);
    CHECK_EQ(rd(&f, SPI1 + P_DR), 0x44u);
    /* SPE cleared: the pin let go */
    wr(&f, SPI1 + P_CR1, 0);
    CHECK_EQ(f.pin[0][6].drive, -1);
    f4_close(&f);
}

/* CRC-8 (CRCPR 7) over "123456789" and its CRC frame after CRCNEXT, MISO
 * looped back to MOSI; then a line that inverts: CRCERR; CRC-16 */
static void test_spi_crc(void)
{
    f4 f;
    const char *msg = "123456789";
    int i;
    if (!f4_open(&f, 1))
        return;
    spi1_pins(&f);
    f.dev.loop = 1;
    wr(&f, SPI1 + P_CR1, 1u << 13 | 2u << 3 | 1u << 2 | 1u << 9 | 1u << 8 | 1u << 6); /* CRCEN */
    for (i = 0; i < 9; i++) {
        wr(&f, SPI1 + P_DR, (u8)msg[i]);
        if (i == 8)
            wr(&f, SPI1 + P_CR1, rd(&f, SPI1 + P_CR1) | 1u << 12); /* CRCNEXT */
        spi_wait(&f, 1u);
        CHECK_EQ(rd(&f, SPI1 + P_DR), (u32)(u8)msg[i]);
    }
    CHECK_EQ(rd(&f, SPI1 + P_TXCRCR), 0xf4u);
    CHECK_EQ(rd(&f, SPI1 + P_RXCRCR), 0xf4u);
    spi_wait(&f, 1u);                        /* the CRC frame */
    CHECK_EQ(rd(&f, SPI1 + P_DR), 0xf4u);
    CHECK_EQ((u32)f.dev.mosi & 0xffu, 0xf4u);
    CHECK_EQ(f.dev.n, 80);
    CHECK_EQ(rd(&f, SPI1 + P_CR1) & 1u << 12, 0u); /* CRCNEXT cleared */
    CHECK_EQ(rd(&f, SPI1 + P_SR) & 1u << 4, 0u);   /* no CRCERR */
    spi_idle(&f);
    /* CRCEN set again clears the CRCs; an inverting line gives CRCERR */
    wr(&f, SPI1 + P_CR1, 2u << 3 | 1u << 2 | 1u << 9 | 1u << 8 | 1u << 6);
    wr(&f, SPI1 + P_CR1, 1u << 13 | 2u << 3 | 1u << 2 | 1u << 9 | 1u << 8 | 1u << 6);
    CHECK_EQ(rd(&f, SPI1 + P_TXCRCR), 0u);
    f.dev.loop = 2;
    wr(&f, SPI1 + P_DR, 'a');
    wr(&f, SPI1 + P_CR1, rd(&f, SPI1 + P_CR1) | 1u << 12);
    spi_wait(&f, 1u);
    (void)rd(&f, SPI1 + P_DR);
    spi_wait(&f, 1u);
    (void)rd(&f, SPI1 + P_DR);
    spi_idle(&f);
    CHECK(rd(&f, SPI1 + P_SR) & 1u << 4);    /* CRCERR */
    wr(&f, SPI1 + P_SR, ~(1u << 4));         /* cleared by writing 0 */
    CHECK_EQ(rd(&f, SPI1 + P_SR) & 1u << 4, 0u);
    /* CRC-16 (CRCPR 0x1021) over 16-bit frames */
    f.dev.loop = 1;
    wr(&f, SPI1 + P_CR1, 0);
    wr(&f, SPI1 + P_CRCPR, 0x1021u);
    wr(&f, SPI1 + P_CR1, 1u << 13 | 1u << 11 | 2u << 3 | 1u << 2 | 1u << 9 | 1u << 8 | 1u << 6);
    for (i = 0; i < 4; i++) {
        wr(&f, SPI1 + P_DR, (u32)(u8)msg[2 * i] << 8 | (u8)msg[2 * i + 1]);
        spi_wait(&f, 1u);
        (void)rd(&f, SPI1 + P_DR);
    }
    CHECK_EQ(rd(&f, SPI1 + P_TXCRCR), 0x9015u);
    CHECK_EQ(f.warnings, 0);
    f4_close(&f);
}

/* HAL_SPI_TransmitReceive_DMA with CRC: DMA2 stream 3 (SPI1_TX) and stream
 * 0 (SPI1_RX), channel 3; the CRC frame follows the last item by itself */
static void test_spi_dma_crc(void)
{
    f4 f;
    u32 s0 = DMA2 + D_S(0), s3 = DMA2 + D_S(3);
    if (!f4_open(&f, 1))
        return;
    spi1_pins(&f);
    f.dev.loop = 1;
    ram_write(&f, 0x20000400u, "123456789", 9);
    wr(&f, RCC_AHB1ENR, rd(&f, RCC_AHB1ENR) | 1u << 22); /* DMA2EN */
    wr(&f, SPI1 + P_CR1, 1u << 13 | 2u << 3 | 1u << 2 | 1u << 9 | 1u << 8);
    wr(&f, s0 + S_PAR, SPI1 + P_DR);
    wr(&f, s0 + S_M0AR, 0x20000500u);
    wr(&f, s0 + S_NDTR, 9);
    wr(&f, s0 + S_CR, 3u << 25 | 1u << 10 | 1u);           /* CH3, MINC, P2M, EN */
    wr(&f, s3 + S_PAR, SPI1 + P_DR);
    wr(&f, s3 + S_M0AR, 0x20000400u);
    wr(&f, s3 + S_NDTR, 9);
    wr(&f, s3 + S_CR, 3u << 25 | 1u << 10 | 1u << 6 | 1u); /* CH3, MINC, M2P, EN */
    wr(&f, SPI1 + P_CR2, 3u);                               /* RXDMAEN, TXDMAEN */
    wr(&f, SPI1 + P_CR1, rd(&f, SPI1 + P_CR1) | 1u << 6);   /* SPE */
    spi_idle(&f);
    CHECK_EQ(f.dev.n, 80);                    /* nine bytes and the CRC */
    CHECK_EQ((u32)f.dev.mosi & 0xffu, 0xf4u);
    CHECK_EQ(rd(&f, s0 + S_NDTR), 0u);
    CHECK_EQ(ram32(&f, 0x20000500u), 0x34333231u);
    CHECK_EQ(ram32(&f, 0x20000508u) & 0xffu, 0x39u);
    CHECK(rd(&f, SPI1 + P_SR) & 1u);          /* the CRC waits in DR */
    CHECK_EQ(rd(&f, SPI1 + P_DR), 0xf4u);
    CHECK_EQ(rd(&f, SPI1 + P_SR) & 1u << 4, 0u);
    f4_close(&f);
}

/* bidirectional: out on MOSI and nothing received; in on MOSI, the master
 * clocking frame after frame until SPE clears; RXONLY likewise on MISO;
 * the TI mode's frame pulse on NSS */
static void test_spi_modes(void)
{
    f4 f;
    if (!f4_open(&f, 1))
        return;
    spi1_pins(&f);
    wr(&f, SPI1 + P_CR1, 1u << 15 | 1u << 14 | 2u << 3 | 1u << 2 | 1u << 9 | 1u << 8 | 1u << 6);
    wr(&f, SPI1 + P_DR, 0x81);
    spi_idle(&f);
    CHECK_EQ((u32)f.dev.mosi & 0xffu, 0x81u);
    CHECK_EQ(rd(&f, SPI1 + P_SR) & 0x41u, 0u); /* no RXNE, no OVR */
    /* BIDIOE 0: MOSI let go and read, the clock running on its own */
    f.ext[0][7] = 1;
    wr(&f, SPI1 + P_CR1, rd(&f, SPI1 + P_CR1) & ~(1u << 14));
    CHECK_EQ(f.pin[0][7].drive, -1);
    spi_wait(&f, 1u);
    CHECK_EQ(rd(&f, SPI1 + P_DR), 0xffu);
    f.ext[0][7] = 0;
    spi_wait(&f, 1u);
    (void)rd(&f, SPI1 + P_DR);
    spi_wait(&f, 1u);
    CHECK_EQ(rd(&f, SPI1 + P_DR), 0u);
    wr(&f, SPI1 + P_CR1, rd(&f, SPI1 + P_CR1) & ~(1u << 6)); /* SPE off: the frame finishes */
    CHECK_EQ(f.pin[0][5].drive >= 0, 1);
    spi_idle(&f);
    CHECK_EQ(f.pin[0][5].drive, -1);
    /* RXONLY: MISO in, MOSI let go */
    wr(&f, SPI1 + P_CR1, 0);
    (void)rd(&f, SPI1 + P_DR);               /* the frame SPE's clearing finished */
    (void)rd(&f, SPI1 + P_SR);
    dev_answer(&f, 0x6b, 8);
    f.dev.n = 0;
    wr(&f, SPI1 + P_CR1, 1u << 10 | 2u << 3 | 1u << 2 | 1u << 9 | 1u << 8 | 1u << 6);
    CHECK_EQ(f.pin[0][7].drive, -1);
    spi_wait(&f, 1u);
    CHECK_EQ(rd(&f, SPI1 + P_DR), 0x6bu);
    wr(&f, SPI1 + P_CR1, 1u << 10 | 2u << 3 | 1u << 2 | 1u << 9 | 1u << 8);
    spi_idle(&f);
    /* TI mode: NSS pulses one clock before each frame, in the last bit of
     * the previous one when they follow each other */
    f.dev.n = 0;
    f.dev.nnss = 0;
    f.dev.fall = 1;
    wr(&f, SPI1 + P_CR1, 0);
    wr(&f, SPI1 + P_CR2, 1u << 4);           /* FRF */
    wr(&f, SPI1 + P_CR1, 2u << 3 | 1u << 2 | 1u << 6 | 3u); /* CPOL and CPHA ignored */
    CHECK_EQ(f.pin[0][5].drive, 0);
    CHECK_EQ(f.pin[0][4].drive, 0);
    wr(&f, SPI1 + P_DR, 0x12);
    wr(&f, SPI1 + P_DR, 0x34);
    spi_idle(&f);
    CHECK_EQ(f.dev.n, 17);                   /* the sync clock and two frames */
    CHECK_EQ(f.dev.nnss, 2);
    CHECK_EQ(f.dev.nss_at[0], 0);            /* with the sync clock's rising edge */
    CHECK_EQ(f.dev.nss_at[1], 8);            /* with the first frame's last bit */
    CHECK_EQ(f.pin[0][4].drive, 0);
    CHECK_EQ((u32)f.dev.mosi & 0xffffu, 0x1234u);
    f4_close(&f);
}

/* ---- I2C ------------------------------------------------------------------------ */

/* a pin on alternate function `af`, open-drain */
static void pin_od(f4 *f, u32 port, int pin, u32 af)
{
    pin_af(f, port, pin, af);
    wr(f, port + OTYPER, rd(f, port + OTYPER) | 1u << pin);
}

/* I2C1 on PB8 (SCL) and PB9 (SDA) with their pull-ups, at 100 kHz from
 * PCLK1's 16 MHz (HAL_I2C_Init) */
static void i2c1_on(f4 *f)
{
    busy(f);
    f->ext[1][8] = f->ext[1][9] = 1;
    wr(f, RCC_AHB1ENR, rd(f, RCC_AHB1ENR) | 2u);
    wr(f, RCC_APB1ENR, rd(f, RCC_APB1ENR) | 1u << 21); /* I2C1EN */
    pin_od(f, GPIOB, 8, 4);
    pin_od(f, GPIOB, 9, 4);
    wr(f, I2C1 + C_CR2, 16);
    wr(f, I2C1 + C_CCR, 80);
    wr(f, I2C1 + C_TRISE, 17);
    wr(f, I2C1 + C_CR1, 1u);
}

static int i2c_wait(f4 *f, u32 base, u32 sr1)
{
    int i;
    for (i = 0; i < 20000 && !(rd(f, base + C_SR1) & sr1); i++)
        esk_mach_run(&f->m, 8, 0);
    return (rd(f, base + C_SR1) & sr1) != 0;
}

static int i2c_free(f4 *f, u32 base)
{
    int i;
    for (i = 0; i < 20000 && (rd(f, base + C_SR2) & 2u); i++) /* BUSY */
        esk_mach_run(&f->m, 8, 0);
    return !(rd(f, base + C_SR2) & 2u);
}

/* the master's write and its two-byte read (HAL_I2C_Master_Transmit and
 * _Receive's POS procedure), an absent address */
static void test_i2c_master(void)
{
    f4 f;
    if (!f4_open(&f, 1))
        return;
    f.idev.on = 1;
    f.idev.addr = 0x50;
    f.idev.data[0] = 0xab;
    f.idev.data[1] = 0xcd;
    f.idev.data[2] = 0x5a;
    i2c1_on(&f);
    idev_step(&f);
    CHECK_EQ(f.pin[1][8].drive, -1);           /* open-drain, let go */
    CHECK_EQ(rd(&f, I2C1 + C_SR2), 0u);
    wr(&f, I2C1 + C_CR1, 1u | 1u << 8);        /* START */
    CHECK(i2c_wait(&f, I2C1, 1u));             /* SB */
    CHECK_EQ(rd(&f, I2C1 + C_SR2) & 3u, 3u);   /* MSL, BUSY */
    CHECK_EQ(f.idev.nstart, 1);
    wr(&f, I2C1 + C_DR, 0xa0);
    CHECK(i2c_wait(&f, I2C1, 2u));             /* ADDR */
    CHECK_EQ(rd(&f, I2C1 + C_SR2) & 7u, 7u);   /* TRA */
    wr(&f, I2C1 + C_DR, 0x10);
    CHECK(i2c_wait(&f, I2C1, 1u << 7));        /* TXE */
    wr(&f, I2C1 + C_DR, 0x55);
    CHECK(i2c_wait(&f, I2C1, 1u << 2));        /* BTF */
    CHECK_EQ(f.pin[1][8].drive, 0);            /* SCL held */
    wr(&f, I2C1 + C_CR1, 1u | 1u << 9);        /* STOP */
    CHECK(i2c_free(&f, I2C1));
    CHECK_EQ(rd(&f, I2C1 + C_SR2) & 1u, 0u);
    CHECK_EQ(f.idev.nstop, 1);
    CHECK_EQ(f.idev.ngot, 3);
    CHECK_EQ(f.idev.got[1], 0x10);
    CHECK_EQ(f.idev.got[2], 0x55);
    /* two bytes: POS and ACK before ADDR clears, ACK off after */
    wr(&f, I2C1 + C_CR1, 1u | 1u << 8);
    CHECK(i2c_wait(&f, I2C1, 1u));
    wr(&f, I2C1 + C_DR, 0xa1);
    CHECK(i2c_wait(&f, I2C1, 2u));
    wr(&f, I2C1 + C_CR1, 1u | 1u << 10 | 1u << 11); /* ACK, POS */
    (void)rd(&f, I2C1 + C_SR1);
    CHECK_EQ(rd(&f, I2C1 + C_SR2) & 4u, 0u);   /* receiving */
    wr(&f, I2C1 + C_CR1, 1u | 1u << 11);
    CHECK(i2c_wait(&f, I2C1, 1u << 2));        /* BTF: both bytes in */
    wr(&f, I2C1 + C_CR1, 1u | 1u << 11 | 1u << 9);
    CHECK_EQ(rd(&f, I2C1 + C_DR), 0xabu);
    CHECK_EQ(rd(&f, I2C1 + C_DR), 0xcdu);
    CHECK(i2c_free(&f, I2C1));
    CHECK_EQ(f.idev.k, 2);                     /* the second NACKed: no third */
    CHECK_EQ(f.idev.nstop, 2);
    /* one byte, as the reference manual has it: SR1 read, ACK cleared, then
     * the SR2 read clears ADDR (a CR1 write between them must not break the
     * sequence), then STOP */
    wr(&f, I2C1 + C_CR1, 1u | 1u << 10 | 1u << 8);
    CHECK(i2c_wait(&f, I2C1, 1u));
    wr(&f, I2C1 + C_DR, 0xa1);
    CHECK(i2c_wait(&f, I2C1, 2u));
    (void)rd(&f, I2C1 + C_SR1);
    wr(&f, I2C1 + C_CR1, 1u);                  /* ACK off, ADDR still set */
    CHECK_EQ(rd(&f, I2C1 + C_SR1) & 2u, 2u);
    (void)rd(&f, I2C1 + C_SR1);
    wr(&f, I2C1 + C_CR1, 1u);
    (void)rd(&f, I2C1 + C_SR2);
    CHECK_EQ(rd(&f, I2C1 + C_SR1) & 2u, 0u);   /* ADDR cleared */
    wr(&f, I2C1 + C_CR1, 1u | 1u << 9);        /* STOP */
    CHECK(i2c_wait(&f, I2C1, 1u << 6));        /* RXNE */
    CHECK_EQ(rd(&f, I2C1 + C_DR), 0x5au);
    CHECK(i2c_free(&f, I2C1));
    CHECK_EQ(f.idev.k, 3);                     /* NACKed: no second byte */
    CHECK_EQ(f.idev.nstop, 3);
    /* a repeated START after the register address clears BTF and TXE */
    wr(&f, I2C1 + C_CR1, 1u | 1u << 8);
    CHECK(i2c_wait(&f, I2C1, 1u));
    wr(&f, I2C1 + C_DR, 0xa0);
    CHECK(i2c_wait(&f, I2C1, 2u));
    (void)rd(&f, I2C1 + C_SR2);
    wr(&f, I2C1 + C_DR, 0x10);
    CHECK(i2c_wait(&f, I2C1, 1u << 2));        /* BTF */
    wr(&f, I2C1 + C_CR1, 1u | 1u << 8);        /* repeated START */
    CHECK(i2c_wait(&f, I2C1, 1u));
    CHECK_EQ(rd(&f, I2C1 + C_SR1) & (1u << 2 | 1u << 7), 0u);
    CHECK_EQ(f.idev.nstart, 5);
    wr(&f, I2C1 + C_DR, 0xa1);
    CHECK(i2c_wait(&f, I2C1, 2u));
    (void)rd(&f, I2C1 + C_SR1);
    wr(&f, I2C1 + C_CR1, 1u);
    (void)rd(&f, I2C1 + C_SR2);
    wr(&f, I2C1 + C_CR1, 1u | 1u << 9);
    CHECK(i2c_wait(&f, I2C1, 1u << 6));
    (void)rd(&f, I2C1 + C_DR);
    CHECK(i2c_free(&f, I2C1));
    /* nobody at 0x51: AF */
    wr(&f, I2C1 + C_CR1, 1u | 1u << 8);
    CHECK(i2c_wait(&f, I2C1, 1u));
    wr(&f, I2C1 + C_DR, 0xa2);
    CHECK(i2c_wait(&f, I2C1, 1u << 10));
    CHECK_EQ(rd(&f, I2C1 + C_SR1) & 2u, 0u);
    wr(&f, I2C1 + C_CR1, 1u | 1u << 9);
    CHECK(i2c_free(&f, I2C1));
    wr(&f, I2C1 + C_SR1, ~(1u << 10));
    CHECK_EQ(rd(&f, I2C1 + C_SR1) & 1u << 10, 0u);
    CHECK_EQ(f.warnings, 0);
    f4_close(&f);
}

/* a device holding SCL low stretches the clock; another master's 0 loses
 * the arbitration; no pull-ups: no START */
static void test_i2c_stretch_arlo(void)
{
    f4 f;
    int i;
    if (!f4_open(&f, 1))
        return;
    f.idev.on = 1;
    f.idev.addr = 0x50;
    f.idev.stretch = 1;
    i2c1_on(&f);
    idev_step(&f);
    wr(&f, I2C1 + C_CR1, 1u | 1u << 8);
    CHECK(i2c_wait(&f, I2C1, 1u));
    wr(&f, I2C1 + C_DR, 0xa0);
    CHECK(i2c_wait(&f, I2C1, 2u));
    (void)rd(&f, I2C1 + C_SR1);
    (void)rd(&f, I2C1 + C_SR2);
    wr(&f, I2C1 + C_DR, 0x77);
    for (i = 0; i < 400; i++)
        esk_mach_run(&f.m, 8, 0);
    CHECK_EQ(f.ext[1][8], 0);                  /* the device holds SCL */
    CHECK_EQ(f.idev.ngot, 1);                  /* and the byte waits */
    CHECK_EQ(f.pin[1][8].drive, -1);           /* the master let it go */
    f.ext[1][8] = 1;
    f.idev.stretch = 0;
    esk_stm32f4_pins_changed(&f.m);
    idev_step(&f);
    CHECK(i2c_wait(&f, I2C1, 1u << 2));
    CHECK_EQ(f.idev.ngot, 2);
    CHECK_EQ(f.idev.got[1], 0x77);
    wr(&f, I2C1 + C_CR1, 1u | 1u << 9);
    CHECK(i2c_free(&f, I2C1));
    /* another master pulls SDA low while this one sends a 1 */
    f.idev.on = 0;
    wr(&f, I2C1 + C_CR2, 16u | 1u << 8);       /* ITERREN */
    wr(&f, I2C1 + C_CR1, 1u | 1u << 8);
    CHECK(i2c_wait(&f, I2C1, 1u));
    wr(&f, I2C1 + C_DR, 0xa0);
    f.ext[1][9] = 0;
    esk_stm32f4_pins_changed(&f.m);
    CHECK(i2c_wait(&f, I2C1, 1u << 9));        /* ARLO */
    CHECK_EQ(rd(&f, I2C1 + C_SR2) & 3u, 2u);   /* not the master; the bus busy */
    CHECK_EQ(f.pin[1][8].drive, -1);
    CHECK_EQ(f.pin[1][9].drive, -1);
    CHECK(rd(&f, NVIC_ISPR1) & 1u);            /* I2C1_ER, IRQ 32 */
    f4_close(&f);
    /* no pull-ups: SCL stays low and SB never comes */
    if (!f4_open(&f, 1))
        return;
    i2c1_on(&f);
    f.ext[1][8] = f.ext[1][9] = -1;
    esk_stm32f4_pins_changed(&f.m);
    wr(&f, I2C1 + C_CR1, 1u | 1u << 8);
    CHECK(!i2c_wait(&f, I2C1, 1u));
    f4_close(&f);
}

/* the board as the bus's master, by hand */
static void bus_lines(f4 *f, int scl, int sda)
{
    f->ext[1][8] = scl;
    f->ext[1][9] = sda;
    esk_stm32f4_pins_changed(&f->m);
}

static int bus_sda(f4 *f) { return i2c_line(f, 9); }

static void bus_start(f4 *f)
{
    bus_lines(f, 1, 1);
    bus_lines(f, 1, 0);
    bus_lines(f, 0, 0);
}

static void bus_stop(f4 *f)
{
    bus_lines(f, 0, 0);
    bus_lines(f, 1, 0);
    bus_lines(f, 1, 1);
}

/* a byte out and the ACK bit: 1 for ACK */
static int bus_write(f4 *f, u32 b)
{
    int i, ack;
    for (i = 7; i >= 0; i--) {
        int v = (int)(b >> i & 1u);
        bus_lines(f, 0, v);
        bus_lines(f, 1, v);
        bus_lines(f, 0, v);
    }
    bus_lines(f, 0, 1);
    bus_lines(f, 1, 1);
    ack = !bus_sda(f);
    bus_lines(f, 0, 1);
    return ack;
}

/* a byte in, acknowledged (1) or not */
static u32 bus_read(f4 *f, int ack)
{
    u32 b = 0;
    int i;
    for (i = 0; i < 8; i++) {
        bus_lines(f, 0, 1);
        bus_lines(f, 1, 1);
        b = b << 1 | (u32)bus_sda(f);
    }
    bus_lines(f, 0, !ack);
    bus_lines(f, 1, !ack);
    bus_lines(f, 0, !ack);
    bus_lines(f, 0, 1);
    return b;
}

/* I2C1 as a slave (HAL_I2C_Slave_Receive and _Transmit): ADDR and its
 * stretching, RXNE, STOPF, a read with BTF's stretching and the master's
 * NACK, the dual address and the general call */
static void test_i2c_slave(void)
{
    f4 f;
    if (!f4_open(&f, 1))
        return;
    i2c1_on(&f);
    wr(&f, I2C1 + C_OAR1, 0x42u << 1 | 1u << 14);
    wr(&f, I2C1 + C_CR1, 1u | 1u << 10);        /* PE, ACK */
    wr(&f, I2C1 + C_CR2, 16u | 1u << 9);        /* ITEVTEN */
    bus_start(&f);
    CHECK_EQ(rd(&f, I2C1 + C_SR2), 2u);         /* BUSY, not addressed */
    CHECK(!bus_write(&f, 0x60));                /* someone else */
    bus_stop(&f);
    CHECK_EQ(rd(&f, I2C1 + C_SR1), 0u);
    CHECK_EQ(rd(&f, I2C1 + C_SR2), 0u);
    bus_start(&f);
    CHECK(bus_write(&f, 0x84));
    CHECK(rd(&f, I2C1 + C_SR1) & 2u);           /* ADDR */
    CHECK(rd(&f, NVIC_ISPR0) & 1u << 31);       /* I2C1_EV */
    CHECK_EQ(f.pin[1][8].drive, 0);             /* SCL stretched */
    CHECK_EQ(rd(&f, I2C1 + C_SR2) & 4u, 0u);    /* receiving */
    CHECK_EQ(f.pin[1][8].drive, -1);            /* SR1 then SR2: let go */
    CHECK(bus_write(&f, 0x5a));
    CHECK(rd(&f, I2C1 + C_SR1) & 1u << 6);      /* RXNE */
    CHECK(bus_write(&f, 0xa5));                 /* DR still full: */
    CHECK(rd(&f, I2C1 + C_SR1) & 1u << 2);      /* BTF, SCL held */
    CHECK_EQ(f.pin[1][8].drive, 0);
    CHECK_EQ(rd(&f, I2C1 + C_DR), 0x5au);
    CHECK_EQ(f.pin[1][8].drive, -1);
    CHECK_EQ(rd(&f, I2C1 + C_DR), 0xa5u);
    bus_stop(&f);
    CHECK(rd(&f, I2C1 + C_SR1) & 1u << 4);      /* STOPF */
    wr(&f, I2C1 + C_CR1, 1u | 1u << 10);        /* SR1 read, CR1 written */
    CHECK_EQ(rd(&f, I2C1 + C_SR1) & 1u << 4, 0u);
    /* read by the master: DR's bytes, BTF while empty, the NACK's AF */
    bus_start(&f);
    CHECK(bus_write(&f, 0x85));
    (void)rd(&f, I2C1 + C_SR1);
    CHECK(rd(&f, I2C1 + C_SR2) & 4u);           /* TRA */
    CHECK_EQ(f.pin[1][8].drive, 0);             /* until DR is written */
    CHECK(rd(&f, I2C1 + C_SR1) & 1u << 7);      /* TXE */
    wr(&f, I2C1 + C_DR, 0xc3);
    CHECK_EQ(f.pin[1][8].drive, -1);
    CHECK_EQ(bus_read(&f, 1), 0xc3u);
    CHECK(rd(&f, I2C1 + C_SR1) & 1u << 2);      /* BTF: nothing to send */
    CHECK_EQ(f.pin[1][8].drive, 0);
    wr(&f, I2C1 + C_DR, 0x3c);
    wr(&f, I2C1 + C_DR, 0x99);                  /* the next waits in DR */
    CHECK_EQ(bus_read(&f, 1), 0x3cu);
    CHECK_EQ(bus_read(&f, 0), 0x99u);
    CHECK(rd(&f, I2C1 + C_SR1) & 1u << 10);     /* AF */
    CHECK_EQ(f.pin[1][9].drive, -1);
    bus_stop(&f);
    CHECK_EQ(rd(&f, I2C1 + C_SR1) & 1u << 4, 0u); /* no STOPF after the NACK */
    wr(&f, I2C1 + C_SR1, 0);
    /* the HAL's I2C_Flush_DR writes 0 to clear TXE: a byte received next
     * goes to DR over it */
    wr(&f, I2C1 + C_DR, 0);
    bus_start(&f);
    CHECK(bus_write(&f, 0x84));
    (void)rd(&f, I2C1 + C_SR1);
    (void)rd(&f, I2C1 + C_SR2);
    CHECK(bus_write(&f, 0x5a));
    CHECK_EQ(rd(&f, I2C1 + C_SR1) & (1u << 6 | 1u << 2), 1u << 6); /* RXNE, no BTF */
    CHECK_EQ(rd(&f, I2C1 + C_DR), 0x5au);
    bus_stop(&f);
    CHECK(rd(&f, I2C1 + C_SR1) & 1u << 4);      /* STOPF after an ACKed byte */
    wr(&f, I2C1 + C_CR1, 1u | 1u << 10);
    wr(&f, I2C1 + C_CR1, 1u | 1u << 10 | 1u << 6); /* ENGC */
    /* OAR2's dual address and the general call */
    wr(&f, I2C1 + C_OAR2, 0x30u << 1 | 1u);
    bus_start(&f);
    CHECK(bus_write(&f, 0x60));
    (void)rd(&f, I2C1 + C_SR1);
    CHECK(rd(&f, I2C1 + C_SR2) & 1u << 7);      /* DUALF */
    bus_start(&f);                              /* a repeated START */
    CHECK(bus_write(&f, 0x00));
    (void)rd(&f, I2C1 + C_SR1);
    CHECK(rd(&f, I2C1 + C_SR2) & 1u << 4);      /* GENCALL */
    CHECK(bus_write(&f, 0x06));
    bus_stop(&f);
    CHECK_EQ(rd(&f, I2C1 + C_DR), 0x06u);
    /* a misplaced STOP inside a byte: BERR */
    (void)rd(&f, I2C1 + C_SR1);
    wr(&f, I2C1 + C_CR1, 1u | 1u << 10 | 1u << 6);
    bus_start(&f);
    CHECK(bus_write(&f, 0x84));
    (void)rd(&f, I2C1 + C_SR1);
    (void)rd(&f, I2C1 + C_SR2);
    bus_lines(&f, 0, 0);
    bus_lines(&f, 1, 0);
    bus_lines(&f, 0, 0);
    bus_lines(&f, 0, 1);
    bus_lines(&f, 1, 1);                        /* two bits in */
    bus_lines(&f, 1, 0);
    bus_lines(&f, 1, 1);                        /* SDA rises with SCL high */
    CHECK(rd(&f, I2C1 + C_SR1) & 1u << 8);      /* BERR */
    f4_close(&f);
}

/* HAL_I2C_Master_Transmit_DMA with PEC (DMA1 stream 6, channel 1), then
 * _Receive_DMA with LAST (stream 0): the last byte NACKed by itself */
static void test_i2c_dma(void)
{
    f4 f;
    u32 s6 = DMA1 + D_S(6), s0 = DMA1 + D_S(0);
    if (!f4_open(&f, 1))
        return;
    f.idev.on = 1;
    f.idev.addr = 0x50;
    f.idev.data[0] = 1;
    f.idev.data[1] = 2;
    f.idev.data[2] = 3;
    i2c1_on(&f);
    idev_step(&f);
    ram_write(&f, 0x20000400u, "\x10\x20\x30", 3);
    wr(&f, RCC_AHB1ENR, rd(&f, RCC_AHB1ENR) | 1u << 21);
    wr(&f, s6 + S_PAR, I2C1 + C_DR);
    wr(&f, s6 + S_M0AR, 0x20000400u);
    wr(&f, s6 + S_NDTR, 3);
    wr(&f, s6 + S_CR, 1u << 25 | 1u << 10 | 1u << 6 | 1u); /* CH1, MINC, M2P, EN */
    wr(&f, I2C1 + C_CR1, 1u | 1u << 5);                     /* ENPEC */
    wr(&f, I2C1 + C_CR2, 16u | 1u << 11);                   /* DMAEN */
    wr(&f, I2C1 + C_CR1, 1u | 1u << 5 | 1u << 8);
    CHECK(i2c_wait(&f, I2C1, 1u));
    wr(&f, I2C1 + C_DR, 0xa0);
    CHECK(i2c_wait(&f, I2C1, 2u));
    (void)rd(&f, I2C1 + C_SR1);
    (void)rd(&f, I2C1 + C_SR2);
    CHECK(i2c_wait(&f, I2C1, 1u << 2));        /* BTF after the PEC */
    CHECK_EQ(rd(&f, s6 + S_NDTR), 0u);
    wr(&f, I2C1 + C_CR1, 1u | 1u << 5 | 1u << 9);
    CHECK(i2c_free(&f, I2C1));
    CHECK_EQ(f.idev.ngot, 5);
    CHECK_EQ(f.idev.got[3], 0x30);
    CHECK_EQ(f.idev.got[4], 0x63);             /* CRC-8 of A0 10 20 30 */
    /* three bytes received by DMA, LAST: the third NACKed */
    wr(&f, I2C1 + C_CR1, 1u);
    wr(&f, s0 + S_PAR, I2C1 + C_DR);
    wr(&f, s0 + S_M0AR, 0x20000500u);
    wr(&f, s0 + S_NDTR, 3);
    wr(&f, s0 + S_CR, 1u << 25 | 1u << 10 | 1u);           /* CH1, MINC, P2M, EN */
    wr(&f, I2C1 + C_CR2, 16u | 1u << 11 | 1u << 12);       /* DMAEN, LAST */
    wr(&f, I2C1 + C_CR1, 1u | 1u << 10 | 1u << 8);         /* ACK, START */
    CHECK(i2c_wait(&f, I2C1, 1u));
    wr(&f, I2C1 + C_DR, 0xa1);
    CHECK(i2c_wait(&f, I2C1, 2u));
    (void)rd(&f, I2C1 + C_SR1);
    (void)rd(&f, I2C1 + C_SR2);
    {
        int i;
        for (i = 0; i < 20000 && rd(&f, s0 + S_NDTR); i++)
            esk_mach_run(&f.m, 8, 0);
    }
    CHECK_EQ(rd(&f, s0 + S_NDTR), 0u);
    wr(&f, I2C1 + C_CR1, 1u | 1u << 10 | 1u << 9);         /* the DMA's TC: STOP */
    CHECK(i2c_free(&f, I2C1));
    CHECK_EQ(ram32(&f, 0x20000500u) & 0xffffffu, 0x030201u);
    CHECK_EQ(f.idev.k, 3);                                 /* no fourth asked for */
    f4_close(&f);
}

/* I2C1 (PB8, PB9) wired to I2C3 (PA8, PC9): a 10-bit address with PEC at
 * both ends, the slave model's stretching against the master model, then a
 * 10-bit read after a repeated START */
static void test_i2c_wired(void)
{
    f4 f;
    if (!f4_open(&f, 1))
        return;
    f.wire[0][0] = 1 * 16 + 8;
    f.wire[0][1] = 0 * 16 + 8;
    f.wire[1][0] = 1 * 16 + 9;
    f.wire[1][1] = 2 * 16 + 9;
    i2c1_on(&f);
    wr(&f, RCC_AHB1ENR, rd(&f, RCC_AHB1ENR) | 7u);
    wr(&f, RCC_APB1ENR, rd(&f, RCC_APB1ENR) | 1u << 23); /* I2C3EN */
    pin_od(&f, GPIOA, 8, 4);
    pin_od(&f, GPIOC, 9, 4);
    wr(&f, I2C3 + C_CR2, 16);
    wr(&f, I2C3 + C_OAR1, 1u << 15 | 0x234u);  /* ADDMODE: 10 bits */
    wr(&f, I2C3 + C_CR1, 1u | 1u << 10 | 1u << 5); /* PE, ACK, ENPEC */
    wr(&f, I2C1 + C_CR1, 1u | 1u << 5 | 1u << 8);
    CHECK(i2c_wait(&f, I2C1, 1u));
    CHECK(rd(&f, I2C3 + C_SR2) & 2u);          /* the slave sees the bus busy */
    wr(&f, I2C1 + C_DR, 0xf4);                 /* the header */
    CHECK(i2c_wait(&f, I2C1, 1u << 3));        /* ADD10 */
    wr(&f, I2C1 + C_DR, 0x34);
    CHECK(i2c_wait(&f, I2C1, 2u));
    CHECK(rd(&f, I2C3 + C_SR1) & 2u);          /* the slave addressed */
    (void)rd(&f, I2C1 + C_SR1);
    (void)rd(&f, I2C1 + C_SR2);
    wr(&f, I2C1 + C_DR, 0x11);
    esk_mach_run(&f.m, 8000, 0);
    CHECK_EQ(rd(&f, I2C1 + C_SR1) & 1u << 2, 0u); /* held by the slave's ADDR */
    (void)rd(&f, I2C3 + C_SR1);
    CHECK_EQ(rd(&f, I2C3 + C_SR2) & 4u, 0u);
    CHECK(i2c_wait(&f, I2C3, 1u << 6));
    CHECK_EQ(rd(&f, I2C3 + C_DR), 0x11u);
    wr(&f, I2C1 + C_DR, 0x22);
    CHECK(i2c_wait(&f, I2C3, 1u << 6));
    wr(&f, I2C3 + C_CR1, 1u | 1u << 10 | 1u << 5 | 1u << 12); /* the PEC next */
    CHECK_EQ(rd(&f, I2C3 + C_DR), 0x22u);
    wr(&f, I2C1 + C_CR1, 1u | 1u << 5 | 1u << 12);            /* send the PEC */
    CHECK(i2c_wait(&f, I2C3, 1u << 6));
    CHECK_EQ(rd(&f, I2C3 + C_DR), 0xbdu);
    CHECK_EQ(rd(&f, I2C3 + C_SR1) & 1u << 12, 0u);            /* no PECERR */
    CHECK(i2c_wait(&f, I2C1, 1u << 2));
    CHECK_EQ(rd(&f, I2C1 + C_SR1) & 1u << 10, 0u);            /* the PEC ACKed */
    /* a repeated START and the read header */
    wr(&f, I2C1 + C_CR1, 1u | 1u << 8);
    CHECK(i2c_wait(&f, I2C1, 1u));
    wr(&f, I2C1 + C_DR, 0xf5);
    CHECK(i2c_wait(&f, I2C3, 2u));
    (void)rd(&f, I2C3 + C_SR1);
    CHECK(rd(&f, I2C3 + C_SR2) & 4u);          /* TRA */
    wr(&f, I2C3 + C_DR, 0x9e);
    CHECK(i2c_wait(&f, I2C1, 2u));
    (void)rd(&f, I2C1 + C_SR1);
    (void)rd(&f, I2C1 + C_SR2);                /* one byte: ACK off already */
    wr(&f, I2C1 + C_CR1, 1u | 1u << 9);
    CHECK(i2c_wait(&f, I2C1, 1u << 6));
    CHECK_EQ(rd(&f, I2C1 + C_DR), 0x9eu);
    CHECK(i2c_free(&f, I2C1));
    CHECK(rd(&f, I2C3 + C_SR1) & 1u << 10);    /* the master's NACK */
    CHECK_EQ(rd(&f, I2C3 + C_SR1) & 1u << 4, 0u); /* no STOPF after a NACK */
    CHECK_EQ(rd(&f, I2C3 + C_SR2) & 2u, 0u);
    f4_close(&f);
}

/* ---- ADC and DAC -------------------------------------------------------------- */

static void set_volts(f4 *f, int port, int bit, double v)
{
    f->volts[port][bit] = v;
    f->has_volts[port][bit] = 1;
}

/* ADC1 on, ADCPRE /4: a 4 MHz ADC clock from PCLK2's 16 MHz */
static void adc1_on(f4 *f)
{
    busy(f);
    wr(f, RCC_APB2ENR, rd(f, RCC_APB2ENR) | 7u << 8); /* ADC1EN, ADC2EN, ADC3EN */
    wr(f, ADCC + AC_CCR, 1u << 16);
    wr(f, ADC1 + A_CR2, 1u);                         /* ADON */
}

/* the core cycles until `flag` sets */
static u32 adc_wait(f4 *f, u32 base, u32 flag, int *cycles)
{
    u64 t0 = f->m.cycles;
    int i;
    for (i = 0; i < 20000 && !(rd(f, base + A_SR) & flag); i++)
        esk_mach_run(&f->m, 1, 0);
    if (cycles)
        *cycles = (int)(f->m.cycles - t0);
    return rd(f, base + A_SR) & flag;
}

static u32 rd16(f4 *f, u32 a)
{
    u32 v = 0;
    CHECK_EQ(esk_bus_read(&f->m.bus, a, 2, &v), 0);
    return v & 0xffffu;
}

/* the core runs on until `n` more cycles have passed */
static void run_for(f4 *f, u64 n)
{
    u64 end = f->m.cycles + n;
    while (f->m.cycles < end)
        if (!esk_mach_run(&f->m, end - f->m.cycles, 0) && f->m.asleep)
            esk_mach_idle(&f->m, end - f->m.cycles); /* as the runtime does */
}

/* one conversion (HAL_ADC_Start, PollForConversion): its code, its time
 * (3 + 12 ADC clocks), RES and ALIGN, the internal channels against the
 * part's calibration words */
static void test_adc_single(void)
{
    f4 f;
    int t;
    u32 ts1, ts2, code;
    if (!f4_open(&f, 1))
        return;
    set_volts(&f, 0, 0, 1.65);
    adc1_on(&f);
    wr(&f, ADC1 + A_CR2, 1u | 1u << 30);              /* SWSTART */
    CHECK(rd(&f, ADC1 + A_SR) & 1u << 4);             /* STRT */
    CHECK(adc_wait(&f, ADC1, 2u, &t));
    CHECK(t >= 55 && t <= 66);                        /* 15 ADC clocks of 4 cycles */
    CHECK_EQ(rd(&f, ADC1 + A_DR), 2048u);
    CHECK_EQ(rd(&f, ADC1 + A_SR) & 2u, 0u);           /* reading DR cleared EOC */
    wr(&f, ADC1 + A_CR1, 2u << 24);                   /* 8 bits */
    wr(&f, ADC1 + A_CR2, 1u | 1u << 11 | 1u << 30);   /* left-aligned */
    CHECK(adc_wait(&f, ADC1, 2u, &t));
    CHECK(t >= 39 && t <= 50);                        /* 3 + 8 clocks */
    CHECK_EQ(rd(&f, ADC1 + A_DR), 128u << 8);
    /* the temperature sensor at 25 degC, then 85, against TS_CAL1/2 */
    ts1 = rd16(&f, 0x1FFF7A2Cu);
    ts2 = rd16(&f, 0x1FFF7A2Eu);
    CHECK_EQ(ts1, 959u);
    CHECK_EQ(ts2, 1207u);
    CHECK_EQ(rd16(&f, 0x1FFF7A22u), 512u);            /* F_SIZE */
    wr(&f, ADC1 + A_CR1, 0);
    wr(&f, ADC1 + A_SMPR1, 7u << 24);                 /* channel 18: 480 cycles */
    wr(&f, ADC1 + A_SQR3, 18);
    wr(&f, ADC1 + A_CR2, 1u | 1u << 30);
    CHECK(adc_wait(&f, ADC1, 2u, NULL));
    CHECK_EQ(rd(&f, ADC1 + A_DR), 0u);                /* TSVREFE off */
    wr(&f, ADCC + AC_CCR, 1u << 16 | 1u << 23);       /* TSVREFE */
    wr(&f, ADC1 + A_CR2, 1u | 1u << 30);
    CHECK(adc_wait(&f, ADC1, 2u, NULL));
    code = rd(&f, ADC1 + A_DR);
    CHECK_EQ(code, 943u);                             /* 0.76 V */
    CHECK(80 * ((int)code - (int)ts1) / ((int)ts2 - (int)ts1) + 30 == 25); /* AN3964's formula */
    f.has_world = 1;
    f.world_c = 85.0;
    wr(&f, ADC1 + A_CR2, 1u | 1u << 30);
    CHECK(adc_wait(&f, ADC1, 2u, NULL));
    code = rd(&f, ADC1 + A_DR);
    CHECK_EQ(code, 1129u);                            /* 0.91 V */
    CHECK(80 * ((int)code - (int)ts1) / ((int)ts2 - (int)ts1) + 30 == 84);
    wr(&f, ADCC + AC_CCR, 1u << 16 | 1u << 23 | 1u << 22); /* VBATE wins */
    wr(&f, ADC1 + A_CR2, 1u | 1u << 30);
    CHECK(adc_wait(&f, ADC1, 2u, NULL));
    CHECK_EQ(rd(&f, ADC1 + A_DR), 1024u);             /* VBAT / 4 */
    wr(&f, ADC1 + A_SQR3, 17);                        /* VREFINT */
    wr(&f, ADC1 + A_CR2, 1u | 1u << 30);
    CHECK(adc_wait(&f, ADC1, 2u, NULL));
    CHECK_EQ(rd(&f, ADC1 + A_DR), rd16(&f, 0x1FFF7A2Au));
    CHECK_EQ(f.warnings, 0);
    f4_close(&f);
}

/* a scan of PA0, PA1, PA4 into memory by DMA2 stream 0, circular and
 * continuous; then OVR with EOCS and nobody reading */
static void test_adc_scan_dma(void)
{
    f4 f;
    u32 s0 = DMA2 + D_S(0);
    if (!f4_open(&f, 1))
        return;
    set_volts(&f, 0, 0, 0.825);
    set_volts(&f, 0, 1, 1.65);
    set_volts(&f, 0, 4, 3.3);
    adc1_on(&f);
    wr(&f, RCC_AHB1ENR, rd(&f, RCC_AHB1ENR) | 1u << 22);
    wr(&f, s0 + S_PAR, ADC1 + A_DR);
    wr(&f, s0 + S_M0AR, 0x20000600u);
    wr(&f, s0 + S_NDTR, 6);
    wr(&f, s0 + S_CR, 1u << 13 | 1u << 11 | 1u << 10 | 1u << 8 | 1u); /* 16-bit, MINC, CIRC */
    wr(&f, ADC1 + A_SQR1, 2u << 20);                    /* three conversions */
    wr(&f, ADC1 + A_SQR3, 0u | 1u << 5 | 4u << 10);
    wr(&f, ADC1 + A_CR1, 1u << 8);                      /* SCAN */
    wr(&f, ADC1 + A_CR2, 1u | 1u << 1 | 1u << 8 | 1u << 9 | 1u << 30); /* CONT, DMA, DDS */
    run_for(&f, 1000);
    CHECK_EQ(ram32(&f, 0x20000600u), 2048u << 16 | 1024u);
    CHECK_EQ(ram32(&f, 0x20000604u), 1024u << 16 | 4095u);
    CHECK_EQ(ram32(&f, 0x20000608u), 4095u << 16 | 2048u);
    CHECK_EQ(rd(&f, ADC1 + A_SR) & 1u << 5, 0u);        /* no OVR: the DMA keeps up */
    /* EOCS, no DMA, nobody reading: OVR and the group stops */
    wr(&f, ADC1 + A_CR2, 0);                            /* ADON off: the group stops */
    wr(&f, ADC1 + A_CR2, 1u);
    wr(&f, ADC1 + A_SR, 0);
    wr(&f, ADC1 + A_CR1, 1u << 8 | 1u << 26);           /* OVRIE */
    wr(&f, ADC1 + A_CR2, 1u | 1u << 1 | 1u << 10 | 1u << 30);
    CHECK(adc_wait(&f, ADC1, 1u << 5, NULL));
    CHECK(rd(&f, NVIC_ISPR0) & 1u << 18);               /* ADC, IRQ 18 */
    CHECK_EQ(rd(&f, ADC1 + A_DR), 1024u);               /* the first kept */
    run_for(&f, 200);
    CHECK_EQ(rd(&f, ADC1 + A_SR) & 2u, 0u);             /* stopped: no new result */
    f4_close(&f);
}

/* the injected group interrupting a continuous regular one, with JOFR;
 * the analog watchdog; TIM2's TRGO starting the regular group */
static void test_adc_injected_trigger(void)
{
    f4 f;
    int n = 0;
    u64 end;
    if (!f4_open(&f, 1))
        return;
    set_volts(&f, 0, 0, 1.65);
    set_volts(&f, 0, 1, 0.33);
    adc1_on(&f);
    wr(&f, ADC1 + A_JOFR1, 100);                         /* rank 1's offset */
    wr(&f, ADC1 + A_JSQR, 1u << 15);                     /* JL 0: JSQ4 = channel 1 */
    wr(&f, ADC1 + A_HTR, 4000);
    wr(&f, ADC1 + A_LTR, 500);
    wr(&f, ADC1 + A_CR1, 1u << 23 | 1u << 22 | 1u << 6); /* AWDEN, JAWDEN, AWDIE */
    wr(&f, ADC1 + A_CR2, 1u | 1u << 1 | 1u << 30);       /* continuous regular */
    run_for(&f, 30);                                     /* in the middle of one */
    wr(&f, ADC1 + A_CR2, 1u | 1u << 1 | 1u << 22);       /* JSWSTART */
    CHECK(rd(&f, ADC1 + A_SR) & 1u << 3);                /* JSTRT */
    CHECK(adc_wait(&f, ADC1, 1u << 2, NULL));            /* JEOC */
    CHECK_EQ(rd(&f, ADC1 + A_JDR1 + 0), 409u - 100u);    /* 0.33 V less the offset */
    CHECK(rd(&f, ADC1 + A_SR) & 1u);                     /* AWD: 409 < 500 */
    CHECK(rd(&f, NVIC_ISPR0) & 1u << 18);
    CHECK(adc_wait(&f, ADC1, 2u, NULL));
    CHECK_EQ(rd(&f, ADC1 + A_DR), 2048u);                /* the regular group went on */
    /* TIM2's update as TRGO, every 1000 cycles, starting single conversions */
    wr(&f, ADC1 + A_CR2, 0);
    wr(&f, ADC1 + A_CR1, 0);
    wr(&f, ADC1 + A_CR2, 1u | 6u << 24 | 1u << 28);      /* EXTSEL TIM2_TRGO, rising */
    wr(&f, RCC_APB1ENR, rd(&f, RCC_APB1ENR) | 1u);
    wr(&f, TIM2 + T_ARR, 999);
    wr(&f, TIM2 + T_CR2, 2u << 4);                       /* MMS: update */
    wr(&f, TIM2 + T_CR1, 1u);
    for (end = f.m.cycles + 5200; f.m.cycles < end;) {
        run_for(&f, 1);
        if (rd(&f, ADC1 + A_SR) & 2u) {
            n++;
            (void)rd(&f, ADC1 + A_DR);
        }
    }
    CHECK(n == 5);
    f4_close(&f);
}

/* dual regular simultaneous mode with DMA mode 2: CDR's words */
static void test_adc_dual(void)
{
    f4 f;
    if (!f4_open(&f, 1))
        return;
    set_volts(&f, 0, 0, 0.825);
    set_volts(&f, 0, 1, 2.475);
    adc1_on(&f);
    wr(&f, ADC2 + A_CR2, 1u);
    wr(&f, ADC2 + A_SQR3, 1);
    wr(&f, ADCC + AC_CCR, 1u << 16 | 2u << 14 | 6u);    /* ADCPRE /4, DMA mode 2, regular simultaneous */
    wr(&f, ADC2 + A_CR2, 1u | 1u << 30);                 /* a slave's own start: ignored */
    esk_mach_run(&f.m, 200, 0);
    CHECK_EQ(rd(&f, ADC2 + A_SR) & 2u, 0u);
    wr(&f, ADC1 + A_CR2, 1u | 1u << 30);
    CHECK(adc_wait(&f, ADC2, 2u, NULL));
    CHECK(rd(&f, ADC1 + A_SR) & 2u);
    CHECK_EQ(rd(&f, ADCC + AC_CSR) & 0x202u, 0x202u);    /* EOC1, EOC2 */
    CHECK_EQ(rd(&f, ADCC + AC_CDR), 3072u << 16 | 1024u);
    f4_close(&f);
}

/* the DAC: DHR to DOR and the pin's voltage, the buffer's range, TIM6's
 * TRGO with a triangle, the software trigger, a DMA underrun */
static void test_dac(void)
{
    f4 f;
    int steps = 0;
    u64 end;
    u32 last = 0;
    if (!f4_open(&f, 1))
        return;
    busy(&f);
    wr(&f, RCC_AHB1ENR, rd(&f, RCC_AHB1ENR) | 1u);
    wr(&f, GPIOA + MODER, rd(&f, GPIOA + MODER) | 0xfu << 8); /* PA4, PA5 analog */
    wr(&f, RCC_APB1ENR, rd(&f, RCC_APB1ENR) | 1u << 29 | 1u << 4); /* DACEN, TIM6EN */
    wr(&f, DAC + DAC_CR, 1u);                            /* EN1, buffered */
    wr(&f, DAC + DAC_DHR12R1, 2048);
    CHECK_EQ(rd(&f, DAC + DAC_DOR1), 0u);                /* one APB1 clock later */
    esk_mach_run(&f.m, 4, 0);
    CHECK_EQ(rd(&f, DAC + DAC_DOR1), 2048u);
    CHECK_EQ(f.pin[0][4].aout, 1);
    CHECK(f.pin[0][4].volts > 1.6499 && f.pin[0][4].volts < 1.6501);
    CHECK_EQ(rd(&f, DAC + DAC_DHR12L1), 2048u << 4);     /* every view */
    wr(&f, DAC + DAC_DHR8R1, 0);
    esk_mach_run(&f.m, 4, 0);
    CHECK(f.pin[0][4].volts > 0.1999 && f.pin[0][4].volts < 0.2001); /* the buffer's floor */
    wr(&f, DAC + DAC_CR, 1u | 1u << 1);                  /* BOFF */
    CHECK(f.pin[0][4].volts < 0.0001);
    CHECK_EQ(f.pin[0][5].aout, 0);
    wr(&f, DAC + DAC_DHR12RD, 4095u << 16 | 1000u);     /* both channels */
    esk_mach_run(&f.m, 4, 0);
    CHECK_EQ(rd(&f, DAC + DAC_DOR2), 4095u);
    /* a triangle of amplitude 7 on TIM6's update every 100 cycles */
    wr(&f, DAC + DAC_DHR12R1, 100);
    wr(&f, DAC + DAC_CR, 1u | 1u << 2 | 0u << 3 | 2u << 6 | 2u << 8); /* TEN, TIM6_TRGO, triangle, MAMP 2 */
    wr(&f, TIM6 + T_ARR, 99);
    wr(&f, TIM6 + T_CR2, 2u << 4);
    wr(&f, TIM6 + T_CR1, 1u);
    for (end = f.m.cycles + 2000; f.m.cycles < end;) {
        u32 d;
        run_for(&f, 1);
        d = rd(&f, DAC + DAC_DOR1);
        CHECK(d >= 100 && d <= 107);
        if (d != last)
            steps++;
        last = d;
    }
    CHECK(steps >= 18 && steps <= 21);                   /* one step a trigger */
    /* the software trigger, and an underrun with nothing to serve DMA */
    wr(&f, TIM6 + T_CR1, 0);
    wr(&f, DAC + DAC_CR, 1u | 1u << 2 | 7u << 3 | 1u << 12 | 1u << 13); /* SWTRIG, DMAEN, DMAUDRIE */
    wr(&f, DAC + DAC_DHR12R1, 300);
    wr(&f, DAC + DAC_SWTRIGR, 1u);
    CHECK_EQ(rd(&f, DAC + DAC_SWTRIGR), 0u);
    esk_mach_run(&f.m, 20, 0);
    CHECK_EQ(rd(&f, DAC + DAC_DOR1), 300u);
    CHECK_EQ(rd(&f, DAC + DAC_SR), 0u);
    wr(&f, DAC + DAC_SWTRIGR, 1u);                       /* the request still waits */
    CHECK(rd(&f, DAC + DAC_SR) & 1u << 13);              /* DMAUDR1 */
    CHECK(rd(&f, NVIC_ISPR1) & 1u << 22);                /* TIM6_DAC, IRQ 54 */
    wr(&f, DAC + DAC_SR, 1u << 13);
    CHECK_EQ(rd(&f, DAC + DAC_SR), 0u);
    f4_close(&f);
}

/* ---- resets and watchdogs ----------------------------------------------------------- */

/* the runtime's side of a requested reset: the machine starts again, its
 * RAM kept, with the cause the model gave */
static int reset_if_asked(f4 *f)
{
    if (!f->m.reset_req)
        return 0;
    esk_mach_reset(&f->m);
    f->m.reset_cause = ESK_RESET_POWER;
    return 1;
}

/* the core runs until a reset is asked for, at most `limit` cycles: the
 * cycles it took, or -1 */
static long long run_to_reset(f4 *f, u64 limit)
{
    u64 t0 = f->m.cycles;
    while (!f->m.reset_req && f->m.cycles - t0 < limit)
        esk_mach_run(&f->m, 256, 0);
    return f->m.reset_req ? (long long)(f->m.cycles - t0) : -1;
}

/* RCC_CSR's flags through a power-on, RMVF, a software reset (RAM kept)
 * and the reset pin */
static void test_reset_flags(void)
{
    f4 f;
    if (!f4_open(&f, 1))
        return;
    busy(&f);
    CHECK_EQ(rd(&f, RCC_CSR) & 0xff000000u, 0x0e000000u); /* PORRSTF, PINRSTF, BORRSTF */
    wr(&f, RCC_CSR, 1u << 24);                            /* RMVF */
    CHECK_EQ(rd(&f, RCC_CSR) & 0xff000000u, 0u);
    CHECK_EQ(esk_bus_debug_write(&f.m.bus, 0x20000800u, 4, 0xfeedc0deu), 0);
    wr(&f, AIRCR, 0x05fa0004u);                           /* SYSRESETREQ */
    CHECK(reset_if_asked(&f));
    CHECK_EQ(rd(&f, RCC_CSR) & 0xff000000u, 0x14000000u); /* SFTRSTF, PINRSTF */
    CHECK_EQ(ram32(&f, 0x20000800u), 0xfeedc0deu);        /* RAM kept */
    f.m.reset_cause = ESK_RESET_PIN;
    esk_mach_reset(&f.m);
    CHECK_EQ(rd(&f, RCC_CSR) & 0xff000000u, 0x14000000u); /* the flags kept, PINRSTF again */
    f4_close(&f);
}

/* the IWDG: its keys, PVU/RVU, LSI forced on, the reset at 0 and its
 * flag; then the option bytes' hardware watchdog */
static void test_iwdg(void)
{
    f4 f;
    long long t;
    if (!f4_open(&f, 1))
        return;
    busy(&f);
    wr(&f, IWDG + 0x08, 99);                     /* RLR, locked: ignored */
    CHECK_EQ(rd(&f, IWDG + 0x08), 0xfffu);
    wr(&f, IWDG, 0x5555);
    wr(&f, IWDG + 0x08, 99);
    CHECK_EQ(rd(&f, IWDG + 0x0C), 2u);           /* RVU */
    run_for(&f, 2600);                           /* five LSI clocks */
    CHECK_EQ(rd(&f, IWDG + 0x0C), 0u);
    CHECK_EQ(rd(&f, RCC_CSR) & 2u, 0u);          /* LSI off */
    wr(&f, IWDG, 0xcccc);
    CHECK_EQ(rd(&f, RCC_CSR) & 2u, 0u);          /* forced on: LSI starts */
    CHECK(wait_set(&f, RCC_CSR, 2u, 40) >= 14);  /* LSIRDY after tsu(LSI) */
    wr(&f, IWDG, 0xaaaa);                        /* 100 counts of 4 LSI clocks */
    t = run_to_reset(&f, 400000);
    CHECK(t >= 199000 && t <= 201000);           /* 12.5 ms at 16 MHz */
    CHECK(reset_if_asked(&f));
    CHECK(rd(&f, RCC_CSR) & 1u << 29);           /* IWDGRSTF */
    CHECK_EQ(run_to_reset(&f, 300000), -1);      /* a software watchdog: stopped */
    /* WDG_SW 0: it starts by itself, from 0xFFF at LSI / 4 (512 ms) */
    CHECK_EQ(rd(&f, FLASH_OPTCR) & 0xffffu, 0xaaedu);           /* the factory's */
    CHECK_EQ(esk_bus_debug_write(&f.m.bus, 0x1FFFC000u, 1, 0xcdu), 0); /* WDG_SW 0 */
    f.m.reset_cause = ESK_RESET_PIN;
    esk_mach_reset(&f.m);
    CHECK_EQ(rd(&f, FLASH_OPTCR) & 0xffffu, 0xaacdu);           /* loaded at reset */
    CHECK(wait_set(&f, RCC_CSR, 2u, 40) >= 14);                 /* LSI forced on */
    wr(&f, IWDG, 0x5555);
    wr(&f, IWDG + 0x04, 0);
    wr(&f, IWDG + 0x08, 9);
    run_for(&f, 2600);
    wr(&f, IWDG, 0xaaaa);
    t = run_to_reset(&f, 100000);
    CHECK(t >= 19000 && t <= 21000);             /* 10 counts: 1.25 ms */
    f4_close(&f);
}

/* the WWDG: a refresh above the window resets, one inside does not, EWI at
 * 0x40, then the reset as T6 clears */
static void test_wwdg(void)
{
    f4 f;
    long long t;
    if (!f4_open(&f, 1))
        return;
    busy(&f);
    wr(&f, RCC_APB1ENR, rd(&f, RCC_APB1ENR) | 1u << 11); /* WWDGEN */
    wr(&f, WWDG + 0x04, 0x50u | 1u << 9);        /* W 0x50, WDGTB 0, EWI */
    wr(&f, WWDG, 0x80u | 0x7fu);                 /* WDGA, T 0x7F */
    run_for(&f, 4096 * 10);
    CHECK_EQ(rd(&f, WWDG) & 0x7fu, 0x75u);       /* a count every 4096 PCLK1 clocks */
    wr(&f, WWDG, 0x80u | 0x7fu);                 /* 0x75 > W: too early */
    CHECK(reset_if_asked(&f));
    CHECK(rd(&f, RCC_CSR) & 1u << 30);           /* WWDGRSTF */
    wr(&f, RCC_APB1ENR, rd(&f, RCC_APB1ENR) | 1u << 11);
    wr(&f, WWDG + 0x04, 0x50u | 1u << 9);
    wr(&f, WWDG, 0x80u | 0x7fu);
    run_for(&f, 4096 * 0x30 + 100);              /* 0x4F: inside the window */
    wr(&f, WWDG, 0x80u | 0x7fu);
    CHECK(!f.m.reset_req);
    t = run_to_reset(&f, 4096 * 0x50);
    CHECK(rd(&f, WWDG + 0x08) & 1u);             /* EWIF at 0x40 */
    CHECK(rd(&f, NVIC_ISPR0) & 1u);              /* WWDG, IRQ 0 */
    CHECK(t >= 4096 * 0x40 - 300 && t <= 4096 * 0x40 + 300); /* 0x7F to 0x3F */
    f4_close(&f);
}

/* ---- the RTC ------------------------------------------------------------------------- */

/* HAL_RTC_Init's way: PWR's DBP, LSE, RTCSEL LSE, RTCEN; the keys, INIT,
 * and fast prescalers (a "second" of 1/1024 s: 15625 core cycles) */
static void rtc_on(f4 *f)
{
    busy(f);
    wr(f, RCC_APB1ENR, rd(f, RCC_APB1ENR) | 1u << 28);  /* PWREN */
    wr(f, PWR_CR, rd(f, PWR_CR) | 1u << 8);             /* DBP */
    wr(f, RCC_BDCR, 1u);                                /* LSEON */
    CHECK(wait_set(f, RCC_BDCR, 2u, 2100000) >= 1999000); /* LSERDY: the crystal's 2 s */
    wr(f, RCC_BDCR, 1u | 1u << 8 | 1u << 15);           /* RTCSEL LSE, RTCEN */
    wr(f, RTC + R_WPR, 0xca);
    wr(f, RTC + R_WPR, 0x53);
    wr(f, RTC + R_ISR, 1u << 7);                        /* INIT */
    CHECK_EQ(rd(f, RTC + R_ISR) & 1u << 6, 0u);         /* INITF two RTCCLK clocks later */
    run_for(f, 1000);
    CHECK(rd(f, RTC + R_ISR) & 1u << 6);
    wr(f, RTC + R_PRER, 3u << 16 | 7u);                 /* /4, /8 */
}

static void rtc_go(f4 *f)
{
    wr(f, RTC + R_ISR, 0);                              /* INIT off */
    run_for(f, 1000);
    CHECK(rd(f, RTC + R_ISR) & 1u << 5);                /* RSF */
}

#define RTC_SEC 15625u

/* the calendar across a leap day's midnight, its write protection, the
 * shadow registers' locking, 12-hour mode */
static void test_rtc_calendar(void)
{
    f4 f;
    if (!f4_open(&f, 1))
        return;
    CHECK_EQ(rd(&f, RTC + R_DR), 0x2101u);               /* 2000-01-01, Monday */
    rtc_on(&f);
    wr(&f, RTC + R_TR, 0x235958u);                      /* 23:59:58 */
    wr(&f, RTC + R_DR, 0x24u << 16 | 3u << 13 | 0x02u << 8 | 0x28u); /* 2024-02-28, Wednesday */
    rtc_go(&f);
    CHECK(rd(&f, RTC + R_ISR) & 1u << 4);                /* INITS: the year is set */
    run_for(&f, 3 * RTC_SEC + 2500);                     /* (it counts again four RTCCLK clocks after INIT) */
    CHECK_EQ(rd(&f, RTC + R_TR), 0x000001u);             /* 00:00:01 */
    CHECK_EQ(rd(&f, RTC + R_DR), 0x24u << 16 | 4u << 13 | 0x02u << 8 | 0x29u); /* the 29th, Thursday */
    wr(&f, RTC + R_WPR, 0xff);                           /* locked again */
    wr(&f, RTC + R_ISR, 1u << 7);
    run_for(&f, 1000);
    CHECK_EQ(rd(&f, RTC + R_ISR) & 1u << 6, 0u);         /* INIT ignored */
    /* SSR read: TR and DR held until DR is read */
    (void)rd(&f, RTC + R_SSR);
    run_for(&f, 2 * RTC_SEC);
    CHECK_EQ(rd(&f, RTC + R_TR), 0x000001u);
    (void)rd(&f, RTC + R_DR);
    run_for(&f, 100);
    CHECK_EQ(rd(&f, RTC + R_TR), 0x000003u);
    (void)rd(&f, RTC + R_DR);
    /* 12-hour mode, in INIT only: 13:00 reads 01 PM */
    wr(&f, RTC + R_WPR, 0xca);
    wr(&f, RTC + R_WPR, 0x53);
    wr(&f, RTC + R_ISR, 1u << 7);
    run_for(&f, 1000);
    wr(&f, RTC + R_CR, 1u << 6);                         /* FMT */
    wr(&f, RTC + R_TR, 1u << 22 | 0x01u << 16);          /* 1 PM */
    rtc_go(&f);
    CHECK_EQ(rd(&f, RTC + R_TR), 1u << 22 | 0x010000u);
    (void)rd(&f, RTC + R_DR);
    wr(&f, RTC + R_CR, 1u << 6 | 1u << 16);              /* ADD1H */
    CHECK_EQ(rd(&f, RTC + R_TR) & 0x7f0000u, 1u << 22 | 0x020000u);
    (void)rd(&f, RTC + R_DR);
    f4_close(&f);
}

/* alarm A on the seconds through EXTI17 and RTC_OUT on PC13; the wakeup
 * timer through EXTI22 */
static void test_rtc_alarm_wakeup(void)
{
    f4 f;
    u64 t0;
    if (!f4_open(&f, 1))
        return;
    rtc_on(&f);
    wr(&f, RTC + R_ISR, 0);                              /* the calendar starts */
    t0 = f.m.cycles;
    run_for(&f, 1000);
    wr(&f, EXTI_IMR, 1u << 17 | 1u << 22);
    wr(&f, EXTI_RTSR, 1u << 17 | 1u << 22);
    CHECK(rd(&f, RTC + R_ISR) & 1u);                     /* ALRAWF: alarm A writable */
    wr(&f, RTC + R_ALRMAR, 1u << 31 | 1u << 23 | 1u << 15 | 0x05u); /* any day, hour, minute; second 05 */
    wr(&f, RTC + R_TAFCR, 1u << 18);                     /* RTC_OUT push-pull */
    wr(&f, RTC + R_CR, 1u << 8 | 1u << 12 | 1u << 21);   /* ALRAE, ALRAIE, OSEL alarm A */
    CHECK_EQ(rd(&f, RTC + R_ISR) & 1u, 0u);
    CHECK_EQ(f.pin[2][13].drive, 0);
    while (!(rd(&f, RTC + R_ISR) & 1u << 8) && f.m.cycles - t0 < 10 * RTC_SEC)
        run_for(&f, 50);
    CHECK(rd(&f, RTC + R_ISR) & 1u << 8);                /* ALRAF */
    CHECK(f.m.cycles - t0 >= 5 * RTC_SEC + 1950 && f.m.cycles - t0 <= 5 * RTC_SEC + 2200); /* and 4 RTCCLK */
    CHECK_EQ(rd(&f, RTC + R_TR), 0x05u);
    (void)rd(&f, RTC + R_DR);
    CHECK(rd(&f, NVIC_ISPR1) & 1u << 9);                 /* RTC_Alarm, IRQ 41 */
    CHECK_EQ(f.pin[2][13].drive, 1);                     /* RTC_OUT */
    wr(&f, RTC + R_ISR, ~(1u << 8 | 1u << 7));          /* ALRAF, INIT left alone */
    CHECK_EQ(f.pin[2][13].drive, 0);
    /* the wakeup timer: 16 counts of RTCCLK / 16 */
    wr(&f, RTC + R_CR, 0);
    run_for(&f, 1000);
    CHECK(rd(&f, RTC + R_ISR) & 1u << 2);                /* WUTWF */
    wr(&f, RTC + R_WUTR, 15);
    wr(&f, RTC + R_CR, 1u << 10 | 1u << 14);             /* WUTE, WUTIE, RTC/16 */
    t0 = f.m.cycles;
    while (!(rd(&f, RTC + R_ISR) & 1u << 10) && f.m.cycles - t0 < 400000)
        run_for(&f, 50);
    CHECK(rd(&f, RTC + R_ISR) & 1u << 10);               /* WUTF */
    CHECK(f.m.cycles - t0 >= 124000 && f.m.cycles - t0 <= 126100); /* 256 RTCCLK clocks */
    CHECK(rd(&f, NVIC_ISPR0) & 1u << 3);                 /* RTC_WKUP, IRQ 3 */
    f4_close(&f);
}

/* the backup domain through a reset; the timestamp and the tamper on PC13 */
static void test_rtc_backup_tamper(void)
{
    f4 f;
    if (!f4_open(&f, 1))
        return;
    rtc_on(&f);
    rtc_go(&f);
    wr(&f, RTC + R_BKP0R, 0x32f2u);                      /* the HAL's "configured" mark */
    run_for(&f, 2 * RTC_SEC + 2500);
    wr(&f, AIRCR, 0x05fa0004u);
    CHECK(reset_if_asked(&f));
    busy(&f);
    CHECK_EQ(rd(&f, RTC + R_BKP0R), 0x32f2u);            /* kept */
    CHECK_EQ(rd(&f, RCC_BDCR) & 0x8301u, 0x8101u);       /* RTCEN, LSE */
    run_for(&f, 2 * RTC_SEC);
    CHECK_EQ(rd(&f, RTC + R_TR), 0x04u);                 /* its time went on */
    (void)rd(&f, RTC + R_DR);
    wr(&f, RTC + R_BKP0R, 0);                            /* DBP cleared by the reset */
    CHECK_EQ(rd(&f, RTC + R_BKP0R), 0x32f2u);
    wr(&f, RCC_APB1ENR, rd(&f, RCC_APB1ENR) | 1u << 28);
    wr(&f, PWR_CR, rd(&f, PWR_CR) | 1u << 8);
    /* a timestamp on PC13's rising edge, then a tamper erasing the backup registers */
    f.ext[2][13] = 0;
    esk_stm32f4_pins_changed(&f.m);
    wr(&f, RCC_AHB1ENR, rd(&f, RCC_AHB1ENR) | 4u);
    wr(&f, RTC + R_WPR, 0xca);
    wr(&f, RTC + R_WPR, 0x53);
    wr(&f, RTC + R_CR, 1u << 11);                        /* TSE, rising */
    f.ext[2][13] = 1;
    esk_stm32f4_pins_changed(&f.m);
    CHECK(rd(&f, RTC + R_ISR) & 1u << 11);               /* TSF */
    CHECK_EQ(rd(&f, RTC + R_TSTR), 0x04u);
    wr(&f, RTC + R_CR, 0);
    wr(&f, RTC + R_TAFCR, 1u);                           /* TAMP1E, rising */
    f.ext[2][13] = 0;
    esk_stm32f4_pins_changed(&f.m);
    CHECK_EQ(rd(&f, RTC + R_ISR) & 1u << 13, 0u);
    f.ext[2][13] = 1;
    esk_stm32f4_pins_changed(&f.m);
    CHECK(rd(&f, RTC + R_ISR) & 1u << 13);               /* TAMP1F */
    CHECK_EQ(rd(&f, RTC + R_BKP0R), 0u);                 /* erased */
    f4_close(&f);
}

/* the core clock goes from HSI's 16 MHz to the PLL's 180 MHz between two
 * seconds: the calendar keeps the room's time */
static void test_rtc_clock_change(void)
{
    f4 f;
    if (!f4_open(&f, 1))
        return;
    rtc_on(&f);
    rtc_go(&f);
    run_for(&f, RTC_SEC + 2500);                         /* one second at 16 MHz */
    CHECK_EQ(rd(&f, RTC + R_TR), 0x01u);
    (void)rd(&f, RTC + R_DR);
    pll_from_hse(&f, PLLCFGR_180);
    overdrive(&f);
    to_pll(&f, 5, 5, 4);
    CHECK_EQ(esk_mach_core_hz(&f.m), 180000000u);
    run_for(&f, (u64)2 * 180000000u / 1024u);            /* two at 180 MHz */
    CHECK_EQ(rd(&f, RTC + R_TR), 0x03u);
    (void)rd(&f, RTC + R_DR);
    f4_close(&f);
}

/* ---- the low-power modes ------------------------------------------------------- */

#define SCR 0xE000ED10u
#define NVIC_ISER0 0xE000E100u
#define BKPSRAM 0x40024000u

/* the program at reset: `insn` (WFI 0xbf30, WFE 0xbf20), then b . */
static void sleep_program(f4 *f, u32 insn)
{
    CHECK_EQ(esk_bus_debug_write(&f->m.bus, 0x100, 4, 0xe7fe0000u | insn), 0);
    esk_mach_reset(&f->m);
}

/* `n` cycles, the core asleep through those no device event fills */
static void run_idle(f4 *f, u64 n)
{
    u64 end = f->m.cycles + n;
    while (f->m.cycles < end && !f->m.reset_req)
        if (!esk_mach_run(&f->m, end - f->m.cycles, 0) && f->m.asleep)
            esk_mach_idle(&f->m, end - f->m.cycles);
}

static u32 peek(f4 *f, u32 a)
{
    u32 v = 0;
    CHECK_EQ(esk_bus_debug_read(&f->m.bus, a, 4, &v), 0);
    return v;
}

/* Stop: the PLL and HSE off, SYSCLK on HSI, the 1.2 V domain's clocks (a
 * timer) stopped while the RTC counts; its wake-up event, an EXTI event
 * on line 22, ends the WFE; the under-drive Stop's UDRDY */
static void test_stop(void)
{
    f4 f;
    u32 cnt;
    if (!f4_open(&f, 1))
        return;
    rtc_on(&f);
    rtc_go(&f);
    pll_from_hse(&f, PLLCFGR_180);
    overdrive(&f);
    to_pll(&f, 5, 5, 4);
    CHECK_EQ(esk_mach_core_hz(&f.m), 180000000u);
    wr(&f, RCC_APB1ENR, rd(&f, RCC_APB1ENR) | 1u);     /* TIM2EN */
    wr(&f, TIM2 + T_ARR, 0xffffffffu);
    wr(&f, TIM2 + T_CR1, 1u);                          /* CEN */
    wr(&f, EXTI + 0x04u, 1u << 22);                    /* EMR22 */
    wr(&f, EXTI_RTSR, 1u << 22);
    wr(&f, RTC + R_CR, 0);
    run_idle(&f, 1000);
    wr(&f, RTC + R_WUTR, 15);
    wr(&f, RTC + R_CR, 1u << 10 | 1u << 14);           /* WUTE, WUTIE, RTC/16: 256 RTCCLK */
    wr(&f, PWR_CR, rd(&f, PWR_CR) | 1u);               /* LPDS */
    wr(&f, SCR, 4u);                                   /* SLEEPDEEP */
    CHECK_EQ(esk_bus_debug_write(&f.m.bus, 0x100, 4, 0xe7febf20u), 0); /* wfe; b . */
    run_idle(&f, 100);
    CHECK(f.m.asleep && f.m.clocks_stopped && !f.m.powered_down);
    CHECK_EQ(peek(&f, RCC_CR) & (1u << 16 | 1u << 24), 0u); /* HSEON, PLLON cleared */
    CHECK_EQ(peek(&f, RCC_CFGR) & 0xFu, 0u);           /* SW, SWS: HSI */
    CHECK_EQ(esk_mach_core_hz(&f.m), 16000000u);
    cnt = rd(&f, TIM2 + T_CNT);
    run_idle(&f, 50000);
    CHECK(f.m.clocks_stopped);
    CHECK_EQ(rd(&f, TIM2 + T_CNT), cnt);             /* the timer's clock stopped */
    run_idle(&f, 200000);                              /* 256 RTCCLK at 16 MHz: 125000 */
    CHECK(!f.m.clocks_stopped);
    CHECK(peek(&f, RTC + R_ISR) & 1u << 10);           /* WUTF */
    run_idle(&f, 1000);
    CHECK(rd(&f, TIM2 + T_CNT) != cnt);              /* counting again */
    f4_close(&f);
    /* the under-drive Stop: UDRDY set, cleared by writing 1 */
    if (!f4_open(&f, 1))
        return;
    busy(&f);
    run_idle(&f, 100);
    wr(&f, RCC_APB1ENR, rd(&f, RCC_APB1ENR) | 1u << 28); /* PWREN */
    wr(&f, PWR_CR, rd(&f, PWR_CR) | 1u | 1u << 10 | 3u << 18); /* LPDS, LPUDS, UDEN */
    wr(&f, EXTI + 0x04u, 1u << 22);
    wr(&f, SCR, 4u);
    CHECK_EQ(esk_bus_debug_write(&f.m.bus, 0x100, 4, 0xe7febf20u), 0);
    run_idle(&f, 100);
    CHECK(f.m.clocks_stopped);
    CHECK_EQ(peek(&f, PWR_CSR) & 3u << 18, 3u << 18);
    wr(&f, EXTI + 0x10u, 1u << 22);                    /* SWIER: an event */
    run_idle(&f, 100);
    CHECK(!f.m.clocks_stopped);
    wr(&f, PWR_CSR, 0);                                /* writing 0 keeps it */
    CHECK_EQ(rd(&f, PWR_CSR) & 3u << 18, 3u << 18);
    wr(&f, PWR_CSR, 3u << 18);
    CHECK_EQ(rd(&f, PWR_CSR) & 3u << 18, 0u);
    f4_close(&f);
}

/* Standby: RAM lost, the backup SRAM kept by BRE, the pins let go, the
 * WKUP pin pulled down; no interrupt wakes the core, the WKUP pin's
 * rising edge resets the part with SBF and WUF; CWUF and CSBF; a system
 * reset keeps SBF and BRE */
static void test_standby(void)
{
    f4 f;
    if (!f4_open(&f, 1))
        return;
    sleep_program(&f, 0xbf30); /* WFI */
    wr(&f, RCC_APB1ENR, rd(&f, RCC_APB1ENR) | 1u << 28); /* PWREN */
    wr(&f, RCC_AHB1ENR, 0x5u);                         /* GPIOAEN, GPIOCEN */
    wr(&f, GPIOA + MODER, (rd(&f, GPIOA + MODER) & ~(3u << 10)) | 1u << 10); /* PA5 output */
    wr(&f, GPIOA + BSRR, 1u << 5);
    CHECK_EQ(f.pin[0][5].drive, 1);
    wr(&f, RCC_CSR, 1u << 24);                         /* RMVF */
    CHECK_EQ(esk_bus_debug_write(&f.m.bus, 0x20000800u, 4, 0xfeedc0deu), 0);
    CHECK_EQ(esk_bus_debug_write(&f.m.bus, BKPSRAM, 4, 0x5eed1234u), 0);
    wr(&f, PWR_CSR, 1u << 9);                          /* BRE */
    CHECK(rd(&f, PWR_CSR) & 1u << 3);                  /* BRR */
    f.ext[0][0] = 0;
    wr(&f, PWR_CSR, rd(&f, PWR_CSR) | 1u << 8);        /* EWUP: PA0 */
    CHECK_EQ(f.pin[0][0].pull, 2);                     /* an input with a pull-down */
    CHECK_EQ(rd(&f, PWR_CSR) & 1u, 0u);                /* low when enabled: no WUF */
    wr(&f, PWR_CR, rd(&f, PWR_CR) | 2u);               /* PDDS */
    wr(&f, SCR, 4u);
    run_idle(&f, 100);
    CHECK(f.m.asleep && f.m.powered_down);
    CHECK_EQ(ram32(&f, 0x20000800u), 0u);              /* RAM lost */
    CHECK_EQ(peek(&f, BKPSRAM), 0x5eed1234u);          /* the backup regulator's */
    CHECK_EQ(f.pin[0][5].drive, -1);                   /* the pins let go */
    CHECK_EQ(f.pin[0][0].pull, 2);
    wr(&f, NVIC_ISER0, 1u << 3);
    wr(&f, NVIC_ISPR0, 1u << 3);                       /* an interrupt: still asleep */
    run_idle(&f, 1000);
    CHECK(f.m.asleep && !f.m.reset_req);
    f.ext[0][0] = 1;                                   /* the WKUP pin's rising edge */
    esk_stm32f4_pins_changed(&f.m);
    CHECK(f.m.reset_req);
    CHECK_EQ(f.m.reset_cause, ESK_RESET_STANDBY);
    esk_mach_reset(&f.m);
    f.m.reset_cause = ESK_RESET_POWER;
    CHECK(!f.m.asleep && !f.m.powered_down && !f.m.clocks_stopped);
    wr(&f, RCC_APB1ENR, rd(&f, RCC_APB1ENR) | 1u << 28);
    CHECK_EQ(rd(&f, PWR_CSR) & 0x30Bu, 0x30Bu);        /* WUF, SBF, BRR, EWUP, BRE kept */
    CHECK_EQ(rd(&f, RCC_CSR) & 0xff000000u, 0x0e000000u); /* a power reset's flags */
    CHECK_EQ(rd(&f, PWR_CR) & 2u, 0u);                 /* PDDS reset */
    wr(&f, PWR_CR, 1u << 2);                           /* CWUF */
    CHECK_EQ(rd(&f, PWR_CSR) & 1u, 0u);
    CHECK_EQ(rd(&f, PWR_CR) & 1u << 2, 0u);            /* reads 0 */
    wr(&f, AIRCR, 0x05fa0004u);                        /* a system reset */
    CHECK(reset_if_asked(&f));
    wr(&f, RCC_APB1ENR, rd(&f, RCC_APB1ENR) | 1u << 28);
    CHECK_EQ(rd(&f, PWR_CSR) & 0x30Bu, 0x20Au);        /* SBF and BRE (BRR) kept, EWUP not */
    wr(&f, PWR_CR, 1u << 3);                           /* CSBF */
    CHECK_EQ(rd(&f, PWR_CSR) & 2u, 0u);
    /* WUF set as Standby is entered ends it at once: a WKUP pin enabled
     * while high is a wake-up event */
    sleep_program(&f, 0xbf30);
    wr(&f, RCC_APB1ENR, rd(&f, RCC_APB1ENR) | 1u << 28);
    wr(&f, PWR_CSR, 1u << 8);
    esk_stm32f4_pins_changed(&f.m);
    CHECK(rd(&f, PWR_CSR) & 1u);                       /* WUF */
    wr(&f, PWR_CR, 2u);
    wr(&f, SCR, 4u);
    run_idle(&f, 100);
    CHECK(f.m.reset_req);
    CHECK_EQ(f.m.reset_cause, ESK_RESET_STANDBY);
    f4_close(&f);
}

/* the option bytes' nRST_STDBY 0: entering Standby resets the part
 * (LPWRRSTF) */
static void test_low_power_reset(void)
{
    f4 f;
    if (!f4_open(&f, 1))
        return;
    CHECK_EQ(esk_bus_debug_write(&f.m.bus, 0x1FFFC000u, 1, 0x6du), 0); /* nRST_STDBY 0 */
    sleep_program(&f, 0xbf30);
    wr(&f, RCC_CSR, 1u << 24);
    wr(&f, RCC_APB1ENR, rd(&f, RCC_APB1ENR) | 1u << 28);
    wr(&f, PWR_CR, 2u);
    wr(&f, SCR, 4u);
    run_idle(&f, 100);
    CHECK(f.m.reset_req && !f.m.powered_down);
    CHECK_EQ(f.m.reset_cause, ESK_RESET_LOW_POWER);
    CHECK(reset_if_asked(&f));
    CHECK_EQ(rd(&f, RCC_CSR) & 0xff000000u, 0x84000000u); /* LPWRRSTF, PINRSTF */
    f4_close(&f);
}

/* SCR's SEVONPEND: a disabled interrupt's becoming pending ends a WFE;
 * SLEEPONEXIT: the return to Thread mode sleeps */
static void test_scr_sleep_bits(void)
{
    f4 f;
    struct esk_cm_state *cs;
    if (!f4_open(&f, 1))
        return;
    cs = (struct esk_cm_state *)f.m.cpu;
    sleep_program(&f, 0xbf20);                         /* WFE */
    wr(&f, SCR, 0x10u);                                /* SEVONPEND */
    run_idle(&f, 100);
    CHECK(f.m.asleep);
    wr(&f, NVIC_ISPR0, 1u << 5);                       /* IRQ 5, not enabled */
    run_idle(&f, 100);
    CHECK_EQ(cs->pc, 0x102u);                          /* past the WFE, on b . */
    /* SLEEPONEXIT: IRQ 0's handler (bx lr) returns to a loop that never
     * idles (adds r0, #1; b), and the core sleeps instead of running it */
    CHECK_EQ(esk_bus_debug_write(&f.m.bus, 0x100, 4, 0xe7fd3001u), 0);
    CHECK_EQ(esk_bus_debug_write(&f.m.bus, 0x40, 4, 0x111u), 0); /* IRQ 0's vector */
    CHECK_EQ(esk_bus_debug_write(&f.m.bus, 0x110, 2, 0x4770u), 0); /* bx lr */
    esk_mach_reset(&f.m);
    run_idle(&f, 100);
    CHECK(!f.m.asleep);
    wr(&f, SCR, 2u);                                   /* SLEEPONEXIT */
    wr(&f, NVIC_ISER0, 1u);
    wr(&f, NVIC_ISPR0, 1u);
    run_idle(&f, 200);
    CHECK(f.m.asleep);
    CHECK_EQ(cs->ipsr, 0u);                            /* back in Thread mode */
    f4_close(&f);
}

/* ---- the flash interface --------------------------------------------------------- */

#define FLASH_KEYR 0x40023C04u
#define FLASH_OPTKEYR 0x40023C08u
#define FLASH_SR 0x40023C0Cu
#define FLASH_CR 0x40023C10u
#define SECTOR2 0x08008000u

static u32 flash16(f4 *f, u32 a)
{
    u32 v = 0;
    CHECK_EQ(esk_bus_debug_read(&f->m.bus, a, 2, &v), 0);
    return v & 0xffffu;
}

static void flash_unlock(f4 *f)
{
    wr(f, FLASH_KEYR, 0x45670123u);
    wr(f, FLASH_KEYR, 0xCDEF89ABu);
    CHECK_EQ(rd(f, FLASH_CR) & 1u << 31, 0u);
}

/* the keys, programming with its errors, a sector erase, EOP and its
 * interrupt, the option bytes' write protection and OPTSTRT */
static void test_flash(void)
{
    f4 f;
    int r;
    if (!f4_open(&f, 1))
        return;
    for (r = 0; r < f.m.bus.nrg; r++) /* read-only to the core, as an image's load leaves it */
        f.m.bus.rg[r].rom = f.m.bus.rg[r].kind == ESK_MEM_FLASH || f.m.bus.rg[r].kind == ESK_MEM_ROM;
    CHECK_EQ(rd(&f, FLASH_CR), 0x80000000u);                /* LOCK */
    wr(&f, FLASH_CR, 1u);                                   /* locked: ignored */
    CHECK_EQ(rd(&f, FLASH_CR), 0x80000000u);
    CHECK(esk_bus_write(&f.m.bus, SECTOR2, 2, 0x1234u) == 0);
    CHECK(rd(&f, FLASH_SR) & 1u << 7);                      /* PGSERR: no PG */
    CHECK_EQ(flash16(&f, SECTOR2), 0xffffu);
    wr(&f, FLASH_SR, 1u << 7);                              /* rc_w1 */
    CHECK_EQ(rd(&f, FLASH_SR), 0u);
    flash_unlock(&f);
    wr(&f, FLASH_CR, 1u << 24 | 1u << 8 | 1u);              /* EOPIE, PSIZE x16, PG */
    CHECK(esk_bus_write(&f.m.bus, SECTOR2, 2, 0x1234u) == 0);
    CHECK_EQ(flash16(&f, SECTOR2), 0x1234u);
    CHECK_EQ(rd(&f, FLASH_SR), 1u << 16);                   /* BSY, no EOP yet */
    r = (int)wait_clr(&f, FLASH_SR, 1u << 16, 100);         /* tprog's 16 us */
    CHECK(r >= 15 && r <= 17);
    CHECK_EQ(rd(&f, FLASH_SR) & 0x1ffffu, 1u);              /* EOP */
    CHECK(rd(&f, NVIC_ISPR0) & 1u << 4);                    /* FLASH, IRQ 4 */
    wr(&f, FLASH_SR, 1u);
    CHECK(esk_bus_write(&f.m.bus, SECTOR2, 2, 0xff0fu) == 0); /* bits only go to 0 */
    CHECK_EQ(flash16(&f, SECTOR2), 0x1204u);
    CHECK(wait_clr(&f, FLASH_SR, 1u << 16, 100) >= 15);
    CHECK(esk_bus_write(&f.m.bus, SECTOR2 + 4, 4, 0u) == 0);  /* a word with x16 */
    CHECK(rd(&f, FLASH_SR) & 1u << 6);                      /* PGPERR */
    CHECK_EQ(flash16(&f, SECTOR2 + 4), 0xffffu);
    wr(&f, FLASH_SR, 0xf3u);
    wr(&f, FLASH_CR, 1u << 9 | 1u);                         /* PSIZE x32 */
    CHECK(esk_bus_write(&f.m.bus, SECTOR2 + 14, 4, 0u) == 0); /* across a 128-bit row */
    CHECK(rd(&f, FLASH_SR) & 1u << 5);                      /* PGAERR */
    wr(&f, FLASH_SR, 0xf3u);
    /* erase sector 2 */
    wr(&f, FLASH_CR, 1u << 9 | 2u << 3 | 1u << 1);          /* SER, SNB 2 */
    wr(&f, FLASH_CR, rd(&f, FLASH_CR) | 1u << 16);          /* STRT */
    CHECK_EQ(flash16(&f, SECTOR2), 0xffffu);
    CHECK(rd(&f, FLASH_CR) & 1u << 16);                     /* STRT until BSY clears */
    wr(&f, FLASH_CR, 1u << 31);                             /* BSY: CR is not written */
    CHECK_EQ(rd(&f, FLASH_CR) & 1u << 31, 0u);
    {   /* a 16 KiB sector at x32: 250 ms, the core (in flash) stalled */
        u64 i0 = f.m.insns;
        long ms = wait_clr(&f, FLASH_SR, 1u << 16, 300000);
        CHECK(ms >= 249000 && ms <= 251000);
        CHECK(f.m.insns - i0 <= 2); /* only after it: the core waited */
    }
    CHECK_EQ(rd(&f, FLASH_CR) & 1u << 16, 0u);
    wr(&f, FLASH_CR, 1u << 31);                             /* LOCK again */
    CHECK(rd(&f, FLASH_CR) & 1u << 31);
    /* a wrong key: a bus error, locked until reset */
    CHECK(esk_bus_write(&f.m.bus, FLASH_KEYR, 4, 0x12345678u) != 0);
    wr(&f, FLASH_KEYR, 0x45670123u);
    wr(&f, FLASH_KEYR, 0xCDEF89ABu);
    CHECK(rd(&f, FLASH_CR) & 1u << 31);
    /* the option bytes: sector 2 write-protected, nRST_STDBY 0 */
    esk_mach_reset(&f.m);
    flash_unlock(&f);
    wr(&f, FLASH_OPTKEYR, 0x08192A3Bu);
    wr(&f, FLASH_OPTKEYR, 0x4C5D6E7Fu);
    CHECK_EQ(rd(&f, FLASH_OPTCR) & 1u, 0u);                 /* OPTLOCK cleared */
    wr(&f, FLASH_OPTCR, (rd(&f, FLASH_OPTCR) & ~(1u << 18) & ~(1u << 7)) | 2u); /* nWRP2 0, nRST_STDBY 0, OPTSTRT */
    CHECK_EQ(flash16(&f, 0x1FFFC000u), 0xaa6du);
    CHECK_EQ(flash16(&f, 0x1FFFC008u) & 0xffu, 0xfbu);
    esk_mach_reset(&f.m);
    CHECK_EQ(rd(&f, FLASH_OPTCR) & 0x00ff00ffu, 0x00fb006du); /* loaded at reset */
    flash_unlock(&f);
    wr(&f, FLASH_CR, 1u << 9 | 1u);
    CHECK(esk_bus_write(&f.m.bus, SECTOR2, 4, 0u) == 0);
    CHECK(rd(&f, FLASH_SR) & 1u << 4);                      /* WRPERR */
    CHECK_EQ(flash16(&f, SECTOR2), 0xffffu);
    f4_close(&f);
}

/* DS10693's start-up times: an 8 MHz crystal's 2 ms (a board with one),
 * the ST-Link's clock with HSEBYP at once, the PLL's lock time between
 * 75 and 100 us, LSI's 15 us and LSE's 2 s; a source that has become
 * ready with its CIR enable sets its flag and the RCC interrupt, and the
 * RDYC bit clears it */
static void test_startup_times(void)
{
    f4 f;
    long t;
    if (!f4_open(&f, 0))
        return;
    wr(&f, RCC_CIR, 1u << 11 | 1u << 12);          /* HSERDYIE, PLLRDYIE */
    wr(&f, RCC_CR, rd(&f, RCC_CR) | 1u << 16);     /* HSEON, a crystal */
    CHECK_EQ(rd(&f, RCC_CR) & 1u << 17, 0u);
    t = wait_set(&f, RCC_CR, 1u << 17, 3000);
    CHECK(t >= 1999 && t <= 2001);
    CHECK_EQ(rd(&f, RCC_CIR) & 0xffu, 1u << 3);    /* HSERDYF */
    CHECK(rd(&f, NVIC_ISPR0) & 1u << 5);           /* RCC */
    wr(&f, RCC_CIR, rd(&f, RCC_CIR) | 1u << 19);   /* HSERDYC */
    CHECK_EQ(rd(&f, RCC_CIR), 1u << 11 | 1u << 12);
    wr(&f, RCC_PLLCFGR, 4u | 90u << 6 | 1u << 22 | 2u << 24 | 2u << 28); /* from HSE: 2 MHz x 90 */
    wr(&f, RCC_CR, rd(&f, RCC_CR) | 1u << 24);
    t = wait_set(&f, RCC_CR, 1u << 25, 300);       /* a 180 MHz VCO: 81 us */
    CHECK(t >= 80 && t <= 82);
    CHECK_EQ(rd(&f, RCC_CIR) & 0xffu, 1u << 4);    /* PLLRDYF */
    /* turned off, it is not ready at once; on again, it starts again */
    wr(&f, RCC_CR, rd(&f, RCC_CR) & ~(1u << 24));
    CHECK_EQ(rd(&f, RCC_CR) & 1u << 25, 0u);
    wr(&f, RCC_CR, rd(&f, RCC_CR) | 1u << 24);
    CHECK_EQ(rd(&f, RCC_CR) & 1u << 25, 0u);
    CHECK(wait_set(&f, RCC_CR, 1u << 25, 300) >= 80);
    f4_close(&f);
    if (!f4_open(&f, 1))
        return;
    wr(&f, RCC_CR, rd(&f, RCC_CR) | 1u << 18 | 1u << 16); /* the ST-Link's MCO, bypassed */
    CHECK_EQ(wait_set(&f, RCC_CR, 1u << 17, 10), 1);      /* six HSE clocks */
    /* LSE through a pin reset: the backup domain's crystal keeps running */
    wr(&f, RCC_APB1ENR, 1u << 28);
    wr(&f, PWR_CR, rd(&f, PWR_CR) | 1u << 8);
    wr(&f, RCC_BDCR, 1u);
    t = wait_set(&f, RCC_BDCR, 2u, 2100000);
    CHECK(t >= 1999000 && t <= 2001000);
    f.m.reset_cause = ESK_RESET_PIN;
    esk_mach_reset(&f.m);
    CHECK_EQ(rd(&f, RCC_BDCR) & 3u, 3u);
    CHECK_EQ(rd(&f, RCC_CR) & 3u, 3u);                    /* HSI on and ready out of reset */
    f4_close(&f);
}

/* the CRC unit: CRC-32 (0x04C11DB7) of the words written, from 0xFFFFFFFF;
 * the values CRC-32/MPEG-2 gives the same bytes, most significant first */
static void test_crc(void)
{
    f4 f;
    if (!f4_open(&f, 1))
        return;
    wr(&f, CRC_DR, 0x12345678u);                   /* its clock is off */
    CHECK_EQ(rd(&f, CRC_DR), 0u);
    wr(&f, RCC_AHB1ENR, rd(&f, RCC_AHB1ENR) | 1u << 12); /* CRCEN */
    CHECK_EQ(rd(&f, CRC_DR), 0xFFFFFFFFu);
    wr(&f, CRC_DR, 0x12345678u);
    CHECK_EQ(rd(&f, CRC_DR), 0xDF8A8A2Bu);
    wr(&f, CRC_DR, 0x9ABCDEF0u);
    CHECK_EQ(rd(&f, CRC_DR), 0x7D24A31Bu);
    wr(&f, CRC_IDR, 0x1A5u);                       /* a byte of storage */
    CHECK_EQ(rd(&f, CRC_IDR), 0xA5u);
    wr(&f, CRC_CR, 1u);                            /* RESET */
    CHECK_EQ(rd(&f, CRC_CR), 0u);
    CHECK_EQ(rd(&f, CRC_DR), 0xFFFFFFFFu);
    CHECK_EQ(rd(&f, CRC_IDR), 0xA5u);              /* RESET leaves it */
    wr(&f, CRC_DR, 0x31323334u);                   /* "1234" */
    CHECK_EQ(rd(&f, CRC_DR), 0xA695C4AAu);
    f4_close(&f);
}

/* ---- USB OTG FS (otg_fs.c), driven as the HAL's PCD driver does, with
 * the host's transactions made from here ------------------------------------- */

#define OTG 0x50000000u
#define RCC_AHB2ENR (RCC + 0x34u)
#define G_GOTGCTL (OTG + 0x000u)
#define G_GOTGINT (OTG + 0x004u)
#define G_GAHBCFG (OTG + 0x008u)
#define G_GUSBCFG (OTG + 0x00Cu)
#define G_GRSTCTL (OTG + 0x010u)
#define G_GINTSTS (OTG + 0x014u)
#define G_GINTMSK (OTG + 0x018u)
#define G_GRXSTSR (OTG + 0x01Cu)
#define G_GRXSTSP (OTG + 0x020u)
#define G_GRXFSIZ (OTG + 0x024u)
#define G_DIEPTXF0 (OTG + 0x028u)
#define G_GCCFG (OTG + 0x038u)
#define G_GSNPSID (OTG + 0x040u)
#define G_DIEPTXF1 (OTG + 0x104u)
#define D_DCFG (OTG + 0x800u)
#define D_DCTL (OTG + 0x804u)
#define D_DSTS (OTG + 0x808u)
#define D_DIEPMSK (OTG + 0x810u)
#define D_DOEPMSK (OTG + 0x814u)
#define D_DAINT (OTG + 0x818u)
#define D_DAINTMSK (OTG + 0x81Cu)
#define D_DIEPCTL(n) (OTG + 0x900u + 0x20u * (n))
#define D_DIEPINT(n) (OTG + 0x908u + 0x20u * (n))
#define D_DIEPTSIZ(n) (OTG + 0x910u + 0x20u * (n))
#define D_DTXFSTS(n) (OTG + 0x918u + 0x20u * (n))
#define D_DOEPCTL(n) (OTG + 0xB00u + 0x20u * (n))
#define D_DOEPINT(n) (OTG + 0xB08u + 0x20u * (n))
#define D_DOEPTSIZ(n) (OTG + 0xB10u + 0x20u * (n))
#define FIFO(n) (OTG + 0x1000u * ((n) + 1u))
#define NVIC_ISPR2 0xE000E208u
/* PLLM 8, PLLN 336, PLLP /2, PLLQ 7 from the 8 MHz HSE: 168 MHz SYSCLK
 * and 48 MHz on PLL_Q, the USB's clock */
#define PLLCFGR_168 (8u | 336u << 6 | 0u << 16 | 1u << 22 | 7u << 24 | 2u << 28)

static int usb_changes, otg_entries, usb_wakeups;
static void r_usb_changed(void *ctx) { (void)ctx; usb_changes++; }
static void r_usb_wakeup(void *ctx) { (void)ctx; usb_wakeups++; }
static void r_exc(void *ctx, int core, int n)
{
    (void)ctx;
    (void)core;
    if (n == 16 + 67)
        otg_entries++;
}

static void run_us(f4 *f, long us) { (void)wait_set(f, OTG + 0x3FFFCu, 1u, us); /* a word that stays 0 */ }

/* PA11 and PA12 on AF10, VBUS (PA9) an input, the core's clock */
static void otg_msp(f4 *f)
{
    wr(f, RCC_AHB1ENR, rd(f, RCC_AHB1ENR) | 1u);       /* GPIOAEN */
    pin_af(f, GPIOA, 11, 10);
    pin_af(f, GPIOA, 12, 10);
    wr(f, RCC_AHB2ENR, rd(f, RCC_AHB2ENR) | 1u << 7);  /* OTGFSEN */
}

static int pulled(f4 *f) { return f->m.usb.pullup(f->m.usb.ctx); }

static void test_otg_fs(void)
{
    f4 f;
    static const u8 get_dev[8] = { 0x80, 6, 0, 1, 0, 0, 64, 0 };
    static const u8 set_addr[8] = { 0x00, 5, 5, 0, 0, 0, 0, 0 };
    u8 pkt[64], out4[4] = { 1, 2, 3, 4 };
    u32 len = 0, st, i;
    int d1 = 0;
    if (!f4_open(&f, 1))
        return;
    f.m.host.usb_changed = r_usb_changed;
    f.m.host.usb_remote_wakeup = r_usb_wakeup;
    usb_changes = usb_wakeups = 0;
    CHECK(f.m.usb.pullup != NULL);
    CHECK_EQ(f.m.usb.dp_at, 12);
    CHECK_EQ(rd(&f, G_GRSTCTL), 0u); /* the clock is off */
    otg_msp(&f);
    CHECK_EQ(rd(&f, G_GRSTCTL), 0x80000000u); /* AHBIDL */
    CHECK_EQ(rd(&f, G_GSNPSID), 0x4F54320Au);
    CHECK_EQ(rd(&f, D_DCTL), 2u);             /* SDIS out of reset */
    CHECK_EQ(rd(&f, G_GINTSTS) & 1u, 0u);     /* CMOD: a device (ID high) */

    /* USB_CoreReset without the 48 MHz clock: CSRST stays set, as the
     * HAL's loop then times out */
    wr(&f, G_GRSTCTL, rd(&f, G_GRSTCTL) | 1u);
    CHECK_EQ(wait_clr(&f, G_GRSTCTL, 1u, 200), -1);
    CHECK_EQ(esk_stm32f4_ck48(&f.m), 0u);
    pll_from_hse(&f, PLLCFGR_168);
    CHECK_EQ(esk_stm32f4_ck48(&f.m), 48000000u);
    CHECK(wait_clr(&f, G_GRSTCTL, 1u, 10) >= 0);

    /* a forced mode takes 25 ms */
    wr(&f, G_GUSBCFG, rd(&f, G_GUSBCFG) | 1u << 29); /* FHMOD */
    CHECK_EQ(wait_set(&f, G_GINTSTS, 1u, 20000), -1);
    CHECK(wait_set(&f, G_GINTSTS, 1u, 10000) >= 0);
    wr(&f, G_GUSBCFG, (rd(&f, G_GUSBCFG) & ~(1u << 29)) | 1u << 30); /* FDMOD */
    CHECK(wait_clr(&f, G_GINTSTS, 1u, 30000) >= 20000);

    /* USB_DevInit with VBUS sensing: the pull-up needs the transceiver,
     * VBUS above 4 V on PA9 and SDIS cleared (USB_DevConnect) */
    wr(&f, G_GCCFG, 1u << 16 | 1u << 21); /* PWRDWN, VBDEN */
    wr(&f, G_GRXFSIZ, 0x80u);
    wr(&f, G_DIEPTXF0, 0x40u << 16 | 0x80u);
    wr(&f, G_DIEPTXF1, 0x80u << 16 | 0xC0u);
    CHECK_EQ(rd(&f, G_GOTGCTL) & 1u << 19, 0u); /* no session */
    f.has_volts[0][9] = 1;
    f.volts[0][9] = 3.5;                         /* below the comparator */
    esk_stm32_otg_fs_pins(&f.m);
    CHECK_EQ(rd(&f, G_GOTGCTL) & 1u << 19, 0u);
    f.volts[0][9] = 5.0;
    esk_stm32_otg_fs_pins(&f.m);
    CHECK(rd(&f, G_GOTGCTL) & 1u << 19);         /* BSVLD */
    CHECK_EQ(pulled(&f), ESK_USB_DETACHED);      /* SDIS still set */
    CHECK_EQ(usb_changes, 0);
    wr(&f, D_DCTL, rd(&f, D_DCTL) & ~2u);
    CHECK_EQ(pulled(&f), ESK_USB_FULL_SPEED);
    CHECK_EQ(usb_changes, 1);
    CHECK_EQ((rd(&f, D_DSTS) >> 22) & 3u, 2u);   /* the line idles at J */

    /* idle 3 ms: suspend; the interrupt line with GINT and USBSUSPM */
    wr(&f, G_GINTMSK, 1u << 11 | 1u << 12 | 1u << 13 | 1u << 4 | 1u << 18 | 1u << 19 | 1u << 31);
    wr(&f, G_GAHBCFG, 1u);
    CHECK_EQ(wait_set(&f, G_GINTSTS, 1u << 11, 2900), -1);
    CHECK(wait_set(&f, G_GINTSTS, 1u << 11, 200) >= 0);
    CHECK(rd(&f, G_GINTSTS) & 1u << 10);         /* ESUSP */
    CHECK_EQ(rd(&f, D_DSTS) & 1u, 1u);           /* SUSPSTS */
    CHECK(rd(&f, NVIC_ISPR2) & 1u << 3);         /* OTG_FS (67) pending */
    /* a level: a handler that returns with the flags still set is entered
     * again, and no more once they are cleared */
    CHECK_EQ(esk_bus_debug_write(&f.m.bus, 0x40u + 4u * 67u, 4, 0x201u), 0);
    CHECK_EQ(esk_bus_debug_write(&f.m.bus, 0x200u, 2, 0x4770u), 0); /* bx lr */
    f.m.host.exc = r_exc;
    otg_entries = 0;
    wr(&f, 0xE000E108u, 1u << 3);                /* NVIC_ISER2 */
    run_us(&f, 20);
    CHECK(otg_entries > 3);
    wr(&f, G_GINTSTS, 1u << 10 | 1u << 11);      /* ESUSP, USBSUSP */
    otg_entries = 0;
    run_us(&f, 20);
    CHECK(otg_entries <= 1);
    wr(&f, 0xE000E188u, 1u << 3);                /* NVIC_ICER2 */
    f.m.host.exc = NULL;

    /* RWUSIG while suspended: remote wakeup, heard by the host once */
    wr(&f, D_DCTL, rd(&f, D_DCTL) | 1u);
    CHECK_EQ(usb_wakeups, 1);
    wr(&f, D_DCTL, rd(&f, D_DCTL) | 1u);         /* still set: no new edge */
    CHECK_EQ(usb_wakeups, 1);
    wr(&f, D_DCTL, rd(&f, D_DCTL) & ~1u);

    /* the host resets the port: WKUINT (it was suspended), USBRST, and
     * at its end ENUMDNE at full speed */
    f.m.usb.bus(f.m.usb.ctx, ESK_USB_RESET, 0);
    CHECK(rd(&f, G_GINTSTS) & 1u << 31);
    CHECK(rd(&f, G_GINTSTS) & 1u << 12);
    CHECK_EQ(rd(&f, D_DSTS) & 1u, 0u);
    run_us(&f, 5000);                            /* SE0 is not idle: no suspend */
    CHECK_EQ(rd(&f, G_GINTSTS) & 1u << 11, 0u);
    wr(&f, G_GINTSTS, 0xFFFFFFFFu);              /* the HAL clears them */
    CHECK_EQ(rd(&f, G_GINTSTS) & (1u << 11 | 1u << 12 | 1u << 31), 0u);
    f.m.usb.bus(f.m.usb.ctx, ESK_USB_RESET_END, 0);
    CHECK(rd(&f, G_GINTSTS) & 1u << 13);
    CHECK_EQ((rd(&f, D_DSTS) >> 1) & 3u, 3u);    /* ENUMSPD full speed */
    f.m.usb.bus(f.m.usb.ctx, ESK_USB_SOF, 0x123u);
    CHECK_EQ((rd(&f, D_DSTS) >> 8) & 0x3FFFu, 0x123u);

    /* USB_EP0_OutStart, then a SETUP: its status and data in the receive
     * FIFO, STUP only once the "setup done" status is taken, which the
     * host's next token puts there; EP0 NAKs until CNAK */
    wr(&f, D_DOEPMSK, 1u | 1u << 3);
    wr(&f, D_DIEPMSK, 1u);
    wr(&f, D_DAINTMSK, 0x10001u);
    wr(&f, D_DOEPTSIZ(0), 3u << 29 | 1u << 19 | 24u);
    CHECK_EQ(f.m.usb.setup(f.m.usb.ctx, 0, 0, get_dev), ESK_USB_ACK);
    CHECK(rd(&f, G_GINTSTS) & 1u << 4);          /* RXFLVL */
    st = rd(&f, G_GRXSTSR);
    CHECK_EQ(st, 6u << 17 | 8u << 4);            /* SETUP data, 8 bytes, EP0 */
    CHECK_EQ(rd(&f, G_GRXSTSR), st);             /* a peek */
    CHECK_EQ((rd(&f, D_DOEPTSIZ(0)) >> 29) & 3u, 2u); /* STUPCNT */
    CHECK_EQ(f.m.usb.in(f.m.usb.ctx, 0, 0, pkt, sizeof pkt, &len, &d1), ESK_USB_NAK);
    CHECK_EQ(rd(&f, G_GRXSTSP), st);
    CHECK_EQ(rd(&f, FIFO(0)), 0x01000680u);
    CHECK_EQ(rd(&f, FIFO(0)), 0x00400000u);
    CHECK_EQ(rd(&f, D_DOEPINT(0)) & 1u << 3, 0u);
    CHECK_EQ(rd(&f, G_GRXSTSP), 4u << 17);       /* SETUP stage done */
    CHECK(rd(&f, D_DOEPINT(0)) & 1u << 3);       /* STUP */
    CHECK(rd(&f, D_DAINT) & 1u << 16);
    CHECK(rd(&f, G_GINTSTS) & 1u << 19);         /* OEPINT */
    CHECK_EQ(rd(&f, G_GINTSTS) & 1u << 4, 0u);
    wr(&f, D_DOEPINT(0), 1u << 3);

    /* the data stage: an 18-byte packet goes once all its words are in
     * EP0's FIFO; XFRC and EPENA's clearing after it */
    wr(&f, D_DIEPTSIZ(0), 1u << 19 | 18u);
    wr(&f, D_DIEPCTL(0), rd(&f, D_DIEPCTL(0)) | 1u << 26 | 1u << 31);
    CHECK_EQ(rd(&f, D_DTXFSTS(0)), 0x40u);
    for (i = 0; i < 4; i++)
        wr(&f, FIFO(0), 0x03020100u + 0x04040404u * i);
    CHECK_EQ(rd(&f, D_DTXFSTS(0)), 0x3Cu);
    CHECK_EQ(f.m.usb.in(f.m.usb.ctx, 0, 0, pkt, sizeof pkt, &len, &d1), ESK_USB_NAK);
    wr(&f, FIFO(0), 0x00001110u);
    CHECK_EQ(f.m.usb.in(f.m.usb.ctx, 0, 0, pkt, sizeof pkt, &len, &d1), ESK_USB_ACK);
    CHECK_EQ(len, 18u);
    CHECK_EQ(d1, 1);                             /* DATA1 after a SETUP */
    for (i = 0; i < 18; i++)
        CHECK_EQ(pkt[i], (u8)i);
    CHECK(rd(&f, D_DIEPINT(0)) & 1u);            /* XFRC */
    CHECK_EQ(rd(&f, D_DIEPCTL(0)) & 1u << 31, 0u);
    CHECK_EQ(f.m.usb.in(f.m.usb.ctx, 0, 0, pkt, sizeof pkt, &len, &d1), ESK_USB_NAK);
    wr(&f, D_DIEPINT(0), 1u);

    /* SET_ADDRESS: the HAL writes DAD in its setup stage; the core answers
     * address 0 until the status stage's IN packet has gone */
    CHECK_EQ(f.m.usb.setup(f.m.usb.ctx, 0, 0, set_addr), ESK_USB_ACK);
    CHECK_EQ(f.m.usb.in(f.m.usb.ctx, 0, 0, pkt, sizeof pkt, &len, &d1), ESK_USB_NAK);
    (void)rd(&f, G_GRXSTSP);
    (void)rd(&f, FIFO(0));
    (void)rd(&f, FIFO(0));
    (void)rd(&f, G_GRXSTSP);
    wr(&f, D_DCFG, (rd(&f, D_DCFG) & ~(0x7Fu << 4)) | 5u << 4);
    wr(&f, D_DIEPTSIZ(0), 1u << 19);
    wr(&f, D_DIEPCTL(0), rd(&f, D_DIEPCTL(0)) | 1u << 26 | 1u << 31);
    CHECK_EQ(f.m.usb.in(f.m.usb.ctx, 0, 0, pkt, sizeof pkt, &len, &d1), ESK_USB_ACK);
    CHECK_EQ(len, 0u);
    CHECK_EQ(f.m.usb.in(f.m.usb.ctx, 0, 0, pkt, sizeof pkt, &len, &d1), ESK_USB_NONE);
    CHECK_EQ(f.m.usb.in(f.m.usb.ctx, 5, 0, pkt, sizeof pkt, &len, &d1), ESK_USB_NAK);

    /* an interrupt OUT endpoint: DATA0 first; a DATA1 repeat is ACKed and
     * dropped; the short packet ends the transfer (XFRC when its "OUT
     * done" status is taken) */
    wr(&f, D_DOEPCTL(1), 64u | 1u << 15 | 3u << 18 | 1u << 28);
    wr(&f, D_DOEPTSIZ(1), 1u << 19 | 64u);
    CHECK_EQ(f.m.usb.out(f.m.usb.ctx, 5, 1, 0, out4, 4), ESK_USB_NAK); /* not enabled */
    wr(&f, D_DOEPCTL(1), rd(&f, D_DOEPCTL(1)) | 1u << 26 | 1u << 31);
    CHECK_EQ(f.m.usb.out(f.m.usb.ctx, 5, 1, 1, out4, 4), ESK_USB_ACK);
    CHECK_EQ(rd(&f, G_GINTSTS) & 1u << 4, 0u);   /* dropped */
    CHECK_EQ(f.m.usb.out(f.m.usb.ctx, 5, 1, 0, out4, 4), ESK_USB_ACK);
    CHECK_EQ(rd(&f, G_GRXSTSP), 2u << 17 | 4u << 4 | 1u);
    CHECK_EQ(rd(&f, FIFO(0)), 0x04030201u);
    CHECK_EQ(rd(&f, D_DOEPTSIZ(1)) & 0x7FFFFu, 60u);
    CHECK_EQ(rd(&f, G_GRXSTSP), 3u << 17 | 1u);
    CHECK(rd(&f, D_DOEPINT(1)) & 1u);
    CHECK_EQ(rd(&f, D_DOEPCTL(1)) & 1u << 31, 0u);
    CHECK_EQ(f.m.usb.out(f.m.usb.ctx, 5, 1, 1, out4, 4), ESK_USB_NAK);

    /* a stalled IN endpoint; a token for an endpoint not active */
    wr(&f, D_DIEPCTL(1), 4u | 1u << 15 | 3u << 18 | 1u << 22 | 1u << 21);
    CHECK_EQ(f.m.usb.in(f.m.usb.ctx, 5, 1, pkt, sizeof pkt, &len, &d1), ESK_USB_STALL);
    CHECK_EQ(f.m.usb.in(f.m.usb.ctx, 5, 2, pkt, sizeof pkt, &len, &d1), ESK_USB_NONE);

    /* RWUSIG on a bus that is not suspended: nothing (USB 2.0 7.1.7.7) */
    wr(&f, D_DCTL, rd(&f, D_DCTL) | 1u);
    wr(&f, D_DCTL, rd(&f, D_DCTL) & ~1u);
    CHECK_EQ(usb_wakeups, 1);

    /* VBUS gone: the session ends (SEDET), the pull-up goes */
    f.volts[0][9] = 0.0;
    esk_stm32_otg_fs_pins(&f.m);
    CHECK_EQ(pulled(&f), ESK_USB_DETACHED);
    CHECK_EQ(usb_changes, 2);
    CHECK(rd(&f, G_GOTGINT) & 1u << 2);
    CHECK_STR(f.codes, "mcu.clock_gated "); /* the read before OTGFSEN */
    f4_close(&f);
}

/* TIM5_OR's IT4_RMP: LSI on TI4, measured as HAL's GetLSIFrequency does
 * (input capture every 8 rising edges): 8 periods of 32 kHz are 4000
 * counts of a 16 MHz timer clock; nothing while LSI is off; the pin no
 * longer reaches TI4 */
static void test_tim5_lsi(void)
{
    f4 f;
    u32 a, b;
    if (!f4_open(&f, 1))
        return;
    wr(&f, RCC_APB1ENR, rd(&f, RCC_APB1ENR) | 1u << 3); /* TIM5EN */
    wr(&f, TIM5 + 0x50, 1u << 6);                       /* OR: IT4_RMP LSI */
    wr(&f, TIM5 + T_CCMR2, 1u << 8 | 3u << 10);         /* CC4S TI4, IC4PSC /8 */
    wr(&f, TIM5 + T_CCER, 1u << 12);                    /* CC4E, rising */
    wr(&f, TIM5 + T_ARR, 0xffffffffu);
    wr(&f, TIM5 + T_CR1, 1u);
    CHECK_EQ(wait_set(&f, TIM5 + T_SR, 1u << 4, 2000), -1); /* LSI off: no edges */
    esk_stm32_tim_signal_input(&f.m, "TIM5_CH4", 1);    /* the pin: not connected */
    esk_stm32_tim_signal_input(&f.m, "TIM5_CH4", 0);
    CHECK_EQ(rd(&f, TIM5 + T_SR) & 1u << 4, 0u);
    wr(&f, RCC_CSR, 1u);                                /* LSION */
    CHECK(wait_set(&f, RCC_CSR, 2u, 40) >= 14);
    CHECK(wait_set(&f, TIM5 + T_SR, 1u << 4, 1000) >= 0);
    a = rd(&f, TIM5 + T_CCR4);
    CHECK(wait_set(&f, TIM5 + T_SR, 1u << 4, 1000) >= 0);
    b = rd(&f, TIM5 + T_CCR4);
    CHECK(b - a >= 3999u && b - a <= 4001u);
    wr(&f, TIM5 + 0x50, 0);                             /* back to the pin */
    wr(&f, TIM5 + T_SR, 0);
    CHECK_EQ(wait_set(&f, TIM5 + T_SR, 1u << 4, 1000), -1);
    f4_close(&f);
}

/* TIM3 and TIM4 are 16-bit (RM0390 16.4): the upper halves of CNT, ARR and
 * CCRx are reserved, so a compare value advanced past 65535 (TIM_OCToggle's
 * CCR + step) wraps as the counter does; TIM2's are 32-bit */
static void test_tim_widths(void)
{
    f4 f;
    if (!f4_open(&f, 1))
        return;
    wr(&f, RCC_APB1ENR, rd(&f, RCC_APB1ENR) | 1u << 0 | 1u << 1 | 1u << 2); /* TIM2-TIM4 */
    wr(&f, TIM3 + T_CCR1, 0x12345u);
    CHECK_EQ(rd(&f, TIM3 + T_CCR1), 0x2345u);
    wr(&f, TIM3 + T_ARR, 0x1FFFFu);
    CHECK_EQ(rd(&f, TIM3 + T_ARR), 0xFFFFu);
    wr(&f, 0x40000800u + T_CCR4, 0x10001u);                 /* TIM4 */
    CHECK_EQ(rd(&f, 0x40000800u + T_CCR4), 1u);
    wr(&f, TIM2 + T_CCR1, 0x12345u);
    CHECK_EQ(rd(&f, TIM2 + T_CCR1), 0x12345u);
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
RUN(test_spi_master);
RUN(test_spi_modf);
RUN(test_spi_slave);
RUN(test_spi_crc);
RUN(test_spi_dma_crc);
RUN(test_spi_modes);
RUN(test_i2c_master);
RUN(test_i2c_stretch_arlo);
RUN(test_i2c_slave);
RUN(test_i2c_dma);
RUN(test_i2c_wired);
RUN(test_adc_single);
RUN(test_adc_scan_dma);
RUN(test_adc_injected_trigger);
RUN(test_adc_dual);
RUN(test_dac);
RUN(test_reset_flags);
RUN(test_iwdg);
RUN(test_wwdg);
RUN(test_rtc_calendar);
RUN(test_rtc_alarm_wakeup);
RUN(test_rtc_backup_tamper);
RUN(test_rtc_clock_change);
RUN(test_stop);
RUN(test_standby);
RUN(test_low_power_reset);
RUN(test_scr_sleep_bits);
RUN(test_flash);
RUN(test_startup_times);
RUN(test_crc);
RUN(test_tim5_lsi);
RUN(test_tim_widths);
RUN(test_otg_fs);
TEST_MAIN_END
