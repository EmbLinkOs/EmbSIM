/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* wifi.c -- the ESP32-C3's Wi-Fi MAC at 0x60033000-0x60035FFF, which no
 * document describes: learnt from what ESP-IDF's closed libpp (its hal_*,
 * lmac* and wDev_* routines keep their names) and the ROM's copy of it do
 * with the registers (docs/engineering/c3-radios.md, step 3).
 *
 *   - Transmit: five queues. Queue q's register (+0xD08 - 8q) takes its
 *     first descriptor (bits 19:0, an offset from +0xC64's high bits, the
 *     data RAM at 0x3FC00000) and starts it (bits 31:30). The descriptors
 *     are lldesc's (owner, eof, length with the FCS, size; the buffer; the
 *     next one); the buffer holds the 802.11 frame. Its PLCP word (+0x12F8
 *     - 76q, mac_tx_set_plcp1) gives the length and the PHY rate (bits
 *     16:12, ESP-IDF's wifi_phy_rate_t). The frame goes on the air for its
 *     airtime; when it is over (and its ACK came, or the time for one
 *     passed) the queue's PMD word (+0x1320 - 76q) holds the outcome
 *     (bits 15:12: 0 sent, 5 no ACK), +0xCB0 the queue's done bit (cleared
 *     through +0xCAC), the start bits clear and the interrupt status's bit
 *     7 is set. The queues share one radio: one frame at a time, the
 *     lowest queue first.
 *   - Receive: a descriptor list from +0x88 (absolute), enabled by +0x84
 *     bit 31; +0x84 bit 0 reloads it after the driver gave descriptors
 *     back (from the last filled one's next, +0x90, when it had run out).
 *     A frame for one of the two interfaces (address 1 its MAC under its
 *     mask, +0x40/+0x44 and +0x60/+0x64 for the station, +0x48/+0x4C and
 *     +0x68/+0x6C for the access point, bit 16 of the mask's high word
 *     enabling it) or for a group fills the next descriptor the hardware
 *     owns: ESP-IDF's 48-byte wifi_pkt_rx_ctrl_t (RSSI, rate, channel,
 *     the time in microseconds, the length with the FCS, the interfaces
 *     whose filter took it at bits 28 and 29 of the first word), then the
 *     frame and its FCS. The descriptor goes back to the driver (owner
 *     clear, eof, the length), +0x90 names it, +0x8C the next, and the
 *     interrupt status's bit 24 is set. A unicast frame taken is answered
 *     by an ACK.
 *   - The interrupt status (+0xC3C), enable (+0xC34) and clear (+0xC40,
 *     ones clear) drive interrupt source 0 (WIFI_MAC).
 *   - The time: +0x2000 counts microseconds from the MAC's start, and a
 *     write of bit 0 (bit 1) to +0x200C latches the station's (the access
 *     point's) TSF at +0x2018 (low) and +0x201C (high), hal_get_tsf_time.
 *   - hal_init's request (+0xD14 bit 1) reads acknowledged (bit 0).
 *   - The channel is the RF PLL's: the analog I2C block's frequency code
 *     (0x6000E150 bits 26:20) is the MHz above 2400.
 * The rest of its registers keep what is written.
 * Not yet: retransmissions (a frame without its ACK is not sent again by
 * the hardware), contention (EDCA backoff, CCA: a frame goes out when the
 * radio is free), the TSF's adjustment to an access point's beacons and its
 * timers (TBTT, the TSF timer interrupts at 0x60035118), hardware
 * encryption (CCMP, the key entries at +0x800), aggregation (A-MPDU), the
 * transmit power the PHY sets (20 dBm, the chip's maximum, is used), the
 * receive filters for promiscuous mode and the BSSID filter (+0x00 to
 * +0x2C: frames of other networks reach the driver, which drops them). */
#include <string.h>

#include "mcu/esp32c3/esp32c3.h"

#define MAC_BASE      0x60033000u
#define MAC_SIZE      0x3000u

#define ADDR_LO(i)    (0x40u + 8u * (u32)(i)) /* i 0: the station, 1: the access point */
#define ADDR_HI(i)    (0x44u + 8u * (u32)(i))
#define MASK_LO(i)    (0x60u + 8u * (u32)(i))
#define MASK_HI(i)    (0x64u + 8u * (u32)(i)) /* bit 16 enables the interface */
#define RX_CTRL       0x84u              /* bit 31 receive, bit 0 reload */
#define RX_BASE       0x88u
#define RX_NEXT       0x8Cu
#define RX_LAST       0x90u
#define INT_ENA       0xC34u
#define INT_ST        0xC3Cu
#define INT_CLR       0xC40u
#define DESC_HI       0xC64u /* bits 31:20 of the descriptors' addresses */
#define TXQ_DONE_CLR  0xCACu
#define TXQ_DONE      0xCB0u
#define TXQ(q)        (0xD08u - 8u * (q))
#define INIT          0xD14u
#define PLCP1(q)      (0x12F8u - 76u * (q))
#define PMD(q)        (0x1320u - 76u * (q))
#define MAC_TIME      0x2000u
#define TSF_LATCH     0x200Cu
#define TSF_LO        0x2018u
#define TSF_HI        0x201Cu

#define NQUEUES       5
#define INT_RX        (1u << 24)
#define INT_TX        (1u << 7)
#define RFPLL_CODE    0x6000E150u
#define TX_DBM        20
#define MAX_FRAME     2400u
#define RX_CTRL_LEN   48u

struct c3_wifi {
    struct esk_mach *s;
    u32 word[MAC_SIZE / 4u];
    u32 rx_next;     /* the next receive descriptor, 0 when it ran out */
    u32 pending;     /* the queues started and waiting for the radio */
    int busy;        /* a frame on the air: its queue + 1 */
    u32 seq;         /* the frames sent, for their tokens */
    u64 us;          /* the microseconds since the MAC's start */
    u64 frac;        /* cycles * 1e6 owed, in core_hz units */
    int clocked;
    u8 frame[MAX_FRAME];
};

static u32 *reg(struct c3_wifi *k, u32 off) { return &k->word[(off % MAC_SIZE) / 4u]; }

static u32 rd32(struct c3_wifi *k, u32 a)
{
    u32 v = 0;
    (void)esk_bus_read(&k->s->bus, a, 4, &v);
    return v;
}

static void wr32(struct c3_wifi *k, u32 a, u32 v) { (void)esk_bus_write(&k->s->bus, a, 4, v); }

static void irq(struct c3_wifi *k)
{
    esk_esp32c3_source(k->s, 0, (*reg(k, INT_ST) & *reg(k, INT_ENA)) != 0);
}

/* the 2.4 GHz channel's centre frequency the RF PLL is on, 0 before the
 * PHY set it */
static u32 tuned_mhz(struct c3_wifi *k)
{
    u32 v = 0, code;
    (void)esk_bus_debug_read(&k->s->bus, RFPLL_CODE, 4, &v);
    code = (v >> 20) & 0x7Fu;
    return code ? 2400u + code : 0u;
}

/* wifi_phy_rate_t's rates, in kbit/s */
static u32 rate_kbps(u32 code)
{
    static const u32 legacy[16] = { 1000, 2000, 5500, 11000, 1000, 2000, 5500, 11000,
                                    48000, 24000, 12000, 6000, 54000, 36000, 18000, 9000 };
    static const u32 ht[16] = { 6500, 13000, 19500, 26000, 39000, 52000, 58500, 65000,
                                7200, 14400, 21700, 28900, 43300, 57800, 65000, 72200 };
    code &= 31u;
    return code < 16u ? legacy[code] : ht[code - 16u];
}

static u32 rate_code(u32 kbps)
{
    u32 c;
    for (c = 0; c < 32u; c++)
        if (rate_kbps(c) == kbps)
            return c;
    return 0;
}

static u32 desc_addr(struct c3_wifi *k, u32 v)
{
    u32 hi = *reg(k, DESC_HI) & 0xFFF00000u;
    return (hi ? hi : 0x3FC00000u) | (v & 0xFFFFFu);
}

/* ---- transmit --------------------------------------------------------------- */

static void tx_next(struct c3_wifi *k);

static void tx_done(void *ctx, u32 token, int acked)
{
    struct c3_wifi *k = (struct c3_wifi *)ctx;
    u32 q = token & 7u, group;
    if (!k->busy || (u32)(k->busy - 1) != q || (token >> 3) != (k->seq & 0x1FFFFFFFu))
        return; /* a frame from before a reset */
    k->busy = 0;
    group = k->frame[4] & 1u;
    /* the outcome: sent (a group frame, or answered), or no ACK */
    *reg(k, PMD(q)) = (*reg(k, PMD(q)) & ~0x00FFF000u) | (acked || group ? 0u : 5u << 12);
    *reg(k, TXQ(q)) &= ~0xC0000000u;
    *reg(k, TXQ_DONE) |= 1u << q;
    *reg(k, INT_ST) |= INT_TX;
    irq(k);
    tx_next(k);
}

/* the next queue waiting goes on the air when the radio is free */
static void tx_next(struct c3_wifi *k)
{
    u32 q, d, len = 0, plcp, n, mhz;
    int i;
    if (k->busy || !k->pending)
        return;
    for (q = 0; !(k->pending & (1u << q)); q++)
        ;
    k->pending &= ~(1u << q);
    d = desc_addr(k, *reg(k, TXQ(q)));
    /* the frame's bytes, descriptor after descriptor to the one with eof */
    for (i = 0; i < 16 && d; i++) {
        u32 w0 = rd32(k, d), buf = rd32(k, d + 4u), dl = (w0 >> 12) & 0xFFFu, j;
        for (j = 0; j < dl && len < MAX_FRAME; j++) {
            u32 b = 0;
            (void)esk_bus_read(&k->s->bus, buf + j, 1, &b);
            k->frame[len++] = (u8)b;
        }
        if (w0 & (1u << 30))
            break;
        d = rd32(k, d + 8u);
    }
    plcp = *reg(k, PLCP1(q));
    n = plcp & 0xFFFu; /* the length the PLCP header says, with the FCS */
    if (n && n < len)
        len = n;
    len = len > 4u ? len - 4u : 0u; /* the hardware adds the FCS */
    mhz = tuned_mhz(k);
    k->seq++;
    k->busy = (int)q + 1;
    if (!len || !mhz || !k->s->host.radio_tx ||
        k->s->host.radio_tx(k->s->host.ctx, k->frame, len, mhz, TX_DBM, rate_kbps(plcp >> 12),
                            0, ESK_RADIO_WIFI, (k->seq & 0x1FFFFFFFu) << 3 | q) != 0) {
        /* no air to send on (a chip alone): the frame went out unheard */
        tx_done(k, (k->seq & 0x1FFFFFFFu) << 3 | q, 0);
    }
}

/* ---- receive ---------------------------------------------------------------- */

static int addr_match(struct c3_wifi *k, int i, const u8 *a1)
{
    u32 lo = (u32)a1[0] | (u32)a1[1] << 8 | (u32)a1[2] << 16 | (u32)a1[3] << 24;
    u32 hi = (u32)a1[4] | (u32)a1[5] << 8;
    if (!(*reg(k, MASK_HI(i)) & (1u << 16)))
        return 0;
    if (a1[0] & 1u)
        return 1; /* a group address: every interface takes it */
    return ((lo ^ *reg(k, ADDR_LO(i))) & *reg(k, MASK_LO(i))) == 0 &&
           ((hi ^ *reg(k, ADDR_HI(i))) & *reg(k, MASK_HI(i)) & 0xFFFFu) == 0;
}

static u32 tuned(void *ctx, int kind)
{
    struct c3_wifi *k = (struct c3_wifi *)ctx;
    if (kind != ESK_RADIO_WIFI || !(*reg(k, RX_CTRL) & 0x80000000u))
        return 0;
    return tuned_mhz(k);
}

static int rx(void *ctx, const u8 *frame, u32 len, u32 mhz, int rssi, u32 kbps, int kind)
{
    struct c3_wifi *k = (struct c3_wifi *)ctx;
    u32 d = k->rx_next, w0, buf, size, tot = len + 4u, i, match = 0;
    u8 hdr[RX_CTRL_LEN];
    int ch;
    (void)kind;
    if (len < 10u || !(*reg(k, RX_CTRL) & 0x80000000u))
        return 0;
    if (addr_match(k, 0, frame + 4))
        match |= 1u << 28;
    if (addr_match(k, 1, frame + 4))
        match |= 1u << 29;
    if (!match)
        return 0;
    w0 = d ? rd32(k, d) : 0u;
    if (!(w0 & 0x80000000u)) {
        k->rx_next = 0;
        *reg(k, RX_NEXT) = 0;
        return 0; /* no buffer: the frame is lost, and not answered */
    }
    buf = rd32(k, d + 4u);
    size = w0 & 0xFFFu;
    if (RX_CTRL_LEN + tot > size)
        return 0; /* larger than the buffers: dropped */
    memset(hdr, 0, sizeof hdr);
    hdr[0] = (u8)(rssi < -128 ? -128 : rssi > 0 ? 0 : rssi);
    hdr[1] = (u8)rate_code(kbps); /* sig_mode 0, a legacy frame */
    hdr[3] |= (u8)(match >> 24);
    hdr[8] = 0xA0u; /* the noise floor, -96 dBm */
    ch = mhz == 2484u ? 14 : (int)(mhz - 2407u) / 5;
    hdr[10] = (u8)(ch & 15);
    hdr[12] = (u8)k->us;
    hdr[13] = (u8)(k->us >> 8);
    hdr[14] = (u8)(k->us >> 16);
    hdr[15] = (u8)(k->us >> 24);
    hdr[44] = (u8)tot;
    hdr[45] = (u8)((tot >> 8) & 0x0Fu);
    hdr[47] = 0; /* rx_state: received well */
    for (i = 0; i < RX_CTRL_LEN; i++)
        (void)esk_bus_write(&k->s->bus, buf + i, 1, hdr[i]);
    for (i = 0; i < len; i++)
        (void)esk_bus_write(&k->s->bus, buf + RX_CTRL_LEN + i, 1, frame[i]);
    for (i = 0; i < 4u; i++) /* the FCS, checked and kept */
        (void)esk_bus_write(&k->s->bus, buf + RX_CTRL_LEN + len + i, 1, 0);
    wr32(k, d, size | (RX_CTRL_LEN + tot) << 12 | 1u << 30);
    k->rx_next = rd32(k, d + 8u);
    if (k->rx_next && !(rd32(k, k->rx_next) & 0x80000000u))
        k->rx_next = 0; /* the next one is still the driver's */
    *reg(k, RX_LAST) = d & 0xFFFFFu;
    *reg(k, RX_NEXT) = k->rx_next;
    *reg(k, INT_ST) |= INT_RX;
    irq(k);
    return !(frame[4] & 1u); /* a unicast frame taken is answered */
}

/* ---- the registers ------------------------------------------------------------ */

static void sync_time(struct c3_wifi *k, u32 cycles)
{
    u32 hz = esk_mach_core_hz(k->s);
    if (!hz)
        return;
    k->frac += (u64)cycles * 1000000u;
    k->us += k->frac / hz;
    k->frac %= hz;
}

static u32 mac_read(void *ctx, u32 off, int n)
{
    struct c3_wifi *k = (struct c3_wifi *)ctx;
    u32 w = off & ~3u;
    (void)n;
    if (w == MAC_TIME)
        *reg(k, w) = (u32)k->us;
    return *reg(k, w) >> (8u * (off & 3u));
}

static void mac_write(void *ctx, u32 off, int n, u32 v)
{
    struct c3_wifi *k = (struct c3_wifi *)ctx;
    u32 w = off & ~3u, sh = 8u * (off & 3u), *r = reg(k, w);
    u32 mask = (n >= 4 ? 0xffffffffu : ((1u << (8 * n)) - 1u)) << sh;
    v = v << sh & mask;
    if (w == INT_CLR) {
        *reg(k, INT_ST) &= ~v;
        irq(k);
        return;
    }
    if (w == TXQ_DONE_CLR) {
        *reg(k, TXQ_DONE) &= ~v;
        return;
    }
    *r = (*r & ~mask) | v;
    if (w == INT_ENA)
        irq(k);
    else if (w == INIT && (*r & 2u))
        *r |= 1u; /* hal_init: acknowledged */
    else if (w == RX_BASE) {
        k->rx_next = *r;
        *reg(k, RX_NEXT) = *r;
    } else if (w == RX_CTRL && (*r & 1u)) {
        /* reload: the hardware goes on from the last one it filled */
        if (!k->rx_next && *reg(k, RX_LAST))
            k->rx_next = rd32(k, desc_addr(k, *reg(k, RX_LAST)) + 8u);
        if (k->rx_next && !(rd32(k, k->rx_next) & 0x80000000u))
            k->rx_next = 0;
        *reg(k, RX_NEXT) = k->rx_next;
        *r &= ~1u;
    } else if (w == TSF_LATCH && (*r & 3u)) {
        *reg(k, TSF_LO) = (u32)k->us;
        *reg(k, TSF_HI) = (u32)(k->us >> 32);
    } else if (w >= TXQ(NQUEUES - 1u) && w <= TXQ(0) && ((TXQ(0) - w) & 7u) == 0 && (v & 0xC0000000u)) {
        k->pending |= 1u << ((TXQ(0) - w) / 8u);
        tx_next(k);
    }
}

static void mac_tick(void *ctx, u32 cycles) { sync_time((struct c3_wifi *)ctx, cycles); }

static void mac_reset(void *ctx)
{
    struct c3_wifi *k = (struct c3_wifi *)ctx;
    memset(k->word, 0, sizeof k->word);
    k->rx_next = 0;
    k->pending = 0;
    k->busy = 0; /* a frame on the air is no longer heard back */
    k->us = k->frac = 0;
    k->clocked = 0;
    esk_mach_clock(k->s, &k->clocked, 1);
}

static const struct esk_dev_ops wifi_ops = {
    "esp32c3-wifi", mac_read, mac_write, mac_reset, mac_tick, 0,
};

int esk_esp32c3_wifi_create(struct esk_mach *s)
{
    struct c3_wifi *k = (struct c3_wifi *)esk_mach_alloc(s, sizeof *k);
    if (!k)
        return -1;
    memset(k, 0, sizeof *k);
    k->s = s;
    s->radio.ctx = k;
    s->radio.kinds = ESK_RADIO_WIFI;
    s->radio.tuned = tuned;
    s->radio.rx = rx;
    s->radio.tx_done = tx_done;
    if (esk_mach_add_dev(s, MAC_BASE, MAC_SIZE, &wifi_ops, k) != ES_OK)
        return -1;
    mac_reset(k);
    return 0;
}
