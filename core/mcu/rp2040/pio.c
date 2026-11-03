/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* pio.c -- the RP2040's programmable I/O blocks, PIO0 and PIO1 (its
 * datasheet, chapter 3).
 *
 * Each block: 32 words of instruction memory shared by four state
 * machines, a 4-deep TX and RX FIFO each (8 deep one way with FJOIN), eight
 * IRQ flags, and the 30 GPIOs it drives where IO_BANK0 gives them to it
 * (FUNCSEL 6 or 7). Each state machine:
 *
 *   - runs at clk_sys / (CLKDIV.INT + CLKDIV.FRAC / 256), one cycle a tick
 *     of that clock, from SM_ENABLE; SM_RESTART clears its shift counters,
 *     ISR, delay and stall, CLKDIV_RESTART its clock's phase;
 *   - executes the nine instructions as 3.4 gives them: JMP (always, !X,
 *     X--, !Y, Y--, X!=Y, PIN, !OSRE), WAIT (GPIO, PIN, IRQ, either
 *     polarity; an IRQ waited high is cleared), IN (PINS, X, Y, NULL, ISR,
 *     OSR), OUT (PINS, X, Y, NULL, PINDIRS, PC, ISR, EXEC), PUSH and PULL
 *     (IfFull, IfEmpty, Block; a non-blocking PULL on an empty FIFO copies
 *     X), MOV (to PINS, X, Y, EXEC, PC, ISR, OSR; from PINS, X, Y, NULL,
 *     STATUS, ISR, OSR; invert, bit-reverse), IRQ (set, clear, wait; the
 *     relative index), SET (PINS, X, Y, PINDIRS);
 *   - the delay/side-set field: SIDESET_COUNT bits of side-set (the top one
 *     its enable with SIDE_EN), to pins or pin directions (SIDE_PINDIR),
 *     asserted as the instruction starts; the rest delay cycles once it
 *     completes; a stalled instruction is retried each cycle;
 *   - wraps from WRAP_TOP to WRAP_BOTTOM;
 *   - shifts ISR and OSR left or right (IN_SHIFTDIR, OUT_SHIFTDIR) and with
 *     AUTOPUSH and AUTOPULL fills or empties them at PUSH_THRESH and
 *     PULL_THRESH (0 meaning 32), stalling while the FIFO cannot;
 *   - its pins: OUT_BASE/OUT_COUNT, SET_BASE/SET_COUNT, SIDESET_BASE, the
 *     IN_BASE inputs rotated, JMP_PIN; outputs and enables of all state
 *     machines combined, the higher-numbered state machine winning a pin
 *     both write in a cycle;
 *   - STATUS (STATUS_SEL, STATUS_N): all ones while the TX (or RX) level is
 *     below N;
 *   - SMx_INSTR written executes that instruction at once (EXEC_STALLED
 *     while it stalls).
 * The block: TXFx writes push (TXOVER when full), RXFx reads pop (RXUNDER
 * when empty); FSTAT, FLEVEL and FDEBUG (TXSTALL, RXSTALL as the state
 * machines stall); IRQ and IRQ_FORCE; INTR (RXNEMPTY, TXNFULL, the SM IRQ
 * flags 0-3), IRQ0/IRQ1 INTE, INTF and INTS raising PIOx_IRQ_0 and
 * PIOx_IRQ_1; the FIFOs' DMA requests (TX while not full, RX while not
 * empty).
 *
 * Omissions: the input synchroniser's two cycles (INPUT_SYNC_BYPASS: inputs
 * are read at once), OUT_STICKY and INLINE_OUT_EN, the debug registers
 * (DBG_PADOUT, DBG_PADOE give the outputs; DBG_CFGINFO its sizes), a
 * delay on an EXEC'd instruction taken as on any other. */
#include <string.h>

#include "mcu/rp2040/rp2040.h"

struct sm {
    u32 pc, x, y, isr, osr;
    int isr_n, osr_n;     /* bits shifted in, out */
    int delay;            /* cycles of delay left */
    int exec;             /* an instruction to run next (OUT EXEC, MOV EXEC, SMx_INSTR) */
    u16 exec_instr;
    u32 tx[8], rx[8];
    int ntx, nrx;
    u64 acc;              /* clk_sys cycles * 256 towards its next clock */
    int stalled;
};

struct pio {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    int unit;
    u16 mem[32];
    struct sm sm[4];
    u32 irqf;             /* the 8 IRQ flags */
    u32 out, oe;          /* the pins it drives */
    u32 fdebug;
    int irq0, irq1, was0, was1, running;
    int poke;             /* a bus access since the last tick: every stalled machine retries */
};

static u32 rv(struct pio *k, u32 off) { return esk_rf_read(k->p, off, 4); }

static struct esk_rf_reg *at(struct pio *k, u32 off)
{
    int i;
    for (i = 0; i < k->p->nreg; i++)
        if (k->p->reg[i]->addr == k->p->base + off)
            return k->p->reg[i];
    return 0;
}

static void hw(struct pio *k, u32 off, u32 v)
{
    struct esk_rf_reg *r = at(k, off);
    if (r)
        esk_rf_hw(r, v);
}

/* a state machine's registers: 0 CLKDIV, 1 EXECCTRL, 2 SHIFTCTRL, 3 ADDR, 4 INSTR, 5 PINCTRL */
static u32 smr(struct pio *k, int n, int r) { return rv(k, 0xc8u + (u32)n * 0x18u + (u32)r * 4u); }

static int depth_tx(struct pio *k, int n)
{
    u32 sh = smr(k, n, 2);
    return (sh & (1u << 31)) ? 0 : (sh & (1u << 30)) ? 8 : 4;
}

static int depth_rx(struct pio *k, int n)
{
    u32 sh = smr(k, n, 2);
    return (sh & (1u << 30)) ? 0 : (sh & (1u << 31)) ? 8 : 4;
}

static u32 rotr(u32 v, int n) { return n ? (v >> n) | (v << (32 - n)) : v; }

static u32 bitrev(u32 v)
{
    u32 r = 0;
    int i;
    for (i = 0; i < 32; i++)
        r |= (v >> i & 1u) << (31 - i);
    return r;
}

/* the 30 GPIOs' input levels */
static u32 pins_in(struct pio *k) { return esk_rp2040_gpio_in(k->s); }

/* `count` bits of `v` onto the pins (or pin directions) from `base` */
static void put_pins(struct pio *k, int dirs, int base, int count, u32 v)
{
    int i;
    for (i = 0; i < count; i++) {
        int pin = (base + i) % 32;
        u32 b = 1u << pin, bit = v >> i & 1u;
        if (dirs)
            k->oe = (k->oe & ~b) | (bit ? b : 0);
        else
            k->out = (k->out & ~b) | (bit ? b : 0);
    }
}

static void status(struct pio *k)
{
    u32 fstat = 0, flevel = 0, intr = 0, ints0, ints1;
    int n;
    for (n = 0; n < 4; n++) {
        struct sm *m = &k->sm[n];
        int dt = depth_tx(k, n), dr = depth_rx(k, n);
        if (m->nrx >= dr)
            fstat |= 1u << n;
        if (!m->nrx)
            fstat |= 1u << (8 + n);
        if (m->ntx >= dt)
            fstat |= 1u << (16 + n);
        if (!m->ntx)
            fstat |= 1u << (24 + n);
        flevel |= (u32)(m->ntx & 15) << (8 * n) | (u32)(m->nrx & 15) << (8 * n + 4);
        if (m->nrx)
            intr |= 1u << n;
        if (m->ntx < dt)
            intr |= 1u << (4 + n);
        /* ADDR, and INSTR reads the instruction it is on */
        hw(k, 0xc8u + (u32)n * 0x18u + 12u, m->pc);
        hw(k, 0xc8u + (u32)n * 0x18u + 16u, k->mem[m->pc & 31u]);
        {
            u32 ex = smr(k, n, 1);
            hw(k, 0xc8u + (u32)n * 0x18u + 4u, (ex & ~(1u << 31)) | (m->exec && m->stalled ? 1u << 31 : 0));
        }
    }
    intr |= (k->irqf & 15u) << 8;
    hw(k, 0x004, fstat);
    hw(k, 0x008, k->fdebug);
    hw(k, 0x00c, flevel);
    hw(k, 0x030, k->irqf);
    hw(k, 0x03c, k->out);
    hw(k, 0x040, k->oe);
    hw(k, 0x044, 32u << 16 | 4u << 8 | 4u); /* IMEM_SIZE, SM_COUNT, FIFO_DEPTH */
    hw(k, 0x128, intr);
    ints0 = (intr | rv(k, 0x130)) & rv(k, 0x12c) & 0xfffu;
    ints1 = (intr | rv(k, 0x13c)) & rv(k, 0x138) & 0xfffu;
    hw(k, 0x134, ints0);
    hw(k, 0x140, ints1);
    esk_rf_irq_level(k->p, k->irq0, ints0 != 0, &k->was0);
    esk_rf_irq_level(k->p, k->irq1, ints1 != 0, &k->was1);
}

static int thresh(u32 v) { return v ? (int)v : 32; }

/* ---- the instructions --------------------------------------------------- */

static int irq_index(int n, u32 idx)
{
    if (idx & 0x10u) /* relative: the state machine's number added to the low two bits */
        return (int)((idx & 4u) | ((idx + (u32)n) & 3u));
    return (int)(idx & 7u);
}

static int push(struct pio *k, int n, int block)
{
    struct sm *m = &k->sm[n];
    if (m->nrx >= depth_rx(k, n)) {
        if (block) {
            k->fdebug |= 1u << n; /* RXSTALL */
            return 0;
        }
        m->isr = 0; /* not blocking: the ISR's contents are lost */
        m->isr_n = 0;
        return 1;
    }
    m->rx[m->nrx++] = m->isr;
    m->isr = 0;
    m->isr_n = 0;
    return 1;
}

static int pull(struct pio *k, int n, int block)
{
    struct sm *m = &k->sm[n];
    if (!m->ntx) {
        if (block) {
            k->fdebug |= 1u << (24 + n); /* TXSTALL */
            return 0;
        }
        m->osr = m->x;
        m->osr_n = 0;
        return 1;
    }
    m->osr = m->tx[0];
    memmove(m->tx, m->tx + 1, (size_t)(m->ntx - 1) * sizeof m->tx[0]);
    m->ntx--;
    m->osr_n = 0;
    return 1;
}

/* one instruction; 1 when it completed, 0 when it stalls. *jumped when it
 * set the PC itself */
static int execute(struct pio *k, int n, u16 ins, int *jumped)
{
    struct sm *m = &k->sm[n];
    u32 ex = smr(k, n, 1), sh = smr(k, n, 2), pc_ = smr(k, n, 5);
    int op = ins >> 13, a = ins >> 5 & 7, idx = ins & 31;
    int out_base = (int)(pc_ & 31u), out_count = (int)(pc_ >> 20 & 63u), set_base = (int)(pc_ >> 5 & 31u);
    int set_count = (int)(pc_ >> 26 & 7u), in_base = (int)(pc_ >> 15 & 31u);
    int push_t = thresh(sh >> 20 & 31u), pull_t = thresh(sh >> 25 & 31u);
    *jumped = 0;
    switch (op) {
    case 0: { /* JMP */
        int go = 0;
        switch (a) {
        case 0: go = 1; break;
        case 1: go = m->x == 0; break;
        case 2: go = m->x != 0; m->x--; break;
        case 3: go = m->y == 0; break;
        case 4: go = m->y != 0; m->y--; break;
        case 5: go = m->x != m->y; break;
        case 6: go = (int)(pins_in(k) >> (ex >> 24 & 31u) & 1u); break;
        default: go = m->osr_n < pull_t; break;
        }
        if (go) {
            m->pc = (u32)idx;
            *jumped = 1;
        }
        return 1;
    }
    case 1: { /* WAIT */
        int pol = ins >> 7 & 1, src = ins >> 5 & 3, ok = 1;
        if (src == 0)
            ok = (int)(pins_in(k) >> idx & 1u) == pol;
        else if (src == 1)
            ok = (int)(pins_in(k) >> ((in_base + idx) % 32) & 1u) == pol;
        else if (src == 2) {
            int f = irq_index(n, (u32)idx);
            ok = (int)(k->irqf >> f & 1u) == pol;
            if (ok && pol)
                k->irqf &= ~(1u << f);
        }
        return ok;
    }
    case 2: { /* IN */
        int cnt = idx ? idx : 32;
        u32 v = 0, mask = cnt == 32 ? 0xffffffffu : (1u << cnt) - 1u;
        if ((sh & (1u << 16)) && m->isr_n >= push_t && m->nrx >= depth_rx(k, n))
            return 0; /* autopush: waits for room before shifting more in */
        switch (a) {
        case 0: v = rotr(pins_in(k), in_base); break;
        case 1: v = m->x; break;
        case 2: v = m->y; break;
        case 6: v = m->isr; break;
        case 7: v = m->osr; break;
        default: v = 0; break;
        }
        v &= mask;
        if (sh & (1u << 18)) /* right */
            m->isr = cnt == 32 ? v : (m->isr >> cnt) | (v << (32 - cnt));
        else
            m->isr = cnt == 32 ? v : (m->isr << cnt) | v;
        m->isr_n = m->isr_n + cnt > 32 ? 32 : m->isr_n + cnt;
        if ((sh & (1u << 16)) && m->isr_n >= push_t)
            (void)push(k, n, 1);
        return 1;
    }
    case 3: { /* OUT */
        int cnt = idx ? idx : 32;
        u32 v;
        if ((sh & (1u << 17)) && m->osr_n >= pull_t) { /* autopull: refill first */
            if (!m->ntx) {
                k->fdebug |= 1u << (24 + n);
                return 0;
            }
            (void)pull(k, n, 1);
        }
        if (sh & (1u << 19)) { /* right */
            v = cnt == 32 ? m->osr : m->osr & ((1u << cnt) - 1u);
            m->osr = cnt == 32 ? 0 : m->osr >> cnt;
        } else {
            v = cnt == 32 ? m->osr : m->osr >> (32 - cnt);
            m->osr = cnt == 32 ? 0 : m->osr << cnt;
        }
        m->osr_n = m->osr_n + cnt > 32 ? 32 : m->osr_n + cnt;
        switch (a) {
        case 0: put_pins(k, 0, out_base, out_count, v); break;
        case 1: m->x = v; break;
        case 2: m->y = v; break;
        case 4: put_pins(k, 1, out_base, out_count, v); break;
        case 5: m->pc = v & 31u; *jumped = 1; break;
        case 6: m->isr = v; m->isr_n = cnt; break;
        case 7: m->exec = 1; m->exec_instr = (u16)v; break;
        default: break;
        }
        if ((sh & (1u << 17)) && m->osr_n >= pull_t && m->ntx)
            (void)pull(k, n, 1);
        return 1;
    }
    case 4: /* PUSH, PULL */
        if (ins & 0x80u) {
            if ((ins & 0x40u) && m->osr_n < pull_t)
                return 1; /* IfEmpty: not yet */
            return pull(k, n, (ins & 0x20u) != 0);
        }
        if ((ins & 0x40u) && m->isr_n < push_t)
            return 1; /* IfFull: not yet */
        return push(k, n, (ins & 0x20u) != 0);
    case 5: { /* MOV */
        int src = ins & 7, opn = ins >> 3 & 3;
        u32 v = 0;
        switch (src) {
        case 0: v = rotr(pins_in(k), in_base); break;
        case 1: v = m->x; break;
        case 2: v = m->y; break;
        case 5: {
            int nn = (int)(ex & 15u);
            v = (ex & 0x10u) ? (m->nrx < nn ? 0xffffffffu : 0) : (m->ntx < nn ? 0xffffffffu : 0);
            break;
        }
        case 6: v = m->isr; break;
        case 7: v = m->osr; break;
        default: v = 0; break;
        }
        if (opn == 1)
            v = ~v;
        else if (opn == 2)
            v = bitrev(v);
        switch (a) {
        case 0: put_pins(k, 0, out_base, out_count, v); break;
        case 1: m->x = v; break;
        case 2: m->y = v; break;
        case 4: m->exec = 1; m->exec_instr = (u16)v; break;
        case 5: m->pc = v & 31u; *jumped = 1; break;
        case 6: m->isr = v; m->isr_n = 0; break;
        case 7: m->osr = v; m->osr_n = 0; break;
        default: break;
        }
        return 1;
    }
    case 6: { /* IRQ */
        int f = irq_index(n, (u32)idx);
        if (ins & 0x40u) {
            k->irqf &= ~(1u << f);
            return 1;
        }
        if (!m->stalled)
            k->irqf |= 1u << f;
        if (ins & 0x20u) /* wait until the system clears it */
            return !(k->irqf >> f & 1u);
        return 1;
    }
    default: /* SET */
        switch (a) {
        case 0: put_pins(k, 0, set_base, set_count, (u32)idx); break;
        case 1: m->x = (u32)idx; break;
        case 2: m->y = (u32)idx; break;
        case 4: put_pins(k, 1, set_base, set_count, (u32)idx); break;
        default: break;
        }
        return 1;
    }
}

/* the side-set and delay of an instruction */
static void side_delay(struct pio *k, int n, u16 ins, int *delay, int apply_side)
{
    u32 ex = smr(k, n, 1), pc_ = smr(k, n, 5);
    int count = (int)(pc_ >> 29 & 7u), base = (int)(pc_ >> 10 & 31u), field = ins >> 8 & 31;
    int opt = (ex & (1u << 30)) != 0, dbits = 5 - count, bits = count - opt;
    *delay = dbits > 0 ? field & ((1 << dbits) - 1) : 0;
    if (count && apply_side) {
        u32 side = (u32)(field >> dbits);
        int enabled = 1;
        if (opt) {
            enabled = (int)(side >> bits & 1u);
            side &= (1u << bits) - 1u;
        }
        if (enabled && bits > 0)
            put_pins(k, (ex & (1u << 29)) != 0, base, bits, side);
    }
}

/* one clock of state machine n */
static void cycle(struct pio *k, int n)
{
    struct sm *m = &k->sm[n];
    u32 ex = smr(k, n, 1);
    u16 ins;
    int jumped, done, delay, from_exec = m->exec;
    if (m->delay > 0 && !m->exec) {
        m->delay--;
        return;
    }
    ins = from_exec ? m->exec_instr : k->mem[m->pc & 31u];
    side_delay(k, n, ins, &delay, !m->stalled);
    if (from_exec)
        m->exec = 0;
    done = execute(k, n, ins, &jumped);
    if (!done) {
        m->stalled = 1;
        if (from_exec) { /* a stalled EXEC'd instruction stays pending */
            m->exec = 1;
            m->exec_instr = ins;
        }
        return;
    }
    m->stalled = 0;
    m->delay = delay;
    if (!jumped && !from_exec) {
        u32 top = ex >> 12 & 31u, bottom = ex >> 7 & 31u;
        m->pc = m->pc == top ? bottom : (m->pc + 1u) & 31u;
    }
}

/* ---- time --------------------------------------------------------------- */

static int any_on(struct pio *k) { return (rv(k, 0) & 15u) != 0; }

static u64 period(struct pio *k, int n)
{
    u32 d = smr(k, n, 0);
    u64 p = (u64)(d >> 16) * 256u + (d >> 8 & 0xffu);
    return p ? p : 65536u * 256u; /* INT 0 means 65536 */
}

/* a state machine stalled with no delay left on anything but a WAIT for a
 * GPIO or pin: only a bus write (a FIFO, IRQ, SMx_INSTR, CTRL) or another
 * state machine releases it, so its own clock brings nothing */
static int quiet(const struct pio *k, int n)
{
    const struct sm *m = &k->sm[n];
    u16 ins;
    if (k->poke || !m->stalled || m->delay > 0)
        return 0;
    ins = m->exec ? m->exec_instr : k->mem[m->pc & 31u];
    return !((ins >> 13) == 1u && (ins >> 5 & 3u) < 2u);
}

static void pio_tick(void *ctx, u32 cycles)
{
    struct pio *k = (struct pio *)ctx;
    u32 en = rv(k, 0) & 15u, c;
    u64 per[4];
    int n, live = 0;
    for (n = 0; n < 4; n++) {
        per[n] = en >> n & 1u ? period(k, n) : 0;
        if (per[n] && !quiet(k, n))
            live = 1;
    }
    if (!live) { /* every machine waits on the bus: only their clocks' phases go on */
        for (n = 0; n < 4; n++)
            if (per[n])
                k->sm[n].acc = (k->sm[n].acc + (u64)cycles * 256u) % per[n];
        return;
    }
    for (c = 0; c < cycles; c++) {
        u32 out = k->out, oe = k->oe;
        for (n = 0; n < 4; n++) {
            struct sm *m = &k->sm[n];
            if (!per[n])
                continue;
            m->acc += 256u;
            while (m->acc >= per[n]) {
                m->acc -= per[n];
                cycle(k, n);
            }
        }
        if (k->out != out || k->oe != oe) {
            /* the pins as they are this cycle, dated where they changed */
            k->s->output_lag = cycles - c - 1;
            esk_rp2040_pins_changed(k->s);
            k->s->output_lag = 0;
        }
    }
    k->poke = 0;
    status(k);
}

static int pio_next(void *ctx, u32 *cycles)
{
    struct pio *k = (struct pio *)ctx;
    u32 en = rv(k, 0) & 15u, best = 0xffffffffu;
    int n, any = 0;
    /* the next clock of a running state machine: it acts on its own clock */
    for (n = 0; n < 4; n++) {
        u64 per, need;
        if (!(en >> n & 1u) || quiet(k, n))
            continue;
        per = period(k, n);
        need = (per - k->sm[n].acc + 255u) / 256u;
        if (need < best)
            best = need ? (u32)need : 1u;
        any = 1;
    }
    *cycles = best;
    return any;
}

/* ---- the registers ------------------------------------------------------ */

static u32 pio_read(void *ctx, u32 off, int n)
{
    struct pio *k = (struct pio *)ctx;
    u32 o = off & ~3u;
    if (o >= 0x20u && o <= 0x2cu && !k->s->bus.debug) { /* RXFx: popped */
        struct sm *m = &k->sm[(o - 0x20u) / 4u];
        u32 v = 0;
        if (m->nrx) {
            v = m->rx[0];
            memmove(m->rx, m->rx + 1, (size_t)(m->nrx - 1) * sizeof m->rx[0]);
            m->nrx--;
        } else {
            k->fdebug |= 1u << (8 + (o - 0x20u) / 4u); /* RXUNDER */
        }
        k->poke = 1;
        status(k);
        return v;
    }
    return esk_rf_read(k->p, off, n);
}

static void clock_it(struct pio *k) { esk_rf_clock(k->p, &k->running, any_on(k)); }

static void pio_write(void *ctx, u32 off, int n, u32 v)
{
    struct pio *k = (struct pio *)ctx;
    u32 o = off & ~3u;
    int i;
    k->poke = 1;
    if (o >= 0x10u && o <= 0x1cu) { /* TXFx: pushed */
        struct sm *m = &k->sm[(o - 0x10u) / 4u];
        int sm = (int)((o - 0x10u) / 4u);
        if (m->ntx < depth_tx(k, sm))
            m->tx[m->ntx++] = v;
        else
            k->fdebug |= 1u << (16 + sm); /* TXOVER */
        status(k);
        return;
    }
    if (o >= 0x48u && o < 0x48u + 32u * 4u) { /* INSTR_MEMx */
        k->mem[(o - 0x48u) / 4u] = (u16)v;
        return;
    }
    esk_rf_write(k->p, off, n, v);
    if (o == 0x000u) { /* CTRL: SM_RESTART and CLKDIV_RESTART act and read 0 */
        for (i = 0; i < 4; i++) {
            struct sm *m = &k->sm[i];
            if (v >> (4 + i) & 1u) {
                m->isr = m->osr = 0;
                m->isr_n = 0;
                m->osr_n = 32; /* empty */
                m->delay = 0;
                m->stalled = 0;
                m->exec = 0;
            }
            if (v >> (8 + i) & 1u)
                m->acc = 0;
        }
        hw(k, 0, v & 15u);
    } else if (o == 0x008u) {
        k->fdebug &= ~v; /* write 1 to clear */
    } else if (o == 0x030u) {
        k->irqf &= ~(v & 0xffu);
    } else if (o == 0x034u) {
        k->irqf |= v & 0xffu;
    } else if (o >= 0xc8u && o < 0xc8u + 4u * 0x18u && (o - 0xc8u) % 0x18u == 16u) { /* SMx_INSTR: at once */
        int sm = (int)((o - 0xc8u) / 0x18u), jumped, delay;
        struct sm *m = &k->sm[sm];
        side_delay(k, sm, (u16)v, &delay, 1);
        if (!execute(k, sm, (u16)v, &jumped)) {
            m->exec = 1;
            m->exec_instr = (u16)v;
            m->stalled = 1;
        }
        esk_rp2040_pins_changed(k->s);
    }
    status(k);
    clock_it(k);
}

static void pio_reset(void *ctx)
{
    struct pio *k = (struct pio *)ctx;
    int i;
    memset(k->sm, 0, sizeof k->sm);
    for (i = 0; i < 4; i++)
        k->sm[i].osr_n = 32;
    memset(k->mem, 0, sizeof k->mem);
    k->irqf = k->out = k->oe = k->fdebug = 0;
    status(k);
    clock_it(k);
}

const struct esk_dev_ops esk_rp2040_pio_ops = { "rp2040-pio", pio_read, pio_write, pio_reset, pio_tick, pio_next };

static struct pio *pio_of(struct esk_mach *s, int unit)
{
    static const char *const names[] = { "PIO0", "PIO1" };
    return unit >= 0 && unit <= 1 ? (struct pio *)esk_rp_model(s, names[unit], &esk_rp2040_pio_ops) : 0;
}

void esk_rp2040_pio_pins(struct esk_mach *s, int unit, u32 *out, u32 *oe)
{
    struct pio *k = pio_of(s, unit);
    *out = k ? k->out : 0;
    *oe = k ? k->oe : 0;
}

int esk_rp2040_pio_dreq(struct esk_mach *s, int unit, int sm, int rx)
{
    struct pio *k = pio_of(s, unit);
    if (!k || sm < 0 || sm > 3)
        return 0;
    return rx ? k->sm[sm].nrx > 0 : k->sm[sm].ntx < depth_tx(k, sm);
}

void *esk_rp2040_pio_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct pio *k;
    if (strcmp(p->name, "PIO0") != 0 && strcmp(p->name, "PIO1") != 0)
        return 0;
    k = (struct pio *)esk_mach_alloc(s, sizeof *k);
    if (!k)
        return 0;
    k->s = s;
    k->p = p;
    k->unit = p->name[3] - '0';
    k->irq0 = esk_rf_irq(p, "IRQ_0");
    k->irq1 = esk_rf_irq(p, "IRQ_1");
    p->model_rsv = 0;
    return k;
}
