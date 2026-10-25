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
 *     The station's TSF is what libpp loads from its access point's beacons
 *     (+0x2010 and +0x2014, then +0x200C bit 4, hal_set_sta_tsf).
 *   - Power save (step 5): the TBTT (hal_set_sta_tbtt: the next target
 *     beacon time's low 26 bits at +0x2020, taken by +0x200C bit 8; the
 *     beacon interval in TU and the interrupt's ahead time in us at +0x2030;
 *     on by +0x2028 bit 26) raises the power interrupt's bit 4 its ahead
 *     time before each target beacon time, and four TSF timers (control
 *     +0x205C + 8i, bit 31 on; the TSF target +0x2060 + 8i) raise bit 8 - i
 *     once; the power interrupt's enable, status and clear (+0x2110,
 *     +0x2118, +0x211C) drive source 2 (WIFI_PWR). Between beacons ESP-IDF
 *     stops the Wi-Fi/BT common clock (SYSCON WIFI_CLK_EN, 0x78078F) as it
 *     powers the PHY down, and the radio hears nothing until it is on again:
 *     a station in power save misses what its access point does not buffer,
 *     and draws its sleeping current.
 *   - hal_init's request (+0xD14 bit 1) reads acknowledged (bit 0).
 *   - +0x207C (hal_random) is a new random number at each read, which
 *     net80211 uses for ESP-NOW's and the scans' nonces (it waits for one
 *     that differs from the last): a generator seeded by the chip's own
 *     MAC, as apbctrl.c's RND_DATA is, in place of the radio's noise.
 *   - The channel is the RF PLL's: the analog I2C block's frequency code
 *     (0x6000E150 bits 26:20) is the MHz above 2400; with a 40 MHz
 *     channel the PLL sits at its centre and +0x400 bits 1:0 (rom_mac_tx_
 *     chan_offset) say where the 20 MHz primary is: 3 below it (the
 *     secondary above, HT40+), 1 above it (HT40-), 0 none. The frames are
 *     sent and heard on the primary channel.
 *   - CCMP (hal_crypto_set_key_entry, hal_crypto_enable): 40-byte key
 *     entries from +0x1400 (the peer's MAC, a word whose bits 20:18 are the
 *     cipher, 3 for CCMP, and 31:30 its kind, 0 pairwise and 1 group; the
 *     key), valid by their bit in +0x814; an interface's crypto on by bit 0
 *     of +0x800 (+0x804). A frame queued with the Protected bit and the
 *     PLCP word's bits 21:17 naming a key entry (a station's pairwise key
 *     at 4; an access point's group key at 2, its stations' from 8) leaves
 *     encrypted under that entry's key: the driver wrote the CCMP header
 *     (its PN and key ID) and left the MIC's 8 bytes. A protected data frame
 *     received (and a management one, an ESP-NOW action frame, with bit 17
 *     of the interface's +0x800) is decrypted in place under a key of its
 *     transmitter, its CCMP header and MIC kept
 *     for net80211's ccmp_decap, which checks the PN and strips them; one
 *     no key opens is answered and dropped.
 * The rest of its registers keep what is written.
 * Not yet: retransmissions (a frame without its ACK is not sent again by
 * the hardware), contention (EDCA backoff, CCA: a frame goes out when the
 * radio is free), the TSF timers' wake-up of a chip in light sleep (bit 30:
 * the chip does not sleep lightly yet), TKIP, WEP, GCMP
 * and protected management frames, the key entries' other fields (their
 * key ID and index bits are not used: every key of the peer is tried),
 * aggregation (A-MPDU), the transmit power the PHY sets (20 dBm, the chip's
 * maximum, is used), the receive filters for promiscuous mode and the BSSID
 * filter (+0x00 to +0x2C: frames of other networks reach the driver, which
 * drops them). */
#include <string.h>

#include "mcu/esp32c3/esp32c3.h"
#include "util/wcrypto.h"

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
#define CRYPTO_CONF   0x800u /* +4 per interface (hal_crypto_enable) */
#define KEY_VALID     0x814u /* a bit per key entry */
#define KEY_ENTRY(n)  (0x1400u + 40u * (u32)(n))
#define NKEYS         25
#define MAC_TIME      0x2000u
#define TSF_LATCH     0x200Cu
#define TSF_LO        0x2018u
#define TSF_HI        0x201Cu
#define MAC_RANDOM    0x207Cu
/* the station's TSF and its timers (hal_tsf.o): a TSF loaded from +0x2010
 * and +0x2014 by +0x200C bit 4; the next TBTT's low 26 bits at +0x2020,
 * taken by +0x200C bit 8; the beacon interval in TU (bits 15:0) and how
 * long before the TBTT its interrupt comes in us (bits 31:16) at +0x2030;
 * TBTT on by +0x2028 bit 26; four TSF timers (control +0x205C + 8i, bit 31
 * on; their TSF target +0x2060 + 8i); the power interrupt's enable, status
 * and clear (+0x2110, +0x2118, +0x211C: bit 4 the TBTT, bit 8 - i timer i)
 * on interrupt source 2 (WIFI_PWR) */
#define TSF_SET_LO    0x2010u
#define TSF_SET_HI    0x2014u
#define TBTT_TGT      0x2020u
#define TSF_CTRL      0x2028u
#define TBTT_INTV     0x2030u
#define TSF_TMR(i)    (0x205Cu + 8u * (u32)(i))
#define TSF_TGT(i)    (0x2060u + 8u * (u32)(i))
#define PWR_ENA       0x2110u
#define PWR_ST        0x2118u
#define PWR_CLR       0x211Cu
#define PWR_TBTT      0x10u
#define PWR_TMR(i)    (0x100u >> (i))

#define NQUEUES       5
#define INT_RX        (1u << 24)
#define INT_TX        (1u << 7)
#define RFPLL_CODE    0x6000E150u
#define CHAN_OFFSET   0x400u
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
    u64 rng;         /* hal_random's generator, 0 until seeded */
    long long sta_off; /* the station's TSF less the microseconds */
    u64 tbtt;          /* the station's next TBTT, its TSF */
    int tbtt_set;
    u8 frame[MAX_FRAME];
    u8 scratch[MAX_FRAME];
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

static void pwr_irq(struct c3_wifi *k)
{
    esk_esp32c3_source(k->s, 2, (*reg(k, PWR_ST) & *reg(k, PWR_ENA)) != 0);
}

static u64 sta_tsf(const struct c3_wifi *k) { return (u64)((long long)k->us + k->sta_off); }

/* the TBTT's interrupt (its ahead time before it) and the TSF timers'
 * targets come: their status bits are set; the TBTT moves on a beacon
 * interval, a timer is done until set again */
static void tsf_events(struct c3_wifi *k)
{
    u64 t = sta_tsf(k), intv = (u64)(*reg(k, TBTT_INTV) & 0xFFFFu) * 1024u;
    u32 ahead = *reg(k, TBTT_INTV) >> 16, before = *reg(k, PWR_ST);
    int i;
    if (k->tbtt_set && (*reg(k, TSF_CTRL) & (1u << 26)) && intv && t + ahead >= k->tbtt) {
        *reg(k, PWR_ST) |= PWR_TBTT;
        while (t + ahead >= k->tbtt)
            k->tbtt += intv;
    }
    for (i = 0; i < 4; i++)
        if ((*reg(k, TSF_TMR(i)) & 0x80000000u) && (int)((u32)t - *reg(k, TSF_TGT(i))) >= 0) {
            *reg(k, PWR_ST) |= PWR_TMR(i);
            *reg(k, TSF_TMR(i)) &= ~0x80000000u;
        }
    if (*reg(k, PWR_ST) != before)
        pwr_irq(k);
}

/* the microseconds until the next of them, or 0 for none */
static int tsf_next_us(const struct c3_wifi *k, u64 *us)
{
    u64 t = sta_tsf(k), best = 0, intv = (u64)(*reg((struct c3_wifi *)k, TBTT_INTV) & 0xFFFFu) * 1024u;
    u32 ahead = *reg((struct c3_wifi *)k, TBTT_INTV) >> 16;
    int any = 0, i;
    if (k->tbtt_set && (*reg((struct c3_wifi *)k, TSF_CTRL) & (1u << 26)) && intv) {
        best = k->tbtt > t + ahead ? k->tbtt - ahead - t : 0;
        any = 1;
    }
    for (i = 0; i < 4; i++)
        if (*reg((struct c3_wifi *)k, TSF_TMR(i)) & 0x80000000u) {
            int d = (int)(*reg((struct c3_wifi *)k, TSF_TGT(i)) - (u32)t);
            u64 at = d > 0 ? (u64)d : 0;
            if (!any || at < best)
                best = at;
            any = 1;
        }
    *us = best;
    return any;
}

/* the 2.4 GHz channel's centre frequency the RF PLL is on, 0 before the
 * PHY set it */
static u32 tuned_mhz(struct c3_wifi *k)
{
    u32 v = 0, code;
    (void)esk_bus_debug_read(&k->s->bus, RFPLL_CODE, 4, &v);
    code = (v >> 20) & 0x7Fu;
    if (!code)
        return 0u;
    switch (*reg(k, CHAN_OFFSET) & 3u) {
    case 3u: /* HT40+: the primary 10 MHz below the centre */
        return 2400u + code - 10u;
    case 1u: /* HT40- */
        return 2400u + code + 10u;
    default:
        return 2400u + code;
    }
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

/* ---- the key table and CCMP --------------------------------------------------- */

/* key entry `n` (hal_crypto_set_key_entry): valid, CCMP (bits 20:18 of its
 * second word 3), for the peer `mac`; its 16-byte key */
static int key_entry(struct c3_wifi *k, int n, const u8 *mac, u8 tk[16])
{
    u32 w0 = *reg(k, KEY_ENTRY(n)), w1 = *reg(k, KEY_ENTRY(n) + 4u);
    int i;
    if (!((*reg(k, KEY_VALID) >> n) & 1u) || ((w1 >> 18) & 7u) != 3u)
        return 0;
    if (mac[0] != (u8)w0 || mac[1] != (u8)(w0 >> 8) || mac[2] != (u8)(w0 >> 16) || mac[3] != (u8)(w0 >> 24) ||
        mac[4] != (u8)w1 || mac[5] != (u8)(w1 >> 8))
        return 0;
    for (i = 0; i < 16; i++)
        tk[i] = (u8)(*reg(k, KEY_ENTRY(n) + 8u + 4u * (u32)(i / 4)) >> (8 * (i % 4)));
    return 1;
}

/* the entry's kind (bits 31:30 of its second word): 0 a pairwise key, 1 a
 * group one (the station puts its access point's group key at entry 0, the
 * pairwise one at entry 4) */
static int key_pairwise(struct c3_wifi *k, int n) { return ((*reg(k, KEY_ENTRY(n) + 4u) >> 30) & 3u) == 0u; }

/* a frame queued with the PLCP word's key entry: the payload between the
 * CCMP header the driver wrote (its PN, its key ID) and the MIC's 8 bytes
 * encrypted under that entry's key (a station's pairwise key at 4, an
 * access point's group key at 1-3 and its stations' from 8); an entry not
 * valid, the key for the receiver (a pairwise one to a station or an access
 * point, a group one to a group) */
static void tx_encrypt(struct c3_wifi *k, u32 len, int entry)
{
    u8 *f = k->frame, tk[16];
    size_t hl = esk_80211_header_len(f, len);
    const u8 *ra = f + 4;
    uint64_t pn;
    int n, pass;
    if (!hl || len < hl + 16u)
        return;
    pn = (uint64_t)f[hl] | (uint64_t)f[hl + 1] << 8 | (uint64_t)f[hl + 4] << 16 | (uint64_t)f[hl + 5] << 24 |
         (uint64_t)f[hl + 6] << 32 | (uint64_t)f[hl + 7] << 40;
    if (entry < NKEYS && ((*reg(k, KEY_VALID) >> entry) & 1u) && ((*reg(k, KEY_ENTRY(entry) + 4u) >> 18) & 7u) == 3u) {
        int i;
        size_t out;
        for (i = 0; i < 16; i++)
            tk[i] = (u8)(*reg(k, KEY_ENTRY(entry) + 8u + 4u * (u32)(i / 4)) >> (8 * (i % 4)));
        out = esk_ccmp_encrypt(tk, f, hl, pn, f[hl + 3] >> 6, f + hl + 8, len - hl - 16u, k->scratch);
        memcpy(f + hl, k->scratch, out);
        return;
    }
    for (pass = 0; pass < 2; pass++) /* the kind the receiver asks for first */
        for (n = 0; n < NKEYS; n++) {
            int want = !(ra[0] & 1u);
            if (key_pairwise(k, n) != (pass ? !want : want))
                continue;
            if (key_entry(k, n, (ra[0] & 1u) ? f + 10 : ra, tk) || key_entry(k, n, f + 16, tk)) {
                size_t out = esk_ccmp_encrypt(tk, f, hl, pn, f[hl + 3] >> 6, f + hl + 8, len - hl - 16u,
                                              k->scratch);
                memcpy(f + hl, k->scratch, out);
                return;
            }
        }
}

/* a protected frame received decrypted in place, its CCMP header and MIC
 * kept (net80211's ccmp_decap checks the PN and strips them): 1, or 0 when
 * no key of its transmitter opens it */
static int rx_decrypt(struct c3_wifi *k, const u8 *f, u32 len, u8 *out)
{
    size_t hl = esk_80211_header_len(f, len);
    int n;
    if (!hl || len < hl + 16u)
        return 0;
    for (n = 0; n < NKEYS; n++) {
        u8 tk[16];
        long pl;
        if (!key_entry(k, n, f + 10, tk))
            continue;
        pl = esk_ccmp_decrypt(tk, f, hl, f + hl, len - hl, out + hl + 8, NULL);
        if (pl >= 0) {
            memcpy(out, f, hl + 8);
            memcpy(out + hl + 8 + pl, f + len - 8, 8);
            return 1;
        }
    }
    return 0;
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
    if ((k->frame[1] & 0x40u) && ((plcp >> 17) & 31u))
        tx_encrypt(k, len, (int)((plcp >> 17) & 31u));
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

/* the Wi-Fi and Bluetooth common clock (SYSCON's WIFI_CLK_EN, the
 * WIFI_BT_COMMON bits) runs: ESP-IDF stops it as it powers the PHY down
 * (esp_phy_disable: modem sleep between beacons), and the radio hears
 * nothing then */
static int common_clock(struct c3_wifi *k)
{
    u32 v = 0x78078Fu;
    (void)esk_bus_debug_read(&k->s->bus, 0x60026014u, 4, &v);
    return (v & 0x78078Fu) != 0;
}

static u32 tuned(void *ctx, int kind)
{
    struct c3_wifi *k = (struct c3_wifi *)ctx;
    if (kind != ESK_RADIO_WIFI || !(*reg(k, RX_CTRL) & 0x80000000u) || !common_clock(k))
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
    if ((frame[1] & 0x40u) && ((frame[0] >> 2) & 3u) != 1u) {
        /* CCMP, when the interface's crypto is on: bit 0 for data frames,
         * bit 17 for management ones (hal_crypto_mgmt_rx_enabled) */
        u32 on = ((frame[0] >> 2) & 3u) == 2u ? 1u : 1u << 17;
        if (!(*reg(k, CRYPTO_CONF) & on) && !(*reg(k, CRYPTO_CONF + 4u) & on)) {
            if (on == 1u)
                return !(frame[4] & 1u);
            goto take; /* as it came, for the driver */
        }
        if (!rx_decrypt(k, frame, len, k->scratch))
            return !(frame[4] & 1u); /* answered, then dropped: no key opens it */
        frame = k->scratch;
    }
take:
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
    if (w == MAC_RANDOM) { /* xorshift64*, this chip's own sequence */
        if (!k->rng) {
            int i;
            k->rng = 0x6D61635F726E6731ull;
            if (k->s->has_factory_mac)
                for (i = 0; i < 6; i++)
                    k->rng = (k->rng ^ k->s->factory_mac[i]) * 0x100000001B3ull;
            if (!k->rng)
                k->rng = 1;
        }
        k->rng ^= k->rng >> 12;
        k->rng ^= k->rng << 25;
        k->rng ^= k->rng >> 27;
        *reg(k, w) = (u32)((k->rng * 0x2545F4914F6CDD1Dull) >> 32);
    }
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
    } else if (w == TSF_LATCH) {
        if (*r & 0x10u) /* hal_set_sta_tsf */
            k->sta_off = (long long)((u64)*reg(k, TSF_SET_HI) << 32 | *reg(k, TSF_SET_LO)) - (long long)k->us;
        if (*r & 0x100u) { /* hal_set_sta_tbtt: the next TBTT's low 26 bits, near the TSF now */
            u64 t = sta_tsf(k), c = (t & ~(u64)0x3FFFFFFu) | (*reg(k, TBTT_TGT) & 0x3FFFFFFu);
            if (c + 0x2000000u < t)
                c += 0x4000000u;
            else if (c > t + 0x2000000u && c >= 0x4000000u)
                c -= 0x4000000u;
            k->tbtt = c;
            k->tbtt_set = 1;
        }
        *r &= ~0x110u; /* commands, done */
        if (*r & 1u) {
            u64 t = sta_tsf(k);
            *reg(k, TSF_LO) = (u32)t;
            *reg(k, TSF_HI) = (u32)(t >> 32);
        } else if (*r & 2u) {
            *reg(k, TSF_LO) = (u32)k->us;
            *reg(k, TSF_HI) = (u32)(k->us >> 32);
        }
        tsf_events(k);
    } else if (w == PWR_CLR) {
        *reg(k, PWR_ST) &= ~*r;
        *r = 0;
        pwr_irq(k);
    } else if (w == PWR_ENA || w == TSF_CTRL || w == TBTT_INTV || (w >= TSF_TMR(0) && w <= TSF_TGT(3))) {
        tsf_events(k);
        pwr_irq(k);
    } else if (w >= TXQ(NQUEUES - 1u) && w <= TXQ(0) && ((TXQ(0) - w) & 7u) == 0 && (v & 0xC0000000u)) {
        k->pending |= 1u << ((TXQ(0) - w) / 8u);
        tx_next(k);
    }
}

static void mac_tick(void *ctx, u32 cycles)
{
    struct c3_wifi *k = (struct c3_wifi *)ctx;
    sync_time(k, cycles);
    if (k->tbtt_set || (*reg(k, TSF_TMR(0)) | *reg(k, TSF_TMR(1)) | *reg(k, TSF_TMR(2)) | *reg(k, TSF_TMR(3))) & 0x80000000u)
        tsf_events(k);
}

/* the cycles until the TBTT's interrupt or a TSF timer's */
static int mac_next(void *ctx, u32 *cycles)
{
    struct c3_wifi *k = (struct c3_wifi *)ctx;
    u32 hz = esk_mach_core_hz(k->s);
    u64 us, c;
    if (!hz || !tsf_next_us(k, &us))
        return 0;
    c = us * hz > k->frac ? (us * hz - k->frac + 999999u) / 1000000u : 1u;
    *cycles = c == 0 ? 1u : c > 0xffffffffu ? 0xffffffffu : (u32)c;
    return 1;
}

static void mac_reset(void *ctx)
{
    struct c3_wifi *k = (struct c3_wifi *)ctx;
    memset(k->word, 0, sizeof k->word);
    k->rx_next = 0;
    k->pending = 0;
    k->busy = 0; /* a frame on the air is no longer heard back */
    k->us = k->frac = 0;
    k->sta_off = 0;
    k->tbtt = 0;
    k->tbtt_set = 0;
    k->clocked = 0;
    esk_mach_clock(k->s, &k->clocked, 1);
}

static const struct esk_dev_ops wifi_ops = {
    "esp32c3-wifi", mac_read, mac_write, mac_reset, mac_tick, mac_next,
};

int esk_esp32c3_wifi_create(struct esk_mach *s)
{
    struct c3_wifi *k = (struct c3_wifi *)esk_mach_alloc(s, sizeof *k);
    if (!k)
        return -1;
    memset(k, 0, sizeof *k);
    k->s = s;
    {
        struct esk_radio_port part;
        memset(&part, 0, sizeof part);
        part.ctx = k;
        part.kinds = ESK_RADIO_WIFI;
        part.tuned = tuned;
        part.rx = rx;
        part.tx_done = tx_done;
        if (esk_esp32c3_radio_join(s, ESK_RADIO_WIFI, &part) != 0)
            return -1;
    }
    if (esk_mach_add_dev(s, MAC_BASE, MAC_SIZE, &wifi_ops, k) != ES_OK)
        return -1;
    mac_reset(k);
    return 0;
}
