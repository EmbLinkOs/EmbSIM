/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* main.c -- Nordic's nrfx NVMC driver on the nRF52840 DK, as an
 * application keeping its settings in flash uses it, each operation timed
 * on TIMER1 at 16 MHz while the core, running from the flash, waits it
 * out: the flash's geometry from FICR; a page erased with
 * nrfx_nvmc_page_erase, words written with nrfx_nvmc_words_write and a
 * byte with nrfx_nvmc_byte_write, read back; a page erased in 10 ms parts
 * with nrfx_nvmc_page_partial_erase_continue until the driver says it is
 * done; an address within a page refused (-EACCES); UICR's CUSTOMER[0]
 * written with nrfx_nvmc_word_write, then a reset: the second boot reads
 * it back, as the UICR keeps it. UARTE0 on the board's VCOM pins (P0.06
 * TX) at 115200 baud carries the report. */
#include <stdio.h>
#include <string.h>

#include <hal/nrf_gpio.h>
#include <nrfx_nvmc.h>
#include <nrfx_uarte.h>

#define TX_PIN NRF_GPIO_PIN_MAP(0, 6)
#define RX_PIN NRF_GPIO_PIN_MAP(0, 8)
#define PAGE_A 0x000F0000u /* two pages near the end of the 1 MiB */
#define PAGE_B 0x000F1000u

static nrfx_uarte_t uarte = NRFX_UARTE_INSTANCE(NRF_UARTE0);

/* `s` out through UARTE0's EasyDMA, which reads RAM only: copied there */
static void say(const char *s)
{
    static char buf[128];
    size_t n = strlen(s) < sizeof buf ? strlen(s) : sizeof buf - 1;
    memcpy(buf, s, n);
    nrfx_uarte_tx(&uarte, (const uint8_t *)buf, n, NRFX_UARTE_TX_BLOCKING);
}

static uint32_t now(void)
{
    NRF_TIMER1->TASKS_CAPTURE[0] = 1;
    return NRF_TIMER1->CC[0];
}

/* microseconds since `t0` at 16 MHz */
static unsigned long us_since(uint32_t t0) { return (unsigned long)((now() - t0) / 16u); }

int main(void)
{
    char line[128];
    static const uint32_t words[3] = { 0x11223344u, 0x55667788u, 0x99AABBCCu };
    nrfx_uarte_config_t ucfg = NRFX_UARTE_DEFAULT_CONFIG(TX_PIN, RX_PIN);
    uint32_t t0;
    int rc, parts = 0;

    nrfx_uarte_init(&uarte, &ucfg, NULL);
    NRF_TIMER1->MODE = TIMER_MODE_MODE_Timer;
    NRF_TIMER1->BITMODE = TIMER_BITMODE_BITMODE_32Bit;
    NRF_TIMER1->PRESCALER = 0; /* 16 MHz */
    NRF_TIMER1->TASKS_START = 1;

    snprintf(line, sizeof line, "EmbSIM nRF52840 NVMC: %lu pages of %lu bytes, UICR CUSTOMER[0] 0x%08lx\r\n",
             (unsigned long)nrfx_nvmc_flash_page_count_get(), (unsigned long)nrfx_nvmc_flash_page_size_get(),
             (unsigned long)NRF_UICR->CUSTOMER[0]);
    say(line);
    if (NRF_UICR->CUSTOMER[0] == 0x00C0FFEEu) {
        snprintf(line, sizeof line, "second boot: CUSTOMER[0] kept, page A %08lx %08lx %02x\r\n",
                 (unsigned long)*(volatile uint32_t *)PAGE_A, (unsigned long)*(volatile uint32_t *)(PAGE_A + 8),
                 *(volatile uint8_t *)(PAGE_A + 12));
        say(line);
        say("done\r\n");
        for (;;) {
        }
    }

    t0 = now();
    rc = nrfx_nvmc_page_erase(PAGE_A);
    snprintf(line, sizeof line, "page erase %d in %lu us, first word %08lx\r\n", rc, us_since(t0),
             (unsigned long)*(volatile uint32_t *)PAGE_A);
    say(line);
    t0 = now();
    nrfx_nvmc_words_write(PAGE_A, words, 3);
    snprintf(line, sizeof line, "3 words written in %lu us\r\n", us_since(t0));
    say(line);
    t0 = now();
    nrfx_nvmc_byte_write(PAGE_A + 12, 0x5Au);
    snprintf(line, sizeof line, "a byte written in %lu us\r\n", us_since(t0));
    say(line);
    nrfx_nvmc_word_write(PAGE_A + 4, 0x0F0F0F0Fu); /* over a written word: bits only go to 0 */
    while (!nrfx_nvmc_write_done_check())
        ;
    snprintf(line, sizeof line, "read back %08lx %08lx %08lx %02x\r\n", (unsigned long)*(volatile uint32_t *)PAGE_A,
             (unsigned long)*(volatile uint32_t *)(PAGE_A + 4), (unsigned long)*(volatile uint32_t *)(PAGE_A + 8),
             *(volatile uint8_t *)(PAGE_A + 12));
    say(line);

    nrfx_nvmc_word_write(PAGE_B, 0u);
    while (!nrfx_nvmc_write_done_check())
        ;
    rc = nrfx_nvmc_page_partial_erase_init(PAGE_B, 10);
    t0 = now();
    while (!nrfx_nvmc_page_partial_erase_continue())
        parts++;
    parts++;
    snprintf(line, sizeof line, "partial erase %d: %d parts in %lu us, first word %08lx\r\n", rc, parts,
             us_since(t0), (unsigned long)*(volatile uint32_t *)PAGE_B);
    say(line);
    snprintf(line, sizeof line, "an address within a page: %d\r\n", nrfx_nvmc_page_erase(PAGE_A + 4));
    say(line);

    nrfx_nvmc_word_write((uint32_t)&NRF_UICR->CUSTOMER[0], 0x00C0FFEEu);
    while (!nrfx_nvmc_write_done_check())
        ;
    snprintf(line, sizeof line, "UICR CUSTOMER[0] 0x%08lx; reset\r\n", (unsigned long)NRF_UICR->CUSTOMER[0]);
    say(line);
    NVIC_SystemReset();
}
