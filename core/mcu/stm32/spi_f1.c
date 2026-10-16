/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* spi_f1.c -- the STM32F1's SPI as a master (RM0008 25), over the register
 * file, on its pins.
 *
 *   - with CR1.SPE and MSTR, SCK idles at CPOL and MOSI holds its last
 *     bit; NSS is driven low with CR2.SSOE (else it is the program's GPIO,
 *     as ST's HAL leaves it with software NSS);
 *   - a write of DR fills the transmit buffer (TXE clears); with the shift
 *     register idle the buffer moves into it at once (TXE sets, BSY sets)
 *     and the frame starts: 8 or 16 bits (DFF), MSB or LSB first
 *     (LSBFIRST), at PCLK divided by 2 to 256 (BR), in the mode CPOL and
 *     CPHA give: with CPHA 0 MOSI is valid before the leading edge and
 *     MISO is sampled on it, with CPHA 1 MOSI changes on the leading edge
 *     and MISO is sampled on the trailing one;
 *   - at the frame's end the received bits are DR's (RXNE sets; OVR if
 *     RXNE was still set) and a waiting buffer starts the next frame at
 *     once, else BSY clears; reading DR clears RXNE;
 *   - CR2's TXEIE and RXNEIE make SPIx's interrupt pending as TXE and RXNE
 *     set;
 *   - the pins are AFIO's: SPI1 NSS PA4, SCK PA5, MISO PA6, MOSI PA7
 *     (remapped PA15, PB3, PB4, PB5), SPI2 PB12 to PB15; MISO is the
 *     circuit's level on its pin;
 *   - an edge is a device event, at most one in an instruction: a clock
 *     faster than the instructions that wait on it is stretched to them.
 *
 * Not modelled: slave mode, the bidirectional and receive-only modes, CRC,
 * MODF from the NSS input, I2S, DMA requests. */
#include <string.h>

#include "mcu/stm32/stm32.h"

#define CR1_CPHA (1u << 0)
#define CR1_CPOL (1u << 1)
#define CR1_MSTR (1u << 2)
#define CR1_SPE (1u << 6)
#define CR1_LSBFIRST (1u << 7)
#define CR1_DFF (1u << 11)
#define CR2_SSOE (1u << 2)
#define CR2_RXNEIE (1u << 6)
#define CR2_TXEIE (1u << 7)
#define SR_RXNE (1u << 0)
#define SR_TXE (1u << 1)
#define SR_OVR (1u << 6)
#define SR_BSY (1u << 7)

enum { P_NSS, P_SCK, P_MISO, P_MOSI };

struct f1spi {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *cr1, *cr2, *sr, *dr;
    int unit, irq, apb, running;
    u32 tx_buf;
    int tx_full;
    u32 out, in;     /* the frame's shift registers */
    int bits, bit;   /* the frame's length and the bit under way */
    int leading;     /* the next edge is the bit's leading edge */
    int busy;
    u64 left, since; /* core cycles to the next edge */
    u32 rx;          /* the receive buffer */
    int sck, mosi;   /* the levels driven */
};

static u32 reg(const struct esk_rf_reg *r) { return (u32)r->value; }

static int on(const struct f1spi *k)
{
    return (reg(k->cr1) & (CR1_SPE | CR1_MSTR)) == (CR1_SPE | CR1_MSTR);
}

/* pin `which` of the unit, as AFIO's MAPR places it: port * 16 + bit */
static int pin_of(struct esk_mach *s, int unit, int which)
{
    static const int spi1[2][4] = { { 4, 5, 6, 7 }, { 15, 16 + 3, 16 + 4, 16 + 5 } };
    if (unit == 1)
        return spi1[esk_stm32f1_mapr(s) & 1u][which];
    if (unit == 2)
        return 16 + 12 + which;
    return -1;
}

static const struct f1spi *spi_unit(struct esk_mach *s, int unit)
{
    char name[8] = "SPIx";
    struct esk_rf_periph *p;
    name[3] = (char)('0' + unit);
    p = s->rf ? esk_rf_periph(s->rf, name) : 0;
    return p && p->ops == &esk_stm32f1_spi_ops ? (const struct f1spi *)p->ctx : 0;
}

int esk_stm32f1_spi_pin(struct esk_mach *s, int port, int bit)
{
    int unit;
    for (unit = 1; unit <= 2; unit++) {
        const struct f1spi *k = spi_unit(s, unit);
        int at = port * 16 + bit;
        if (!k || !on(k))
            continue;
        if (at == pin_of(s, unit, P_SCK))
            return k->sck;
        if (at == pin_of(s, unit, P_MOSI))
            return k->mosi;
        if (at == pin_of(s, unit, P_NSS) && (reg(k->cr2) & CR2_SSOE))
            return 0;
    }
    return -1;
}

static void pins(struct f1spi *k)
{
    esk_stm32f1_port_changed(k->s, pin_of(k->s, k->unit, P_SCK) / 16);
    if (pin_of(k->s, k->unit, P_MOSI) / 16 != pin_of(k->s, k->unit, P_SCK) / 16)
        esk_stm32f1_port_changed(k->s, pin_of(k->s, k->unit, P_MOSI) / 16);
}

static int miso(struct f1spi *k)
{
    struct esk_mach *s = k->s;
    int at = pin_of(s, k->unit, P_MISO), level = 0;
    if (s->host.pin_in && s->host.pin_in(s->host.ctx, at / 16, at % 16, &level))
        return level != 0;
    return 0;
}

/* core cycles per half bit */
static u32 half(const struct f1spi *k)
{
    u32 hclk = esk_stm32f1_hclk(k->s), pclk = esk_stm32f1_pclk(k->s, k->apb);
    u32 ratio = hclk && pclk ? hclk / pclk : 1;
    u32 div = 2u << (reg(k->cr1) >> 3 & 7u);
    return (ratio ? ratio : 1) * div / 2u;
}

/* the frame's bit i, as it goes out */
static int out_bit(const struct f1spi *k, int i)
{
    int pos = (reg(k->cr1) & CR1_LSBFIRST) ? i : k->bits - 1 - i;
    return (int)(k->out >> pos & 1u);
}

static void in_bit(struct f1spi *k, int i, int v)
{
    int pos = (reg(k->cr1) & CR1_LSBFIRST) ? i : k->bits - 1 - i;
    if (v)
        k->in |= 1u << pos;
}

static void raise(struct f1spi *k, u32 flag)
{
    esk_rf_hw(k->sr, k->sr->value | flag);
    if ((flag == SR_TXE && (reg(k->cr2) & CR2_TXEIE)) || (flag == SR_RXNE && (reg(k->cr2) & CR2_RXNEIE)))
        esk_rf_interrupt(k->p, k->irq);
}

static void wait(struct f1spi *k)
{
    k->left = half(k);
    k->since = k->s->cycles;
    esk_rf_clock(k->p, &k->running, 1);
}

static void start(struct f1spi *k)
{
    k->out = k->tx_buf;
    k->tx_full = 0;
    k->in = 0;
    k->bits = (reg(k->cr1) & CR1_DFF) ? 16 : 8;
    k->bit = 0;
    k->leading = 1;
    k->busy = 1;
    esk_rf_hw(k->sr, k->sr->value | SR_BSY);
    raise(k, SR_TXE);
    if (!(reg(k->cr1) & CR1_CPHA)) {
        k->mosi = out_bit(k, 0);
        pins(k);
    }
    wait(k);
}

static void frame_end(struct f1spi *k)
{
    if (k->sr->value & SR_RXNE)
        esk_rf_hw(k->sr, k->sr->value | SR_OVR);
    else
        k->rx = k->in;
    esk_rf_hw(k->dr, k->rx);
    raise(k, SR_RXNE);
    if (k->tx_full && on(k)) {
        start(k);
        return;
    }
    k->busy = 0;
    esk_rf_hw(k->sr, k->sr->value & ~(u64)SR_BSY);
    esk_rf_clock(k->p, &k->running, 0);
}

static void edge(struct f1spi *k)
{
    int cpha = (reg(k->cr1) & CR1_CPHA) != 0, cpol = (reg(k->cr1) & CR1_CPOL) != 0;
    if (k->leading) {
        k->sck = !cpol;
        if (cpha)
            k->mosi = out_bit(k, k->bit);
        pins(k);
        if (!cpha)
            in_bit(k, k->bit, miso(k));
        k->leading = 0;
        wait(k);
        return;
    }
    if (cpha)
        in_bit(k, k->bit, miso(k));
    k->sck = cpol;
    k->bit++;
    if (!cpha && k->bit < k->bits)
        k->mosi = out_bit(k, k->bit);
    pins(k);
    k->leading = 1;
    if (k->bit < k->bits)
        wait(k);
    else
        frame_end(k);
}

static void spi_tick(void *ctx, u32 cycles)
{
    struct f1spi *k = (struct f1spi *)ctx;
    if (!k->running)
        return;
    cycles = esk_mach_after(k->s, cycles, k->since);
    k->since = 0;
    if (k->left > cycles) {
        k->left -= cycles;
        return;
    }
    {
        /* one edge in an instruction; what the instruction ran past the
         * edge counts toward the next, up to a whole half bit */
        u64 over = cycles - k->left;
        k->left = 0;
        edge(k);
        if (k->running && k->left) {
            u64 cut = over < k->left ? over : k->left - 1;
            k->left -= cut;
            k->since = 0;
        }
    }
}

static int spi_next(void *ctx, u32 *cycles)
{
    struct f1spi *k = (struct f1spi *)ctx;
    if (!k->running)
        return 0;
    *cycles = k->left > 0xffffffffu ? 0xffffffffu : (u32)(k->left ? k->left : 1);
    return 1;
}

static u32 spi_read(void *ctx, u32 off, int n)
{
    struct f1spi *k = (struct f1spi *)ctx;
    u32 v = esk_rf_read(k->p, off, n);
    if (off == esk_rf_off(k->dr) && !k->s->bus.debug)
        esk_rf_hw(k->sr, k->sr->value & ~(u64)SR_RXNE);
    return v;
}

static void spi_write(void *ctx, u32 off, int n, u32 v)
{
    struct f1spi *k = (struct f1spi *)ctx;
    int was = on(k);
    if (off == esk_rf_off(k->dr)) {
        k->tx_buf = v & ((reg(k->cr1) & CR1_DFF) ? 0xffffu : 0xffu);
        k->tx_full = 1;
        esk_rf_hw(k->sr, k->sr->value & ~(u64)SR_TXE);
        if (on(k) && !k->busy)
            start(k);
        return;
    }
    esk_rf_write(k->p, off, n, v);
    if (off == esk_rf_off(k->cr1) || off == esk_rf_off(k->cr2)) {
        if (!on(k)) {
            k->busy = 0;
            esk_rf_hw(k->sr, k->sr->value & ~(u64)SR_BSY);
            esk_rf_clock(k->p, &k->running, 0);
        } else if (!k->busy) {
            k->sck = (reg(k->cr1) & CR1_CPOL) != 0;
            if (k->tx_full)
                start(k);
        }
        if (was != on(k) || !k->busy)
            pins(k);
        esk_stm32f1_port_changed(k->s, pin_of(k->s, k->unit, P_NSS) / 16);
    }
}

static void spi_reset(void *ctx)
{
    struct f1spi *k = (struct f1spi *)ctx;
    k->running = 0;
    k->busy = k->tx_full = 0;
    k->rx = 0;
    k->sck = 0;
    k->mosi = 0;
    k->left = 0;
    esk_rf_hw(k->sr, k->sr->value | SR_TXE);
}

const struct esk_dev_ops esk_stm32f1_spi_ops = {
    "stm32f1-spi", spi_read, spi_write, spi_reset, spi_tick, spi_next,
};

void *esk_stm32f1_spi_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct f1spi k, *c;
    memset(&k, 0, sizeof k);
    k.s = s;
    k.p = p;
    k.cr1 = esk_rf_reg(p, "CR1");
    k.cr2 = esk_rf_reg(p, "CR2");
    k.sr = esk_rf_reg(p, "SR");
    k.dr = esk_rf_reg(p, "DR");
    if (!k.cr1 || !k.cr2 || !k.sr || !k.dr || strncmp(p->name, "SPI", 3) || p->name[3] < '1' || p->name[3] > '2')
        return 0;
    k.unit = p->name[3] - '0';
    k.apb = k.unit == 1 ? 2 : 1;
    k.irq = esk_rf_irq(p, "SPI");
    esk_rf_set_reset(k.sr, k.sr->reset | SR_TXE);
    c = (struct f1spi *)esk_mach_alloc(s, sizeof *c);
    if (!c)
        return 0;
    *c = k;
    return c;
}
