/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* ble.c -- the ESP32-C3's Bluetooth LE core at 0x60031000, RivieraWaves'
 * BLE IP (its VERSION reads 0x09001B00, which the ROM's r_lld_core_init
 * checks), as the controller in the ROM drives it (its r_rwip_*, r_lld_*,
 * r_sch_* and r_emi_* routines keep their names; docs/engineering/
 * c3-radios.md, step 6):
 *
 *   - +0x00 the control register: its soft resets (bits 31:29) are done at
 *     once;
 *   - the native clock: +0x1C bit 31 samples it, bits 27:0 then hold the
 *     half-slots (312.5 us) since the core started and +0x20 the
 *     half-microseconds left in the half-slot, counting down from 624
 *     (r_rwip_time_get);
 *   - the timers: the 10 ms one (+0xE4, in 32 half-slots; interrupt bit
 *     9), the half-slot one (+0xE8; bit 10) and the half-microsecond one
 *     (+0xEC the half-slot, +0xF0 the count down within it; bit 11), each
 *     raising its bit when the clock reaches it;
 *   - the interrupts: +0x0C enables them, +0x10 reads those raised and
 *     enabled, +0x18 (and +0x38C) acknowledges by ones. With +0x2D8 bit 31
 *     on (r_lld_core_init sets it) each one raised goes into a FIFO the
 *     controller's r_rwble_isr reads there: the oldest entry's bits in
 *     bits 30:10, the count in 9:5; a write of bit 0 removes it. Interrupt
 *     source 8 (RWBLE) is asserted while the FIFO holds an entry (or,
 *     without it, while one is raised and enabled). The scheduler's bits:
 *     1 a connection's packet sent after one of its descriptors was
 *     acknowledged, 2 a connection's packet received, 5 an event's end;
 *   - the exchange memory (EM), the controller's structures in RAM, in
 *     1 KB blocks each mapped through one of the registers from +0x204 by
 *     the hardware's fixed table (the ROM's em_base_reg_lut): a register's
 *     bits 31:18 are the EM offset its region starts at (in words), bits
 *     17:0 the RAM's word address under 0x3FC00000 (r_emi_*);
 *   - the exchange table: 16 entries of 16 bytes from EM offset 0
 *     (r_sch_prog_ble_push): its first halfword's bits 5:3 the event's
 *     state (0 waiting, 2 under way, 3 over), its start (+2 and +4 the
 *     half-slot, +6 the count down within it), +8 its control structure
 *     (the EM offset / 2; 90-byte structures from 0x400, one per link). A
 *     write of bit 31 with an entry's index to +0x100 starts it: at its
 *     time the event runs as its control structure's format says, then its
 *     state reads 3 and the end interrupt is raised;
 *   - an advertising event (format 4 or 5): the control structure's
 *     address (+6 to +11) and transmit descriptor (+0x1C: its PDU header
 *     at +2, its data's EM offset at +4) make the advertising PDU, sent on
 *     each primary channel of the map (+0x16 bits 2:0: 37, 38, 39) at 1
 *     Mbit/s, one after another with time for a request between them
 *     (T_IFS and a SCAN_REQ's airtime), at +9 dBm (ESP-IDF's default level,
 *     P9). After a connectable PDU it listens T_IFS on its channel: a
 *     CONNECT_IND for its address is received, and ends the event;
 *   - a connection event as the slave (format 3): on the data channel
 *     channel selection algorithm #1 gives from the last unmapped channel
 *     and the hop increment (+0x16 bits 5:0 and 12:8) and the map (+0x22,
 *     37 bits), it listens from its start for the receive window (+0x1A, in
 *     2 us units, or 625 us ones with bit 15); the master's packet is
 *     received, and answered T_IFS after it with the packet at the
 *     transmit descriptor (+0x1C: next at +0 bits 14:0, header at +2, data
 *     at +4) or an empty one; the sequence numbers acknowledge (the
 *     descriptor acknowledged is marked done, bit 15 of its first
 *     halfword, and +0x1C moves to the next) and drop a resent packet;
 *     while either side has more data it listens again, otherwise the
 *     event ends;
 *   - the received packets go into the receive descriptors (20 bytes from
 *     0x1000, the next at +0x24): +0 bit 15 filled, +2 the status (bit 6 a
 *     packet already received), +4 the header, +6 bits 15:14 the PHY (1M:
 *     0), +8 and +10 the half-slot of the access address's end and +12 its
 *     count down (bits 9:0) with the link (bits 15:11), +18 the EM offset
 *     of the buffer the payload goes to (r_lld_rxdesc_check, ..._free,
 *     r_lld_adv_pkt_rx_connect_ind, r_lld_con_rx).
 * The other registers keep what is written.
 * Not yet: scan requests and responses, scanning and initiating (the
 * master's side), the transmit power the controller sets, the received
 * signal strength in the descriptor, the 2M and Coded PHYs, channel
 * selection algorithm #2, extended and periodic advertising, encryption,
 * privacy (resolvable addresses), slave latency, an event's maximum length,
 * sleep, CRC errors, the radio's controls (+0x70 to +0x9C) beyond keeping
 * their values. */
#include <string.h>

#include "mcu/esp32c3/esp32c3.h"

#define BLE_BASE     0x60031000u
#define BLE_SIZE     0x1000u
#define CNTL         0x000u
#define VERSION      0x004u
#define INTCNTL      0x00Cu
#define INTSTAT      0x010u
#define INTACK       0x018u
#define SLOTCLK      0x01Cu
#define FINETIMECNT  0x020u
#define TGT_10MS     0x0E4u
#define TGT_HS       0x0E8u
#define TGT_HUS_SLOT 0x0ECu
#define TGT_HUS_FINE 0x0F0u
#define ACTSCHCNTL   0x100u
#define EMI_MAP      0x204u
#define EMI_MAPS     39
#define EM_BLOCKS    51
#define ISR_FIFO     0x2D8u
#define INTACK2      0x38Cu
#define IP_VERSION   0x09001B00u
#define SOURCE       8 /* ETS_RWBLE_INTR_SOURCE */
#define FIFO_DEPTH   16
#define I_TX         (1u << 1)
#define I_RX         (1u << 2)
#define I_END        (1u << 5)
#define I_10MS       (1u << 9)
#define I_HS         (1u << 10)
#define I_HUS        (1u << 11)
#define HUS_PER_HS   625u /* half-microseconds in a half-slot */
#define ET_SIZE      16u
#define ET_N         16
#define ST_RUNNING   2u
#define ST_OVER      3u
#define TX_DBM       9
#define T_IFS_HUS    300u /* 150 us */
#define SCAN_REQ_HUS 352u /* a SCAN_REQ's 176 us at 1 Mbit/s */
#define SYNC_HUS     80u  /* a 1M packet's preamble and access address */
#define RXDESC       0x1000u
#define RXDESC_SIZE  20u
#define RXDESCPTR    0x024u
#define CS_BASE      0x400u
#define CS_SIZE      90u

struct c3_ble {
    struct esk_mach *s;
    u32 word[BLE_SIZE / 4u];
    u64 hus;  /* half-microseconds since the core's start */
    u64 frac; /* cycles * 2e6 owed, in core_hz units */
    u32 raw;  /* raw interrupt status */
    u32 fifo[FIFO_DEPTH]; /* the interrupt FIFO's entries, oldest first */
    int nfifo;
    int clocked;
    /* the timers armed (their target written since they last fired) */
    int arm_10ms, arm_hs, arm_hus;
    /* the exchange table's entries started, waiting for their time */
    u32 et_pending;
    u64 et_at[ET_N];
    int et_step[ET_N];
    u8 pdu[264];
    /* listening after an advertising PDU */
    u32 listen_mhz;
    u64 listen_until;
    int listen_e;
    /* a packet being received: whole at rx_end */
    int rx_on, rx_e, rx_rssi;
    u64 rx_sync, rx_end;
    u32 rx_len;
    u8 rx_pdu[264];
    /* each link's (control structure's) sequence numbers, the descriptor it
     * sent last and whether the peer has more */
    u8 sn[16], nesn[16], md_peer[16];
    u32 txsent[16];
    u8 tx_acked[16]; /* a descriptor acknowledged: the transmit interrupt after the answer */
    u32 ch_mhz[ET_N]; /* each event's data channel */
};

static const u32 ADV_MHZ[3] = { 2402u, 2426u, 2480u };

static u32 *reg(struct c3_ble *k, u32 off) { return &k->word[(off % BLE_SIZE) / 4u]; }

static int fifo_on(struct c3_ble *k) { return (*reg(k, ISR_FIFO) & 0x80000000u) != 0; }

static void irq(struct c3_ble *k)
{
    esk_esp32c3_source(k->s, SOURCE, fifo_on(k) ? k->nfifo > 0 : (k->raw & *reg(k, INTCNTL)) != 0);
}

/* interrupt bits arise: in the FIFO, one entry with them, when it is on */
static void raise(struct c3_ble *k, u32 bits)
{
    k->raw |= bits;
    bits &= *reg(k, INTCNTL);
    if (bits && fifo_on(k) && k->nfifo < FIFO_DEPTH)
        k->fifo[k->nfifo++] = bits;
    irq(k);
}

static u64 clkn(const struct c3_ble *k) { return k->hus / HUS_PER_HS; }

static u64 due_hus(struct c3_ble *k)
{
    return (u64)(*reg(k, TGT_HUS_SLOT) & 0x0FFFFFFFu) * HUS_PER_HS + 624u - (*reg(k, TGT_HUS_FINE) & 0x3FFu);
}

static void timers(struct c3_ble *k)
{
    u64 c = clkn(k);
    if (k->arm_10ms && (c >> 5) >= (*reg(k, TGT_10MS) & 0x007FFFFFu)) {
        k->arm_10ms = 0;
        raise(k, I_10MS);
    }
    if (k->arm_hs && c >= (*reg(k, TGT_HS) & 0x0FFFFFFFu)) {
        k->arm_hs = 0;
        raise(k, I_HS);
    }
    if (k->arm_hus && k->hus >= due_hus(k)) {
        k->arm_hus = 0;
        raise(k, I_HUS);
    }
}

/* ---- the exchange memory ----------------------------------------------------- */

/* the exchange memory's 1 KB blocks: the mapping register each one goes
 * through and the EM offset that register's region starts at (the
 * hardware's wiring, as the ROM's em_base_reg_lut gives it) */
static const u8 EM_REG[EM_BLOCKS] = { 0,  1,  1,  2,  3,  4,  4,  5,  6,  7,  7,  8,  8,  9,  9,  10, 10,
                                      11, 11, 12, 12, 13, 13, 14, 14, 15, 15, 16, 16, 17, 18, 19, 20, 21,
                                      22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38 };

/* an EM offset's RAM address: 0 when it is not mapped */
static u32 em_addr(struct c3_ble *k, u32 o)
{
    u32 b = o >> 10, r, start;
    if (b >= EM_BLOCKS)
        return 0;
    r = *reg(k, EMI_MAP + 4u * EM_REG[b]);
    start = (r >> 18) << 2;
    if (!(r & 0x3FFFFu) || start > o)
        return 0; /* not mapped */
    return (0x3FC00000u | ((r << 2) & 0xFFFFCu)) + (o - start);
}

static u32 em_read(struct c3_ble *k, u32 o, int n)
{
    u32 a = em_addr(k, o), v = 0;
    if (a)
        (void)esk_bus_debug_read(&k->s->bus, a, n, &v);
    return v;
}

static void em_write16(struct c3_ble *k, u32 o, u32 v)
{
    u32 a = em_addr(k, o);
    if (a)
        (void)esk_bus_debug_write(&k->s->bus, a, 2, v & 0xFFFFu);
}

/* ---- the events -------------------------------------------------------------- */

static void et_state(struct c3_ble *k, int e, u32 state)
{
    u32 et = ET_SIZE * (u32)e;
    em_write16(k, et, (em_read(k, et, 2) & ~0x38u) | state << 3);
}

static void et_over(struct c3_ble *k, int e)
{
    et_state(k, e, ST_OVER);
    k->et_pending &= ~(1u << e);
    raise(k, I_END);
}

/* an advertising event's next PDU, on its next channel; or its end */
static void adv_step(struct c3_ble *k, int e)
{
    u32 et = ET_SIZE * (u32)e, cs = 2u * em_read(k, et + 8u, 2), desc = em_read(k, cs + 0x1Cu, 2);
    u32 hdr = em_read(k, desc + 2u, 2), len = hdr >> 8, data = em_read(k, desc + 4u, 2);
    u32 chmap = em_read(k, cs + 0x16u, 2) & 7u, i, n = 0, nth = 0;
    int ch = -1, c;
    u64 air;
    for (c = 0; c < 3; c++)
        if ((chmap >> c) & 1u) {
            if ((int)nth == k->et_step[e]) {
                ch = c;
                break;
            }
            nth++;
        }
    if (ch < 0 || len < 6u) {
        et_over(k, e);
        return;
    }
    k->pdu[n++] = (u8)hdr;
    k->pdu[n++] = (u8)len;
    for (i = 0; i < 6u; i++) /* AdvA, the control structure's address */
        k->pdu[n++] = (u8)em_read(k, cs + 6u + i, 1);
    for (i = 0; i < len - 6u; i++)
        k->pdu[n++] = (u8)em_read(k, data + i, 1);
    air = (u64)(1u + 4u + n + 3u) * 8u * ES_PS_PER_US; /* preamble, access address, PDU, CRC */
    if (k->s->host.radio_tx)
        (void)k->s->host.radio_tx(k->s->host.ctx, k->pdu, n, ADV_MHZ[ch], TX_DBM, 1000u, air, ESK_RADIO_BLE,
                                  0x80000000u | (u32)e);
    k->et_step[e]++;
    if ((hdr & 15u) == 0u || (hdr & 15u) == 1u || (hdr & 15u) == 6u) { /* it listens for a request */
        k->listen_mhz = ADV_MHZ[ch];
        k->listen_e = e;
        k->listen_until = k->et_at[e] + air / (ES_PS_PER_US / 2u) + T_IFS_HUS + SYNC_HUS + 32u;
    }
    k->et_at[e] += air / (ES_PS_PER_US / 2u) + T_IFS_HUS + SCAN_REQ_HUS;
}

/* ---- connections --------------------------------------------------------------- */

static u32 cs_of(struct c3_ble *k, int e) { return 2u * em_read(k, ET_SIZE * (u32)e + 8u, 2); }

static int link_of(u32 cs) { return (int)(((cs - CS_BASE) / CS_SIZE) & 15u); }

static u32 data_mhz(u32 ch) { return ch <= 10u ? 2404u + 2u * ch : 2428u + 2u * (ch - 11u); }

/* the event's data channel by channel selection algorithm #1: the last
 * unmapped channel and the hop increment (+0x16), the map (+0x22) */
static u32 con_channel(struct c3_ble *k, u32 cs)
{
    u32 w = em_read(k, cs + 0x16u, 2), un = ((w & 0x3Fu) + ((w >> 8) & 0x1Fu)) % 37u, used = 0, i, map[37], n = 0;
    for (i = 0; i < 37u; i++)
        if ((em_read(k, cs + 0x22u + i / 8u, 1) >> (i % 8u)) & 1u)
            map[n++] = i;
    used = n;
    if (!used || ((em_read(k, cs + 0x22u + un / 8u, 1) >> (un % 8u)) & 1u))
        return un;
    return map[un % used];
}

/* the receive window from now: +0x1A in 2 us units, or in 625 us ones
 * with bit 15 */
static u64 con_window(struct c3_ble *k, u32 cs)
{
    u32 w = em_read(k, cs + 0x1Au, 2);
    return (w & 0x8000u) ? (u64)(w & 0x7FFFu) * 1250u : (u64)w * 4u;
}

static void con_listen(struct c3_ble *k, int e, u64 until)
{
    k->listen_mhz = k->ch_mhz[e];
    k->listen_e = e;
    k->listen_until = until;
    k->et_step[e] = 1;
    k->et_at[e] = until + 1u;
}

/* the slave's answer, T_IFS after the master's packet: its descriptor
 * (+0x1C) or an empty PDU */
static void con_send(struct c3_ble *k, int e)
{
    u32 cs = cs_of(k, e), l = (u32)link_of(cs), desc = em_read(k, cs + 0x1Cu, 2) & 0x7FFFu, n = 0, i, len = 0, llid = 1;
    u32 next = 0;
    u64 air;
    int md = 0;
    if (desc && !(em_read(k, desc, 2) & 0x8000u)) {
        u32 hdr = em_read(k, desc + 2u, 2), data = em_read(k, desc + 4u, 2);
        llid = hdr & 3u;
        len = hdr >> 8;
        for (i = 0; i < len; i++)
            k->pdu[2u + i] = (u8)em_read(k, data + i, 1);
        next = em_read(k, desc, 2) & 0x7FFFu;
        md = next && !(em_read(k, next, 2) & 0x8000u) && (em_read(k, next + 2u, 2) & 3u);
        k->txsent[l] = desc;
    } else
        k->txsent[l] = 0;
    k->pdu[n++] = (u8)(llid | (u32)k->nesn[l] << 2 | (u32)k->sn[l] << 3 | (md ? 1u << 4 : 0u));
    k->pdu[n++] = (u8)len;
    n += len;
    air = (u64)(1u + 4u + n + 3u) * 8u * ES_PS_PER_US;
    if (k->s->host.radio_tx)
        (void)k->s->host.radio_tx(k->s->host.ctx, k->pdu, n, k->ch_mhz[e], TX_DBM, 1000u, air, ESK_RADIO_BLE,
                                  0x80000000u | (u32)e);
    if (k->tx_acked[l]) { /* this answer sent: the controller frees what was acknowledged */
        k->tx_acked[l] = 0;
        raise(k, I_TX);
    }
    if (md || k->md_peer[l]) /* more: the master's next packet */
        con_listen(k, e, k->hus + air / (ES_PS_PER_US / 2u) + T_IFS_HUS + SYNC_HUS + 32u);
    else {
        k->et_step[e] = 3;
        k->et_at[e] = k->hus + air / (ES_PS_PER_US / 2u);
    }
}

/* a connection event's next step: its start (listening on its channel),
 * no packet heard (its end), its answer sent */
static void con_step(struct c3_ble *k, int e)
{
    u32 cs = cs_of(k, e);
    int l = link_of(cs);
    if (k->et_step[e] == 0) {
        if (em_read(k, cs + 0x50u, 2) == 0) { /* a new link: its event counter (+0x50) at 0 */
            k->sn[l] = k->nesn[l] = k->md_peer[l] = k->tx_acked[l] = 0;
            k->txsent[l] = 0;
        }
        k->ch_mhz[e] = data_mhz(con_channel(k, cs));
        con_listen(k, e, k->hus + con_window(k, cs) + SYNC_HUS);
        return;
    }
    if (k->et_step[e] == 2) {
        con_send(k, e);
        return;
    }
    k->listen_mhz = 0;
    et_over(k, e);
}

/* the packet received is whole: into the receive descriptor */
static void rx_done(struct c3_ble *k)
{
    u32 d = *reg(k, RXDESCPTR) & 0x7FFFu, buf, i, cs = 2u * em_read(k, ET_SIZE * (u32)k->rx_e + 8u, 2);
    u64 slot = k->rx_sync / HUS_PER_HS;
    u32 fine = 624u - (u32)(k->rx_sync % HUS_PER_HS), status = 0, fmt = em_read(k, cs, 2) & 0x1Fu;
    int con = fmt != 4u && fmt != 5u, l = link_of(cs);
    k->rx_on = 0;
    if (!d)
        d = RXDESC;
    if (con) { /* the sequence numbers */
        u32 h = k->rx_pdu[0];
        if (((h >> 2) & 1u) != k->sn[l]) { /* ours was received */
            if (k->txsent[l]) {
                em_write16(k, k->txsent[l], em_read(k, k->txsent[l], 2) | 0x8000u);
                em_write16(k, cs + 0x1Cu, em_read(k, k->txsent[l], 2) & 0x7FFFu);
                k->txsent[l] = 0;
                k->tx_acked[l] = 1;
            }
            k->sn[l] ^= 1u;
        }
        if (((h >> 3) & 1u) != k->nesn[l])
            status |= 1u << 6; /* resent: already taken */
        else
            k->nesn[l] ^= 1u;
        k->md_peer[l] = (u8)((h >> 4) & 1u);
    }
    buf = em_read(k, d + 18u, 2);
    for (i = 2; i < k->rx_len; i++)
        (void)esk_bus_debug_write(&k->s->bus, em_addr(k, buf + i - 2u), 1, k->rx_pdu[i]);
    em_write16(k, d + 2u, status);
    em_write16(k, d + 4u, (u32)k->rx_pdu[0] | (u32)k->rx_pdu[1] << 8);
    em_write16(k, d + 6u, (u32)(u8)k->rx_rssi); /* 1M (bits 15:14 0) */
    em_write16(k, d + 8u, (u32)slot);
    em_write16(k, d + 10u, (u32)(slot >> 16) & 0xFFFu);
    em_write16(k, d + 12u, (fine & 0x3FFu) | ((cs - CS_BASE) / CS_SIZE) << 11);
    em_write16(k, d + 14u, 0);
    em_write16(k, d + 0u, em_read(k, d, 2) | 0x8000u);
    *reg(k, RXDESCPTR) = em_read(k, d, 2) & 0x7FFFu;
    if (con) {
        raise(k, I_RX);
        k->et_step[k->rx_e] = 2; /* the answer */
        k->et_at[k->rx_e] = k->rx_end + T_IFS_HUS;
    } else if ((k->rx_pdu[0] & 15u) == 5u) /* CONNECT_IND: the advertising is over */
        et_over(k, k->rx_e);
}

/* the events' steps due, each at its time */
static void events(struct c3_ble *k)
{
    int e, again = 1;
    if (k->rx_on && k->hus >= k->rx_end)
        rx_done(k);
    while (again)
        for (again = 0, e = 0; e < ET_N; e++)
            if (((k->et_pending >> e) & 1u) && k->hus >= k->et_at[e] && !(k->rx_on && k->rx_e == e)) {
                u32 cs = 2u * em_read(k, ET_SIZE * (u32)e + 8u, 2), fmt = em_read(k, cs, 2) & 0x1Fu;
                if (k->et_step[e] == 0)
                    et_state(k, e, ST_RUNNING);
                if (fmt == 4u || fmt == 5u)
                    adv_step(k, e);
                else if (fmt == 3u)
                    con_step(k, e);
                else
                    et_over(k, e); /* not modelled yet */
                again = 1;
            }
}

static void ble_tick(void *ctx, u32 cycles)
{
    struct c3_ble *k = (struct c3_ble *)ctx;
    u32 hz = esk_mach_core_hz(k->s);
    if (!hz)
        return;
    k->frac += (u64)cycles * 2000000u;
    k->hus += k->frac / hz;
    k->frac %= hz;
    if (k->arm_10ms || k->arm_hs || k->arm_hus)
        timers(k);
    if (k->et_pending || k->rx_on)
        events(k);
}

/* cycles until the next timer fires or event acts */
static int ble_next(void *ctx, u32 *cycles)
{
    struct c3_ble *k = (struct c3_ble *)ctx;
    u32 hz = esk_mach_core_hz(k->s);
    u64 best = 0, at;
    int any = 0, e;
    if (!hz)
        return 0;
    if (k->arm_hus) {
        best = due_hus(k);
        any = 1;
    }
    if (k->arm_hs) {
        at = (u64)(*reg(k, TGT_HS) & 0x0FFFFFFFu) * HUS_PER_HS;
        if (!any || at < best)
            best = at;
        any = 1;
    }
    if (k->arm_10ms) {
        at = (u64)(*reg(k, TGT_10MS) & 0x007FFFFFu) * 32u * HUS_PER_HS;
        if (!any || at < best)
            best = at;
        any = 1;
    }
    for (e = 0; e < ET_N; e++)
        if ((k->et_pending >> e) & 1u) {
            if (!any || k->et_at[e] < best)
                best = k->et_at[e];
            any = 1;
        }
    if (k->rx_on && (!any || k->rx_end < best)) {
        best = k->rx_end;
        any = 1;
    }
    if (!any)
        return 0;
    if (best <= k->hus) {
        *cycles = 1;
        return 1;
    }
    {
        u64 c = ((best - k->hus) * hz - k->frac + 1999999u) / 2000000u;
        *cycles = c == 0 ? 1u : c > 0xffffffffu ? 0xffffffffu : (u32)c;
    }
    return 1;
}

/* ---- the registers ------------------------------------------------------------- */

static u32 ble_read(void *ctx, u32 off, int n)
{
    struct c3_ble *k = (struct c3_ble *)ctx;
    u32 w = off & ~3u;
    (void)n;
    if (w == VERSION)
        *reg(k, w) = IP_VERSION;
    else if (w == INTSTAT)
        *reg(k, w) = k->raw & *reg(k, INTCNTL);
    else if (w == ISR_FIFO)
        *reg(k, w) = (*reg(k, w) & 0x8000001Eu) | (u32)k->nfifo << 5 |
                     (k->nfifo ? (k->fifo[0] & 0x1FFFFFu) << 10 : 0u);
    return *reg(k, w) >> (8u * (off & 3u));
}

static void ble_write(void *ctx, u32 off, int n, u32 v)
{
    struct c3_ble *k = (struct c3_ble *)ctx;
    u32 w = off & ~3u, sh = 8u * (off & 3u), *r = reg(k, w);
    u32 mask = (n >= 4 ? 0xffffffffu : ((1u << (8 * n)) - 1u)) << sh;
    v = v << sh & mask;
    if (w == INTACK || w == INTACK2) {
        k->raw &= ~v;
        irq(k);
        return;
    }
    if (w == VERSION)
        return;
    *r = (*r & ~mask) | v;
    switch (w) {
    case CNTL:
        *r &= ~0xE0000000u; /* the soft resets are done at once */
        break;
    case SLOTCLK:
        if (*r & 0x80000000u) { /* the clock sampled */
            *r = (u32)(clkn(k) & 0x0FFFFFFFu);
            *reg(k, FINETIMECNT) = 624u - (u32)(k->hus % HUS_PER_HS);
        }
        break;
    case INTCNTL:
        irq(k);
        break;
    case ISR_FIFO:
        if ((v & 1u) && k->nfifo) { /* the oldest entry read */
            memmove(k->fifo, k->fifo + 1, (size_t)(k->nfifo - 1) * sizeof k->fifo[0]);
            k->nfifo--;
        }
        if (!fifo_on(k))
            k->nfifo = 0;
        *r &= ~1u;
        irq(k);
        break;
    case TGT_10MS:
        k->arm_10ms = 1;
        break;
    case TGT_HS:
        k->arm_hs = 1;
        break;
    case TGT_HUS_SLOT:
    case TGT_HUS_FINE:
        k->arm_hus = 1;
        break;
    case ACTSCHCNTL:
        if (*r & 0x80000000u) { /* an exchange table entry started: its time */
            int e = (int)(*r & 15u);
            u32 et = ET_SIZE * (u32)e;
            u64 slot = (u64)em_read(k, et + 2u, 2) | (u64)(em_read(k, et + 4u, 2) & 0x0FFFu) << 16;
            k->et_at[e] = slot * HUS_PER_HS + 624u - (em_read(k, et + 6u, 2) & 0x3FFu);
            k->et_step[e] = 0;
            k->et_pending |= 1u << e;
            *r &= ~0x80000000u;
        }
        break;
    default:
        break;
    }
    if (k->arm_10ms || k->arm_hs || k->arm_hus)
        timers(k);
    if (k->et_pending)
        events(k);
}

static void ble_reset(void *ctx)
{
    struct c3_ble *k = (struct c3_ble *)ctx;
    memset(k->word, 0, sizeof k->word);
    k->hus = k->frac = 0;
    k->raw = 0;
    k->nfifo = 0;
    k->et_pending = 0;
    k->arm_10ms = k->arm_hs = k->arm_hus = 0;
    k->listen_mhz = 0;
    k->rx_on = 0;
    memset(k->sn, 0, sizeof k->sn);
    memset(k->nesn, 0, sizeof k->nesn);
    memset(k->md_peer, 0, sizeof k->md_peer);
    memset(k->tx_acked, 0, sizeof k->tx_acked);
    memset(k->txsent, 0, sizeof k->txsent);
    k->clocked = 0;
    esk_mach_clock(k->s, &k->clocked, 1);
}

static const struct esk_dev_ops ble_ops = {
    "esp32c3-ble", ble_read, ble_write, ble_reset, ble_tick, ble_next,
};

static u32 ble_tuned(void *ctx, int kind)
{
    struct c3_ble *k = (struct c3_ble *)ctx;
    if (kind != ESK_RADIO_BLE || k->rx_on || !k->listen_mhz)
        return 0;
    if (k->hus > k->listen_until) {
        k->listen_mhz = 0;
        return 0;
    }
    return k->listen_mhz;
}

/* a packet's access address heard: it is taken whole at its end */
static int ble_rx(void *ctx, const u8 *f, u32 len, u32 mhz, int rssi, u32 kbps, int kind)
{
    struct c3_ble *k = (struct c3_ble *)ctx;
    u32 cs, i;
    (void)kbps;
    if (kind != ESK_RADIO_BLE || k->rx_on || !k->listen_mhz || mhz != k->listen_mhz || len < 2u ||
        len > sizeof k->rx_pdu)
        return 0;
    cs = 2u * em_read(k, ET_SIZE * (u32)k->listen_e + 8u, 2);
    if ((em_read(k, cs, 2) & 0x1Fu) == 4u || (em_read(k, cs, 2) & 0x1Fu) == 5u) {
        if ((f[0] & 15u) != 5u || len != 36u) /* a CONNECT_IND, for this advertiser */
            return 0;
        for (i = 0; i < 6u; i++)
            if (f[8u + i] != (u8)em_read(k, cs + 6u + i, 1))
                return 0;
    }
    k->rx_on = 1;
    k->rx_e = k->listen_e;
    k->rx_rssi = rssi;
    k->rx_sync = k->hus;
    k->rx_end = k->hus + (u64)(len + 3u) * 16u;
    k->rx_len = len;
    memcpy(k->rx_pdu, f, len);
    k->listen_mhz = 0;
    return 0;
}

int esk_esp32c3_ble_create(struct esk_mach *s)
{
    struct c3_ble *k = (struct c3_ble *)esk_mach_alloc(s, sizeof *k);
    struct esk_radio_port part;
    if (!k)
        return -1;
    memset(k, 0, sizeof *k);
    k->s = s;
    memset(&part, 0, sizeof part);
    part.ctx = k;
    part.kinds = ESK_RADIO_BLE;
    part.tuned = ble_tuned;
    part.rx = ble_rx;
    if (esk_esp32c3_radio_join(s, ESK_RADIO_BLE, &part) != 0)
        return -1;
    if (esk_mach_add_dev(s, BLE_BASE, BLE_SIZE, &ble_ops, k) != ES_OK)
        return -1;
    ble_reset(k);
    return 0;
}
