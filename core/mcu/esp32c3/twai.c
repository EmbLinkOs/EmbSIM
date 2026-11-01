/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* twai.c -- the ESP32-C3's Two-Wire Automotive Interface (TWAI0 at
 * 0x6002B000, TRM v1.4 chapter 22): a CAN 2.0 controller compatible with
 * the SJA1000 in PeliCAN mode, on the twai_tx and twai_rx signals of the
 * GPIO matrix (74) and matrix source 25. A transceiver on the bench, or a
 * jumper from TX to RX as ESP-IDF's twai_self_test allows, closes the bus.
 *
 *   - Bit timing: a time quantum is 2 x (BRP + 1) APB_CLK cycles; a bit is
 *     1 + (TSEG1 + 1) + (TSEG2 + 1) quanta, sampled after the first two
 *     segments. The controller drives twai_tx at each bit's start and
 *     samples twai_rx at its sample point; from idle it hard-synchronises
 *     on the falling edge of a start of frame.
 *   - MODE: RM (out of reset 1) holds the controller off the bus; leaving it
 *     the controller integrates (11 recessive bits) before it may send or
 *     receive. LOM listens without acknowledging or sending; STM (self
 *     test) takes a frame no node acknowledges as sent. AFM selects the
 *     single or dual acceptance filter.
 *   - Frames, base and extended, data and remote: bit stuffing over the
 *     start of frame to the CRC, CRC-15, the delimiters, the ACK slot and
 *     the end of frame. A transmitter that reads dominant where it sent
 *     recessive in the arbitration field loses arbitration there (ALC,
 *     ALI) and goes on as a receiver.
 *   - CMD: TR and SRR send the TX buffer's frame (SRR also receives it, if
 *     the filter passes it); AT with them sends it once only; AT alone
 *     aborts a frame not yet started; RRB releases the receive buffer's
 *     frame; CDO clears the overrun.
 *   - The 0x40-0x70 window: in reset mode the acceptance code and mask
 *     (ACR0-3, AMR0-3); in operating mode the TX buffer when written and
 *     the receive FIFO's oldest frame when read (frame information, the
 *     identifier, the data). The FIFO holds 64 bytes, a base frame taking
 *     3 + n, an extended one 5 + n; a frame that does not fit sets DOS and
 *     DOI. RX_MESSAGE_CNT counts the frames held.
 *   - Errors: bit, stuff, CRC, form and acknowledgement errors start an
 *     error frame (six dominant bits while error active, six recessive
 *     while error passive, then the eight-bit delimiter); ERR_CODE_CAP
 *     captures the first since it was read (its type, direction and
 *     segment) and raises BEI. The counters follow CAN 2.0's rules: a
 *     transmitter's error adds 8, a receiver's 1, a success takes 1 away;
 *     over 127 a node is error passive (EPI as it crosses), and a
 *     transmitter over 255 goes bus off: BS, RM set, TX_ERR_CNT 128, EI.
 *     Leaving reset mode then, it waits for 128 times 11 recessive bits and
 *     comes back error active with its counters at 0, EI again. ES is set
 *     while a counter reaches ERR_WARNING_LIMIT. A frame not sent is sent
 *     again, after the eight-bit suspension of an error-passive node.
 *   - STATUS: RBS, DOS, TBS, TCS, RS, TS, ES, BS. INT_RAW reads RI (set
 *     while the FIFO holds a frame), TI, EI, DOI, EPI, ALI, BEI, and the
 *     read clears all but RI; the interrupt is INT_RAW and INT_ENA.
 * Where it differs inside: the bus's clocks are exact, so after the hard
 * synchronisation no resynchronisation (SJW) is needed; a receiver takes a
 * frame at the end of its end of frame, not at its sixth bit. Not yet:
 * overload frames, triple sampling (SAM), the clock output (CLOCK_DIVIDER,
 * twai_clkout), twai_bus_off_on, STATUS's MS, light-sleep. */
#include <stdio.h>
#include <string.h>

#include "mcu/esp32c3/esp32c3.h"

#define SOURCE 25
#define SIG_TWAI 74 /* twai_tx out, twai_rx in */
#define FIFO_SIZE 64u
#define MAXBITS 200

#define MOD_RM 1u
#define MOD_LOM 2u
#define MOD_STM 4u
#define MOD_AFM 8u

#define SR_RBS 0x01u
#define SR_DOS 0x02u
#define SR_TBS 0x04u
#define SR_TCS 0x08u
#define SR_RS 0x10u
#define SR_TS 0x20u
#define SR_ES 0x40u
#define SR_BS 0x80u

#define IR_RI 0x01u
#define IR_TI 0x02u
#define IR_EI 0x04u
#define IR_DOI 0x08u
#define IR_EPI 0x20u
#define IR_ALI 0x40u
#define IR_BEI 0x80u

/* ECC's error types and segments (SJA1000 data sheet, table 22) */
#define EC_BIT 0u
#define EC_FORM 1u
#define EC_STUFF 2u
#define EC_OTHER 3u
#define SEG_SOF 0x03u
#define SEG_ID28 0x02u
#define SEG_ID20 0x06u
#define SEG_SRTR 0x04u
#define SEG_IDE 0x05u
#define SEG_ID17 0x07u
#define SEG_ID12 0x0Fu
#define SEG_ID4 0x0Eu
#define SEG_RTR 0x0Cu
#define SEG_RES 0x0Du
#define SEG_DLC 0x0Bu
#define SEG_DATA 0x0Au
#define SEG_CRC 0x08u
#define SEG_CRCDEL 0x18u
#define SEG_ACK 0x19u
#define SEG_ACKDEL 0x1Bu
#define SEG_EOF 0x1Au
#define SEG_ERRFLAG 0x12u /* active error flag */
#define SEG_ERRDEL 0x17u
#define SEG_INTERM 0x13u

enum st { OFF, INTEGRATE, IDLE, FRAME, ERRFLAG, ERRWAIT, ERRDEL, INTERM, SUSPEND, BUSOFF };

struct c3_twai {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *mode, *cmd, *status, *intr, *ena, *btr0, *btr1, *alc, *ecc, *ewl, *rec, *tec, *data[13], *rmc,
        *clkdiv;
    u8 acr[4], amr[4];
    u8 txbuf[13];
    u8 fifo[FIFO_SIZE];
    u32 fifo_len, nframes;
    u32 ir;          /* the latched interrupts but RI */
    u32 sr;          /* DOS, TBS, TCS, BS kept here; the rest computed */
    int alc_armed, ecc_armed;
    int clocked, level;
    /* the bit engine */
    enum st st;
    u64 next;        /* the next event, in core cycles */
    int at_sample;   /* the next event is a sample point (else a bit start) */
    int tx_level;    /* driven on twai_tx */
    int rx_last;
    u32 count;       /* bits in the current state */
    u32 recovery;    /* bus-off recovery: occurrences of 11 recessive bits */
    int passive_tx_suspend;
    /* the frame on the bus as this node decodes it */
    u8 bits[MAXBITS]; /* destuffed, SOF on */
    u32 nbits, need;  /* bits so far; the destuffed bits up to the CRC's end */
    int run, last;    /* stuffing: equal bits in a row */
    u32 tail;         /* bits after the CRC (delimiter on) */
    int crc_ok;
    /* the frame this node sends */
    int txpend, txing, single, self;
    u8 txq[13];
    u8 txbits[MAXBITS + 16];
    u32 ntxbits, txpos;   /* stuffed bits to the CRC's end, the next to drive */
    u32 arb_end;          /* destuffed index of the arbitration field's last bit */
};

static u32 reg(const struct esk_rf_reg *r) { return (u32)r->value; }

static u32 tq_cycles(const struct c3_twai *k)
{
    u32 apb = esk_esp32c3_apb_hz(k->s), core = esk_mach_core_hz(k->s);
    u64 q = 2ull * ((reg(k->btr0) & 0x1fffu) + 1u);
    if (!apb || !core)
        return 0;
    q = q * core / apb;
    return q ? (u32)q : 1u;
}
static u32 seg1(const struct c3_twai *k) { return 1u + (reg(k->btr1) & 0xfu) + 1u; } /* sync + tseg1 */
static u32 seg2(const struct c3_twai *k) { return ((reg(k->btr1) >> 4) & 7u) + 1u; }

static int tec(const struct c3_twai *k) { return (int)(reg(k->tec) & 0xffu); }
static int rec(const struct c3_twai *k) { return (int)(reg(k->rec) & 0xffu); }
static int passive(const struct c3_twai *k) { return tec(k) > 127 || rec(k) > 127; }

static void update(struct c3_twai *k)
{
    u32 st = k->ir | (k->nframes ? IR_RI : 0u), sr = k->sr & (SR_DOS | SR_TBS | SR_TCS | SR_BS);
    int level = (st & reg(k->ena) & 0xffu) != 0;
    u32 ewl = reg(k->ewl) & 0xffu;
    if (k->nframes)
        sr |= SR_RBS;
    if (k->st == FRAME && !k->txing)
        sr |= SR_RS;
    if (k->st == FRAME && k->txing)
        sr |= SR_TS;
    if ((u32)tec(k) >= ewl || (u32)rec(k) >= ewl)
        sr |= SR_ES;
    esk_rf_hw(k->status, sr);
    esk_rf_hw(k->intr, st);
    esk_rf_hw(k->rmc, k->nframes);
    if (level != k->level) {
        k->level = level;
        esk_esp32c3_source(k->s, SOURCE, level);
    }
}

static void set_counters(struct c3_twai *k, int t, int r)
{
    int was_passive = passive(k), was_es, now_es;
    u32 ewl = reg(k->ewl) & 0xffu;
    was_es = (u32)tec(k) >= ewl || (u32)rec(k) >= ewl;
    if (t < 0) t = 0;
    if (r < 0) r = 0;
    if (r > 255) r = 255;
    esk_rf_hw(k->tec, (u32)(t > 255 ? 255 : t));
    esk_rf_hw(k->rec, (u32)r);
    now_es = (u32)tec(k) >= ewl || (u32)rec(k) >= ewl;
    if (was_passive != passive(k))
        k->ir |= IR_EPI;
    if (was_es != now_es)
        k->ir |= IR_EI;
}

static void drive(struct c3_twai *k, int level)
{
    if (level != k->tx_level) {
        k->tx_level = level;
        esk_esp32c3_signal(k->s, SIG_TWAI, level, 1);
    }
}

/* ---- frames ---------------------------------------------------------------------------- */

static u32 crc15(const u8 *b, u32 n)
{
    u32 crc = 0, i;
    for (i = 0; i < n; i++) {
        u32 nxt = (b[i] ^ (crc >> 14)) & 1u;
        crc = (crc << 1) & 0x7fffu;
        if (nxt)
            crc ^= 0x4599u;
    }
    return crc;
}

static u32 dlc_bytes(u32 dlc) { return dlc > 8u ? 8u : dlc; }

/* a buffer's frame (13 bytes) as destuffed bits, SOF to the CRC's end;
 * the arbitration field's last bit index */
static u32 frame_bits(const u8 *f, u8 *b, u32 *arb_end)
{
    u32 n = 0, i, id, dlc = f[0] & 0xfu, rtr = (f[0] >> 6) & 1u, ext = (f[0] >> 7) & 1u, nd, crc;
    const u8 *d;
    b[n++] = 0; /* SOF */
    if (ext) {
        id = (u32)f[1] << 21 | (u32)f[2] << 13 | (u32)f[3] << 5 | (u32)f[4] >> 3;
        for (i = 0; i < 11; i++) b[n++] = (u8)((id >> (28 - i)) & 1u);
        b[n++] = 1; /* SRR */
        b[n++] = 1; /* IDE */
        for (i = 0; i < 18; i++) b[n++] = (u8)((id >> (17 - i)) & 1u);
        b[n++] = (u8)rtr;
        *arb_end = n - 1;
        b[n++] = 0; /* r1 */
        b[n++] = 0; /* r0 */
        d = f + 5;
    } else {
        id = (u32)f[1] << 3 | (u32)f[2] >> 5;
        for (i = 0; i < 11; i++) b[n++] = (u8)((id >> (10 - i)) & 1u);
        b[n++] = (u8)rtr;
        *arb_end = n - 1;
        b[n++] = 0; /* IDE */
        b[n++] = 0; /* r0 */
        d = f + 3;
    }
    for (i = 0; i < 4; i++) b[n++] = (u8)((dlc >> (3 - i)) & 1u);
    nd = rtr ? 0u : dlc_bytes(dlc);
    for (i = 0; i < nd * 8u; i++) b[n++] = (u8)(((u32)d[i / 8u] >> (7u - i % 8u)) & 1u);
    crc = crc15(b, n);
    for (i = 0; i < 15; i++) b[n++] = (u8)((crc >> (14 - i)) & 1u);
    return n;
}

static void stage_tx(struct c3_twai *k)
{
    u8 b[MAXBITS];
    u32 n = frame_bits(k->txq, b, &k->arb_end), i, run = 0;
    int last = -1;
    k->ntxbits = 0;
    for (i = 0; i < n; i++) {
        k->txbits[k->ntxbits++] = b[i];
        run = (int)b[i] == last ? run + 1 : 1;
        last = b[i];
        if (run == 5) { /* the stuff bit, after the CRC's last bit too */
            k->txbits[k->ntxbits++] = (u8)!b[i];
            last = !b[i];
            run = 1;
        }
    }
}

/* the decoded frame's fields from its destuffed bits; 0 while incomplete */
static u32 field(const u8 *b, u32 from, u32 n)
{
    u32 v = 0, i;
    for (i = 0; i < n; i++)
        v = v << 1 | b[from + i];
    return v;
}

/* the destuffed bits a frame needs once its header is in: 0 until known */
static u32 bits_needed(const struct c3_twai *k)
{
    const u8 *b = k->bits;
    u32 n = k->nbits, ext, rtr, dlc, base;
    if (n < 14)
        return 0;
    ext = b[13];
    base = ext ? 39u : 19u; /* bits before the data */
    if (n < base)
        return 0;
    rtr = ext ? b[32] : b[12];
    dlc = field(b, base - 4, 4);
    return base + (rtr ? 0u : dlc_bytes(dlc) * 8u) + 15u;
}

/* the decoded frame in buffer layout (13 bytes) */
static void frame_bytes(const struct c3_twai *k, u8 *f)
{
    const u8 *b = k->bits;
    u32 ext = b[13], rtr = ext ? b[32] : b[12], base = ext ? 39u : 19u, dlc = field(b, base - 4, 4), id, i, nd;
    u8 *d;
    memset(f, 0, 13);
    f[0] = (u8)(ext << 7 | rtr << 6 | dlc);
    if (ext) {
        id = field(b, 1, 11) << 18 | field(b, 14, 18);
        f[1] = (u8)(id >> 21);
        f[2] = (u8)(id >> 13);
        f[3] = (u8)(id >> 5);
        f[4] = (u8)(id << 3);
        d = f + 5;
    } else {
        id = field(b, 1, 11);
        f[1] = (u8)(id >> 3);
        f[2] = (u8)(id << 5 | rtr << 4);
        d = f + 3;
    }
    nd = rtr ? 0u : dlc_bytes(dlc);
    for (i = 0; i < nd; i++)
        d[i] = (u8)field(b, base + 8u * i, 8);
}

/* the acceptance filter (SJA1000 data sheet 6.4.15) */
static int accepted(const struct c3_twai *k, const u8 *f)
{
    u32 ext = f[0] >> 7, rtr = (f[0] >> 6) & 1u, nd = rtr ? 0u : dlc_bytes(f[0] & 0xfu);
    u8 c[4], m[4];
    int i;
    for (i = 0; i < 4; i++) {
        c[i] = k->acr[i];
        m[i] = k->amr[i];
    }
#define OK(x, a, mm) ((((x) ^ (a)) & (u8) ~(mm)) == 0)
    if (reg(k->mode) & MOD_AFM) { /* single filter */
        if (ext)
            return OK(f[1], c[0], m[0]) && OK(f[2], c[1], m[1]) && OK(f[3], c[2], m[2]) &&
                   OK((u8)(f[4] | rtr << 2), c[3], (u8)(m[3] | 3u));
        return OK(f[1], c[0], m[0]) && OK((u8)(f[2] | rtr << 4), c[1], (u8)(m[1] | 0x0fu)) &&
               (nd < 1 || OK(f[3], c[2], m[2])) && (nd < 2 || OK(f[4], c[3], m[3]));
    }
    if (ext) /* dual: ID28-13 against either filter */
        return (OK(f[1], c[0], m[0]) && OK(f[2], c[1], m[1])) || (OK(f[1], c[2], m[2]) && OK(f[2], c[3], m[3]));
    {
        u8 hi = (u8)(f[2] | rtr << 4);
        int one = OK(f[1], c[0], m[0]) && OK(hi, c[1], (u8)(m[1] | 0x0fu)) &&
                  (nd < 1 || (OK((u8)(f[3] >> 4), (u8)(c[1] & 0x0fu), (u8)(m[1] & 0x0fu)) &&
                              OK((u8)(f[3] & 0x0fu), (u8)(c[3] & 0x0fu), (u8)(m[3] & 0x0fu))));
        int two = OK(f[1], c[2], m[2]) && OK(hi, (u8)(c[3] & 0xf0u), (u8)(m[3] | 0x0fu));
        return one || two;
    }
#undef OK
}

static void store(struct c3_twai *k, const u8 *f)
{
    u32 size = ((f[0] & 0x80u) ? 5u : 3u) + (((f[0] >> 6) & 1u) ? 0u : dlc_bytes(f[0] & 0xfu));
    if (!accepted(k, f))
        return;
    if (k->fifo_len + size > FIFO_SIZE) {
        k->sr |= SR_DOS;
        k->ir |= IR_DOI;
        return;
    }
    memcpy(k->fifo + k->fifo_len, f, size);
    k->fifo_len += size;
    k->nframes++;
}

static u32 head_size(const struct c3_twai *k)
{
    const u8 *f = k->fifo;
    return ((f[0] & 0x80u) ? 5u : 3u) + (((f[0] >> 6) & 1u) ? 0u : dlc_bytes(f[0] & 0xfu));
}

/* ---- the bit engine ---------------------------------------------------------------------- */

static void frame_start(struct c3_twai *k)
{
    k->st = FRAME;
    k->nbits = 0;
    k->need = 0;
    k->run = 0;
    k->last = -1;
    k->tail = 0;
    k->crc_ok = 0;
}

static u32 seg_of(const struct c3_twai *k)
{
    u32 n = k->nbits, ext = n > 13 ? k->bits[13] : 0;
    if (k->need && n >= k->need)
        return SEG_CRC;
    if (n == 0) return SEG_SOF;
    if (n <= 8) return SEG_ID28;
    if (n <= 11) return SEG_ID20;
    if (n == 12) return SEG_SRTR;
    if (n == 13) return SEG_IDE;
    if (ext) {
        if (n <= 18) return SEG_ID17;
        if (n <= 26) return SEG_ID12;
        if (n <= 31) return SEG_ID4;
        if (n == 32) return SEG_RTR;
        if (n <= 34) return SEG_RES;
        if (n <= 38) return SEG_DLC;
    } else {
        if (n == 14) return SEG_RES;
        if (n <= 18) return SEG_DLC;
    }
    return k->need && n + 15 >= k->need ? SEG_CRC : SEG_DATA;
}

static void error(struct c3_twai *k, u32 type, u32 seg)
{
    int t = tec(k), r = rec(k);
    if (k->ecc_armed) {
        esk_rf_hw(k->ecc, type << 6 | (k->txing ? 0u : 1u) << 5 | seg);
        k->ecc_armed = 0;
        k->ir |= IR_BEI;
    }
    if (k->txing)
        t += 8;
    else
        r += 1;
    set_counters(k, t, r);
    k->st = ERRFLAG;
    k->count = 0;
    if (k->txing && t > 255) { /* bus off */
        k->txing = 0;
        k->st = BUSOFF;
        k->sr |= SR_BS;
        k->ir |= IR_EI;
        esk_rf_hw(k->mode, reg(k->mode) | MOD_RM);
        esk_rf_hw(k->tec, 128u);
        k->recovery = 0;
        k->count = 0;
        drive(k, 1);
    }
    k->txing = 0; /* sent again after the error frame, unless single shot */
}

static void tx_done(struct c3_twai *k, int ok)
{
    k->txing = 0;
    k->txpend = 0;
    k->sr |= SR_TBS;
    if (ok)
        k->sr |= SR_TCS;
    k->ir |= IR_TI;
}

static void frame_end(struct c3_twai *k)
{
    u8 f[13];
    frame_bytes(k, f);
    if (k->txing) {
        set_counters(k, tec(k) - 1, rec(k));
        if (k->self)
            store(k, f);
        tx_done(k, 1);
        k->passive_tx_suspend = passive(k);
    } else {
        int r = rec(k);
        set_counters(k, tec(k), r > 127 ? 120 : r - 1);
        store(k, f);
    }
    k->st = INTERM;
    k->count = 0;
}

/* the bit driven at a bit's start */
static int bit_out(struct c3_twai *k)
{
    switch (k->st) {
    case FRAME:
        if (k->txing) {
            if (k->txpos < k->ntxbits)
                return k->txbits[k->txpos];
            return 1; /* delimiters, the ACK slot, EOF */
        }
        /* a receiver acknowledges a good frame in the ACK slot */
        return !(k->tail == 1 && k->crc_ok && !(reg(k->mode) & MOD_LOM));
    case ERRFLAG:
        return passive(k) ? 1 : 0;
    case IDLE:
        if (k->txpend && !(reg(k->mode) & MOD_LOM)) { /* the start of frame */
            frame_start(k);
            k->txing = 1;
            k->txpos = 0;
            return 0;
        }
        return 1;
    default:
        return 1;
    }
}

static void sample(struct c3_twai *k, int rx)
{
    switch (k->st) {
    case OFF:
        return;
    case INTEGRATE:
    case BUSOFF:
        k->count = rx ? k->count + 1 : 0;
        if (k->count >= 11) {
            k->count = 0;
            if (k->st == INTEGRATE) {
                k->st = IDLE;
            } else if (!(reg(k->mode) & MOD_RM) && ++k->recovery >= 128u) {
                k->sr &= ~SR_BS;
                set_counters(k, 0, 0);
                k->ir |= IR_EI;
                k->st = IDLE;
            }
        }
        return;
    case IDLE:
        if (!rx) { /* a start of frame from another node */
            frame_start(k);
            k->bits[k->nbits++] = 0;
            k->run = 1;
            k->last = 0;
        }
        return;
    case SUSPEND:
        if (!rx) {
            frame_start(k);
            k->bits[k->nbits++] = 0;
            k->run = 1;
            k->last = 0;
            return;
        }
        if (++k->count >= 8)
            k->st = IDLE;
        return;
    case INTERM:
        if (++k->count >= 3) {
            k->count = 0;
            k->st = (k->txpend && passive(k) && k->passive_tx_suspend) ? SUSPEND : IDLE;
            k->passive_tx_suspend = 0;
        }
        return;
    case ERRFLAG:
        if (++k->count >= 6) {
            k->st = ERRWAIT;
            k->count = 0;
        }
        return;
    case ERRWAIT: /* other nodes' flags: wait for recessive */
        if (rx) {
            k->st = ERRDEL;
            k->count = 1;
        }
        return;
    case ERRDEL:
        if (!rx) { /* form error in the delimiter */
            error(k, EC_FORM, SEG_ERRDEL);
            return;
        }
        if (++k->count >= 8) {
            k->st = INTERM;
            k->count = 0;
            if (k->txpend) {
                k->passive_tx_suspend = 1;
                if (k->single)
                    tx_done(k, 0);
            }
        }
        return;
    case FRAME:
        break;
    }
    /* in a frame */
    if (k->txing && k->txpos < k->ntxbits) {
        int sent = k->txbits[k->txpos++];
        if (sent != rx) {
            if (sent && !rx && k->nbits <= k->arb_end) { /* lost arbitration */
                if (k->alc_armed) {
                    esk_rf_hw(k->alc, k->nbits);
                    k->alc_armed = 0;
                    k->ir |= IR_ALI;
                }
                k->txing = 0;
            } else {
                error(k, EC_BIT, seg_of(k));
                return;
            }
        }
    }
    if (!k->need || k->nbits < k->need || k->run == 5) { /* the stuffed part, to the CRC's end */
        if (k->run == 5) {
            if (rx == k->last) {
                error(k, EC_STUFF, seg_of(k));
                return;
            }
            k->run = k->need && k->nbits >= k->need ? 0 : 1; /* after the CRC's last bit: the tail next */
            k->last = rx;
            return; /* a stuff bit */
        }
        k->run = rx == k->last ? k->run + 1 : 1;
        k->last = rx;
        if (k->nbits < MAXBITS)
            k->bits[k->nbits++] = (u8)rx;
        if (!k->need)
            k->need = bits_needed(k);
        if (k->need && k->nbits == k->need)
            k->crc_ok = crc15(k->bits, k->need - 15) == field(k->bits, k->need - 15, 15);
        return;
    }
    /* the fixed-form tail: CRC delimiter, ACK slot, ACK delimiter, EOF */
    k->tail++;
    if (k->tail == 1) { /* CRC delimiter */
        if (!rx) {
            error(k, EC_FORM, SEG_CRCDEL);
            return;
        }
        return;
    }
    if (k->tail == 2) { /* ACK slot */
        if (k->txing && rx && !(reg(k->mode) & MOD_STM)) {
            error(k, EC_OTHER, SEG_ACK);
            return;
        }
        return;
    }
    if (k->tail == 3) { /* ACK delimiter; a receiver's CRC error is flagged after it */
        if (!rx) {
            error(k, EC_FORM, SEG_ACKDEL);
            return;
        }
        if (!k->crc_ok && !k->txing)
            error(k, EC_OTHER, SEG_CRC);
        return;
    }
    if (!rx) {
        error(k, EC_FORM, SEG_EOF);
        return;
    }
    if (k->tail == 10) /* the seventh bit of EOF */
        frame_end(k);
}

/* the bit's events: a start then its sample point */
static void event(struct c3_twai *k)
{
    u32 q = tq_cycles(k);
    if (!k->at_sample) {
        if (k->st == FRAME && !k->txing)
            /* the ACK slot follows the CRC delimiter: tail counts sampled bits */
            drive(k, (k->tail == 1 && k->crc_ok && !(reg(k->mode) & MOD_LOM)) ? 0 : 1);
        else
            drive(k, bit_out(k));
        k->at_sample = 1;
        k->next += (u64)q * seg1(k);
    } else {
        sample(k, esk_esp32c3_signal_in(k->s, SIG_TWAI));
        k->at_sample = 0;
        k->next += (u64)q * seg2(k);
    }
    update(k);
}

static int running(const struct c3_twai *k) { return k->st != OFF && tq_cycles(k) != 0; }

static void twai_tick(void *ctx, u32 cycles)
{
    struct c3_twai *k = (struct c3_twai *)ctx;
    struct esk_mach *s = k->s;
    u64 end = s->cycles;
    if (!running(k))
        return;
    while (k->next <= end && running(k)) {
        s->output_lag = (u32)(end - k->next);
        event(k);
    }
    s->output_lag = 0;
    (void)cycles;
}

static int twai_next(void *ctx, u32 *cycles)
{
    struct c3_twai *k = (struct c3_twai *)ctx;
    u64 now = k->s->cycles, d;
    if (!running(k))
        return 0;
    d = k->next > now ? k->next - now : 1u;
    *cycles = d > 0xffffffffu ? 0xffffffffu : (u32)d;
    return 1;
}

/* an edge on twai_rx: a start of frame from idle synchronises the bit */
static void rx_watch(void *ctx)
{
    struct c3_twai *k = (struct c3_twai *)ctx;
    struct esk_mach *s = k->s;
    int level = esk_esp32c3_signal_in(s, SIG_TWAI);
    u64 lag = (u64)s->input_lead + s->output_lag, t = s->cycles > lag ? s->cycles - lag : 0;
    if (level == k->rx_last)
        return;
    k->rx_last = level;
    if (!level && (k->st == IDLE || k->st == SUSPEND) && !k->txpend && running(k)) {
        k->next = t + (u64)tq_cycles(k) * seg1(k); /* hard synchronisation */
        k->at_sample = 1;
    }
}

static void go_off(struct c3_twai *k)
{
    if (k->st == BUSOFF)
        return; /* the recovery waits for RM to clear */
    k->st = OFF;
    k->txing = 0;
    k->txpend = 0; /* reset mode releases the transmit buffer */
    k->sr |= SR_TBS;
    drive(k, 1);
}

static void go_on(struct c3_twai *k)
{
    if (k->st == BUSOFF) {
        k->recovery = 0;
        k->count = 0;
    } else {
        k->st = INTEGRATE;
        k->count = 0;
    }
    k->at_sample = 0;
    k->next = k->s->cycles + 1u;
}

static u32 twai_read(void *ctx, u32 off, int n)
{
    struct c3_twai *k = (struct c3_twai *)ctx;
    u32 w = off & ~3u, v;
    if (w >= 0x40u && w <= 0x70u) {
        u32 i = (w - 0x40u) / 4u;
        if (reg(k->mode) & MOD_RM)
            return i < 4 ? k->acr[i] : i < 8 ? k->amr[i - 4] : 0u;
        return k->nframes && i < head_size(k) ? k->fifo[i] : 0u;
    }
    v = esk_rf_read(k->p, off, n);
    if (w == 0x0Cu) { /* INT_RAW: the read clears all but RI */
        k->ir = 0;
        update(k);
    } else if (w == 0x2Cu) {
        k->alc_armed = 1;
    } else if (w == 0x30u) {
        k->ecc_armed = 1;
    } else if (w == 0x04u) {
        v = 0; /* CMD reads 0 */
    }
    return v;
}

static void twai_write(void *ctx, u32 off, int n, u32 v)
{
    struct c3_twai *k = (struct c3_twai *)ctx;
    u32 w = off & ~3u, was = reg(k->mode);
    int rm = (was & MOD_RM) != 0;
    if (w >= 0x40u && w <= 0x70u) {
        u32 i = (w - 0x40u) / 4u;
        if (rm) {
            if (i < 4) k->acr[i] = (u8)v;
            else if (i < 8) k->amr[i - 4] = (u8)v;
        } else if (k->sr & SR_TBS) {
            k->txbuf[i] = (u8)v;
        }
        return;
    }
    if (w == 0x04u) { /* CMD */
        if (v & 0x04u && k->nframes) { /* RRB */
            u32 sz = head_size(k);
            memmove(k->fifo, k->fifo + sz, k->fifo_len - sz);
            k->fifo_len -= sz;
            k->nframes--;
        }
        if (v & 0x08u) /* CDO */
            k->sr &= ~SR_DOS;
        if ((v & 0x11u) && !rm && (k->sr & SR_TBS)) { /* TR or SRR */
            memcpy(k->txq, k->txbuf, sizeof k->txq);
            stage_tx(k);
            k->txpend = 1;
            k->self = (v & 0x10u) != 0;
            k->single = (v & 0x02u) != 0;
            k->sr &= ~(SR_TBS | SR_TCS);
        } else if ((v & 0x02u) && k->txpend && !k->txing) { /* AT */
            k->txpend = 0;
            k->sr |= SR_TBS;
            k->ir |= IR_TI;
        } else if ((v & 0x02u) && k->txing) {
            k->single = 1; /* the frame on the bus goes on; no retry */
        }
        update(k);
        return;
    }
    if (w == 0x08u || w == 0x0Cu || w == 0x74u)
        return; /* read-only */
    if (!rm && (w == 0x18u || w == 0x1Cu || w == 0x34u || w == 0x38u || w == 0x3Cu))
        return; /* written in reset mode only */
    esk_rf_write(k->p, off, n, v);
    if (w == 0x00u) {
        u32 now = reg(k->mode);
        if (!(was & MOD_RM) && (now & MOD_RM))
            go_off(k);
        else if ((was & MOD_RM) && !(now & MOD_RM))
            go_on(k);
    }
    update(k);
    esk_rf_clock(k->p, &k->clocked, running(k));
}

static void twai_reset(void *ctx)
{
    struct c3_twai *k = (struct c3_twai *)ctx;
    memset(k->acr, 0, sizeof k->acr);
    memset(k->amr, 0xff, sizeof k->amr);
    memset(k->txbuf, 0, sizeof k->txbuf);
    k->fifo_len = k->nframes = 0;
    k->ir = 0;
    k->sr = SR_TBS | SR_TCS;
    k->alc_armed = k->ecc_armed = 1;
    k->st = OFF;
    k->txpend = k->txing = 0;
    k->tx_level = -1;
    drive(k, 1);
    k->rx_last = esk_esp32c3_signal_in(k->s, SIG_TWAI);
    k->level = 0;
    esk_esp32c3_source(k->s, SOURCE, 0);
    k->clocked = 0;
    update(k);
    esk_rf_clock(k->p, &k->clocked, 0);
}

const struct esk_dev_ops esk_esp32c3_twai_ops = {
    "esp32c3-twai", twai_read, twai_write, twai_reset, twai_tick, twai_next,
};

void *esk_esp32c3_twai_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct c3_twai *k = (struct c3_twai *)esk_mach_alloc(s, sizeof *k);
    if (!k)
        return 0;
    memset(k, 0, sizeof *k);
    k->s = s;
    k->p = p;
    k->mode = esk_rf_reg(p, "MODE");
    k->cmd = esk_rf_reg(p, "CMD");
    k->status = esk_rf_reg(p, "STATUS");
    k->intr = esk_rf_reg(p, "INT_RAW");
    k->ena = esk_rf_reg(p, "INT_ENA");
    k->btr0 = esk_rf_reg(p, "BUS_TIMING_0");
    k->btr1 = esk_rf_reg(p, "BUS_TIMING_1");
    k->alc = esk_rf_reg(p, "ARB_LOST_CAP");
    k->ecc = esk_rf_reg(p, "ERR_CODE_CAP");
    k->ewl = esk_rf_reg(p, "ERR_WARNING_LIMIT");
    k->rec = esk_rf_reg(p, "RX_ERR_CNT");
    k->tec = esk_rf_reg(p, "TX_ERR_CNT");
    k->rmc = esk_rf_reg(p, "RX_MESSAGE_CNT");
    k->clkdiv = esk_rf_reg(p, "CLOCK_DIVIDER");
    if (!k->mode || !k->cmd || !k->status || !k->intr || !k->ena || !k->btr0 || !k->btr1 || !k->alc || !k->ecc ||
        !k->ewl || !k->rec || !k->tec || !k->rmc)
        return 0;
    if (esk_esp32c3_signal_watch(s, rx_watch, k) != 0)
        return 0;
    return k;
}
