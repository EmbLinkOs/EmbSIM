/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* main.c -- the NUCLEO-G474RE example: 170 MHz from the 24 MHz crystal X3
 * in range 1 boost, LD2 (PA5) blinking at 1 Hz from SysTick, B1 (PC13,
 * active low on the Nucleo-64) counted through EXTI13's falling edges (an
 * edge after 20 ms without one: B1's contacts bounce), and the STLINK-V3E's
 * virtual COM port (LPUART1 on PA2/PA3 at 115200 baud) echoing in upper
 * case. The registers are written directly, in the order STM32CubeG4's
 * SystemClock_Config writes them (RM0440 7.2.7: the AHB at /2 for the
 * switch above 80 MHz). */
#include <stdint.h>

#define REG(a) (*(volatile uint32_t *)(a))

#define RCC_CR REG(0x40021000u)
#define RCC_CFGR REG(0x40021008u)
#define RCC_PLLCFGR REG(0x4002100Cu)
#define RCC_AHB2ENR REG(0x4002104Cu)
#define RCC_APB1ENR1 REG(0x40021058u)
#define RCC_APB1ENR2 REG(0x4002105Cu)
#define RCC_APB2ENR REG(0x40021060u)
#define RCC_CSR REG(0x40021094u)
#define PWR_CR5 REG(0x40007080u)
#define FLASH_ACR REG(0x40022000u)
#define GPIOA 0x48000000u
#define GPIOC 0x48000800u
#define MODER(p) REG((p) + 0x00u)
#define IDR(p) REG((p) + 0x10u)
#define ODR(p) REG((p) + 0x14u)
#define AFRL(p) REG((p) + 0x20u)
#define LPUART1 0x40008000u
#define LP_CR1 REG(LPUART1 + 0x00u)
#define LP_BRR REG(LPUART1 + 0x0Cu)
#define LP_ISR REG(LPUART1 + 0x1Cu)
#define LP_RDR REG(LPUART1 + 0x24u)
#define LP_TDR REG(LPUART1 + 0x28u)
#define SYSCFG_EXTICR4 REG(0x40010014u)
#define EXTI_IMR1 REG(0x40010400u)
#define EXTI_FTSR1 REG(0x4001040Cu)
#define EXTI_PR1 REG(0x40010414u)
#define SYST_CSR REG(0xE000E010u)
#define SYST_RVR REG(0xE000E014u)
#define SYST_CVR REG(0xE000E018u)
#define NVIC_ISER1 REG(0xE000E104u)
#define NVIC_ISER2 REG(0xE000E108u)
#define FLASH_SIZE (*(volatile uint16_t *)0x1FFF75E0u)

static volatile uint32_t ticks, presses, reported, last_edge;

static void putc_(char c)
{
    while (!(LP_ISR & (1u << 7))) /* TXE */
        ;
    LP_TDR = (uint8_t)c;
}

static void puts_(const char *s)
{
    while (*s)
        putc_(*s++);
}

static void putu(uint32_t v)
{
    char b[11];
    int n = 0;
    do {
        b[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n)
        putc_(b[--n]);
}

/* range 1 boost, HSE (the crystal) through the PLL: 24 MHz / 6 x 85 / 2 =
 * 170 MHz, four wait states */
static void clock_170mhz(void)
{
    RCC_APB1ENR1 |= 1u << 28;  /* PWREN */
    PWR_CR5 &= ~(1u << 8);     /* R1MODE 0: range 1 boost */
    RCC_CR |= 1u << 16;        /* HSEON */
    while (!(RCC_CR & (1u << 17))) /* HSERDY: the crystal's start-up */
        ;
    RCC_PLLCFGR = 3u | (5u << 4) | (85u << 8) | (1u << 24); /* HSE, M /6, N 85, R /2, PLLREN */
    RCC_CR |= 1u << 24;                                     /* PLLON */
    while (!(RCC_CR & (1u << 25)))
        ;
    FLASH_ACR = (FLASH_ACR & ~0xFu) | 4u | (1u << 8);  /* LATENCY 4, PRFTEN */
    RCC_CFGR = (RCC_CFGR & ~0xF0u) | 0x80u;            /* HPRE /2 for the switch */
    RCC_CFGR = (RCC_CFGR & ~3u) | 3u;                  /* SW PLL */
    while (((RCC_CFGR >> 2) & 3u) != 3u)
        ;
    for (volatile int i = 0; i < 100; i++) /* the 1 us the AHB stays at /2 */
        ;
    RCC_CFGR &= ~0xF0u; /* HPRE /1 */
}

/* SYSCLK as the RCC's registers give it */
static uint32_t sysclk_khz(void)
{
    uint32_t pll = RCC_PLLCFGR;
    uint32_t m = ((pll >> 4) & 0xFu) + 1u, n = (pll >> 8) & 0x7Fu, r = 2u * (((pll >> 25) & 3u) + 1u);
    return 24000u / m * n / r;
}

void SysTick_Handler(void)
{
    if (++ticks % 500u == 0u)
        ODR(GPIOA) ^= 1u << 5; /* LD2 */
}

void EXTI15_10_IRQHandler(void)
{
    if (EXTI_PR1 & (1u << 13)) {
        uint32_t now = ticks;
        EXTI_PR1 = 1u << 13;
        /* B1's contacts bounce: a press is a falling edge after the line has
         * been still for 20 ms */
        if (!(IDR(GPIOC) & (1u << 13)) && now - last_edge >= 20u)
            presses++;
        last_edge = now;
    }
}

void LPUART1_IRQHandler(void)
{
    if (LP_ISR & (1u << 5)) { /* RXNE */
        char c = (char)LP_RDR;
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 'a' + 'A');
        LP_TDR = (uint8_t)c;
    }
}

int main(void)
{
    uint32_t csr = RCC_CSR;
    clock_170mhz();
    RCC_AHB2ENR |= (1u << 0) | (1u << 2); /* GPIOA, GPIOC */
    RCC_APB1ENR2 |= 1u << 0;              /* LPUART1 */
    RCC_APB2ENR |= 1u << 0;               /* SYSCFG */
    (void)RCC_APB2ENR;
    /* PA5 an output (LD2); PA2, PA3 AF12 (LPUART1 to the STLINK-V3E); PC13
     * an input (B1, held high by the board's pull-up) */
    MODER(GPIOA) = (MODER(GPIOA) & ~((3u << 10) | (0xFu << 4))) | (1u << 10) | (0xAu << 4);
    AFRL(GPIOA) = (AFRL(GPIOA) & ~(0xFFu << 8)) | (0xCCu << 8);
    MODER(GPIOC) &= ~(3u << 26);
    /* 115200 baud from PCLK1 = 170 MHz: BRR = 256 x 170 MHz / 115200 */
    LP_BRR = 377778u;
    LP_CR1 = (1u << 5) | (1u << 3) | (1u << 2); /* RXNEIE, TE, RE */
    LP_CR1 |= 1u;                               /* UE */
    while ((LP_ISR & (3u << 21)) != (3u << 21)) /* TEACK, REACK */
        ;
    /* B1: EXTI13 on port C, falling edge (the Nucleo-64's button pulls down) */
    SYSCFG_EXTICR4 = (SYSCFG_EXTICR4 & ~(0xFu << 4)) | (2u << 4);
    EXTI_FTSR1 |= 1u << 13;
    EXTI_IMR1 |= 1u << 13;
    NVIC_ISER1 = 1u << (40 - 32);
    NVIC_ISER2 = 1u << (91 - 64);
    /* SysTick every millisecond from HCLK */
    SYST_RVR = sysclk_khz() - 1u;
    SYST_CVR = 0;
    SYST_CSR = 7u;

    puts_("nucleo-g474re: STM32G474RE, flash ");
    putu(FLASH_SIZE);
    puts_(" KB, SYSCLK ");
    putu(sysclk_khz());
    puts_(" kHz, reset ");
    puts_((csr & (1u << 27)) ? "por" : (csr & (1u << 28)) ? "software" : (csr & (1u << 26)) ? "pin" : "other");
    puts_("\r\n");
    for (;;) {
        __asm volatile("wfi");
        if (presses != reported) {
            reported = presses;
            puts_("button ");
            putu(reported);
            puts_("\r\n");
        }
    }
}
