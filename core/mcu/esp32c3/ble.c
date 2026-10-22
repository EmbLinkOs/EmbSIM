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
 *     without it, while one is raised and enabled). The bits the scheduler
 *     uses: 1 a PDU sent, 2 one received, 5 an event's end, 6 an event
 *     skipped;
 *   - the exchange memory (EM), the controller's structures in RAM: each
 *     register from +0x204 maps an EM offset (bits 31:18, in words) to RAM
 *     (bits 17:0, its word address under 0x3FC00000; r_emi_em_base_init);
 *   - the exchange table: 16 entries of 16 bytes from EM offset 0
 *     (r_sch_prog_ble_push): its first halfword's bits 5:3 the event's
 *     state (0 waiting, 2 under way, 3 over), its start (+2 and +4 the
 *     half-slot, +6 the count down within it), +8 its control structure
 *     (the EM offset / 2). A write of bit 31 with an entry's index to
 *     +0x100 starts it: at its time the event runs as its control
 *     structure's format says, then its state reads 3 and the end
 *     interrupt is raised;
 *   - an advertising event (format 4 or 5): the control structure's
 *     address (+6 to +11) and transmit descriptor (+0x1C: its PDU header
 *     at +2, its data's EM offset at +4) make the advertising PDU, sent on
 *     each primary channel of the map (+0x16 bits 2:0: 37, 38, 39) at 1
 *     Mbit/s, one after another with time for a scan request between them
 *     (T_IFS and a SCAN_REQ's airtime), at +9 dBm (ESP-IDF's default
 *     level, P9).
 * The other registers keep what is written.
 * Not yet: receiving (SCAN_REQ and SCAN_RSP, CONNECT_IND, scanning,
 * connection events: an event of another format ends at once), the
 * transmit power the controller sets, the 2M and Coded PHYs, extended and
 * periodic advertising, encryption, privacy (resolvable addresses), sleep,
 * the radio's controls (+0x70 to +0x9C) beyond keeping their values. */
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
#define EMI_MAPS     51
#define ISR_FIFO     0x2D8u
#define INTACK2      0x38Cu
#define IP_VERSION   0x09001B00u
#define SOURCE       8 /* ETS_RWBLE_INTR_SOURCE */
#define FIFO_DEPTH   16
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

/* an EM offset's RAM address, by the mapping registers: 0 when none maps it */
static u32 em_addr(struct c3_ble *k, u32 o)
{
    u32 best = 0, bstart = 0;
    int i, any = 0;
    for (i = 0; i < EMI_MAPS; i++) {
        u32 r = *reg(k, EMI_MAP + 4u * (u32)i), start = (r >> 18) << 2;
        if (!(r & 0x3FFFFu) || start > o)
            continue;
        if (!any || start >= bstart) {
            best = r;
            bstart = start;
            any = 1;
        }
    }
    return any ? (0x3FC00000u | ((best << 2) & 0xFFFFCu)) + (o - bstart) : 0u;
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
    k->et_at[e] += air / (ES_PS_PER_US / 2u) + T_IFS_HUS + SCAN_REQ_HUS;
}

/* the events' steps due, each at its time */
static void events(struct c3_ble *k)
{
    int e, again = 1;
    while (again)
        for (again = 0, e = 0; e < ET_N; e++)
            if (((k->et_pending >> e) & 1u) && k->hus >= k->et_at[e]) {
                u32 cs = 2u * em_read(k, ET_SIZE * (u32)e + 8u, 2), fmt = em_read(k, cs, 2) & 0x1Fu;
                if (k->et_step[e] == 0)
                    et_state(k, e, ST_RUNNING);
                if (fmt == 4u || fmt == 5u)
                    adv_step(k, e);
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
    if (k->et_pending)
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
    k->clocked = 0;
    esk_mach_clock(k->s, &k->clocked, 1);
}

static const struct esk_dev_ops ble_ops = {
    "esp32c3-ble", ble_read, ble_write, ble_reset, ble_tick, ble_next,
};

static u32 ble_tuned(void *ctx, int kind)
{
    (void)ctx;
    (void)kind;
    return 0; /* it does not receive yet */
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
    if (esk_esp32c3_radio_join(s, ESK_RADIO_BLE, &part) != 0)
        return -1;
    if (esk_mach_add_dev(s, BLE_BASE, BLE_SIZE, &ble_ops, k) != ES_OK)
        return -1;
    ble_reset(k);
    return 0;
}
