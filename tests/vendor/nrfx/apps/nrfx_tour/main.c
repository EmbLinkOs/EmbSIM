/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* main.c -- Nordic's nrfx drivers on the nRF52840 DK, as an nrfx
 * application uses them: the HFXO and the LFXO started through
 * nrfx_clock, UARTE0 on the board's VCOM pins (P0.06 TX, P0.08 RX) at
 * 115200 baud, a line out at each report and a character in through the
 * driver's RX buffers, TIMER1 at 1 MHz, 32 bits wide, with a compare
 * every 100 ms, RTC1 on the LFCLK with a compare at one second, LED1
 * (P0.13, active low) toggled by the timer, and a button on D2 (P1.03,
 * pulled up) through a GPIOTE channel's falling edge. */
#include <stdio.h>
#include <string.h>

#include <nrfx_clock.h>
#include <nrfx_gpiote.h>
#include <nrfx_rtc.h>
#include <nrfx_timer.h>
#include <nrfx_uarte.h>

#define LED1 NRF_GPIO_PIN_MAP(0, 13)
#define BUTTON NRF_GPIO_PIN_MAP(1, 3)
#define TX_PIN NRF_GPIO_PIN_MAP(0, 6)
#define RX_PIN NRF_GPIO_PIN_MAP(0, 8)

static nrfx_uarte_t uarte = NRFX_UARTE_INSTANCE(NRF_UARTE0);
static nrfx_timer_t timer = NRFX_TIMER_INSTANCE(NRF_TIMER1);
static nrfx_rtc_t rtc = NRFX_RTC_INSTANCE(NRF_RTC1);
static nrfx_gpiote_t gpiote = NRFX_GPIOTE_INSTANCE(NRF_GPIOTE);

static volatile unsigned ticks, presses, seconds;
static volatile int got = -1, clock_events;
static uint8_t rx_buf[2][1];
static int rx_next;

static void clock_handler(nrfx_clock_evt_type_t event)
{
    (void)event;
    clock_events++;
}

static void uarte_handler(nrfx_uarte_event_t const *e, void *ctx)
{
    (void)ctx;
    if (e->type == NRFX_UARTE_EVT_RX_BUF_REQUEST) {
        nrfx_uarte_rx_buffer_set(&uarte, rx_buf[rx_next], 1);
        rx_next ^= 1;
    } else if (e->type == NRFX_UARTE_EVT_RX_DONE && e->data.rx.length) {
        got = e->data.rx.p_buffer[0];
    }
}

static void timer_handler(nrf_timer_event_t event, void *ctx)
{
    (void)ctx;
    if (event == NRF_TIMER_EVENT_COMPARE0) {
        ticks++;
        nrf_gpio_pin_toggle(LED1);
    }
}

static void rtc_handler(nrf_rtc_event_t event, void *ctx)
{
    (void)ctx;
    if (event == NRF_RTC_EVENT_COMPARE_0)
        seconds++;
}

static void button_handler(nrfx_gpiote_pin_t pin, nrfx_gpiote_trigger_t trigger, void *ctx)
{
    (void)trigger;
    (void)ctx;
    if (pin == BUTTON)
        presses++;
}

static void say(const char *s)
{
    nrfx_uarte_tx(&uarte, (const uint8_t *)s, strlen(s), NRFX_UARTE_TX_BLOCKING);
}

int main(void)
{
    char line[96];
    uint8_t ch;
    nrf_clock_hfclk_t hf;
    nrf_clock_lfclk_t lf;
    nrfx_uarte_config_t ucfg = NRFX_UARTE_DEFAULT_CONFIG(TX_PIN, RX_PIN);
    nrfx_timer_config_t tcfg = NRFX_TIMER_DEFAULT_CONFIG(NRFX_MHZ_TO_HZ(1));
    nrfx_rtc_config_t rcfg = NRFX_RTC_DEFAULT_CONFIG;
    static const nrf_gpio_pin_pull_t pull = NRF_GPIO_PIN_PULLUP;
    nrfx_gpiote_trigger_config_t trig = { .trigger = NRFX_GPIOTE_TRIGGER_HITOLO, .p_in_channel = &ch };
    static const nrfx_gpiote_handler_config_t hcfg = { .handler = button_handler };
    nrfx_gpiote_input_pin_config_t icfg = { .p_pull_config = &pull, .p_trigger_config = &trig,
                                            .p_handler_config = &hcfg };

    nrfx_clock_init(clock_handler);
    nrfx_clock_enable();
    nrfx_clock_start(NRF_CLOCK_DOMAIN_HFCLK);
    nrfx_clock_start(NRF_CLOCK_DOMAIN_LFCLK);
    while (!nrfx_clock_is_running(NRF_CLOCK_DOMAIN_HFCLK, &hf) || !nrfx_clock_is_running(NRF_CLOCK_DOMAIN_LFCLK, &lf))
        ;

    nrfx_uarte_init(&uarte, &ucfg, uarte_handler);
    nrfx_uarte_rx_enable(&uarte, NRFX_UARTE_RX_ENABLE_CONT);

    nrf_gpio_cfg_output(LED1);
    nrf_gpio_pin_set(LED1);
    nrfx_gpiote_init(&gpiote, NRFX_GPIOTE_DEFAULT_CONFIG_IRQ_PRIORITY);
    nrfx_gpiote_channel_alloc(&gpiote, &ch);
    nrfx_gpiote_input_configure(&gpiote, BUTTON, &icfg);
    nrfx_gpiote_trigger_enable(&gpiote, BUTTON, true);

    tcfg.bit_width = NRF_TIMER_BIT_WIDTH_32; /* the default's 16 bits would compare at 34464 */
    nrfx_timer_init(&timer, &tcfg, timer_handler);
    nrfx_timer_extended_compare(&timer, NRF_TIMER_CC_CHANNEL0, nrfx_timer_us_to_ticks(&timer, 100000),
                                NRF_TIMER_SHORT_COMPARE0_CLEAR_MASK, true);
    nrfx_timer_enable(&timer);

    nrfx_rtc_init(&rtc, &rcfg, rtc_handler);
    nrfx_rtc_cc_set(&rtc, 0, 32768, true);
    nrfx_rtc_enable(&rtc);

    snprintf(line, sizeof line, "EmbSIM nrfx tour, HFXO and LFXO running (%d clock events)\r\n", clock_events);
    say(line);
    for (;;) {
        unsigned t = ticks;
        while (ticks == t)
            __WFE();
        if (ticks % 5 == 0) {
            snprintf(line, sizeof line, "ticks=%u rtc=%lu seconds=%u presses=%u\r\n", ticks,
                     (unsigned long)nrfx_rtc_counter_get(&rtc), seconds, presses);
            say(line);
        }
        if (got >= 0) {
            snprintf(line, sizeof line, "echo %c\r\n", got);
            got = -1;
            say(line);
        }
    }
}

/* the vectors (nrfx_irqs_nrf52840.h's names) */
void POWER_CLOCK_IRQHandler(void) { nrfx_clock_irq_handler(); }
void UARTE0_UART0_IRQHandler(void) { nrfx_uarte_irq_handler(&uarte); }
void TIMER1_IRQHandler(void) { nrfx_timer_irq_handler(&timer); }
void RTC1_IRQHandler(void) { nrfx_rtc_irq_handler(&rtc); }
void GPIOTE_IRQHandler(void) { nrfx_gpiote_irq_handler(&gpiote); }
