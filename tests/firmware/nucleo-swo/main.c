/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* main.c -- printf over SWO on the NUCLEO-F446RE: the core on its 16 MHz
 * HSI, SysTick at 1 kHz, and every 100 ms a line sent through the ITM's
 * stimulus port 0 as CMSIS's ITM_SendChar sends it (it waits for
 * FIFOREADY, and sends nothing while a debugger has not enabled the ITM and
 * the port). The trace's set-up (DBGMCU's TRACE_IOEN, the TPIU's protocol
 * and prescaler, the ITM's TCR and TER) is the debugger's, as OpenOCD's
 * `tpiu` and `itm` commands do it. LD2 toggles with each line. */
#include <stdint.h>

#define REG(a) (*(volatile uint32_t *)(a))
#define RCC_AHB1ENR REG(0x40023830u)
#define GPIOA 0x40020000u
#define MODER(p) REG((p) + 0x00u)
#define ODR(p) REG((p) + 0x14u)
#define SYST_CSR REG(0xE000E010u)
#define SYST_RVR REG(0xE000E014u)
#define SYST_CVR REG(0xE000E018u)
#define ITM_PORT0 REG(0xE0000000u)
#define ITM_PORT0_8 (*(volatile uint8_t *)0xE0000000u)
#define ITM_TER REG(0xE0000E00u)
#define ITM_TCR REG(0xE0000E80u)
#define DEMCR REG(0xE000EDFCu)

static volatile uint32_t ticks;

void SysTick_Handler(void) { ticks++; }

/* CMSIS's ITM_SendChar */
static void itm_putc(char c)
{
    if ((DEMCR & (1u << 24)) && (ITM_TCR & 1u) && (ITM_TER & 1u)) {
        while (ITM_PORT0 == 0u)
            ;
        ITM_PORT0_8 = (uint8_t)c;
    }
}

static void itm_puts(const char *s)
{
    while (*s)
        itm_putc(*s++);
}

static void itm_putu(uint32_t v)
{
    char b[11];
    int n = 0;
    do {
        b[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n)
        itm_putc(b[--n]);
}

int main(void)
{
    uint32_t next = 100, lines = 0;
    RCC_AHB1ENR |= 1u;                               /* GPIOA */
    MODER(GPIOA) = (MODER(GPIOA) & ~(3u << 10)) | (1u << 10); /* PA5 (LD2) output */
    SYST_RVR = 16000u - 1u;
    SYST_CVR = 0;
    SYST_CSR = 7u; /* the core's clock, the interrupt, on */
    for (;;) {
        if (ticks >= next) {
            next += 100u;
            ODR(GPIOA) ^= 1u << 5;
            itm_puts("swo line ");
            itm_putu(lines++);
            itm_puts(" at ");
            itm_putu(ticks);
            itm_puts(" ms\n");
        }
    }
}
