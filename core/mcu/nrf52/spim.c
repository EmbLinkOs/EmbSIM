/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* spim.c -- the nRF52840's SPIM0-3 and TWIM0-1 (Product Specification
 * 6.25, 6.31): one model for each of the shared spaces, SPIM while ENABLE
 * is 7, TWIM while it is 6 (SPIM0 and SPIM1 only), each with EasyDMA from
 * Data RAM, on its PSEL pins.
 *
 * SPIM:
 *   - START (STARTED) clocks max(TXD.MAXCNT, RXD.MAXCNT) bytes: those
 *     beyond TXD.MAXCNT send ORC, those beyond RXD.MAXCNT are not stored;
 *     ENDTX after the last byte of TXD's buffer, ENDRX after RXD's is full,
 *     END after both, with TXD.AMOUNT and RXD.AMOUNT; SHORTS' END_START
 *     starts again; STOP ends it after the byte under way (STOPPED);
 *   - CONFIG's CPOL, CPHA and ORDER (MSB or LSB first); FREQUENCY's rates
 *     from 125 kbps to 8 Mbps (and SPIM3's 16 and 32); SCK idles at CPOL,
 *     MISO is the pin's level; SPIM3's CSN (PSEL.CSN) is active (CSNPOL)
 *     around the transfer;
 * TWIM:
 *   - STARTTX (TXSTARTED) and STARTRX (RXSTARTED) make a START, or a
 *     repeated START when the bus is held, and ADDRESS with the direction;
 *     then TXD.MAXCNT bytes are written, or RXD.MAXCNT read, each ACKed but
 *     the last one read before a STOP or a START; LASTTX and LASTRX come as
 *     the last byte starts, and SHORTS (LASTTX_STARTRX, LASTTX_SUSPEND,
 *     LASTTX_STOP, LASTRX_STARTTX, LASTRX_SUSPEND, LASTRX_STOP) act after
 *     it; SUSPEND holds the bus after the byte under way (SUSPENDED),
 *     RESUME goes on; STOP makes the STOP after it, or at once when the bus
 *     is held or free (STOPPED);
 *   - a NACK of the address sets ERRORSRC.ANACK, of a byte DNACK, with
 *     ERROR; the bus is then held until STOP (or makes the STOP a short
 *     already asked for); ERRORSRC's bits clear when written 1;
 *   - SCL at FREQUENCY's 100, 250 or 400 kbps (390 kbps for 0x06200000),
 *     half a period low and half high, open-drain on PSEL.SCL and PSEL.SDA;
 *     SDA changes a quarter of the low time after SCL falls and is read
 *     as SCL falls;
 *   - an edge or a step is a device event, at most one in an instruction.
 *
 * The register file has one SHORTS for the shared space, with SPIM's field
 * alone (the SVD's alternate peripherals merged): the model keeps
 * TWIM's bits too.
 *
 * Not modelled: SPIM's EasyDMA lists, its STALLSTAT and SPIM3's DCX, its
 * IFTIMING delays; TWIM's EasyDMA lists, clock stretching by the slave,
 * arbitration; the legacy SPI and TWI (ENABLE 1, 5) and the slaves (SPIS,
 * TWIS). */
#include <string.h>

#include "mcu/nrf52/nrf52.h"

#define EN_TWIM 6u
#define EN_SPIM 7u

/* the TWIM's steps */
enum { T_IDLE, T_START, T_BYTE, T_HOLD, T_STOP };

struct nspim {
    struct nrfp x;
    int unit;
    int running;
    u64 left, since;
    u32 last_role;
    /* SPIM */
    int busy;
    u32 tx_ptr, tx_max, rx_ptr, rx_max, n, total;
    u32 out, in;
    int bit, leading, stop;
    int sck, mosi, csn;
    /* TWIM */
    int mode, step;
    int scl, sda;     /* 0: pulled low, 1: released */
    int reading, addr;
    u32 shift;
    int tbit, acked, ack;
    u32 tn;           /* bytes of the buffer done */
    int stop_req, suspend_req, start_req; /* start_req: 1 TX, 2 RX */
    int error;        /* waiting for STOP after a NACK */
};

static u32 rd(const struct nspim *k, u32 off) { return esk_nrfp_rd(&k->x, off); }

static u32 enabled(const struct nspim *k) { return rd(k, 0x500) & 15u; }

static int spim_on(const struct nspim *k) { return enabled(k) == EN_SPIM; }

static int twim_on(const struct nspim *k) { return enabled(k) == EN_TWIM && k->unit <= 1; }

static struct nspim *unit_of(struct esk_mach *s, int unit)
{
    char name[8] = "SPIMx";
    struct esk_rf_periph *p;
    name[4] = (char)('0' + unit);
    p = s->rf ? esk_rf_periph(s->rf, name) : 0;
    return p && p->ops == &esk_nrf_spim_ops ? (struct nspim *)p->ctx : 0;
}

static int pin(const struct nspim *k, u32 off)
{
    int n = esk_nrf_psel(rd(k, off));
    return n >= 0 && n < NRF_PINS ? n : -1;
}

void esk_nrf_spim_roles(struct esk_mach *s, signed char *drive, u8 *od)
{
    int u;
    for (u = 0; u < 4; u++) {
        struct nspim *k = unit_of(s, u);
        int a, b;
        if (!k)
            continue;
        if (spim_on(k)) {
            if ((a = pin(k, 0x508)) >= 0)
                drive[a] = (signed char)k->sck;
            if ((a = pin(k, 0x50c)) >= 0)
                drive[a] = (signed char)k->mosi;
            if ((a = pin(k, 0x510)) >= 0)
                drive[a] = 2; /* MISO, an input */
            if (u == 3 && (a = pin(k, 0x514)) >= 0)
                drive[a] = (signed char)k->csn;
        } else if (twim_on(k)) {
            a = pin(k, 0x508);
            b = pin(k, 0x50c);
            if (a >= 0) {
                drive[a] = k->scl ? 2 : 0;
                od[a] = 1;
            }
            if (b >= 0) {
                drive[b] = k->sda ? 2 : 0;
                od[b] = 1;
            }
        }
    }
}

/* the pins follow when ENABLE or a PSEL changed, or a line did */
static void roles(struct nspim *k, int force)
{
    u32 r = enabled(k) ^ rd(k, 0x508) * 3u ^ rd(k, 0x50c) * 5u ^ rd(k, 0x510) * 7u ^ rd(k, 0x514) * 11u;
    if (force || r != k->last_role) {
        k->last_role = r;
        esk_nrf_pins_changed(k->x.s);
    }
}

static int easydma_ok(struct nspim *k, u32 ptr, u32 n, const char *what)
{
    if (!n || (ptr >= NRF_DATA_RAM && ptr - NRF_DATA_RAM < NRF_DATA_RAM_SIZE &&
               NRF_DATA_RAM_SIZE - (ptr - NRF_DATA_RAM) >= n))
        return 1;
    esk_rf_diag(k->x.s, ES_SEV_WARNING, "mcu.easydma_range",
                "%s's %s 0x%08lx is not in Data RAM: EasyDMA reaches only that (the part may fault or corrupt RAM); nothing is moved",
                k->x.p->name, what, (unsigned long)ptr);
    return 0;
}

static u8 mem_rd(struct nspim *k, u32 a)
{
    u32 v = 0;
    (void)esk_bus_debug_read(&k->x.s->bus, a, 1, &v);
    return (u8)v;
}

static void mem_wr(struct nspim *k, u32 a, u8 v) { (void)esk_bus_debug_write(&k->x.s->bus, a, 1, v); }

static void wait_n(struct nspim *k, u64 n)
{
    k->left = n ? n : 1;
    k->since = k->x.s->cycles;
    esk_rf_clock(k->x.p, &k->running, 1);
}

static void idle_clock(struct nspim *k) { esk_rf_clock(k->x.p, &k->running, 0); }

/* ---- SPIM -------------------------------------------------------------- */

static u32 spim_hz(const struct nspim *k)
{
    switch (rd(k, 0x524)) {
    case 0x02000000u: return 125000u;
    case 0x04000000u: return 250000u;
    case 0x08000000u: return 500000u;
    case 0x10000000u: return 1000000u;
    case 0x20000000u: return 2000000u;
    case 0x40000000u: return 4000000u;
    case 0x80000000u: return 8000000u;
    case 0x0A000000u: return k->unit == 3 ? 16000000u : 0u;
    case 0x14000000u: return k->unit == 3 ? 32000000u : 0u;
    default: return 0u;
    }
}

static u64 spim_half(const struct nspim *k)
{
    u32 hz = spim_hz(k);
    if (!hz)
        return (u64)1 << 62; /* not a rate: nothing is clocked */
    return NRF_CPU_HZ / hz / 2u ? NRF_CPU_HZ / hz / 2u : 1u;
}

static int spim_out_bit(const struct nspim *k, int i)
{
    int pos = (rd(k, 0x554) & 1u) ? i : 7 - i;
    return (int)(k->out >> pos & 1u);
}

static int miso(struct nspim *k)
{
    int p = pin(k, 0x510);
    return p >= 0 ? esk_nrf_pin_level(k->x.s, p) : 0;
}

static void spim_byte(struct nspim *k)
{
    k->out = k->n < k->tx_max ? mem_rd(k, k->tx_ptr + k->n) : (rd(k, 0x5c0) & 0xffu);
    k->in = 0;
    k->bit = 0;
    k->leading = 1;
    if (!(rd(k, 0x554) & 2u)) {
        k->mosi = spim_out_bit(k, 0);
        roles(k, 1);
    }
    wait_n(k, spim_half(k));
}

static void spim_done(struct nspim *k, int stopped)
{
    k->busy = 0;
    k->csn = (rd(k, 0x568) & 1u) ? 0 : 1;
    k->sck = (rd(k, 0x554) & 4u) != 0;
    idle_clock(k);
    roles(k, 1);
    if (stopped)
        esk_nrfp_event(&k->x, 0x104); /* STOPPED */
}

static void spim_start(struct nspim *k)
{
    k->tx_ptr = rd(k, 0x544);
    k->tx_max = rd(k, 0x548) & 0xffffu;
    k->rx_ptr = rd(k, 0x534);
    k->rx_max = rd(k, 0x538) & 0xffffu;
    if (!easydma_ok(k, k->tx_ptr, k->tx_max, "TXD.PTR"))
        k->tx_max = 0;
    if (!easydma_ok(k, k->rx_ptr, k->rx_max, "RXD.PTR"))
        k->rx_max = 0;
    k->n = 0;
    k->total = k->tx_max > k->rx_max ? k->tx_max : k->rx_max;
    k->stop = 0;
    esk_nrfp_hw(&k->x, 0x54c, 0);
    esk_nrfp_hw(&k->x, 0x53c, 0);
    esk_nrfp_event(&k->x, 0x14c); /* STARTED */
    if (!k->total) {
        esk_nrfp_event(&k->x, 0x120); /* ENDTX */
        esk_nrfp_event(&k->x, 0x110); /* ENDRX */
        esk_nrfp_event(&k->x, 0x118); /* END */
        return;
    }
    k->busy = 1;
    k->csn = (rd(k, 0x568) & 1u) ? 1 : 0;
    k->sck = (rd(k, 0x554) & 4u) != 0;
    roles(k, 1);
    spim_byte(k);
}

static void spim_byte_end(struct nspim *k)
{
    if (k->n < k->rx_max)
        mem_wr(k, k->rx_ptr + k->n, (u8)k->in);
    k->n++;
    esk_nrfp_hw(&k->x, 0x54c, k->n < k->tx_max ? k->n : k->tx_max);
    esk_nrfp_hw(&k->x, 0x53c, k->n < k->rx_max ? k->n : k->rx_max);
    if (k->n == k->tx_max)
        esk_nrfp_event(&k->x, 0x120); /* ENDTX */
    if (k->n == k->rx_max)
        esk_nrfp_event(&k->x, 0x110); /* ENDRX */
    if (k->n >= k->total) {
        if (k->tx_max < k->total && !k->tx_max)
            esk_nrfp_event(&k->x, 0x120);
        if (k->rx_max < k->total && !k->rx_max)
            esk_nrfp_event(&k->x, 0x110);
        esk_nrfp_event(&k->x, 0x118); /* END */
        if (k->stop) {
            spim_done(k, 1);
            return;
        }
        spim_done(k, 0);
        if (rd(k, 0x200) & (1u << 17))
            spim_start(k); /* END_START */
        return;
    }
    if (k->stop) {
        spim_done(k, 1);
        return;
    }
    spim_byte(k);
}

static void spim_edge(struct nspim *k)
{
    u32 cfg = rd(k, 0x554);
    int cpha = (cfg & 2u) != 0, cpol = (cfg & 4u) != 0, lsb = (cfg & 1u) != 0;
    int pos = lsb ? k->bit : 7 - k->bit;
    if (k->leading) {
        k->sck = !cpol;
        if (cpha)
            k->mosi = spim_out_bit(k, k->bit);
        roles(k, 1);
        if (!cpha && miso(k))
            k->in |= 1u << pos;
        k->leading = 0;
        wait_n(k, spim_half(k));
        return;
    }
    if (cpha && miso(k))
        k->in |= 1u << pos;
    k->sck = cpol;
    k->bit++;
    if (!cpha && k->bit < 8)
        k->mosi = spim_out_bit(k, k->bit);
    roles(k, 1);
    k->leading = 1;
    if (k->bit < 8)
        wait_n(k, spim_half(k));
    else
        spim_byte_end(k);
}

/* ---- TWIM -------------------------------------------------------------- */

static u64 twim_half(const struct nspim *k)
{
    u32 f = rd(k, 0x524), hz;
    switch (f) {
    case 0x01980000u: hz = 100000u; break;
    case 0x04000000u: hz = 250000u; break;
    case 0x06400000u: hz = 400000u; break;
    case 0x06200000u: hz = 390000u; break;
    default: hz = (u32)((u64)f * 16000000u >> 32); break;
    }
    if (!hz)
        return (u64)1 << 62;
    return NRF_CPU_HZ / hz / 2u ? NRF_CPU_HZ / hz / 2u : 1u;
}

static void lines(struct nspim *k, int scl, int sda)
{
    if (k->scl == scl && k->sda == sda)
        return;
    k->scl = scl;
    k->sda = sda;
    roles(k, 1);
}

static int sda_in(struct nspim *k)
{
    int p = pin(k, 0x50c);
    return p >= 0 ? esk_nrf_pin_level(k->x.s, p) : k->sda;
}

static void begin_start(struct nspim *k, int rx)
{
    k->mode = T_START;
    k->step = 0;
    k->reading = rx;
    k->tn = 0;
    wait_n(k, twim_half(k));
}

static void begin_stop(struct nspim *k)
{
    k->mode = T_STOP;
    k->step = 0;
    k->stop_req = k->suspend_req = k->start_req = 0;
    wait_n(k, twim_half(k) / 4u);
}

static void begin_byte(struct nspim *k, int addr, u32 b)
{
    k->mode = T_BYTE;
    k->addr = addr;
    k->shift = b;
    k->tbit = 0;
    k->step = 0;
    wait_n(k, twim_half(k) / 4u);
}

static void hold(struct nspim *k)
{
    k->mode = T_HOLD;
    idle_clock(k);
}

static u32 buf_max(const struct nspim *k) { return rd(k, k->reading ? 0x538 : 0x548) & 0xffffu; }

static void twim_task_start(struct nspim *k, int rx)
{
    u32 ptr = rd(k, rx ? 0x534 : 0x544), n = rd(k, rx ? 0x538 : 0x548) & 0xffffu;
    if (!easydma_ok(k, ptr, n, rx ? "RXD.PTR" : "TXD.PTR"))
        return;
    esk_nrfp_hw(&k->x, rx ? 0x53c : 0x54c, 0);
    esk_nrfp_event(&k->x, rx ? 0x14c : 0x150); /* RXSTARTED, TXSTARTED */
    k->error = 0;
    if (k->mode == T_IDLE || k->mode == T_HOLD) {
        k->start_req = 0;
        k->suspend_req = 0;
        begin_start(k, rx);
    } else {
        k->start_req = rx ? 2 : 1; /* after the byte under way */
    }
}

/* the bus at a byte's boundary, SCL low: what comes next */
static void twim_next(struct nspim *k)
{
    u32 max;
    if (k->stop_req) {
        begin_stop(k);
        return;
    }
    if (k->error) {
        hold(k);
        return;
    }
    if (k->start_req) {
        int rx = k->start_req == 2;
        k->start_req = 0;
        begin_start(k, rx);
        return;
    }
    if (k->suspend_req) {
        k->suspend_req = 0;
        hold(k);
        esk_nrfp_event(&k->x, 0x148); /* SUSPENDED */
        return;
    }
    max = buf_max(k);
    if (k->tn >= max) {
        hold(k); /* the buffer is done: the bus waits, SCL low */
        return;
    }
    if (k->tn + 1 == max) {
        u32 sh = rd(k, 0x200);
        esk_nrfp_event(&k->x, k->reading ? 0x15c : 0x160); /* LASTRX, LASTTX */
        if (!k->reading) {
            if (sh & (1u << 7))
                twim_task_start(k, 1); /* LASTTX_STARTRX */
            if (sh & (1u << 8))
                k->suspend_req = 1;    /* LASTTX_SUSPEND */
            if (sh & (1u << 9))
                k->stop_req = 1;       /* LASTTX_STOP */
        } else {
            if (sh & (1u << 10))
                twim_task_start(k, 0); /* LASTRX_STARTTX */
            if (sh & (1u << 11))
                k->suspend_req = 1;    /* LASTRX_SUSPEND */
            if (sh & (1u << 12))
                k->stop_req = 1;       /* LASTRX_STOP */
        }
    }
    if (k->reading) {
        /* the last byte read before a STOP or a START is NACKed */
        k->ack = !(k->tn + 1 == max && (k->stop_req || k->start_req || !k->suspend_req));
        begin_byte(k, 0, 0xffu);
    } else {
        begin_byte(k, 0, mem_rd(k, rd(k, 0x544) + k->tn));
    }
}

static void twim_error(struct nspim *k, u32 src)
{
    esk_nrfp_hw(&k->x, 0x4c4, rd(k, 0x4c4) | src);
    k->error = 1;
    k->suspend_req = k->start_req = 0;
    esk_nrfp_event(&k->x, 0x124); /* ERROR */
}

static void twim_byte_done(struct nspim *k)
{
    if (k->addr) {
        if (!k->acked) {
            twim_error(k, 1u << 1); /* ANACK */
            twim_next(k);
            return;
        }
        twim_next(k);
        return;
    }
    if (k->reading) {
        mem_wr(k, rd(k, 0x534) + k->tn, (u8)k->shift);
        k->tn++;
        esk_nrfp_hw(&k->x, 0x53c, k->tn);
    } else {
        k->tn++;
        esk_nrfp_hw(&k->x, 0x54c, k->tn);
        if (!k->acked)
            twim_error(k, 1u << 2); /* DNACK */
    }
    twim_next(k);
}

static void twim_step(struct nspim *k)
{
    u64 half = twim_half(k), q = half / 4u ? half / 4u : 1u;
    switch (k->mode) {
    case T_START:
        /* from a free bus or a held one: SDA released, SCL released, SDA
         * falls, SCL falls */
        if (k->step == 0) {
            lines(k, k->scl, 1);
            k->step = 1;
            wait_n(k, half - q);
        } else if (k->step == 1) {
            lines(k, 1, 1);
            k->step = 2;
            wait_n(k, half);
        } else if (k->step == 2) {
            lines(k, 1, 0);
            k->step = 3;
            wait_n(k, half);
        } else {
            lines(k, 0, 0);
            begin_byte(k, 1, (rd(k, 0x588) & 0x7fu) << 1 | (u32)k->reading);
        }
        return;
    case T_STOP:
        if (k->step == 0) {
            lines(k, 0, 0);
            k->step = 1;
            wait_n(k, half - q);
        } else if (k->step == 1) {
            lines(k, 1, 0);
            k->step = 2;
            wait_n(k, half);
        } else {
            lines(k, 1, 1);
            k->mode = T_IDLE;
            k->error = 0;
            idle_clock(k);
            esk_nrfp_event(&k->x, 0x104); /* STOPPED */
        }
        return;
    case T_BYTE: {
        int rx = k->reading && !k->addr;
        if (k->step == 0) {
            int sda;
            if (k->tbit < 8)
                sda = rx ? 1 : (int)(k->shift >> (7 - k->tbit) & 1u);
            else
                sda = rx ? !k->ack : 1;
            lines(k, 0, sda);
            k->step = 1;
            wait_n(k, half - q);
        } else if (k->step == 1) {
            lines(k, 1, k->sda);
            k->step = 2;
            wait_n(k, half);
        } else if (k->step == 2) {
            int v = sda_in(k);
            if (k->tbit < 8 && rx)
                k->shift = (k->shift << 1 | (u32)v) & 0xffu;
            if (k->tbit == 8)
                k->acked = rx ? 1 : v == 0;
            lines(k, 0, k->sda);
            k->step = 3;
            wait_n(k, q);
        } else if (k->tbit == 8) {
            lines(k, 0, 1);
            twim_byte_done(k);
        } else {
            k->tbit++;
            k->step = 0;
            twim_step(k);
        }
        return;
    }
    default:
        idle_clock(k);
        return;
    }
}

static void twim_task(struct nspim *k, u32 off)
{
    switch (off) {
    case 0x000: /* STARTRX */
        twim_task_start(k, 1);
        break;
    case 0x008: /* STARTTX */
        twim_task_start(k, 0);
        break;
    case 0x014: /* STOP */
        if (k->mode == T_IDLE) {
            esk_nrfp_event(&k->x, 0x104);
        } else if (k->mode == T_HOLD) {
            begin_stop(k);
        } else {
            k->stop_req = 1;
        }
        break;
    case 0x01c: /* SUSPEND */
        if (k->mode != T_IDLE && k->mode != T_HOLD)
            k->suspend_req = 1;
        else if (k->mode == T_HOLD)
            esk_nrfp_event(&k->x, 0x148);
        break;
    case 0x020: /* RESUME */
        k->suspend_req = 0;
        if (k->mode == T_HOLD && !k->error && k->tn < buf_max(k))
            twim_next(k);
        break;
    default:
        break;
    }
}

/* ---- the device -------------------------------------------------------- */

static void spim_tick(void *ctx, u32 cycles)
{
    struct nspim *k = (struct nspim *)ctx;
    if (!k->running)
        return;
    cycles = esk_mach_after(k->x.s, cycles, k->since);
    k->since = 0;
    if (k->left > cycles) {
        k->left -= cycles;
        return;
    }
    {
        /* one step in an instruction; what the instruction ran past it
         * counts toward the next, up to a whole step */
        u64 over = cycles - k->left;
        int was = esk_nrfp_line(&k->x);
        k->left = 0;
        if (twim_on(k))
            twim_step(k);
        else if (spim_on(k) && k->busy)
            spim_edge(k);
        else
            idle_clock(k);
        if (k->running && k->left) {
            u64 cut = over < k->left ? over : k->left - 1;
            k->left -= cut;
            k->since = 0;
        }
        esk_nrfp_irq(&k->x, was, 0);
    }
}

static int spim_next(void *ctx, u32 *cycles)
{
    struct nspim *k = (struct nspim *)ctx;
    if (!k->running)
        return 0;
    *cycles = k->left > 0xffffffffu ? 0xffffffffu : (u32)(k->left ? k->left : 1);
    return 1;
}

static u32 spim_read(void *ctx, u32 off, int n)
{
    struct nspim *k = (struct nspim *)ctx;
    esk_nrfp_inten_read(&k->x, off);
    return esk_rf_read(k->x.p, off, n);
}

static void spim_write(void *ctx, u32 off, int n, u32 v)
{
    struct nspim *k = (struct nspim *)ctx;
    int was = esk_nrfp_line(&k->x);
    u32 errsrc = rd(k, 0x4c4);
    esk_rf_write(k->x.p, off, n, v);
    esk_nrfp_inten_write(&k->x, off, v);
    if (off == 0x4c4)
        esk_nrfp_hw(&k->x, 0x4c4, errsrc & ~v); /* written 1: cleared */
    if (off == 0x200)
        esk_nrfp_hw(&k->x, 0x200, v & ((1u << 17) | (0x3fu << 7))); /* SPIM's and TWIM's SHORTS */
    if (off < 0x100 && (v & 1)) {
        if (spim_on(k)) {
            if (off == 0x010 && !k->busy)
                spim_start(k);
            else if (off == 0x014) {
                if (k->busy)
                    k->stop = 1;
                else
                    esk_nrfp_event(&k->x, 0x104);
            }
        } else if (twim_on(k)) {
            twim_task(k, off);
        }
    }
    if (off == 0x500) {
        /* enabled or disabled: the lines idle */
        k->busy = 0;
        k->mode = T_IDLE;
        k->error = k->stop_req = k->suspend_req = k->start_req = 0;
        k->scl = k->sda = 1;
        k->sck = (rd(k, 0x554) & 4u) != 0;
        k->csn = (rd(k, 0x568) & 1u) ? 0 : 1;
        idle_clock(k);
        roles(k, 1);
    } else if (off == 0x554 && spim_on(k) && !k->busy) {
        k->sck = (v & 4u) != 0;
        roles(k, 1);
    } else if (off >= 0x508 && off <= 0x514) {
        roles(k, 0);
    }
    esk_nrfp_irq(&k->x, was, 1);
}

static void spim_reset(void *ctx)
{
    struct nspim *k = (struct nspim *)ctx;
    k->running = 0;
    k->left = 0;
    k->busy = 0;
    k->mode = T_IDLE;
    k->error = k->stop_req = k->suspend_req = k->start_req = 0;
    k->scl = k->sda = 1;
    k->sck = k->mosi = 0;
    k->csn = 1;
    k->last_role = 0;
    k->x.inten = 0;
}

const struct esk_dev_ops esk_nrf_spim_ops = {
    "nrf-spim", spim_read, spim_write, spim_reset, spim_tick, spim_next,
};

void *esk_nrf_spim_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct nspim *k;
    if (strncmp(p->name, "SPIM", 4) || p->name[4] < '0' || p->name[4] > '3' || p->name[5])
        return 0;
    k = (struct nspim *)esk_mach_alloc(s, sizeof *k);
    if (!k)
        return 0;
    memset(k, 0, sizeof *k);
    if (esk_nrfp_init(&k->x, s, p))
        return 0;
    k->unit = p->name[4] - '0';
    k->scl = k->sda = 1;
    k->csn = 1;
    return k;
}
