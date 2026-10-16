/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* main.c -- Nordic's nrfx drivers for the nRF52840's serial and analog
 * peripherals on the DK, against parts from the catalog:
 *   - nrfx_spim on SPIM2 (D13 SCK, D11 MOSI, D12 MISO, the driver's CS on
 *     D10; mode 0 at 4 MHz) to a 25LC040: WREN, WRITE, RDSR polled through
 *     its write cycle, READ;
 *   - nrfx_twim on TWIM0 (SCL P0.27, SDA P0.26, 400 kHz, 4.7 kOhm pull-ups)
 *     to a 24C02 at 0x50: a page write, address-only writes NACKed through
 *     its write cycle, a write then read (TXRX, a repeated START), a write
 *     without STOP then a read (the bus suspended between them), an absent
 *     address;
 *   - nrfx_pwm on PWM0 (OUT0 D3 at 25 %, OUT1 D5 at 50 %, 1 kHz) looping
 *     one sequence of individual values;
 *   - nrfx_saadc, offset calibrated, in simple mode at 12 bits (gain 1/6,
 *     the internal 0.6 V reference): A0 (AIN1) the PWM through an RC
 *     low-pass, A1 (AIN2) a 1.0 V source, and VDD.
 * The results go out on UARTE0 (the VCOM pins) at 115200 baud. */
#include <stdio.h>
#include <string.h>

#include <nrfx_clock.h>
#include <nrfx_pwm.h>
#include <nrfx_saadc.h>
#include <nrfx_spim.h>
#include <nrfx_twim.h>
#include <nrfx_uarte.h>

#define TX_PIN NRF_GPIO_PIN_MAP(0, 6)
#define RX_PIN NRF_GPIO_PIN_MAP(0, 8)
#define D3 NRF_GPIO_PIN_MAP(1, 4)
#define D5 NRF_GPIO_PIN_MAP(1, 6)
#define D10 NRF_GPIO_PIN_MAP(1, 12)
#define D11 NRF_GPIO_PIN_MAP(1, 13)
#define D12 NRF_GPIO_PIN_MAP(1, 14)
#define D13 NRF_GPIO_PIN_MAP(1, 15)
#define SDA_PIN NRF_GPIO_PIN_MAP(0, 26)
#define SCL_PIN NRF_GPIO_PIN_MAP(0, 27)

static nrfx_uarte_t uarte = NRFX_UARTE_INSTANCE(NRF_UARTE0);
static nrfx_spim_t spim = NRFX_SPIM_INSTANCE(NRF_SPIM2);
static nrfx_twim_t twim = NRFX_TWIM_INSTANCE(NRF_TWIM0);
static nrfx_pwm_t pwm = NRFX_PWM_INSTANCE(NRF_PWM0);

/* in Data RAM, for EasyDMA */
static uint8_t txb[16], rxb[16];
static nrf_pwm_values_individual_t duty[1] = { { 0x8000 | 250, 0x8000 | 500, 0, 0 } };
static nrf_saadc_value_t samples[3];

/* EasyDMA reads Data RAM alone: the line goes out from a copy there */
static void say(const char *s)
{
    static char ram[128];
    size_t n = strlen(s);
    if (n > sizeof ram)
        n = sizeof ram;
    memcpy(ram, s, n);
    nrfx_uarte_tx(&uarte, (const uint8_t *)ram, n, NRFX_UARTE_TX_BLOCKING);
}

static void spi(const uint8_t *out, size_t n)
{
    nrfx_spim_xfer_desc_t x = NRFX_SPIM_XFER_TRX(txb, n, rxb, n);
    memcpy(txb, out, n);
    nrfx_spim_xfer(&spim, &x, 0);
}

static void spi_eeprom(void)
{
    static const uint8_t wren[1] = { 0x06 }, rdsr[2] = { 0x05, 0 };
    static const uint8_t wr[2 + 6] = { 0x02, 0x50, 'n', 'R', 'F', '5', '2', '!' };
    static const uint8_t rd[2 + 6] = { 0x03, 0x50 };
    char line[96];
    unsigned polls = 0, wel;
    nrfx_spim_config_t cfg = NRFX_SPIM_DEFAULT_CONFIG(D13, D11, D12, D10);
    nrfx_spim_init(&spim, &cfg, NULL, NULL);
    spi(wren, 1);
    spi(rdsr, 2);
    wel = rxb[1];
    spi(wr, sizeof wr);
    do {
        spi(rdsr, 2);
        polls++;
    } while (rxb[1] & 1u);
    spi(rd, sizeof rd);
    snprintf(line, sizeof line, "spim: status %02x after WREN, write cycle over after %u polls, read %.6s\r\n", wel,
             polls, (const char *)rxb + 2);
    say(line);
}

static int i2c_tx(uint8_t addr, const uint8_t *p, size_t n, uint32_t flags)
{
    nrfx_twim_xfer_desc_t x = NRFX_TWIM_XFER_DESC_TX(addr, txb, n);
    memcpy(txb, p, n);
    return nrfx_twim_xfer(&twim, &x, flags);
}

static void i2c_eeprom(void)
{
    static const uint8_t page[1 + 5] = { 0x60, 'T', 'W', 'I', 'M', '!' };
    static const uint8_t at60[1] = { 0x60 }, at61[1] = { 0x61 };
    char line[96];
    unsigned busy = 0;
    int err;
    nrfx_twim_config_t cfg = NRFX_TWIM_DEFAULT_CONFIG(SCL_PIN, SDA_PIN);
    nrfx_twim_xfer_desc_t trx = NRFX_TWIM_XFER_DESC_TXRX(0x50, txb, 1, rxb, 5);
    nrfx_twim_xfer_desc_t rx = NRFX_TWIM_XFER_DESC_RX(0x50, rxb, 3);
    nrfx_twim_xfer_desc_t absent = NRFX_TWIM_XFER_DESC_RX(0x51, rxb, 1);
    cfg.frequency = NRF_TWIM_FREQ_400K;
    nrfx_twim_init(&twim, &cfg, NULL, NULL);
    nrfx_twim_enable(&twim);
    err = i2c_tx(0x50, page, sizeof page, 0);
    snprintf(line, sizeof line, "twim: page write %d\r\n", err);
    say(line);
    while (i2c_tx(0x50, at60, 1, 0) != 0)
        busy++;
    snprintf(line, sizeof line, "twim: ready again after %u NACKs\r\n", busy);
    say(line);
    memcpy(txb, at60, 1);
    memset(rxb, 0, sizeof rxb);
    err = nrfx_twim_xfer(&twim, &trx, 0);
    snprintf(line, sizeof line, "twim: txrx %d %.5s\r\n", err, (const char *)rxb);
    say(line);
    memset(rxb, 0, sizeof rxb);
    err = i2c_tx(0x50, at61, 1, NRFX_TWIM_FLAG_TX_NO_STOP);
    err |= nrfx_twim_xfer(&twim, &rx, 0);
    snprintf(line, sizeof line, "twim: tx no stop, rx %d %.3s\r\n", err, (const char *)rxb);
    say(line);
    err = nrfx_twim_xfer(&twim, &absent, 0);
    snprintf(line, sizeof line, "twim: 0x51 %d\r\n", err);
    say(line);
}

static void pwm_start(void)
{
    nrfx_pwm_config_t cfg = NRFX_PWM_DEFAULT_CONFIG(D3, D5, NRF_PWM_PIN_NOT_CONNECTED, NRF_PWM_PIN_NOT_CONNECTED);
    nrf_pwm_sequence_t seq = { .values.p_individual = duty, .length = NRF_PWM_VALUES_LENGTH(duty),
                               .repeats = 0, .end_delay = 0 };
    cfg.load_mode = NRF_PWM_LOAD_INDIVIDUAL;
    nrfx_pwm_init(&pwm, &cfg, NULL, NULL);
    nrfx_pwm_simple_playback(&pwm, &seq, 1, NRFX_PWM_FLAG_LOOP);
    say("pwm: 1 kHz, 25 % and 50 %\r\n");
}

/* millivolts from a 12-bit code, gain 1/6 of 0.6 V */
static unsigned mv(int code) { return code <= 0 ? 0u : (unsigned)((code * 3600 + 2048) / 4096); }

static void adc_read(const char *when)
{
    char line[96];
    nrfx_saadc_buffer_set(samples, 3);
    nrfx_saadc_mode_trigger();
    snprintf(line, sizeof line, "saadc %s: rc=%d (%u mV) vin=%d vdd=%d\r\n", when, samples[0], mv(samples[0]),
             samples[1], samples[2]);
    say(line);
}

static void delay_ms(unsigned ms)
{
    while (ms--)
        NRFX_DELAY_US(1000);
}

int main(void)
{
    nrf_clock_hfclk_t hf;
    nrfx_uarte_config_t ucfg = NRFX_UARTE_DEFAULT_CONFIG(TX_PIN, RX_PIN);
    nrfx_saadc_channel_t ch[3] = {
        NRFX_SAADC_DEFAULT_CHANNEL_SE(NRFX_ANALOG_EXTERNAL_AIN1, 0),
        NRFX_SAADC_DEFAULT_CHANNEL_SE(NRFX_ANALOG_EXTERNAL_AIN2, 1),
        NRFX_SAADC_DEFAULT_CHANNEL_SE(NRFX_ANALOG_INTERNAL_VDD, 2),
    };
    int i;

    nrfx_clock_init(NULL);
    nrfx_clock_enable();
    nrfx_clock_start(NRF_CLOCK_DOMAIN_HFCLK);
    while (!nrfx_clock_is_running(NRF_CLOCK_DOMAIN_HFCLK, &hf))
        ;
    nrfx_uarte_init(&uarte, &ucfg, NULL);
    say("EmbSIM nrfx peripherals\r\n");

    spi_eeprom();
    i2c_eeprom();
    pwm_start();

    for (i = 0; i < 3; i++)
        ch[i].channel_config.gain = NRF_SAADC_GAIN1_6;
    nrfx_saadc_init(NRFX_SAADC_DEFAULT_CONFIG_IRQ_PRIORITY);
    nrfx_saadc_offset_calibrate(NULL);
    nrfx_saadc_channels_config(ch, 3);
    nrfx_saadc_simple_mode_set(7u, NRF_SAADC_RESOLUTION_12BIT, NRF_SAADC_OVERSAMPLE_DISABLED, NULL);
    adc_read("at once");
    delay_ms(400);
    adc_read("after 400 ms");
    say("done\r\n");
    for (;;)
        __WFE();
}

/* the vectors (nrfx_irqs_nrf52840.h's names) */
void POWER_CLOCK_IRQHandler(void) { nrfx_clock_irq_handler(); }
void UARTE0_UART0_IRQHandler(void) { nrfx_uarte_irq_handler(&uarte); }
