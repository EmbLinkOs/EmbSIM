/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* main.c -- the NUCLEO-F446RE example: 180 MHz from the ST-LINK's 8 MHz MCO,
 * LD2 blinking at 1 Hz from SysTick, B1 counted through EXTI13, and the
 * ST-LINK's virtual COM port (USART2 at 115200 baud) echoing in upper case.
 * The registers are written directly, in the order STM32CubeF4's
 * SystemClock_Config writes them. */
#include <stdint.h>

#define REG(a) (*(volatile uint32_t *)(a))

#define RCC_CR REG(0x40023800u)
#define RCC_PLLCFGR REG(0x40023804u)
#define RCC_CFGR REG(0x40023808u)
#define RCC_AHB1ENR REG(0x40023830u)
#define RCC_APB1ENR REG(0x40023840u)
#define RCC_APB2ENR REG(0x40023844u)
#define RCC_CSR REG(0x40023874u)
#define PWR_CR REG(0x40007000u)
#define PWR_CSR REG(0x40007004u)
#define FLASH_ACR REG(0x40023C00u)
#define GPIOA 0x40020000u
#define GPIOC 0x40020800u
#define MODER(p) REG((p) + 0x00u)
#define BSRR(p) REG((p) + 0x18u)
#define ODR(p) REG((p) + 0x14u)
#define AFRL(p) REG((p) + 0x20u)
#define USART2_SR REG(0x40004400u)
#define USART2_DR REG(0x40004404u)
#define USART2_BRR REG(0x40004408u)
#define USART2_CR1 REG(0x4000440Cu)
#define SYSCFG_EXTICR4 REG(0x40013814u)
#define EXTI_IMR REG(0x40013C00u)
#define EXTI_FTSR REG(0x40013C0Cu)
#define EXTI_PR REG(0x40013C14u)
#define SYST_CSR REG(0xE000E010u)
#define SYST_RVR REG(0xE000E014u)
#define SYST_CVR REG(0xE000E018u)
#define NVIC_ISER1 REG(0xE000E104u)
#define F_SIZE (*(volatile uint16_t *)0x1FFF7A22u)

#define PLL_M 8u
#define PLL_N 360u
#define PLL_P 2u
#define PLL_Q 7u
#define PLL_R 2u

static volatile uint32_t ticks, presses, reported;

static void putc_(char c)
{
    while (!(USART2_SR & (1u << 7))) /* TXE */
        ;
    USART2_DR = (uint8_t)c;
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

/* HSE in bypass from the ST-LINK's MCO, the PLL to 180 MHz with over-drive,
 * five wait states, APB1 at 45 MHz and APB2 at 90 MHz */
static void clock_180mhz(void)
{
    RCC_CR |= (1u << 18) | (1u << 16); /* HSEBYP, HSEON */
    while (!(RCC_CR & (1u << 17)))     /* HSERDY */
        ;
    RCC_APB1ENR |= 1u << 28; /* PWR */
    PWR_CR = (PWR_CR & ~(3u << 14)) | (3u << 14); /* VOS scale 1 */
    RCC_PLLCFGR = PLL_M | (PLL_N << 6) | (((PLL_P / 2u) - 1u) << 16) | (1u << 22) | (PLL_Q << 24) | (PLL_R << 28);
    RCC_CR |= 1u << 24; /* PLLON */
    while (!(RCC_CR & (1u << 25)))
        ;
    PWR_CR |= 1u << 16; /* ODEN */
    while (!(PWR_CSR & (1u << 16)))
        ;
    PWR_CR |= 1u << 17; /* ODSWEN */
    while (!(PWR_CSR & (1u << 17)))
        ;
    FLASH_ACR = 5u | (1u << 8) | (1u << 9) | (1u << 10); /* LATENCY 5, PRFTEN, ICEN, DCEN */
    RCC_CFGR = (RCC_CFGR & ~0xFCF3u) | (5u << 10) | (4u << 13) | 2u; /* PPRE1 /4, PPRE2 /2, SW PLL */
    while (((RCC_CFGR >> 2) & 3u) != 2u)
        ;
}

/* SYSCLK as the RCC's registers give it */
static uint32_t sysclk_khz(void)
{
    uint32_t pll = RCC_PLLCFGR;
    uint32_t m = pll & 0x3Fu, n = (pll >> 6) & 0x1FFu, p = (((pll >> 16) & 3u) + 1u) * 2u;
    return 8000u / m * n / p;
}

void SysTick_Handler(void)
{
    if (++ticks % 500u == 0u)
        ODR(GPIOA) ^= 1u << 5; /* LD2 */
}

void EXTI15_10_IRQHandler(void)
{
    if (EXTI_PR & (1u << 13)) {
        EXTI_PR = 1u << 13;
        presses++;
    }
}

void USART2_IRQHandler(void)
{
    if (USART2_SR & (1u << 5)) { /* RXNE */
        char c = (char)USART2_DR;
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 'a' + 'A');
        USART2_DR = (uint8_t)c;
    }
}

int main(void)
{
    uint32_t csr = RCC_CSR;
    clock_180mhz();
    RCC_AHB1ENR |= (1u << 0) | (1u << 2); /* GPIOA, GPIOC */
    RCC_APB1ENR |= 1u << 17;              /* USART2 */
    RCC_APB2ENR |= 1u << 14;              /* SYSCFG */
    (void)RCC_APB2ENR;
    /* PA5 output (LD2); PA2, PA3 AF7 (USART2 to the ST-LINK); PC13 input (B1) */
    MODER(GPIOA) = (MODER(GPIOA) & ~((3u << 10) | (3u << 4) | (3u << 6))) | (1u << 10) | (2u << 4) | (2u << 6);
    AFRL(GPIOA) = (AFRL(GPIOA) & ~0xFF00u) | (7u << 8) | (7u << 12);
    MODER(GPIOC) &= ~(3u << 26);
    /* 115200 baud from PCLK1 = 45 MHz: USARTDIV = 45 MHz / (16 x 115200) =
     * 24.41, mantissa 24 and fraction 7/16 (115 089 baud, -0.1 %) */
    USART2_BRR = (24u << 4) | 7u;
    USART2_CR1 = (1u << 13) | (1u << 5) | (1u << 3) | (1u << 2); /* UE, RXNEIE, TE, RE */
    /* B1: EXTI13 on port C, falling edge */
    SYSCFG_EXTICR4 = (SYSCFG_EXTICR4 & ~(0xFu << 4)) | (2u << 4);
    EXTI_FTSR |= 1u << 13;
    EXTI_IMR |= 1u << 13;
    NVIC_ISER1 = (1u << (38 - 32)) | (1u << (40 - 32));
    /* SysTick every millisecond from HCLK */
    SYST_RVR = sysclk_khz() - 1u;
    SYST_CVR = 0;
    SYST_CSR = 7u;

    puts_("nucleo-f446re: STM32F446RE, flash ");
    putu(F_SIZE);
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
