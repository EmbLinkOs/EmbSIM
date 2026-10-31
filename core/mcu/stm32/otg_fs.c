/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* otg_fs.c -- the STM32F4's USB on-the-go full-speed controller (RM0390
 * 30, the Synopsys DWC2 core) in device mode, over the register file's
 * OTGFS block (tools/importers/dwc2_regs.py), with the host on its cable
 * (engine/usb.h, ADR-031: the bus as transactions).
 *
 * As the core does it:
 *   - its 1.25 KiB of FIFO RAM (320 words): the receive FIFO in its first
 *     GRXFSIZ words, each IN endpoint's transmit FIFO where DIEPTXF0 and
 *     DIEPTXFn place it; a word written to the window at 0x1000 x (n + 1)
 *     goes into endpoint n's, a word read from any window comes out of the
 *     receive FIFO; the RAM itself is at 0x20000 (the debug view);
 *   - the receive FIFO holds each packet's status word and its data:
 *     GRXSTSR shows the next status, GRXSTSP takes it, RXFLVL is set while
 *     one waits; a SETUP's 8 bytes (PKTSTS 6) and an OUT packet's (2) come
 *     with their status, and the "SETUP stage done" (4, pushed when the
 *     host's next token after the SETUP comes) and "OUT transfer done" (3)
 *     statuses raise STUP and XFRC on the endpoint when they are taken;
 *   - a SETUP is always taken (STUPCNT counts down), clears EP0's STALL,
 *     NAKs EP0 both ways until the program's CNAK, and starts both data
 *     toggles at DATA1;
 *   - an OUT packet is taken while its endpoint is enabled, not NAKing,
 *     its packet count not spent and the receive FIFO has room for it,
 *     else it is NAKed (STALL while the endpoint stalls); a packet whose
 *     data toggle repeats the last is ACKed and dropped; a short packet or
 *     the last of PKTCNT ends the transfer (XFRSIZ less what came);
 *   - an IN token is answered with the next packet (MPSIZ or what XFRSIZ
 *     has left; a zero-length one when XFRSIZ is 0) once all its words are
 *     in the endpoint's FIFO, else NAKed; ITTXFE when the FIFO was empty;
 *     XFRC and EPENA's clearing after the last packet; TXFE and the
 *     DIEPEMPMSK interrupt as GAHBCFG's TXFELVL sets (half or all empty);
 *     DTXFSTS the words free;
 *   - the device address (DCFG's DAD) takes effect after the status stage
 *     of the SET_ADDRESS that set it (the core answers the old address
 *     until its IN status packet is sent);
 *   - DCTL's SDIS, GCCFG's PWRDWN, the session (VBUS on PA9 above 4 V with
 *     VBDEN, or GOTGCTL's BVALOEN and BVALOVAL) and PA12 on its OTG_FS_DP
 *     alternate function decide the pull-up on D+ the host sees;
 *   - a bus reset sets USBRST (and WKUINT when suspended) and its end
 *     ENUMDNE at full speed (DSTS's ENUMSPD 3); each SOF sets SOF and
 *     DSTS's frame number; 3 ms without bus activity while attached sets
 *     ESUSP, USBSUSP and SUSPSTS, the next activity (the host's resume
 *     signalling or reset) WKUINT; DCTL's RWUSIG set while suspended is
 *     remote wakeup, which the host hears and answers with its resume;
 *     VBUS dropping while VBDEN is on sets GOTGINT's SEDET;
 *   - the core's soft reset (GRSTCTL's CSRST) needs the 48 MHz clock: it
 *     stays set while RCC gives none (the HAL's USB_CoreReset times out),
 *     and clears the core's state but SDIS, DSPD and PCGCCTL's gating;
 *     RXFFLSH and TXFFLSH (TXFNUM, 0x10 all) empty their FIFOs at once;
 *   - GUSBCFG's FDMOD and FHMOD change CMOD 25 ms after they are written;
 *   - the OTG_FS interrupt is a level: GAHBCFG's GINT and any GINTSTS bit
 *     GINTMSK lets through (DAINT, DAINTMSK and the endpoints' masks
 *     behind IEPINT and OEPINT).
 * Inside: the transactions come whole at the time the host part sends
 * them (engine/usb.h); the device's turnaround is not seen. Not yet: host
 * mode (the host channel registers are storage), the ID pin and OTG's SRP
 * and HNP, isochronous endpoints' frame parity and incomplete-transfer
 * interrupts, LPM, the battery-charging detection of GCCFG, the
 * OTG_FS_WKUP interrupt through
 * EXTI line 18, the SOF pulse on PA8, DMA (the F446's OTG_FS has none). */
#include <string.h>

#include "mcu/stm32/stm32.h"

#define FIFO_WORDS 320u
#define NEP 6
#define REGS_END 0x1000u
#define WIN_BASE 0x1000u
#define WIN_END 0x11000u
#define RAM_BASE 0x20000u
#define RAM_END 0x40000u
#define PS_PER_MS 1000000000ull
#define SUSPEND_PS (3ull * PS_PER_MS)
#define MODE_PS (25ull * PS_PER_MS)
#define VBUS_VALID_V 4.0

/* register offsets */
#define GOTGCTL 0x000u
#define GOTGINT 0x004u
#define GAHBCFG 0x008u
#define GUSBCFG 0x00Cu
#define GRSTCTL 0x010u
#define GINTSTS 0x014u
#define GINTMSK 0x018u
#define GRXSTSR 0x01Cu
#define GRXSTSP 0x020u
#define GRXFSIZ 0x024u
#define DIEPTXF0 0x028u
#define GCCFG 0x038u
#define DIEPTXF(n) (0x100u + 4u * (u32)(n))
#define DCFG 0x800u
#define DCTL 0x804u
#define DSTS 0x808u
#define DIEPMSK 0x810u
#define DOEPMSK 0x814u
#define DAINT 0x818u
#define DAINTMSK 0x81Cu
#define DIEPEMPMSK 0x834u
#define DIEPCTL(n) (0x900u + 0x20u * (u32)(n))
#define DIEPINT(n) (0x908u + 0x20u * (u32)(n))
#define DIEPTSIZ(n) (0x910u + 0x20u * (u32)(n))
#define DTXFSTS(n) (0x918u + 0x20u * (u32)(n))
#define DOEPCTL(n) (0xB00u + 0x20u * (u32)(n))
#define DOEPINT(n) (0xB08u + 0x20u * (u32)(n))
#define DOEPTSIZ(n) (0xB10u + 0x20u * (u32)(n))
#define PCGCCTL 0xE00u

/* bits */
#define BVALOEN (1u << 6)
#define BVALOVAL (1u << 7)
#define CIDSTS (1u << 16)
#define BSVLD (1u << 19)
#define SEDET (1u << 2)
#define GINT (1u << 0)
#define TXFELVL (1u << 7)
#define FHMOD (1u << 29)
#define FDMOD (1u << 30)
#define CSRST (1u << 0)
#define RXFFLSH (1u << 4)
#define TXFFLSH (1u << 5)
#define AHBIDL (1u << 31)
#define I_CMOD (1u << 0)
#define I_OTGINT (1u << 2)
#define I_SOF (1u << 3)
#define I_RXFLVL (1u << 4)
#define I_GINAKEFF (1u << 6)
#define I_BOUTNAKEFF (1u << 7)
#define I_ESUSP (1u << 10)
#define I_USBSUSP (1u << 11)
#define I_USBRST (1u << 12)
#define I_ENUMDNE (1u << 13)
#define I_IEPINT (1u << 18)
#define I_OEPINT (1u << 19)
#define I_WKUINT (1u << 31)
#define I_RO_DEVICE (I_CMOD | I_OTGINT | I_RXFLVL | I_GINAKEFF | I_BOUTNAKEFF | I_IEPINT | I_OEPINT)
#define PWRDWN (1u << 16)
#define VBDEN (1u << 21)
#define SDIS (1u << 1)
#define GINSTS (1u << 2)
#define GONSTS (1u << 3)
#define SGINAK (1u << 7)
#define CGINAK (1u << 8)
#define SGONAK (1u << 9)
#define CGONAK (1u << 10)
#define DSPD_MASK 3u
#define USBAEP (1u << 15)
#define DPID_BIT (1u << 16)
#define NAKSTS (1u << 17)
#define STALL (1u << 21)
#define CNAK (1u << 26)
#define SNAK (1u << 27)
#define SD0PID (1u << 28)
#define SD1PID (1u << 29)
#define EPDIS (1u << 30)
#define EPENA (1u << 31)
#define XFRC (1u << 0)
#define EPDISD (1u << 1)
#define STUP (1u << 3)
#define ITTXFE (1u << 4)
#define TXFE (1u << 7)
#define B2BSTUP (1u << 6)
#define PKT_SETUP_DONE 4u
#define PKT_OUT_DATA 2u
#define PKT_OUT_DONE 3u
#define PKT_SETUP_DATA 6u
#define STPPCLK (1u << 0)
#define GATEHCLK (1u << 1)

struct txq {
    u32 rd, used; /* words, within the FIFO's place in the RAM */
};

struct otg {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    int irq, level;
    struct esk_rf_reg *r[REGS_END / 4u];
    u32 ram[FIFO_WORDS];
    u32 rx_rd, rx_used; /* the receive FIFO: words in it from rx_rd */
    u32 rx_left;        /* data words of the taken status still to read */
    u32 rx_statuses;    /* statuses waiting in it */
    struct txq tx[NEP];
    int in_pid[NEP], out_pid[NEP];
    int setup_done;     /* a SETUP's "stage done" waits for the next token */
    u32 addr;           /* the address it answers */
    int addr_pending;   /* DAD changed: the old one until the status stage */
    int pulled;         /* the pull-up as the host was last told */
    int suspended, reset_on, enumerated;
    int resume_on;      /* the host drives resume (K): not an idle bus */
    int session;        /* B-session valid, as last seen */
    u32 frame;
    int running;
    u64 since, idle_ps, mode_ps;
    u32 mode_want; /* CMOD once mode_ps has passed */
    int mode_wait;
    int warned_fifo, warned_rx;
};

/* ---- registers --------------------------------------------------------- */

static u32 rv(const struct otg *o, u32 off)
{
    const struct esk_rf_reg *r = o->r[off / 4u];
    return r ? (u32)r->value : 0;
}

static void rset(struct otg *o, u32 off, u32 v)
{
    struct esk_rf_reg *r = o->r[off / 4u];
    if (r)
        esk_rf_hw(r, v);
}

static void rbits(struct otg *o, u32 off, u32 set, u32 clear) { rset(o, off, (rv(o, off) & ~clear) | set); }

static u32 core_hz(const struct otg *o)
{
    u32 hz = esk_mach_core_hz(o->s);
    return hz ? hz : 16000000u;
}

static u64 cycles_ps(const struct otg *o, u64 c) { return c * 1000000ull / (core_hz(o) / 1000000u ? core_hz(o) / 1000000u : 1u); }

/* ---- the FIFOs --------------------------------------------------------- */

static u32 rx_depth(const struct otg *o)
{
    u32 d = rv(o, GRXFSIZ) & 0xffffu;
    return d < 16u ? 16u : d > FIFO_WORDS ? FIFO_WORDS : d;
}

/* an IN endpoint's FIFO: its start and depth in words */
static void tx_place(const struct otg *o, int ep, u32 *start, u32 *depth)
{
    u32 v = ep == 0 ? rv(o, DIEPTXF0) : rv(o, DIEPTXF(ep));
    *start = v & 0xffffu;
    *depth = v >> 16;
    if (*depth < 16u)
        *depth = 16u;
}

static u32 ram_at(u32 w) { return w % FIFO_WORDS; }

/* an IN endpoint's FIFO where the RAM has no room for it, said once: the
 * part's FIFOs then overwrite one another */
static void fifo_check(struct otg *o, int ep)
{
    u32 st, d, rx = rx_depth(o);
    if (o->warned_fifo)
        return;
    tx_place(o, ep, &st, &d);
    if (st + d > FIFO_WORDS || st < rx) {
        o->warned_fifo = 1;
        esk_rf_diag(o->s, ES_SEV_WARNING, "mcu.usb_fifo",
                    "OTG_FS: IN endpoint %d's FIFO (words %lu to %lu) overlaps the receive FIFO or runs past the "
                    "core's 320 words of FIFO RAM",
                    ep, (unsigned long)st, (unsigned long)(st + d - 1u));
    }
}

static int rx_push(struct otg *o, u32 status, const u8 *data, u32 len)
{
    u32 words = (len + 3u) / 4u, d = rx_depth(o), i;
    if (o->rx_used + 1u + words > d)
        return 0;
    o->ram[ram_at((o->rx_rd + o->rx_used) % d)] = status;
    o->rx_used++;
    for (i = 0; i < words; i++) {
        u32 w = 0, k;
        for (k = 0; k < 4u && 4u * i + k < len; k++)
            w |= (u32)data[4u * i + k] << (8u * k);
        o->ram[ram_at((o->rx_rd + o->rx_used) % d)] = w;
        o->rx_used++;
    }
    o->rx_statuses++;
    return 1;
}

static u32 rx_pop(struct otg *o)
{
    u32 d = rx_depth(o), w;
    if (!o->rx_used)
        return 0;
    w = o->ram[ram_at(o->rx_rd % d)];
    o->rx_rd = (o->rx_rd + 1u) % d;
    o->rx_used--;
    return w;
}

static void rx_flush(struct otg *o)
{
    o->rx_rd = o->rx_used = o->rx_left = o->rx_statuses = 0;
}

static void tx_flush(struct otg *o, int ep)
{
    o->tx[ep].rd = o->tx[ep].used = 0;
}

static u32 tx_free(const struct otg *o, int ep)
{
    u32 st, d;
    tx_place(o, ep, &st, &d);
    return o->tx[ep].used >= d ? 0 : d - o->tx[ep].used;
}

static void tx_push(struct otg *o, int ep, u32 w)
{
    u32 st, d;
    tx_place(o, ep, &st, &d);
    fifo_check(o, ep);
    if (o->tx[ep].used >= d)
        return; /* the FIFO is full: the word is lost, as on the part */
    o->ram[ram_at(st + (o->tx[ep].rd + o->tx[ep].used) % d)] = w;
    o->tx[ep].used++;
}

static u32 tx_pop(struct otg *o, int ep)
{
    u32 st, d, w;
    tx_place(o, ep, &st, &d);
    if (!o->tx[ep].used)
        return 0;
    w = o->ram[ram_at(st + o->tx[ep].rd % d)];
    o->tx[ep].rd = (o->tx[ep].rd + 1u) % d;
    o->tx[ep].used--;
    return w;
}

/* ---- state seen from outside ------------------------------------------- */

static int session_valid(struct otg *o)
{
    u32 ctl = rv(o, GOTGCTL);
    double v = 0.0;
    if (ctl & BVALOEN)
        return (ctl & BVALOVAL) != 0;
    if (!(rv(o, GCCFG) & VBDEN))
        return 0;
    /* PA9, OTG_FS_VBUS: the comparator at the top of the OTG supplement's
     * B-session valid range (0.8 to 4 V) */
    if (o->s->host.pin_volts && o->s->host.pin_volts(o->s->host.ctx, 0, 9, &v))
        return v > VBUS_VALID_V;
    return 0;
}

static int device_mode(const struct otg *o) { return !(rv(o, GINTSTS) & I_CMOD); }

/* the D+ pull-up the host sees */
static int pullup_now(struct otg *o)
{
    int dp;
    if (esk_rf_gated(o->p))
        return 0;
    if (!device_mode(o) || !(rv(o, GCCFG) & PWRDWN) || (rv(o, DCTL) & SDIS) || !session_valid(o))
        return 0;
    dp = esk_stm32f4_af_pin(o->s, "USB_OTG_FS_DP");
    return dp == 0 * 32 + 12; /* PA12 */
}

static void tell_host(struct otg *o)
{
    int now = pullup_now(o);
    if (now == o->pulled)
        return;
    o->pulled = now;
    if (!now) {
        o->enumerated = 0;
        o->suspended = 0;
    }
    if (o->s->host.usb_changed)
        o->s->host.usb_changed(o->s->host.ctx);
}

static u32 ep_mps(const struct otg *o, int ep)
{
    static const u32 ep0[4] = { 64, 32, 16, 8 };
    u32 ctl = rv(o, DIEPCTL(ep));
    return ep == 0 ? ep0[ctl & 3u] : ctl & 0x7ffu;
}

static u32 out_mps(const struct otg *o, int ep)
{
    static const u32 ep0[4] = { 64, 32, 16, 8 };
    return ep == 0 ? ep0[rv(o, DIEPCTL(0)) & 3u] : rv(o, DOEPCTL(ep)) & 0x7ffu;
}

/* the read-only bits that follow the core's state, and the interrupt */
static void refresh(struct otg *o)
{
    u32 st, ain = 0, aout = 0, ctl, dsts, lvl;
    int ep, session = session_valid(o);
    if (o->session && !session && (rv(o, GCCFG) & VBDEN))
        rbits(o, GOTGINT, SEDET, 0); /* the session ended */
    o->session = session;
    rset(o, GOTGCTL, (rv(o, GOTGCTL) & ~(CIDSTS | BSVLD | (1u << 18) | (1u << 17))) | CIDSTS | (session ? BSVLD : 0u));
    rbits(o, DOEPCTL(0), rv(o, DIEPCTL(0)) & 3u, 3u);
    for (ep = 0; ep < NEP; ep++) {
        u32 d, s0, free = tx_free(o, ep), emp, iin;
        tx_place(o, ep, &s0, &d);
        rset(o, DTXFSTS(ep), free);
        emp = (rv(o, GAHBCFG) & TXFELVL) ? (o->tx[ep].used == 0) : (free >= d / 2u);
        rbits(o, DIEPINT(ep), emp ? TXFE : 0u, TXFE);
        iin = rv(o, DIEPINT(ep)) & (rv(o, DIEPMSK) | (((rv(o, DIEPEMPMSK) >> ep) & 1u) << 7));
        if (iin)
            ain |= 1u << ep;
        if (rv(o, DOEPINT(ep)) & rv(o, DOEPMSK) & ~0x80u)
            aout |= 1u << ep;
        ctl = rv(o, DIEPCTL(ep));
        rset(o, DIEPCTL(ep), (ctl & ~DPID_BIT) | (ep && o->in_pid[ep] ? DPID_BIT : 0u));
        ctl = rv(o, DOEPCTL(ep));
        rset(o, DOEPCTL(ep), (ctl & ~DPID_BIT) | (ep && o->out_pid[ep] ? DPID_BIT : 0u));
    }
    /* DAINT's bits follow the endpoints' interrupts as DIEPMSK and DOEPMSK
     * let them through */
    rset(o, DAINT, ain | aout << 16);
    st = rv(o, GINTSTS) & ~I_RO_DEVICE;
    st |= rv(o, GINTSTS) & I_CMOD;
    if (rv(o, GOTGINT))
        st |= I_OTGINT;
    if (o->rx_statuses && !o->rx_left)
        st |= I_RXFLVL;
    if (rv(o, DCTL) & GINSTS)
        st |= I_GINAKEFF;
    if (rv(o, DCTL) & GONSTS)
        st |= I_BOUTNAKEFF;
    if (rv(o, DAINT) & rv(o, DAINTMSK) & 0xffffu)
        st |= I_IEPINT;
    if (rv(o, DAINT) & rv(o, DAINTMSK) & 0xffff0000u)
        st |= I_OEPINT;
    rset(o, GINTSTS, st);
    /* full speed (ENUMSPD 3), the frame, the line: J (D+ high) when
     * attached and idle */
    dsts = (rv(o, DSTS) & 0x10u) | (o->suspended ? 1u : 0u) | (3u << 1) | ((o->frame & 0x3fffu) << 8);
    if (o->pulled && !o->reset_on)
        dsts |= 2u << 22;
    rset(o, DSTS, dsts);
    lvl = (rv(o, GAHBCFG) & GINT) && (st & rv(o, GINTMSK));
    esk_rf_irq_level(o->p, o->irq, (int)lvl, &o->level);
}

/* the clock runs while time matters: a forced mode on its way, or the
 * bus's idle time towards a suspend */
static void clock_check(struct otg *o)
{
    int on = o->mode_wait || (o->pulled && !o->suspended);
    if (on && !o->running)
        o->since = o->s->cycles;
    esk_rf_clock(o->p, &o->running, on);
}

static void after(struct otg *o)
{
    refresh(o);
    tell_host(o);
    refresh(o);
    clock_check(o);
}

/* ---- the core's resets --------------------------------------------------- */

static void core_soft_reset(struct otg *o)
{
    u32 sdis = rv(o, DCTL) & SDIS, dspd = rv(o, DCFG) & DSPD_MASK, pcg = rv(o, PCGCCTL) & (STPPCLK | GATEHCLK);
    u32 keep_usbcfg = rv(o, GUSBCFG), keep_gccfg = rv(o, GCCFG), keep_ahb = rv(o, GAHBCFG);
    u32 keep_otg = rv(o, GOTGCTL), keep_msk = rv(o, GINTMSK);
    u32 off;
    int ep;
    /* the interrupts and the endpoints' state; the configuration registers
     * the application wrote (GUSBCFG, GCCFG, GAHBCFG, GINTMSK, GOTGCTL's
     * overrides, the FIFO sizes) are kept, as the HAL relies on */
    for (ep = 0; ep < NEP; ep++) {
        esk_rf_hw(o->r[DIEPCTL(ep) / 4u], o->r[DIEPCTL(ep) / 4u]->reset);
        esk_rf_hw(o->r[DOEPCTL(ep) / 4u], o->r[DOEPCTL(ep) / 4u]->reset);
        esk_rf_hw(o->r[DIEPINT(ep) / 4u], o->r[DIEPINT(ep) / 4u]->reset);
        esk_rf_hw(o->r[DOEPINT(ep) / 4u], o->r[DOEPINT(ep) / 4u]->reset);
        esk_rf_hw(o->r[DIEPTSIZ(ep) / 4u], 0);
        esk_rf_hw(o->r[DOEPTSIZ(ep) / 4u], 0);
        tx_flush(o, ep);
        o->in_pid[ep] = o->out_pid[ep] = 0;
    }
    for (off = DCFG; off < DIEPCTL(0); off += 4u)
        if (o->r[off / 4u])
            esk_rf_hw(o->r[off / 4u], o->r[off / 4u]->reset);
    rset(o, GINTSTS, rv(o, GINTSTS) & I_CMOD);
    rset(o, GOTGINT, 0);
    rset(o, DCTL, (rv(o, DCTL) & ~SDIS) | sdis);
    rset(o, DCFG, (rv(o, DCFG) & ~DSPD_MASK) | dspd);
    rset(o, PCGCCTL, pcg);
    rset(o, GUSBCFG, keep_usbcfg);
    rset(o, GCCFG, keep_gccfg);
    rset(o, GAHBCFG, keep_ahb);
    rset(o, GINTMSK, keep_msk);
    rset(o, GOTGCTL, keep_otg);
    rx_flush(o);
    o->setup_done = 0;
    o->addr = 0;
    o->addr_pending = 0;
}

/* CSRST completes once the 48 MHz clock runs: the PHY's domain resets
 * with it */
static void csrst_check(struct otg *o)
{
    if ((rv(o, GRSTCTL) & CSRST) && esk_stm32f4_ck48(o->s)) {
        core_soft_reset(o);
        rbits(o, GRSTCTL, 0, CSRST);
    }
}

/* ---- the program's accesses ---------------------------------------------- */

static void take_status(struct otg *o, u32 st)
{
    int ep = (int)(st & 0xfu);
    u32 pkt = (st >> 17) & 0xfu, bcnt = (st >> 4) & 0x7ffu;
    o->rx_statuses--;
    o->rx_left = pkt == PKT_OUT_DATA || pkt == PKT_SETUP_DATA ? (bcnt + 3u) / 4u : 0;
    if (ep >= NEP)
        return;
    if (pkt == PKT_SETUP_DONE)
        rbits(o, DOEPINT(ep), STUP, 0);
    else if (pkt == PKT_OUT_DONE) {
        rbits(o, DOEPINT(ep), XFRC, 0);
        rbits(o, DOEPCTL(ep), 0, EPENA);
    }
}

static u32 fifo_read(struct otg *o)
{
    if (!o->rx_left) {
        if (!o->warned_rx) {
            o->warned_rx = 1;
            esk_rf_diag(o->s, ES_SEV_WARNING, "mcu.usb_fifo",
                        "OTG_FS: the receive FIFO is read with no packet's data taken (GRXSTSP first)");
        }
        return 0;
    }
    o->rx_left--;
    return rx_pop(o);
}

static u32 otg_read(void *ctx, u32 off, int n)
{
    struct otg *o = (struct otg *)ctx;
    u32 v;
    if (off >= WIN_BASE && off < WIN_END) {
        v = fifo_read(o);
        after(o);
        return v;
    }
    if (off >= RAM_BASE && off < RAM_END) {
        u32 w = (off - RAM_BASE) / 4u;
        v = w < FIFO_WORDS ? o->ram[w] : 0;
        return v >> (8u * (off & 3u));
    }
    if (off >= REGS_END || !o->r[off / 4u])
        return 0;
    if ((off & ~3u) == GRSTCTL)
        csrst_check(o);
    if ((off & ~3u) == GRXSTSR || (off & ~3u) == GRXSTSP) {
        u32 st = 0;
        if (o->rx_statuses && !o->rx_left) {
            u32 d = rx_depth(o);
            st = o->ram[ram_at(o->rx_rd % d)];
            if ((off & ~3u) == GRXSTSP) {
                (void)rx_pop(o);
                take_status(o, st);
            }
        }
        after(o);
        return st >> (8u * (off & 3u));
    }
    refresh(o);
    return esk_rf_read(o->p, off, n);
}

static void ctl_in(struct otg *o, int ep, u32 v, u32 old)
{
    u32 c = rv(o, DIEPCTL(ep));
    if (v & CNAK)
        c &= ~NAKSTS;
    if (v & SNAK)
        c |= NAKSTS;
    if (ep && (v & SD0PID))
        o->in_pid[ep] = 0;
    if (ep && (v & SD1PID))
        o->in_pid[ep] = 1;
    if (ep == 0) {
        c |= USBAEP;
        c &= ~(3u << 18);
    }
    if (!ep && !(v & STALL) && (old & STALL))
        c |= STALL; /* EP0's STALL: only a SETUP clears it */
    if ((c & EPDIS) && (old & EPENA)) {
        c &= ~(EPDIS | EPENA);
        rbits(o, DIEPINT(ep), EPDISD, 0);
    } else if (c & EPDIS) {
        c &= ~EPDIS;
    }
    rset(o, DIEPCTL(ep), c);
}

static void ctl_out(struct otg *o, int ep, u32 v, u32 old)
{
    u32 c = rv(o, DOEPCTL(ep));
    if (v & CNAK)
        c &= ~NAKSTS;
    if (v & SNAK)
        c |= NAKSTS;
    if (ep && (v & SD0PID))
        o->out_pid[ep] = 0;
    if (ep && (v & SD1PID))
        o->out_pid[ep] = 1;
    if (ep == 0) {
        c |= USBAEP;
        c &= ~(EPDIS | (3u << 18));
        if (!(v & STALL) && (old & STALL))
            c |= STALL;
    } else if ((c & EPDIS) && (old & EPENA)) {
        c &= ~(EPDIS | EPENA);
        rbits(o, DOEPINT(ep), EPDISD, 0);
    } else {
        c &= ~EPDIS;
    }
    rset(o, DOEPCTL(ep), c);
}

static void otg_write(void *ctx, u32 off, int n, u32 v)
{
    struct otg *o = (struct otg *)ctx;
    u32 a = off & ~3u, old;
    int ep;
    if (off >= WIN_BASE && off < WIN_END) {
        ep = (int)((off - WIN_BASE) >> 12);
        if (ep < NEP)
            tx_push(o, ep, v);
        after(o);
        return;
    }
    if (off >= RAM_BASE && off < RAM_END) {
        u32 w = (off - RAM_BASE) / 4u;
        if (w < FIFO_WORDS)
            o->ram[w] = n == 4 ? v : (o->ram[w] & ~(((1u << (8 * n)) - 1u) << (8u * (off & 3u)))) |
                                         ((v & ((1u << (8 * n)) - 1u)) << (8u * (off & 3u)));
        return;
    }
    if (off >= REGS_END || !o->r[a / 4u])
        return;
    old = rv(o, a);
    esk_rf_write(o->p, off, n, v);
    if (n != 4)
        v = rv(o, a);
    switch (a) {
    case GUSBCFG: {
        u32 c = rv(o, GUSBCFG), want = c & FHMOD ? 1u : 0u;
        if ((c ^ old) & (FHMOD | FDMOD)) {
            /* the forced mode takes 25 ms (RM0390 30.16.4) */
            o->mode_want = (c & (FHMOD | FDMOD)) ? want : 0u;
            o->mode_wait = 1;
            o->mode_ps = 0;
        }
        break;
    }
    case GRSTCTL: {
        u32 c = rv(o, GRSTCTL);
        if (c & RXFFLSH)
            rx_flush(o);
        if (c & TXFFLSH) {
            u32 num = (c >> 6) & 0x1fu;
            for (ep = 0; ep < NEP; ep++)
                if (num == 0x10u || (u32)ep == num)
                    tx_flush(o, ep);
        }
        rset(o, GRSTCTL, (c & ~(RXFFLSH | TXFFLSH | (1u << 1) | (1u << 2))) | AHBIDL);
        csrst_check(o);
        break;
    }
    case GCCFG:
        if (((rv(o, GCCFG) ^ old) & VBDEN) && o->s->host.pin_watch)
            /* VBUS sensing: PA9's comparator */
            (void)o->s->host.pin_watch(o->s->host.ctx, 0, 9, (rv(o, GCCFG) & VBDEN) != 0, VBUS_VALID_V);
        break;
    case DCTL: {
        u32 c = rv(o, DCTL);
        if (v & SGINAK)
            c |= GINSTS;
        if (v & CGINAK)
            c &= ~GINSTS;
        if (v & SGONAK)
            c |= GONSTS;
        if (v & CGONAK)
            c &= ~GONSTS;
        rset(o, DCTL, c & ~(SGINAK | CGINAK | SGONAK | CGONAK));
        /* RWUSIG on a suspended bus: remote wakeup (K), which the host
         * hears and takes over with its own resume signalling (USB 2.0
         * 7.1.7.7) */
        if ((c & 1u) && !(old & 1u) && o->suspended && o->pulled && o->s->host.usb_remote_wakeup)
            o->s->host.usb_remote_wakeup(o->s->host.ctx);
        break;
    }
    case DCFG:
        if (((rv(o, DCFG) ^ old) >> 4) & 0x7fu)
            o->addr_pending = 1;
        break;
    default:
        for (ep = 0; ep < NEP; ep++) {
            if (a == DIEPCTL(ep))
                ctl_in(o, ep, v, old);
            else if (a == DOEPCTL(ep))
                ctl_out(o, ep, v, old);
        }
        break;
    }
    after(o);
}

/* ---- the bus, as the host on the cable drives it --------------------------- */

static void activity(struct otg *o)
{
    o->idle_ps = 0;
    o->since = o->s->cycles;
    if (o->suspended) {
        o->suspended = 0;
        rbits(o, GINTSTS, I_WKUINT, 0);
    }
}

static void usb_bus(void *ctx, int ev, u32 frame)
{
    struct otg *o = (struct otg *)ctx;
    if (!o->pulled)
        return;
    activity(o);
    switch (ev) {
    case ESK_USB_RESET:
        o->reset_on = 1;
        o->enumerated = 0;
        o->addr = 0;
        o->addr_pending = 0;
        o->setup_done = 0;
        rset(o, DCFG, rv(o, DCFG) & ~(0x7fu << 4));
        rbits(o, GINTSTS, I_USBRST, 0);
        break;
    case ESK_USB_RESET_END:
        o->reset_on = 0;
        o->enumerated = 1;
        rbits(o, GINTSTS, I_ENUMDNE, 0);
        break;
    case ESK_USB_SOF:
        o->frame = frame & 0x7ffu;
        rbits(o, GINTSTS, I_SOF, 0);
        break;
    case ESK_USB_RESUME:
        o->resume_on = 1;
        break;
    case ESK_USB_RESUME_END:
        o->resume_on = 0;
        break;
    default:
        break;
    }
    after(o);
}

/* the token is for this device: its address, or the old one while a
 * SET_ADDRESS's status stage is still to come */
static int for_me(struct otg *o, int addr)
{
    u32 dad = (rv(o, DCFG) >> 4) & 0x7fu;
    if (!o->enumerated)
        return 0;
    if (!o->addr_pending)
        o->addr = dad;
    if ((u32)addr == o->addr)
        return 1;
    if (o->addr_pending && (u32)addr == dad) {
        o->addr = dad; /* the host moved on: the status stage was done */
        o->addr_pending = 0;
        return 1;
    }
    return 0;
}

/* the host's token after a SETUP: the "SETUP stage done" status */
static void setup_stage_done(struct otg *o, int ep)
{
    if (!o->setup_done)
        return;
    if (rx_push(o, (u32)ep | PKT_SETUP_DONE << 17, 0, 0))
        o->setup_done = 0;
}

static int usb_setup(void *ctx, int addr, int ep, const u8 data[8])
{
    struct otg *o = (struct otg *)ctx;
    u32 tsz, cnt;
    if (!for_me(o, addr) || ep != 0)
        return ESK_USB_NONE;
    activity(o);
    if (!rx_push(o, (u32)ep | (8u << 4) | PKT_SETUP_DATA << 17, data, 8)) {
        after(o);
        return ESK_USB_NONE; /* no room: the SETUP is lost, the host retries */
    }
    o->setup_done = 1;
    tsz = rv(o, DOEPTSIZ(0));
    cnt = (tsz >> 29) & 3u;
    if (cnt)
        tsz = (tsz & ~(3u << 29)) | (cnt - 1u) << 29;
    else
        rbits(o, DOEPINT(0), B2BSTUP, 0); /* back-to-back SETUPs past STUPCNT */
    rset(o, DOEPTSIZ(0), tsz);
    /* a SETUP clears EP0's STALL and NAKs EP0 until the program says */
    rbits(o, DIEPCTL(0), NAKSTS, STALL);
    rbits(o, DOEPCTL(0), NAKSTS, STALL);
    o->in_pid[0] = o->out_pid[0] = 1;
    after(o);
    return ESK_USB_ACK;
}

static int usb_out(void *ctx, int addr, int ep, int data1, const u8 *data, u32 len)
{
    struct otg *o = (struct otg *)ctx;
    u32 ctl, tsz, pkt, xfr, mps;
    if (!for_me(o, addr) || ep < 0 || ep >= NEP)
        return ESK_USB_NONE;
    activity(o);
    if (ep == 0)
        setup_stage_done(o, ep);
    ctl = rv(o, DOEPCTL(ep));
    if (!(ctl & USBAEP)) {
        after(o);
        return ESK_USB_NONE;
    }
    if (ctl & STALL) {
        after(o);
        return ESK_USB_STALL;
    }
    tsz = rv(o, DOEPTSIZ(ep));
    pkt = (tsz >> 19) & (ep == 0 ? 1u : 0x3ffu);
    xfr = tsz & (ep == 0 ? 0x7fu : 0x7ffffu);
    if (!(ctl & EPENA) || (ctl & NAKSTS) || (rv(o, DCTL) & GONSTS) || !pkt) {
        after(o);
        return ESK_USB_NAK;
    }
    if ((data1 != 0) != (o->out_pid[ep] != 0)) {
        after(o);
        return ESK_USB_ACK; /* a repeat the device has: ACKed, dropped */
    }
    mps = out_mps(o, ep);
    if (!rx_push(o, (u32)ep | len << 4 | (u32)(data1 ? 2 : 0) << 15 | PKT_OUT_DATA << 17, data, len)) {
        after(o);
        return ESK_USB_NAK;
    }
    o->out_pid[ep] ^= 1;
    pkt--;
    xfr = len > xfr ? 0 : xfr - len;
    rset(o, DOEPTSIZ(ep), (tsz & ~((ep == 0 ? 1u : 0x3ffu) << 19) & ~(ep == 0 ? 0x7fu : 0x7ffffu)) | pkt << 19 | xfr);
    if (!pkt || len < mps)
        (void)rx_push(o, (u32)ep | PKT_OUT_DONE << 17, 0, 0);
    after(o);
    return ESK_USB_ACK;
}

static int usb_in(void *ctx, int addr, int ep, u8 *data, u32 cap, u32 *len, int *data1)
{
    struct otg *o = (struct otg *)ctx;
    u32 ctl, tsz, pkt, xfr, n, words, i, mps;
    if (!for_me(o, addr) || ep < 0 || ep >= NEP)
        return ESK_USB_NONE;
    activity(o);
    if (ep == 0)
        setup_stage_done(o, ep);
    ctl = rv(o, DIEPCTL(ep));
    if (!(ctl & USBAEP)) {
        after(o);
        return ESK_USB_NONE;
    }
    if (ctl & STALL) {
        after(o);
        return ESK_USB_STALL;
    }
    tsz = rv(o, DIEPTSIZ(ep));
    pkt = (tsz >> 19) & (ep == 0 ? 3u : 0x3ffu);
    xfr = tsz & (ep == 0 ? 0x7fu : 0x7ffffu);
    if (!o->tx[ep].used)
        rbits(o, DIEPINT(ep), ITTXFE, 0);
    if (!(ctl & EPENA) || (ctl & NAKSTS) || (rv(o, DCTL) & GINSTS) || !pkt) {
        after(o);
        return ESK_USB_NAK;
    }
    mps = ep_mps(o, ep);
    n = xfr < mps ? xfr : mps;
    words = (n + 3u) / 4u;
    if (o->tx[ep].used < words) {
        after(o);
        return ESK_USB_NAK; /* the packet is not all in the FIFO yet */
    }
    for (i = 0; i < words; i++) {
        u32 w = tx_pop(o, ep), k;
        for (k = 0; k < 4u && 4u * i + k < n; k++)
            if (4u * i + k < cap)
                data[4u * i + k] = (u8)(w >> (8u * k));
    }
    *len = n < cap ? n : cap;
    *data1 = o->in_pid[ep];
    o->in_pid[ep] ^= 1;
    pkt--;
    xfr -= n;
    rset(o, DIEPTSIZ(ep), (tsz & ~((ep == 0 ? 3u : 0x3ffu) << 19) & ~(ep == 0 ? 0x7fu : 0x7ffffu)) | pkt << 19 | xfr);
    if (!pkt) {
        rbits(o, DIEPINT(ep), XFRC, 0);
        rbits(o, DIEPCTL(ep), 0, EPENA);
    }
    if (ep == 0 && n == 0 && o->addr_pending) {
        /* the SET_ADDRESS's status stage: the new address from now */
        o->addr = (rv(o, DCFG) >> 4) & 0x7fu;
        o->addr_pending = 0;
    }
    after(o);
    return ESK_USB_ACK;
}

static int usb_pullup(void *ctx)
{
    struct otg *o = (struct otg *)ctx;
    return o->pulled ? ESK_USB_FULL_SPEED : ESK_USB_DETACHED;
}

/* ---- time ------------------------------------------------------------------- */

static void otg_tick(void *ctx, u32 cycles)
{
    struct otg *o = (struct otg *)ctx;
    u64 ps;
    int changed = 0;
    if (!o->running)
        return;
    if (o->since > o->s->cycles)
        o->since = 0;
    cycles = esk_mach_after(o->s, cycles, o->since);
    o->since = 0;
    ps = cycles_ps(o, cycles);
    if (o->mode_wait) {
        o->mode_ps += ps;
        if (o->mode_ps >= MODE_PS) {
            o->mode_wait = 0;
            rbits(o, GINTSTS, o->mode_want ? I_CMOD : 0u, o->mode_want ? 0u : I_CMOD);
            changed = 1;
        }
    }
    if (o->pulled && !o->suspended && !o->reset_on && !o->resume_on) {
        o->idle_ps += ps;
        if (o->idle_ps >= SUSPEND_PS) {
            /* 3 ms of an idle bus: suspend (USB 2.0 7.1.7.6) */
            o->suspended = 1;
            rbits(o, GINTSTS, I_ESUSP | I_USBSUSP, 0);
            changed = 1;
        }
    }
    if (changed)
        after(o);
    else
        clock_check(o);
}

static int otg_next(void *ctx, u32 *cycles)
{
    struct otg *o = (struct otg *)ctx;
    u64 left = ~0ull, c;
    if (!o->running)
        return 0;
    if (o->mode_wait)
        left = MODE_PS - o->mode_ps;
    if (o->pulled && !o->suspended && !o->reset_on && !o->resume_on && SUSPEND_PS - o->idle_ps < left)
        left = SUSPEND_PS - o->idle_ps;
    if (left == ~0ull)
        return 0;
    c = left / 1000000ull * (core_hz(o) / 1000000u) + 1u;
    *cycles = c > 0xffffffffu ? 0xffffffffu : (u32)c;
    return 1;
}

static void otg_reset(void *ctx)
{
    struct otg *o = (struct otg *)ctx;
    int was = o->pulled, ep;
    memset(o->ram, 0, sizeof o->ram);
    rx_flush(o);
    for (ep = 0; ep < NEP; ep++) {
        tx_flush(o, ep);
        o->in_pid[ep] = o->out_pid[ep] = 0;
    }
    o->setup_done = o->addr_pending = 0;
    o->addr = 0;
    o->suspended = o->reset_on = o->enumerated = o->resume_on = 0;
    o->frame = 0;
    o->running = 0;
    o->idle_ps = o->mode_ps = 0;
    o->mode_wait = 0;
    o->session = 0;
    o->level = 0;
    o->warned_fifo = o->warned_rx = 0;
    o->pulled = 0;
    if (o->s->host.pin_watch)
        (void)o->s->host.pin_watch(o->s->host.ctx, 0, 9, 0, VBUS_VALID_V);
    if (was && o->s->host.usb_changed)
        o->s->host.usb_changed(o->s->host.ctx); /* the pull-up is gone */
}

/* the pins' outside levels changed: VBUS on PA9 may have come or gone */
void esk_stm32_otg_fs_pins(struct esk_mach *m)
{
    if (m->usb.pullup != usb_pullup || !m->usb.ctx)
        return;
    after((struct otg *)m->usb.ctx);
}

const struct esk_dev_ops esk_stm32_otg_fs_ops = {
    "stm32-otg-fs", otg_read, otg_write, otg_reset, otg_tick, otg_next,
};

void *esk_stm32_otg_fs_create(struct esk_mach *m, struct esk_rf_periph *p)
{
    struct otg *o;
    int k;
    if (!esk_rf_reg(p, "GRXSTSP") || !esk_rf_reg(p, "DOEPTSIZ5"))
        return 0;
    o = (struct otg *)esk_mach_alloc(m, sizeof *o);
    if (!o)
        return 0;
    o->s = m;
    o->p = p;
    o->irq = esk_rf_irq_named(p->rf, "OTG_FS");
    p->model_rsv = 1; /* the FIFO windows and the RAM's debug view */
    for (k = 0; k < p->nreg; k++) {
        u32 off = p->reg[k]->addr - p->base;
        if (off < REGS_END)
            o->r[off / 4u] = p->reg[k];
    }
    m->usb.ctx = o;
    m->usb.dp_at = 12; /* PA12 */
    m->usb.pullup = usb_pullup;
    m->usb.bus = usb_bus;
    m->usb.setup = usb_setup;
    m->usb.out = usb_out;
    m->usb.in = usb_in;
    return o;
}
