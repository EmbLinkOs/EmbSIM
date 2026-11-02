/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* swd.c -- an Arm MCU's serial wire as a probe drives it, clock by clock
 * (ARM IHI 0031F, ADIv5.2: B4 the SW-DP, B2 the DP registers, C2 the
 * MEM-AP; the RP2040 datasheet 2.3.4 for its multi-drop wire).
 *
 * Each debug port on the wire sees every level SWDIO has, and keeps:
 *   - its protocol: JTAG (an SWJ-DP after power-on), SWD, or dormant. In
 *     JTAG, at least 50 cycles high then 0xE79E select SWD; in SWD, 50 high
 *     then 0xE3BC go dormant (0xE73C back to JTAG on an SWJ-DP); dormant,
 *     the 128-bit selection alert, four low cycles and the activation code
 *     0x1A select SWD. JTAG itself (the TAP) is not answered;
 *   - the line reset: 50 or more cycles high put it in its reset state
 *     (and out of lockout); the low cycles after are idle, and a high
 *     start bit begins a packet. After power-on, a protocol switch or a
 *     protocol error the port waits for a line reset;
 *   - the packet: the host's 8-bit request (start, APnDP, RnW, A[2:3],
 *     parity, stop, park), a turnaround, the port's 3-bit ACK, then for a
 *     read the port's 32 bits and parity and a turnaround, for a write a
 *     turnaround and the host's 32 bits and parity. A bad request (parity,
 *     stop or park) is not answered and the port waits for a line reset;
 *   - multi-drop (DPv2): in the reset state a TARGETSEL write (no ACK from
 *     anyone) selects the port whose TARGETID and TINSTANCE match and locks
 *     the others out until the next line reset; any other first packet is
 *     answered by every port, as on the wire;
 *   - the DP registers: DPIDR, ABORT (the sticky flags' clears), CTRL/STAT
 *     (the power-up requests acknowledged at once, CSYSPWRUPACK never on a
 *     rescue DP; STICKYERR, STICKYCMP, STICKYORUN, WDATAERR, READOK), DLCR's
 *     turnaround, TARGETID, DLPIDR, EVENTSTAT, SELECT, RESEND, RDBUFF. With
 *     a sticky flag set, every access but a read of DPIDR or CTRL/STAT and
 *     a write of ABORT is answered FAULT;
 *   - the MEM-AP (APSEL 0; none on a rescue DP): CSW (Size 8, 16 or 32
 *     bits, AddrInc off or single -- packed reads back as off, as on the
 *     M0+'s AHB-AP -- Prot, DbgSwEnable, DeviceEn reading 1), TAR, DRW with
 *     its byte lanes and the auto-increment within 1 KiB, BD0-3, CFG, BASE
 *     and IDR. Reads are posted, as the architecture has them: an AP read
 *     answers the previous AP read's data and leaves its own in RDBUFF. A
 *     bus error, or an AP access with the debug domain not powered up
 *     (CDBGPWRUPREQ), sets STICKYERR.
 * Every access completes at once: no WAIT, no overrun detection's data
 * phase (ORUNDETECT is stored, not acted on), no pushed compare (TRNMODE)
 * or transaction counter (TRNCNT), MASKLANE stored. */
#include <string.h>

#include "mcu/swd.h"

enum { PROTO_JTAG, PROTO_DORMANT, PROTO_SWD };
/* S_LOCKED waits for a line reset */
enum { S_LOCKED, S_RESET, S_IDLE, S_HEADER, S_TRN1, S_ACK, S_RDATA, S_TRN2, S_WDATA };

#define ACK_OK 1u
#define ACK_WAIT 2u
#define ACK_FAULT 4u

#define CS_ORUNDETECT 0x1u
#define CS_STICKYORUN 0x2u
#define CS_STICKYCMP 0x10u
#define CS_STICKYERR 0x20u
#define CS_READOK 0x40u
#define CS_WDATAERR 0x80u
#define CS_CDBGRSTREQ (1u << 26)
#define CS_CDBGRSTACK (1u << 27)
#define CS_CDBGPWRUPREQ (1u << 28)
#define CS_CDBGPWRUPACK (1u << 29)
#define CS_CSYSPWRUPREQ (1u << 30)
#define CS_CSYSPWRUPACK (1u << 31)
#define CS_WRITABLE (CS_ORUNDETECT | 0xCu | 0xF00u | 0xFFF000u | CS_CDBGRSTREQ | CS_CDBGPWRUPREQ | CS_CSYSPWRUPREQ)
#define CS_STICKY (CS_STICKYORUN | CS_STICKYCMP | CS_STICKYERR | CS_WDATAERR)

#define CSW_WRITABLE 0xFF000017u
#define CSW_DEVICEEN 0x40u

/* the selection alert, as the wire carries it (first bit lowest) */
static const uint32_t alert[4] = { 0x6209F392u, 0x86852D95u, 0xE3DDAFE9u, 0x19BC0EA2u };

typedef struct swd_port {
    esk_swd_port_cfg cfg;
    int proto, st, pos;
    uint32_t ones;    /* SWDIO high for this many cycles in a row */
    int after;        /* cycles since a run of 50 high ended, 0: none */
    int act;          /* cycles since the selection alert, -1: none */
    uint32_t hist[4]; /* the last 128 levels, the newest in hist[3]'s top bit */
    uint32_t req, ack, rd, wd;
    int apndp, rnw, a, tsel;
    int first;    /* the reset state: no packet since the line reset */
    int selected; /* not locked out */
    uint32_t ctrl, select, rdbuff, resend, dlcr;
    uint32_t csw, tar;
    uint64_t packets, faults;
} swd_port;

struct esk_swd {
    esk_swd_target t;
    int n;
    swd_port p[ESK_SWD_PORTS];
};

static int parity32(uint32_t v)
{
    v ^= v >> 16;
    v ^= v >> 8;
    v ^= v >> 4;
    v ^= v >> 2;
    v ^= v >> 1;
    return (int)(v & 1u);
}

static void port_init(swd_port *p)
{
    uint64_t packets = p->packets, faults = p->faults;
    esk_swd_port_cfg cfg = p->cfg;
    memset(p, 0, sizeof *p);
    p->cfg = cfg;
    p->packets = packets;
    p->faults = faults;
    p->proto = cfg.jtag ? PROTO_JTAG : PROTO_SWD;
    p->st = S_LOCKED;
    p->act = -1;
    p->selected = 1;
    p->csw = 0x03000042u; /* a word, no increment */
}

static int trn(const swd_port *p) { return (int)((p->dlcr >> 8) & 3u) + 1; }

static int instance(const esk_swd *w, const swd_port *p)
{
    int i = w->t.instance ? w->t.instance(w->t.ctx, (int)(p - w->p)) : -1;
    return i >= 0 ? i : p->cfg.instance;
}

/* ---- the MEM-AP ---------------------------------------------------------------- */

static int has_ap(const swd_port *p) { return p->cfg.core >= 0 && (p->select >> 24) == 0; }

static int ap_mem(esk_swd *w, swd_port *p, uint32_t addr, int bytes, int write, uint32_t *v)
{
    if (!w->t.mem || w->t.mem(w->t.ctx, p->cfg.core, addr, bytes, write, v)) {
        p->ctrl |= CS_STICKYERR;
        if (!write)
            *v = 0;
        return 0;
    }
    return 1;
}

/* DRW: the access at TAR of CSW's size, on its byte lanes */
static uint32_t drw(esk_swd *w, swd_port *p, int write, uint32_t data)
{
    uint32_t size = p->csw & 7u, a = p->tar, sh, v = 0, mask;
    int bytes = size == 0 ? 1 : size == 1 ? 2 : size == 2 ? 4 : 0;
    if (!bytes) {
        p->ctrl |= CS_STICKYERR;
        return 0;
    }
    sh = bytes == 4 ? 0u : bytes == 2 ? (a & 2u) * 8u : (a & 3u) * 8u;
    mask = bytes == 4 ? 0xFFFFFFFFu : bytes == 2 ? 0xFFFFu : 0xFFu;
    a &= ~(uint32_t)(bytes - 1);
    if (write) {
        v = (data >> sh) & mask;
        if (!ap_mem(w, p, a, bytes, 1, &v))
            return 0;
    } else {
        if (!ap_mem(w, p, a, bytes, 0, &v))
            return 0;
        v = (v & mask) << sh;
    }
    if (((p->csw >> 4) & 3u) == 1u) /* single auto-increment, within 1 KiB */
        p->tar = (p->tar & ~0x3FFu) | ((p->tar + (uint32_t)bytes) & 0x3FFu);
    return v;
}

static uint32_t ap_read(esk_swd *w, swd_port *p, uint32_t reg)
{
    uint32_t v = 0;
    if (!(p->ctrl & CS_CDBGPWRUPREQ)) {
        p->ctrl |= CS_STICKYERR; /* the debug domain is not powered */
        return 0;
    }
    if (!has_ap(p))
        return 0;
    switch (reg) {
    case 0x00:
        return p->csw | CSW_DEVICEEN;
    case 0x04:
        return p->tar;
    case 0x0C:
        return drw(w, p, 0, 0);
    case 0x10:
    case 0x14:
    case 0x18:
    case 0x1C:
        (void)ap_mem(w, p, (p->tar & ~0xFu) | (reg & 0xCu), 4, 0, &v);
        return v;
    case 0xF8:
        return p->cfg.ap_base;
    case 0xFC:
        return p->cfg.ap_idr;
    default:
        return 0; /* CFG (little-endian, no large data), the TAR's upper word */
    }
}

static void ap_write(esk_swd *w, swd_port *p, uint32_t reg, uint32_t v)
{
    if (!(p->ctrl & CS_CDBGPWRUPREQ)) {
        p->ctrl |= CS_STICKYERR;
        return;
    }
    if (!has_ap(p))
        return;
    switch (reg) {
    case 0x00:
        p->csw = v & CSW_WRITABLE;
        break;
    case 0x04:
        p->tar = v;
        break;
    case 0x0C:
        (void)drw(w, p, 1, v);
        break;
    case 0x10:
    case 0x14:
    case 0x18:
    case 0x1C:
        (void)ap_mem(w, p, (p->tar & ~0xFu) | (reg & 0xCu), 4, 1, &v);
        break;
    default:
        break;
    }
}

/* ---- the DP's registers ---------------------------------------------------------- */

static uint32_t ctrl_read(const swd_port *p)
{
    uint32_t v = p->ctrl & ~(CS_CDBGPWRUPACK | CS_CSYSPWRUPACK | CS_CDBGRSTACK);
    if (p->ctrl & CS_CDBGPWRUPREQ)
        v |= CS_CDBGPWRUPACK;
    if ((p->ctrl & CS_CSYSPWRUPREQ) && !p->cfg.rescue)
        v |= CS_CSYSPWRUPACK;
    if (p->ctrl & CS_CDBGRSTREQ)
        v |= CS_CDBGRSTACK;
    return v;
}

static uint32_t dp_read(const esk_swd *w, const swd_port *p, int a)
{
    switch (a) {
    case 0x0:
        return p->cfg.dpidr;
    case 0x4:
        switch (p->select & 0xFu) {
        case 0:
            return ctrl_read(p);
        case 1:
            return p->dlcr;
        case 2:
            return p->cfg.version >= 2 ? p->cfg.targetid : 0;
        case 3:
            return p->cfg.version >= 2 ? (uint32_t)instance(w, p) << 28 | 1u : 0;
        default:
            return 0; /* EVENTSTAT: an event (0) */
        }
    case 0x8:
        return p->resend;
    default:
        return p->rdbuff;
    }
}

static void dp_write(esk_swd *w, swd_port *p, int a, uint32_t v)
{
    uint32_t was;
    switch (a) {
    case 0x0: /* ABORT */
        if (v & 2u)
            p->ctrl &= ~CS_STICKYCMP;
        if (v & 4u)
            p->ctrl &= ~CS_STICKYERR;
        if (v & 8u)
            p->ctrl &= ~CS_WDATAERR;
        if (v & 16u)
            p->ctrl &= ~CS_STICKYORUN;
        break;
    case 0x4:
        if ((p->select & 0xFu) == 0) {
            was = p->ctrl;
            p->ctrl = (p->ctrl & ~CS_WRITABLE) | (v & CS_WRITABLE);
            if (p->cfg.rescue && ((was ^ p->ctrl) & CS_CDBGPWRUPREQ) && w->t.rescue)
                w->t.rescue(w->t.ctx, (p->ctrl & CS_CDBGPWRUPREQ) != 0);
        } else if ((p->select & 0xFu) == 1) {
            p->dlcr = v & 0x300u;
        }
        break;
    case 0x8:
        p->select = v;
        break;
    default:
        break;
    }
}

/* ---- the packet ---------------------------------------------------------------- */

/* the request is complete: whether and how the port answers */
static void request(esk_swd *w, swd_port *p)
{
    uint32_t r = p->req;
    int par = (int)((r >> 1) ^ (r >> 2) ^ (r >> 3) ^ (r >> 4)) & 1;
    if (!(r & 1u) || (r & 0x40u) || !(r & 0x80u) || par != (int)((r >> 5) & 1u)) {
        p->st = S_LOCKED; /* a protocol error: no answer until a line reset */
        return;
    }
    p->apndp = (int)((r >> 1) & 1u);
    p->rnw = (int)((r >> 2) & 1u);
    p->a = (int)((r >> 3) & 3u) << 2;
    p->tsel = !p->apndp && !p->rnw && p->a == 0xC && p->cfg.version >= 2;
    p->ack = 0;
    p->st = S_TRN1;
    p->pos = 0;
    if (p->tsel)
        return; /* nobody answers TARGETSEL */
    if (!p->selected) {
        p->st = S_LOCKED;
        return;
    }
    p->first = 0;
    p->packets++;
    if ((p->ctrl & CS_STICKY) && !(!p->apndp && p->rnw && (p->a == 0 || p->a == 4)) &&
        !(!p->apndp && !p->rnw && p->a == 0)) {
        p->ack = ACK_FAULT;
        p->faults++;
        return;
    }
    p->ack = ACK_OK;
    if (p->rnw) {
        if (p->apndp) {
            p->rd = p->rdbuff; /* posted: the previous AP read's data */
            p->rdbuff = ap_read(w, p, (p->select & 0xF0u) | (uint32_t)p->a);
            if (!(p->ctrl & CS_STICKYERR))
                p->ctrl |= CS_READOK;
            else
                p->ctrl &= ~CS_READOK;
        } else {
            p->rd = dp_read(w, p, p->a);
        }
        if (!(!p->apndp && p->a == 0x8))
            p->resend = p->rd;
    }
}

static void written(esk_swd *w, swd_port *p, int par_ok)
{
    if (p->tsel) {
        uint32_t v = p->wd;
        if (!p->first)
            return; /* outside the reset state: UNPREDICTABLE, ignored */
        p->first = 0;
        if (p->cfg.multidrop) {
            p->selected = par_ok && (v & 0x0FFFFFFFu) == (p->cfg.targetid & 0x0FFFFFFFu) &&
                          (int)(v >> 28) == instance(w, p);
            if (!p->selected)
                p->st = S_LOCKED;
        }
        return;
    }
    if (!par_ok) {
        p->ctrl |= CS_WDATAERR;
        return;
    }
    if (p->apndp)
        ap_write(w, p, (p->select & 0xF0u) | (uint32_t)p->a, p->wd);
    else
        dp_write(w, p, p->a, p->wd);
}

/* the level port `p` drives in this cycle, if it drives one */
static int drives(const swd_port *p, int *bit)
{
    if (p->proto != PROTO_SWD)
        return 0;
    if (p->st == S_ACK && p->ack) {
        *bit = (int)((p->ack >> p->pos) & 1u);
        return 1;
    }
    if (p->st == S_RDATA) {
        *bit = p->pos < 32 ? (int)((p->rd >> p->pos) & 1u) : parity32(p->rd);
        return 1;
    }
    return 0;
}

static void step(esk_swd *w, swd_port *p, int level)
{
    uint32_t l = (uint32_t)(level & 1);
    int code = -1;
    p->hist[0] = p->hist[0] >> 1 | p->hist[1] << 31;
    p->hist[1] = p->hist[1] >> 1 | p->hist[2] << 31;
    p->hist[2] = p->hist[2] >> 1 | p->hist[3] << 31;
    p->hist[3] = p->hist[3] >> 1 | l << 31;
    if (!l && p->ones >= 50)
        p->after = 1; /* the first cycle of a switching code */
    else if (p->after > 0)
        p->after = p->after < 16 ? p->after + 1 : 0;
    p->ones = l ? p->ones + 1u : 0u;
    if (p->after == 16)
        code = (int)(p->hist[3] >> 16);

    if (p->proto == PROTO_JTAG) {
        if (code == 0xE79E) {
            p->proto = PROTO_SWD;
            p->st = S_LOCKED;
        } else if (p->hist[3] == 0x33BBBBBAu) {
            p->proto = PROTO_DORMANT;
            p->act = -1;
        }
        return;
    }
    if (p->proto == PROTO_DORMANT) {
        if (p->act >= 0) {
            p->act++;
            if (p->act <= 4 && l)
                p->act = -1; /* the four low cycles */
            else if (p->act == 12) {
                if ((p->hist[3] >> 20) == 0x1A0u) { /* SWD's activation code */
                    p->proto = PROTO_SWD;
                    p->st = S_LOCKED;
                }
                p->act = -1;
            }
        }
        if (!memcmp(p->hist, alert, sizeof alert))
            p->act = 0;
        return;
    }
    /* SWD */
    if (code == 0xE3BC) {
        p->proto = PROTO_DORMANT;
        p->act = -1;
        return;
    }
    if (code == 0xE73C && p->cfg.jtag) {
        p->proto = PROTO_JTAG;
        return;
    }
    if (p->ones >= 50) {
        p->st = S_RESET; /* a line reset: the reset state, out of lockout */
        p->first = 1;
        p->selected = 1;
        return;
    }
    switch (p->st) {
    case S_LOCKED:
        break;
    case S_RESET:
        if (!l)
            p->st = S_IDLE;
        break;
    case S_IDLE:
        if (l) {
            p->st = S_HEADER;
            p->req = 1;
            p->pos = 1;
        }
        break;
    case S_HEADER:
        p->req |= l << p->pos;
        if (++p->pos == 8)
            request(w, p);
        break;
    case S_TRN1:
        if (++p->pos >= trn(p)) {
            p->st = S_ACK;
            p->pos = 0;
        }
        break;
    case S_ACK:
        if (++p->pos < 3)
            break;
        p->pos = 0;
        p->wd = 0;
        if (p->tsel)
            p->st = S_TRN2;
        else if (!p->ack)
            p->st = S_LOCKED;
        else if (p->ack == ACK_OK && p->rnw)
            p->st = S_RDATA;
        else
            p->st = S_TRN2;
        break;
    case S_RDATA:
        if (++p->pos == 33) {
            p->st = S_TRN2;
            p->pos = 0;
            p->ack = 0; /* no data phase after the turnaround */
        }
        break;
    case S_TRN2:
        if (++p->pos >= trn(p)) {
            p->pos = 0;
            p->st = (p->tsel || (p->ack == ACK_OK && !p->rnw)) ? S_WDATA : S_IDLE;
        }
        break;
    case S_WDATA:
        if (p->pos < 32) {
            p->wd |= l << p->pos;
            p->pos++;
        } else {
            p->st = S_IDLE;
            written(w, p, (int)l == parity32(p->wd));
        }
        break;
    default:
        break;
    }
}

/* ---- the wire ---------------------------------------------------------------- */

esk_swd *esk_swd_create(esk_alloc *a, const esk_swd_port_cfg *ports, int n, const esk_swd_target *t)
{
    esk_swd *w;
    int i;
    if (n < 1 || n > ESK_SWD_PORTS)
        return NULL;
    w = (esk_swd *)esk_calloc(a, 1, sizeof *w);
    if (!w)
        return NULL;
    w->t = *t;
    w->n = n;
    for (i = 0; i < n; i++) {
        w->p[i].cfg = ports[i];
        port_init(&w->p[i]);
    }
    return w;
}

void esk_swd_destroy(esk_alloc *a, esk_swd *w)
{
    if (w)
        esk_free(a, w, sizeof *w);
}

int esk_swd_clock(esk_swd *w, int drive, int bit)
{
    int i, level = 1, b;
    int present[ESK_SWD_PORTS];
    for (i = 0; i < w->n; i++) {
        present[i] = !w->t.present || w->t.present(w->t.ctx, i);
        if (!present[i])
            port_init(&w->p[i]);
    }
    if (drive)
        level = bit ? 1 : 0;
    else
        for (i = 0; i < w->n; i++)
            if (present[i] && drives(&w->p[i], &b))
                level &= b; /* two ports driving: the low one wins */
    for (i = 0; i < w->n; i++)
        if (present[i])
            step(w, &w->p[i], level);
    return level;
}

void esk_swd_reset(esk_swd *w, int keep_rescue)
{
    int i;
    for (i = 0; w && i < w->n; i++)
        if (!(keep_rescue && w->p[i].cfg.rescue))
            port_init(&w->p[i]);
}

void esk_swd_counts(const esk_swd *w, uint64_t *packets, uint64_t *faults, int *selected)
{
    int i;
    *packets = *faults = 0;
    *selected = -1;
    for (i = 0; w && i < w->n; i++) {
        *packets += w->p[i].packets;
        *faults += w->p[i].faults;
        if (w->p[i].proto == PROTO_SWD && w->p[i].selected && w->p[i].st != S_LOCKED && *selected < 0)
            *selected = i;
    }
}
