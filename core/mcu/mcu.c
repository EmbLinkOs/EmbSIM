/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* mcu.c -- the MCU runtime (mcu.h says what it does). */
#include <stdio.h>
#include <string.h>

#include "mcu/build.h"
#include "mcu/dwarf.h"
#include "mcu/mcu.h"
#include "util/str.h"

#define MCU_PORTS 8
#define MCU_UARTS 8
#define MCU_TEXT_CAP 65536u
#define MCU_INQ_CAP 4096u
#define MCU_DEFAULT_QUANTUM_PS 10000000u /* 10 us */
#define PS_PER_S UINT64_C(1000000000000)
#define MEGA UINT64_C(1000000)

typedef struct mcu_uart {
    const char *unit;
    struct esk_rx_port *rx;
    char *text; /* what it sent, the last MCU_TEXT_CAP - 1 bytes */
    size_t text_len;
    u64 tx_bytes, rx_bytes;
    /* the TX pin's waveform */
    esk_pin *tx_pin;
    unsigned frame;
    int nbits, bit;
    es_time_t bit_ps;
    esk_event_handle tx_ev;
    /* bytes from the session's serial input */
    unsigned char inq[MCU_INQ_CAP];
    size_t inq_head, inq_len;
    esk_event_handle in_ev;
    /* bytes from the RX pin */
    esk_pin *rx_pin;
    int rx_busy, rx_bit;
    unsigned rx_shift;
    es_time_t rx_bit_ps;
    esk_event_handle rx_ev;
} mcu_uart;

typedef struct mcu_pinq {
    esk_mach_pin cfg;     /* the latest configuration */
    esk_event_handle ev;  /* its application, pending */
    int known;
} mcu_pinq;

typedef struct mcu_state {
    esk_comp *c;
    struct esk_mach m;
    int attached;
    esk_pin *gpio[MCU_PORTS][16];
    mcu_pinq pq[MCU_PORTS][16];
    esk_pin *nrst, *boot0;
    unsigned char *image;
    size_t image_len;
    char *image_name;
    esk_dwarf dwarf; /* the image's source lines, empty without DWARF */
    es_time_t quantum_ps;
    /* time: the engine time of the machine's current cycle */
    es_time_t t;
    u64 rem;
    u32 rem_hz;
    /* within a quantum: its start, in time and cycles, and the clock */
    int in_quantum;
    es_time_t q_t;
    u64 q_cyc;
    u32 q_hz;
    esk_event_handle ev;
    es_time_t ev_at;
    int pin_changed;
    int held, boot_fail, stopped, ended_reported;
    int fresh; /* the first quantum after a reset looks at NRST and BOOT0 */
    const char *stop_why;
    u64 steps; /* instructions a debugger's step still runs */
    mcu_uart uart[MCU_UARTS];
    int nuart;
    char console[16];
} mcu_state;

static mcu_state *state_of(const esk_comp *c) { return (mcu_state *)c->state; }

/* ---- time ------------------------------------------------------------------ */

/* `c` cycles at `hz` in picoseconds, with the remainder carried in *rem
 * (c * 10^12 + *rem = ps * hz + new *rem), exactly */
static es_time_t cycles_to_ps(u64 c, u32 hz, u64 *rem)
{
    es_time_t ps = 0;
    u64 r = rem ? *rem : 0;
    if (!hz)
        return 0;
    while (c) {
        u64 chunk = c > (UINT64_C(1) << 40) ? (UINT64_C(1) << 40) : c;
        u64 a = chunk * MEGA, q = a / hz, r1 = a % hz;
        u64 b = r1 * MEGA + r, q2 = b / hz;
        r = b % hz;
        ps += q * MEGA + q2;
        c -= chunk;
    }
    if (rem)
        *rem = r;
    return ps;
}

/* the whole cycles in `ps` at `hz`, rounded down */
static u64 ps_to_cycles(es_time_t ps, u32 hz)
{
    u64 c = 0;
    while (ps) {
        es_time_t chunk = ps > UINT64_C(1000000000000000) ? UINT64_C(1000000000000000) : ps;
        u64 q = chunk / MEGA, r = chunk % MEGA;
        c += (q * hz + (r * hz) / MEGA) / MEGA;
        ps -= chunk;
    }
    return c;
}

static es_time_t local_now(const mcu_state *st)
{
    if (st->in_quantum)
        return st->q_t + cycles_to_ps(st->m.cycles - st->q_cyc, st->q_hz, NULL);
    return st->t;
}

/* the machine's time moves on by `c` cycles from st->t */
static void advance_time(mcu_state *st, u64 c, u32 hz)
{
    if (hz != st->rem_hz) {
        st->rem = 0;
        st->rem_hz = hz;
    }
    st->t += cycles_to_ps(c, hz, &st->rem);
}

/* the core slept (or was held) while the session moved on: its devices see
 * the time up to `now` */
static void catch_up(mcu_state *st, es_time_t now)
{
    u32 hz;
    u64 c;
    if (st->in_quantum || now <= st->t)
        return;
    hz = esk_mach_core_hz(&st->m);
    /* whole cycles, rounded up: the machine may end a fraction of a cycle
     * ahead, never behind (a device one cycle from its event gets there) */
    c = ps_to_cycles(now - st->t, hz);
    if (cycles_to_ps(c, hz, NULL) < now - st->t)
        c++;
    if (c)
        esk_mach_idle(&st->m, c);
    advance_time(st, c, hz);
}

static void raise(mcu_state *st, es_severity_t sev, const char *code, const char *fmt, const char *a,
                  const char *b)
{
    esk_engine *e = st->c->e;
    esk_diag_raise(e->diag, sev, code, local_now(st), "component", st->c->id, NULL, 0, fmt, a ? a : "",
                   b ? b : "");
}

/* ---- the quantum ------------------------------------------------------------- */

static void quantum_cb(void *ctx, void *arg, es_time_t now);

static void schedule_at(mcu_state *st, es_time_t at)
{
    esk_engine *e = st->c->e;
    if (at < e->sched.now)
        at = e->sched.now;
    if (st->ev) {
        if (st->ev_at <= at)
            return; /* an earlier one stands */
        esk_sched_cancel(&e->sched, st->ev);
        st->ev = 0;
    }
    if (esk_sched_at(&e->sched, at, ESK_EV_CORE, quantum_cb, st, NULL, &st->ev) == ES_OK)
        st->ev_at = at;
    else
        esk_comp_diag(st->c, ES_SEV_FATAL, "sched.queue_full", "the MCU's next quantum cannot be scheduled");
}

/* asleep: the next quantum when its devices' next event comes */
static void schedule_sleep(mcu_state *st)
{
    u32 left = 0;
    if (esk_mach_next_event(&st->m, &left))
        schedule_at(st, st->t + cycles_to_ps(left ? left : 1, esk_mach_core_hz(&st->m), NULL));
}

static int runnable(const mcu_state *st)
{
    return st->attached && !st->held && !st->boot_fail && !st->stopped && st->m.state == ESK_MACH_RUN;
}

/* after something outside changed the machine (a pin, a byte): a sleeping
 * core an interrupt woke runs from now */
static void wake(mcu_state *st)
{
    esk_engine *e = st->c->e;
    if (!runnable(st) || st->m.asleep)
        return;
    schedule_at(st, st->t > e->sched.now ? st->t : e->sched.now);
}

static void report_end(mcu_state *st)
{
    char buf[32];
    if (st->ended_reported)
        return;
    st->ended_reported = 1;
    if (st->m.state == ESK_MACH_EXIT) {
        snprintf(buf, sizeof buf, "%d", st->m.exit_status);
        raise(st, ES_SEV_INFO, "mcu.exit", "the firmware exited with status %s (%s)", buf, st->m.end_why);
    } else if (st->m.state == ESK_MACH_LOCKUP) {
        raise(st, ES_SEV_ERROR, "mcu.lockup", "the core locked up: %s%s", st->m.end_why, NULL);
    }
}

static void mcu_reset_now(mcu_state *st);
static int level_of(const esk_pin *p, int dflt);

static void quantum_cb(void *ctx, void *arg, es_time_t now)
{
    mcu_state *st = (mcu_state *)ctx;
    esk_engine *e = st->c->e;
    es_time_t next, limit;
    u64 budget, used;
    u32 hz;
    ES_UNUSED(arg);
    st->ev = 0;
    if (!runnable(st))
        return;
    catch_up(st, now);
    if (st->fresh) {
        /* the pins the reset looks at, now that the circuit has settled */
        st->fresh = 0;
        if (!level_of(st->nrst, 1)) {
            st->held = 1;
            return;
        }
        if (level_of(st->boot0, 0)) {
            st->boot_fail = 1;
            raise(st, ES_SEV_WARNING, "mcu.boot_mode",
                  "BOOT0 is high: the part boots its system memory (the vendor's bootloader), which is "
                  "not simulated; the core stays idle%s%s", NULL, NULL);
            return;
        }
    }
    if (st->m.asleep) {
        if (st->m.cpu->ops->wake_pending(st->m.cpu)) {
            st->m.asleep = 0;
        } else {
            schedule_sleep(st);
            return;
        }
    }
    /* up to the next event that is not an analog step (islands clip to
     * this quantum's end instead), and the quantum's bound */
    next = esk_sched_next_time_excluding(&e->sched, ESK_EV_ANALOG);
    limit = next;
    if (limit == ES_TIME_MAX || limit - now > st->quantum_ps)
        limit = now + st->quantum_ps;
    hz = esk_mach_core_hz(&st->m);
    budget = ps_to_cycles(limit > st->t ? limit - st->t : 0, hz);
    if (!budget)
        budget = 1; /* progress: the instruction may overshoot the next event */
    st->in_quantum = 1;
    st->q_t = st->t;
    st->q_cyc = st->m.cycles;
    st->q_hz = hz;
    st->pin_changed = 0;
    st->m.return_on_sleep = 1;
    {
        u64 insns0 = st->m.insns, stepping = st->steps;
        used = esk_mach_run(&st->m, budget, stepping);
        st->in_quantum = 0;
        advance_time(st, used, hz);
        if (stepping) {
            u64 ran = st->m.insns - insns0;
            st->steps = ran >= stepping ? 0 : stepping - ran;
            if (!st->steps && st->m.state == ESK_MACH_RUN && !st->m.reset_req) {
                st->stopped = 1;
                st->stop_why = "step";
                e->sched.stop_requested = 1;
                return;
            }
        }
    }
    if (st->m.reset_req) {
        mcu_reset_now(st);
        return;
    }
    if (st->m.state != ESK_MACH_RUN) {
        report_end(st);
        return;
    }
    if (st->m.stop_bp || st->m.stop_watch) {
        st->stopped = 1;
        st->stop_why = st->m.stop_bp ? "breakpoint" : "watchpoint";
        e->sched.stop_requested = 1;
        return;
    }
    if (st->m.asleep) {
        schedule_sleep(st);
        return;
    }
    schedule_at(st, st->pin_changed ? st->t + 1 : st->t);
}

/* ---- pins ------------------------------------------------------------------ */

typedef struct pin_apply {
    mcu_state *st;
    int port, bit;
} pin_apply;

static void apply_cfg(mcu_state *st, int port, int bit)
{
    esk_pin *p = st->gpio[port][bit];
    const esk_mach_pin *cfg = &st->pq[port][bit].cfg;
    if (!p)
        return;
    p->model.pull = cfg->pull == 1 ? ESK_PULL_UP : cfg->pull == 2 ? ESK_PULL_DOWN : ESK_PULL_NONE;
    p->model.drive = cfg->open_drain ? ESK_DRIVE_OPEN_DRAIN : ESK_DRIVE_PUSH_PULL;
    if (cfg->drive >= 0) {
        esk_pin_set_direction(p, 1);
        esk_pin_drive(p, cfg->drive ? ESK_L1 : ESK_L0, ESK_STR_STRONG, 0);
    } else {
        esk_pin_drive(p, ESK_LZ, ESK_STR_HIZ, 0);
        esk_pin_set_direction(p, 0);
    }
    esk_pin_refresh(p);
}

static void pin_apply_cb(void *ctx, void *arg, es_time_t now)
{
    mcu_state *st = (mcu_state *)ctx;
    int port = (int)((uintptr_t)arg >> 8), bit = (int)((uintptr_t)arg & 0xffu);
    ES_UNUSED(now);
    st->pq[port][bit].ev = 0;
    apply_cfg(st, port, bit);
}

static void h_pin_cfg(void *ctx, int port, int bit, const esk_mach_pin *cfg)
{
    mcu_state *st = (mcu_state *)ctx;
    esk_engine *e = st->c->e;
    mcu_pinq *q;
    es_time_t at;
    if (port < 0 || port >= MCU_PORTS || bit < 0 || bit > 15 || !st->gpio[port][bit])
        return;
    q = &st->pq[port][bit];
    q->cfg = *cfg;
    q->known = 1;
    at = local_now(st);
    if (at < e->sched.now)
        at = e->sched.now;
    if (q->ev) {
        esk_sched_cancel(&e->sched, q->ev);
        q->ev = 0;
    }
    if (esk_sched_at(&e->sched, at, ESK_EV_SIGNAL, pin_apply_cb, st,
                     (void *)(uintptr_t)((unsigned)port << 8 | (unsigned)bit), &q->ev) != ES_OK)
        esk_comp_diag(st->c, ES_SEV_FATAL, "sched.queue_full", "a pin change cannot be scheduled");
    if (st->in_quantum) {
        st->m.yield = 1;
        st->pin_changed = 1;
    }
}

static int h_pin_in(void *ctx, int port, int bit, int *level)
{
    mcu_state *st = (mcu_state *)ctx;
    esk_pin *p;
    esk_logic v;
    if (port < 0 || port >= MCU_PORTS || bit < 0 || bit > 15 || !(p = st->gpio[port][bit]))
        return 0;
    v = esk_pin_level(p);
    if (v == ESK_LZ)
        return 0;
    *level = v == ESK_L1;
    return 1;
}

static int h_pin_volts(void *ctx, int port, int bit, double *v)
{
    mcu_state *st = (mcu_state *)ctx;
    esk_pin *p;
    const esk_net *n;
    if (port < 0 || port >= MCU_PORTS || bit < 0 || bit > 15 || !(p = st->gpio[port][bit]) || !p->net)
        return 0;
    n = p->net;
    if (n->island)
        *v = n->voltage;
    else
        *v = esk_net_value(n) == ESK_L1 ? st->m.vdd : 0.0;
    return 1;
}

/* ---- serial ------------------------------------------------------------------ */

static mcu_uart *uart_named(mcu_state *st, const char *unit, int add)
{
    int i;
    for (i = 0; i < st->nuart; i++)
        if (!strcmp(st->uart[i].unit, unit))
            return &st->uart[i];
    if (!add || st->nuart == MCU_UARTS)
        return NULL;
    st->uart[st->nuart].unit = esk_mach_strdup(&st->m, unit);
    st->uart[st->nuart].text = (char *)esk_mach_alloc(&st->m, MCU_TEXT_CAP);
    if (!st->uart[st->nuart].unit || !st->uart[st->nuart].text)
        return NULL;
    st->uart[st->nuart].rx = esk_mach_rx_find(&st->m, unit);
    return &st->uart[st->nuart++];
}

static void text_append(mcu_uart *u, int c)
{
    if (c == 0)
        return; /* the text is a C string */
    if (u->text_len + 1 >= MCU_TEXT_CAP) {
        size_t drop = MCU_TEXT_CAP / 4;
        memmove(u->text, u->text + drop, u->text_len - drop);
        u->text_len -= drop;
    }
    u->text[u->text_len++] = (char)c;
    u->text[u->text_len] = 0;
}

static void tx_bit_cb(void *ctx, void *arg, es_time_t now)
{
    mcu_state *st = (mcu_state *)ctx;
    mcu_uart *u = &st->uart[(uintptr_t)arg];
    esk_engine *e = st->c->e;
    u->tx_ev = 0;
    if (!u->tx_pin || u->bit >= u->nbits)
        return;
    esk_pin_drive(u->tx_pin, (u->frame >> u->bit) & 1u ? ESK_L1 : ESK_L0, ESK_STR_STRONG, 0);
    u->bit++;
    if (u->bit < u->nbits)
        (void)esk_sched_at(&e->sched, now + u->bit_ps, ESK_EV_SIGNAL, tx_bit_cb, st, arg, &u->tx_ev);
}

static void h_uart_tx(void *ctx, const char *unit, int c, u32 bit_cycles)
{
    mcu_state *st = (mcu_state *)ctx;
    esk_engine *e = st->c->e;
    mcu_uart *u = uart_named(st, unit, 1);
    int port = 0, bit = 0;
    es_time_t at = local_now(st);
    if (!u)
        return;
    u->tx_bytes++;
    text_append(u, c);
    /* the waveform, when the TX pin is the UART's */
    u->tx_pin = NULL;
    if (st->m.uart_pin && st->m.uart_pin(&st->m, unit, 1, &port, &bit) == 0 && port < MCU_PORTS &&
        st->gpio[port][bit] && st->pq[port][bit].cfg.af && st->pq[port][bit].cfg.drive >= 0) {
        if (u->tx_ev) {
            esk_sched_cancel(&e->sched, u->tx_ev);
            u->tx_ev = 0;
        }
        u->tx_pin = st->gpio[port][bit];
        u->frame = ((unsigned)c & 0xffu) << 1 | 0x200u; /* start 0, 8 data, stop 1 */
        u->nbits = 10;
        u->bit = 0;
        u->bit_ps = cycles_to_ps(bit_cycles, st->in_quantum ? st->q_hz : esk_mach_core_hz(&st->m), NULL);
        if (!u->bit_ps)
            u->bit_ps = 1;
        if (at < e->sched.now)
            at = e->sched.now;
        (void)esk_sched_at(&e->sched, at, ESK_EV_SIGNAL, tx_bit_cb, st, (void *)(uintptr_t)(u - st->uart),
                           &u->tx_ev);
    }
}

static es_time_t frame_ps(mcu_state *st, mcu_uart *u)
{
    u32 bc = u->rx && u->rx->bit_cycles ? u->rx->bit_cycles(u->rx->ctx) : 1;
    es_time_t ps = cycles_to_ps((u64)bc * 10u, esk_mach_core_hz(&st->m), NULL);
    return ps ? ps : 1;
}

/* the session's serial input: a byte a frame, when the receiver has room */
static void in_cb(void *ctx, void *arg, es_time_t now)
{
    mcu_state *st = (mcu_state *)ctx;
    mcu_uart *u = &st->uart[(uintptr_t)arg];
    esk_engine *e = st->c->e;
    u->in_ev = 0;
    if (!u->inq_len || !u->rx)
        return;
    catch_up(st, now);
    if (runnable(st) && u->rx->room(u->rx->ctx)) {
        u->rx->put(u->rx->ctx, u->inq[u->inq_head]);
        u->inq_head = (u->inq_head + 1) % MCU_INQ_CAP;
        u->inq_len--;
        u->rx_bytes++;
        wake(st);
    }
    if (u->inq_len)
        (void)esk_sched_at(&e->sched, now + frame_ps(st, u), ESK_EV_EXTERNAL, in_cb, st, arg, &u->in_ev);
}

static es_status serial_input(mcu_state *st, const char *unit, const esk_json *value)
{
    esk_engine *e = st->c->e;
    const char *text = esk_json_str(value);
    mcu_uart *u = uart_named(st, unit, 1);
    size_t i, n;
    if (!text || !u || !u->rx)
        return ES_EINVAL;
    n = strlen(text);
    for (i = 0; i < n && u->inq_len < MCU_INQ_CAP; i++) {
        u->inq[(u->inq_head + u->inq_len) % MCU_INQ_CAP] = (unsigned char)text[i];
        u->inq_len++;
    }
    if (!u->in_ev)
        (void)esk_sched_at(&e->sched, e->sched.now, ESK_EV_EXTERNAL, in_cb, st,
                           (void *)(uintptr_t)(u - st->uart), &u->in_ev);
    return ES_OK;
}

/* the RX pin's waveform: a start bit's falling edge, then each bit at its middle */
static void rx_bit_cb(void *ctx, void *arg, es_time_t now)
{
    mcu_state *st = (mcu_state *)ctx;
    mcu_uart *u = &st->uart[(uintptr_t)arg];
    esk_engine *e = st->c->e;
    esk_logic v;
    u->rx_ev = 0;
    if (!u->rx_busy || !u->rx_pin)
        return;
    v = esk_pin_level(u->rx_pin);
    if (u->rx_bit < 8) {
        if (v == ESK_L1)
            u->rx_shift |= 1u << u->rx_bit;
        u->rx_bit++;
        (void)esk_sched_at(&e->sched, now + u->rx_bit_ps, ESK_EV_PERIPHERAL, rx_bit_cb, st, arg, &u->rx_ev);
        return;
    }
    u->rx_busy = 0;
    catch_up(st, now);
    if (v == ESK_L1 && runnable(st) && u->rx && u->rx->room(u->rx->ctx)) {
        u->rx->put(u->rx->ctx, (int)(u->rx_shift & 0xffu));
        u->rx_bytes++;
        wake(st);
    }
}

static void rx_edge(mcu_state *st, esk_pin *p)
{
    esk_engine *e = st->c->e;
    int i;
    for (i = 0; i < st->nuart; i++) {
        mcu_uart *u = &st->uart[i];
        u32 bc;
        if (u->rx_pin != p || u->rx_busy || !u->rx || !u->rx->room(u->rx->ctx))
            continue;
        if (esk_pin_level(p) != ESK_L0)
            continue;
        bc = u->rx->bit_cycles ? u->rx->bit_cycles(u->rx->ctx) : 1;
        u->rx_bit_ps = cycles_to_ps(bc, esk_mach_core_hz(&st->m), NULL);
        if (!u->rx_bit_ps)
            u->rx_bit_ps = 1;
        u->rx_busy = 1;
        u->rx_bit = 0;
        u->rx_shift = 0;
        (void)esk_sched_at(&e->sched, e->sched.now + u->rx_bit_ps + u->rx_bit_ps / 2, ESK_EV_PERIPHERAL,
                           rx_bit_cb, st, (void *)(uintptr_t)i, &u->rx_ev);
    }
}

/* the RX pins the UARTs listen on, as the pins are mapped now */
static void map_rx_pins(mcu_state *st)
{
    int i, k, port, bit;
    for (k = 0; k < st->m.nrx; k++)
        (void)uart_named(st, st->m.rx[k].unit, 1);
    for (i = 0; i < st->nuart; i++) {
        mcu_uart *u = &st->uart[i];
        u->rx_pin = NULL;
        if (st->m.uart_pin && st->m.uart_pin(&st->m, u->unit, 0, &port, &bit) == 0 && port < MCU_PORTS)
            u->rx_pin = st->gpio[port][bit];
    }
}

/* ---- the machine's way out ---------------------------------------------------- */

static void h_out(void *ctx, int stream, int c)
{
    mcu_state *st = (mcu_state *)ctx;
    mcu_uart *u = uart_named(st, stream ? "semihosting.stderr" : "semihosting", 1);
    if (u)
        text_append(u, c);
}

static void h_diag(void *ctx, int severity, const char *code, const char *msg)
{
    mcu_state *st = (mcu_state *)ctx;
    esk_engine *e = st->c->e;
    es_severity_t sev = severity == ES_SEV_INFO      ? ES_SEV_INFO
                        : severity == ES_SEV_WARNING ? ES_SEV_WARNING
                        : severity == ES_SEV_ERROR   ? ES_SEV_ERROR
                                                     : ES_SEV_FATAL;
    esk_diag_raise(e->diag, sev, code, local_now(st), "component", st->c->id, NULL, 0, "%s: %s", st->c->id,
                   msg);
}

/* ---- reset ---------------------------------------------------------------------- */

static int level_of(const esk_pin *p, int dflt)
{
    esk_logic v;
    if (!p)
        return dflt;
    v = esk_pin_level(p);
    if (v == ESK_LZ)
        return p->model.pull == ESK_PULL_UP ? 1 : p->model.pull == ESK_PULL_DOWN ? 0 : dflt;
    return v == ESK_L1;
}

/* power-on or a pin reset: memory as the image left it, the machine's reset */
static void mcu_reset_now(mcu_state *st)
{
    esk_engine *e = st->c->e;
    u32 entry = 0;
    char why[160];
    int r;
    if (st->ev) {
        esk_sched_cancel(&e->sched, st->ev);
        st->ev = 0;
    }
    for (r = 0; r < st->m.bus.nrg; r++)
        if (!st->m.bus.rg[r].mirror)
            esk_bus_erase(&st->m.bus.rg[r]);
    if (!st->image) {
        /* no image yet: the part sits blank, its pins inputs */
        esk_mach_reset(&st->m);
        st->boot_fail = 1;
        st->t = e->sched.now;
        return;
    }
    if (esk_mach_load_image(&st->m, st->image, st->image_len, &entry, why, sizeof why) != ES_OK)
        raise(st, ES_SEV_ERROR, "mcu.image", "%s: %s", st->image_name, why);
    st->in_quantum = 0;
    esk_mach_reset(&st->m);
    st->stopped = 0;
    st->ended_reported = 0;
    st->boot_fail = 0;
    st->t = e->sched.now;
    st->rem = 0;
    map_rx_pins(st);
    st->fresh = 1;
    schedule_at(st, e->sched.now + 1);
}

/* ---- the component ------------------------------------------------------------- */

static es_status mcu_create(esk_comp *c)
{
    mcu_state *st = state_of(c);
    st->c = c;
    return ES_OK;
}

static void mcu_destroy(esk_comp *c)
{
    mcu_state *st = state_of(c);
    esk_engine *e = c->e;
    if (!st)
        return;
    if (st->attached)
        esk_mach_free(&st->m);
    if (st->image)
        esk_free(e->a, st->image, st->image_len);
    if (st->dwarf.a)
        esk_dwarf_free(&st->dwarf);
    esk_strfree(e->a, st->image_name);
    st->image = NULL;
    st->attached = 0;
}

static void mcu_reset(esk_comp *c)
{
    mcu_state *st = state_of(c);
    int i, k;
    if (!st->attached)
        return;
    st->ev = 0; /* the scheduler was cleared */
    for (i = 0; i < MCU_PORTS; i++)
        for (k = 0; k < 16; k++) {
            st->pq[i][k].ev = 0;
            st->pq[i][k].known = 0;
        }
    for (i = 0; i < st->nuart; i++) {
        mcu_uart *u = &st->uart[i];
        u->tx_ev = u->in_ev = u->rx_ev = 0;
        u->text_len = 0;
        u->text[0] = 0;
        u->tx_bytes = u->rx_bytes = 0;
        u->inq_len = u->inq_head = 0;
        u->rx_busy = 0;
    }
    st->held = 0;
    st->steps = 0;
    mcu_reset_now(st);
}

static void mcu_on_input(esk_comp *c, esk_pin *p)
{
    mcu_state *st = state_of(c);
    esk_engine *e = c->e;
    if (!st->attached || e->in_reset)
        return;
    catch_up(st, e->sched.now);
    if (p == st->nrst) {
        int low = !level_of(p, 1);
        if (low && !st->held) {
            st->held = 1;
            if (st->ev) {
                esk_sched_cancel(&e->sched, st->ev);
                st->ev = 0;
            }
            /* the pins go to their reset state while NRST is held */
            esk_mach_reset(&st->m);
        } else if (!low && st->held) {
            st->held = 0;
            mcu_reset_now(st);
        }
        return;
    }
    if (st->held)
        return;
    if (st->m.pins_changed)
        st->m.pins_changed(&st->m);
    rx_edge(st, p);
    wake(st);
}

static es_status mcu_on_set(esk_comp *c, const char *input, const esk_json *value)
{
    mcu_state *st = state_of(c);
    if (!st->attached)
        return ES_ESTATE;
    if (!strcmp(input, "reset")) {
        catch_up(st, c->e->sched.now);
        mcu_reset_now(st);
        return ES_OK;
    }
    if (!strcmp(input, "serial") || !strcmp(input, "tx_text"))
        return serial_input(st, st->console, value);
    if (esk_str_has_prefix(input, "serial:"))
        return serial_input(st, input + 7, value);
    return ES_EINVAL;
}

static void mcu_observe(esk_comp *c, esk_json *out)
{
    mcu_state *st = state_of(c);
    esk_alloc *a = c->e->a;
    char key[48];
    int i;
    const char *state;
    if (!st->attached)
        return;
    state = !st->image                            ? "no_image"
            : st->boot_fail                       ? "boot_mode"
            : st->held                            ? "reset"
            : st->m.state == ESK_MACH_EXIT        ? "exited"
            : st->m.state == ESK_MACH_LOCKUP      ? "lockup"
            : st->stopped                         ? "stopped"
            : st->m.asleep                        ? "sleeping"
                                                  : "running";
    esk_json_set_string(a, out, "state", state);
    esk_json_set_integer(a, out, "pc", (int64_t)st->m.cpu->ops->pc(st->m.cpu));
    esk_json_set_integer(a, out, "core_hz", (int64_t)esk_mach_core_hz(&st->m));
    esk_json_set_integer(a, out, "instructions", (int64_t)st->m.insns);
    esk_json_set_integer(a, out, "cycles", (int64_t)st->m.cycles);
    esk_json_set_string(a, out, "image", st->image_name ? st->image_name : "");
    if (st->m.state == ESK_MACH_EXIT)
        esk_json_set_integer(a, out, "exit_status", st->m.exit_status);
    if (st->stopped && st->stop_why)
        esk_json_set_string(a, out, "stop", st->stop_why);
    for (i = 0; i < st->nuart; i++) {
        snprintf(key, sizeof key, "serial.%s", st->uart[i].unit);
        esk_json_set_string(a, out, key, st->uart[i].text);
    }
    /* the console under the virtual terminal's name, for serial probes */
    {
        mcu_uart *u = uart_named(st, st->console, 0);
        esk_json_set_string(a, out, "rx_text", u ? u->text : "");
    }
}

const esk_model_ops esk_mcu_ops = { "mcu", sizeof(mcu_state), mcu_create, mcu_destroy, mcu_reset,
                                     mcu_on_input, NULL, mcu_on_set, mcu_observe, NULL, 0 };

int esk_mcu_is(const esk_comp *c) { return c && c->ops == &esk_mcu_ops; }

const esk_dwarf *esk_mcu_dwarf(const esk_comp *c)
{
    const mcu_state *st = esk_mcu_is(c) ? state_of(c) : NULL;
    return st && st->attached && st->dwarf.nrows ? &st->dwarf : NULL;
}

const unsigned char *esk_mcu_image(const esk_comp *c, size_t *len)
{
    const mcu_state *st = esk_mcu_is(c) ? state_of(c) : NULL;
    if (!st || !st->attached)
        return NULL;
    *len = st->image_len;
    return st->image;
}

struct esk_mach *esk_mcu_machine(esk_comp *c)
{
    return esk_mcu_is(c) && state_of(c)->attached ? &state_of(c)->m : NULL;
}

es_time_t esk_mcu_time(const esk_comp *c) { return esk_mcu_is(c) ? local_now(state_of(c)) : 0; }

int esk_mcu_stopped(const esk_comp *c, const char **why)
{
    const mcu_state *st = esk_mcu_is(c) ? state_of(c) : NULL;
    if (!st || !st->stopped)
        return 0;
    if (why)
        *why = st->stop_why;
    return 1;
}

void esk_mcu_resume(esk_comp *c, u64 steps)
{
    mcu_state *st = esk_mcu_is(c) ? state_of(c) : NULL;
    if (!st || !st->attached)
        return;
    st->stopped = 0;
    st->stop_why = NULL;
    st->steps = steps;
    st->m.bp_skip = 1;
    st->m.bus.watch_hit = 0;
    catch_up(st, c->e->sched.now);
    wake(st);
}

void esk_mcu_halt(esk_comp *c)
{
    mcu_state *st = esk_mcu_is(c) ? state_of(c) : NULL;
    if (!st || !st->attached || st->stopped)
        return;
    st->stopped = 1;
    st->stop_why = "halt";
    c->e->sched.stop_requested = 1;
    if (st->ev) {
        esk_sched_cancel(&c->e->sched, st->ev);
        st->ev = 0;
    }
}

es_status esk_mcu_set_image(esk_comp *c, const unsigned char *image, size_t len, char *why, size_t cap)
{
    mcu_state *st = esk_mcu_is(c) ? state_of(c) : NULL;
    esk_engine *e;
    unsigned char *copy;
    u32 entry = 0;
    if (!st || !st->attached || !len)
        return ES_EINVAL;
    e = c->e;
    copy = (unsigned char *)esk_malloc(e->a, len);
    if (!copy)
        return ES_ENOMEM;
    memcpy(copy, image, len);
    /* the new image must load before it replaces the old one */
    if (esk_mach_load_image(&st->m, copy, len, &entry, why, cap) != ES_OK) {
        esk_free(e->a, copy, len);
        if (st->image)
            (void)esk_mach_load_image(&st->m, st->image, st->image_len, &entry, why, cap);
        return ES_EINVAL;
    }
    if (st->image)
        esk_free(e->a, st->image, st->image_len);
    st->image = copy;
    st->image_len = len;
    if (st->dwarf.a)
        esk_dwarf_free(&st->dwarf);
    (void)esk_dwarf_load(&st->dwarf, e->a, st->image, st->image_len);
    catch_up(st, e->sched.now);
    mcu_reset_now(st);
    return ES_OK;
}

es_status esk_mcu_attach(esk_comp *c, const esk_mcu_setup *su, char *why, size_t cap)
{
    mcu_state *st = state_of(c);
    esk_engine *e = c->e;
    es_status st2;
    int port, bit;
    char name[12];
    if (!esk_mcu_is(c))
        return ES_EINVAL;
    esk_mach_init(&st->m, e->a);
    st->attached = 1;
    st->m.host.ctx = st;
    st->m.host.out = h_out;
    st->m.host.pin_cfg = h_pin_cfg;
    st->m.host.pin_in = h_pin_in;
    st->m.host.uart_tx = h_uart_tx;
    st->m.host.diag = h_diag;
    st->m.host.pin_volts = h_pin_volts;
    st->quantum_ps = su->quantum_ps ? su->quantum_ps : MCU_DEFAULT_QUANTUM_PS;
    snprintf(st->console, sizeof st->console, "%s", su->console ? su->console : "USART1");
    for (port = 0; port < MCU_PORTS; port++)
        for (bit = 0; bit < 16; bit++) {
            snprintf(name, sizeof name, "P%c%d", 'A' + port, bit);
            st->gpio[port][bit] = esk_comp_pin(c, name);
            if (!st->gpio[port][bit]) {
                /* a part that numbers its pins GPIO0..n (an FE310's): port
                 * k holds GPIO 16k to 16k + 15 */
                snprintf(name, sizeof name, "GPIO%d", port * 16 + bit);
                st->gpio[port][bit] = esk_comp_pin(c, name);
            }
            if (st->gpio[port][bit] && st->gpio[port][bit]->model.kind == ESK_PIN_DIGITAL_IO)
                esk_pin_watch(st->gpio[port][bit], 1);
            else
                st->gpio[port][bit] = NULL;
        }
    st->nrst = esk_comp_pin(c, "NRST");
    st->boot0 = esk_comp_pin(c, "BOOT0");
    if (st->nrst)
        esk_pin_watch(st->nrst, 1);
    st2 = esk_mach_build(&st->m, su->mcu, su->regs, su->hse_hz, why, cap);
    if (st2 != ES_OK)
        return st2;
    st->image_name = esk_strdup(e->a, su->image_name ? su->image_name : "firmware");
    if (!st->image_name)
        return ES_ENOMEM;
    if (su->image && su->image_len) {
        u32 entry = 0;
        st->image = (unsigned char *)esk_malloc(e->a, su->image_len);
        if (!st->image)
            return ES_ENOMEM;
        memcpy(st->image, su->image, su->image_len);
        st->image_len = su->image_len;
        st2 = esk_mach_load_image(&st->m, st->image, st->image_len, &entry, why, cap);
        if (st2 != ES_OK)
            return st2;
        (void)esk_dwarf_load(&st->dwarf, e->a, st->image, st->image_len);
    }
    map_rx_pins(st);
    (void)uart_named(st, st->console, 1);
    return ES_OK;
}
