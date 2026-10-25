/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* mcu.c -- the MCU runtime (mcu.h says what it does). */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "mcu/analysis.h"
#include "mcu/build.h"
#include "mcu/dwarf.h"
#include "mcu/mcu.h"
#include "protocol/wsutil.h"
#include "util/num.h"
#include "util/str.h"

#define MCU_PORTS 8
#define MCU_UARTS 8
#define MCU_TEXT_CAP 65536u
#define MCU_INQ_CAP 4096u
#define MCU_TXQ_CAP 64u /* frames sent ahead of the scheduler, a quantum's worth */
#define MCU_DEFAULT_QUANTUM_PS 10000000u /* 10 us */
/* the quantum of the circuit's only core, on a machine whose devices keep
 * lazy time, while no analog island integrates: nothing outside can change
 * before the next event, which still ends the quantum */
#define MCU_QUIET_QUANTUM_PS 100000000u /* 100 us */
#define PS_PER_S UINT64_C(1000000000000)
#define MEGA UINT64_C(1000000)
/* the supply current's window (ADR-036): the core's running and waiting
 * times are averaged over it, as the board's decoupling smooths them; a
 * frame on the air draws from its start to its end */
#define SUPPLY_WINDOW_PS 1000000000u /* 1 ms */
#define SUPPLY_POINTS 6
enum { MCU_WAKE_SUPPLY = 1 };

/* the part's supply current from its description's `supply` (ADR-036) */
typedef struct mcu_supply {
    int on;
    double run[SUPPLY_POINTS][2], idle[SUPPLY_POINTS][2]; /* (Hz, A) */
    int nrun, nidle;
    double held_a, stop_a, standby_a;
    double tx_dsss_a, tx_ofdm_a, tx_ht_a, tx_ble_a, rx_a;
    es_time_t win_t;  /* the window's start */
    es_time_t run_ps; /* how long the core ran in it */
    double cpu_a;     /* the core's average over the last window */
    double tx_a;      /* a frame being sent: the radio's current for it */
    double set_a;     /* what the part draws now */
} mcu_supply;

typedef struct mcu_uart {
    const char *unit;
    struct esk_rx_port *rx;
    char *text; /* what it sent, the last MCU_TEXT_CAP - 1 bytes */
    size_t text_len;
    u64 text_total; /* the bytes of text it ever sent */
    u64 tx_bytes, rx_bytes;
    /* the TX pin's waveform: the frame on the line, then those the core
     * sent while running ahead of the scheduler, each at its own time */
    esk_pin *tx_pin;
    unsigned frame;
    int nbits, bit;
    es_time_t bit_ps;
    esk_event_handle tx_ev;
    struct {
        es_time_t at, bit_ps;
        unsigned frame;
        int nbits;      /* start, data and one stop bit on the line */
        int halves;     /* the frame's length in half bits */
    } txq[MCU_TXQ_CAP];
    unsigned txq_head, txq_len;
    es_time_t tx_free; /* when the last frame's stop bit ends */
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
    /* an output's resistance and edge rate at each speed setting, from the
     * description's io.electrical (esk_mach_pin.speed); 0 left as is */
    double rout[4], slew[4];
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
    /* a pin on an analog island changed in the quantum: how long its edge
     * ramps, and the time the next quantum waits for it to (the core goes
     * on from where it stopped then, less than a cycle behind) */
    es_time_t settle_ps, settle_to;
    int held, boot_fail, stopped, ended_reported;
    int fresh; /* the first quantum after a reset looks at NRST and BOOT0 */
    const char *stop_why;
    u64 steps; /* instructions a debugger's step still runs */
    mcu_uart uart[MCU_UARTS];
    int nuart;
    char console[16];
    esk_an *an;      /* the analyses watching the firmware, or NULL */
    unsigned an_what;
    esk_air_node air; /* its radio on the session's air, when it has one */
    int on_air;
    mcu_supply sup;
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

/* ---- the supply current (ADR-036) ---------------------------------------------- */

/* a table's current at `hz`: between its points, and on the line through
 * the nearest two beyond them (not below 0) */
static double supply_at(double (*t)[2], int n, double hz)
{
    int i = 0;
    double a;
    if (n <= 0)
        return 0.0;
    if (n == 1)
        return t[0][1];
    while (i < n - 2 && hz > t[i + 1][0])
        i++;
    a = t[i][1] + (t[i + 1][1] - t[i][1]) * (hz - t[i][0]) / (t[i + 1][0] - t[i][0]);
    return a > 0.0 ? a : 0.0;
}

static int supply_table(const esk_json *j, double (*t)[2])
{
    size_t i;
    int n = 0;
    for (i = 0; i < esk_json_len(j) && n < SUPPLY_POINTS; i++) {
        const esk_json *pt = esk_json_at(j, i);
        double hz = -1.0, a = -1.0;
        if (esk_json_to_double(esk_json_at(pt, 0), &hz) == 0 && esk_json_to_double(esk_json_at(pt, 1), &a) == 0 &&
            hz > 0.0 && a >= 0.0 && (n == 0 || hz > t[n - 1][0])) {
            t[n][0] = hz;
            t[n][1] = a;
            n++;
        }
    }
    return n;
}

static void supply_setup(mcu_state *st, const esk_json *mcu)
{
    const esk_json *j = esk_json_get(mcu, "supply"), *r = esk_json_get(j, "radio");
    mcu_supply *s = &st->sup;
    memset(s, 0, sizeof *s);
    if (!j)
        return;
    s->nrun = supply_table(esk_json_get(j, "run_a"), s->run);
    s->nidle = supply_table(esk_json_get(j, "idle_a"), s->idle);
    if (!s->nidle) {
        memcpy(s->idle, s->run, sizeof s->idle);
        s->nidle = s->nrun;
    }
    s->held_a = esk_json_get_num(j, "held_a", 0.0);
    s->stop_a = esk_json_get_num(j, "stop_a", 0.0);
    s->standby_a = esk_json_get_num(j, "standby_a", s->stop_a);
    s->rx_a = esk_json_get_num(r, "rx_a", 0.0);
    s->tx_dsss_a = esk_json_get_num(r, "tx_dsss_a", s->rx_a);
    s->tx_ofdm_a = esk_json_get_num(r, "tx_ofdm_a", s->tx_dsss_a);
    s->tx_ht_a = esk_json_get_num(r, "tx_ht_a", s->tx_ofdm_a);
    s->tx_ble_a = esk_json_get_num(r, "tx_ble_a", s->rx_a);
    s->on = s->nrun > 0;
}

/* a frame's transmit current: 802.11b's DSSS rates, 11g's OFDM, 11n's MCS */
static double supply_tx(const mcu_supply *s, int kind, u32 rate_kbps)
{
    if (kind == ESK_AIR_BLE)
        return s->tx_ble_a;
    if (rate_kbps == 1000u || rate_kbps == 2000u || rate_kbps == 5500u || rate_kbps == 11000u)
        return s->tx_dsss_a;
    if (rate_kbps == 6000u || rate_kbps == 9000u || rate_kbps == 12000u || rate_kbps == 18000u ||
        rate_kbps == 24000u || rate_kbps == 36000u || rate_kbps == 48000u || rate_kbps == 54000u)
        return s->tx_ofdm_a;
    return s->tx_ht_a;
}

/* what the part draws now: the core's state (its last window's average
 * while it runs or waits), and the radio sending or listening */
static double supply_now(const mcu_state *st)
{
    const mcu_supply *s = &st->sup;
    double a;
    if (!st->attached)
        return 0.0;
    if (st->held)
        return s->held_a;
    if (st->m.powered_down)
        a = s->standby_a;
    else if (st->m.clocks_stopped)
        a = s->stop_a;
    else
        a = s->cpu_a;
    if (s->tx_a > 0.0)
        a += s->tx_a;
    else if (st->m.radio.tuned && (runnable(st) || st->m.asleep) &&
             (st->m.radio.tuned(st->m.radio.ctx, ESK_AIR_WIFI) || st->m.radio.tuned(st->m.radio.ctx, ESK_AIR_BLE)))
        a += s->rx_a;
    return a;
}

/* the window ends when it is due; the part's current is set again when it
 * changed by more than its noise (0.5 mA, or 3 %): an island is not
 * stepped again for nothing */
static void supply_update(mcu_state *st, int force)
{
    mcu_supply *s = &st->sup;
    esk_engine *e = st->c->e;
    es_time_t now = e->sched.now;
    double a;
    if (!s->on)
        return;
    if (now >= s->win_t + SUPPLY_WINDOW_PS) {
        double hz = (double)esk_mach_core_hz(&st->m), run = (double)s->run_ps / (double)(now - s->win_t);
        if (run > 1.0)
            run = 1.0;
        s->cpu_a = run * supply_at(s->run, s->nrun, hz) + (1.0 - run) * supply_at(s->idle, s->nidle, hz);
        s->win_t = now;
        s->run_ps = 0;
        /* a core asleep until later closes its next window all the same */
        if (st->m.asleep || !runnable(st))
            (void)esk_comp_wake_at(st->c, now + SUPPLY_WINDOW_PS, MCU_WAKE_SUPPLY);
    }
    a = supply_now(st);
    if (force || fabs(a - s->set_a) > (s->set_a * 0.03 > 0.0005 ? s->set_a * 0.03 : 0.0005)) {
        s->set_a = a;
        esk_comp_set_supply(st->c, a, now);
    }
}

/* the earliest time another core of the circuit waits to run from (its
 * next quantum), or ES_TIME_MAX */
static es_time_t peers_behind(const mcu_state *st)
{
    const esk_engine *e = st->c->e;
    es_time_t t = ES_TIME_MAX;
    size_t i;
    for (i = 0; i < e->comps.len; i++) {
        const esk_comp *c = e->comps.data[i];
        const mcu_state *o;
        if (c == st->c || !esk_mcu_is(c))
            continue;
        o = state_of(c);
        if (o && o->ev && o->ev_at < t)
            t = o->ev_at;
    }
    return t;
}

/* after something outside changed the machine (a pin, a byte): a sleeping
 * core an interrupt woke runs from now */
static void wake(mcu_state *st)
{
    esk_engine *e = st->c->e;
    /* a core not yet started since its reset starts when the reset said,
     * whatever the pins do meanwhile (its first quantum reads them): so a
     * session as loaded runs as one reset does */
    if (!runnable(st) || (st->fresh && st->ev))
        return;
    if (st->m.asleep && !st->m.reset_req) {
        /* still asleep, but the input may have given a device an event
         * sooner than the wake-up scheduled (an I2C master seeing SCL
         * high): its quantum comes then, or that device would catch up
         * past it and its edges in between run together */
        schedule_sleep(st);
        return;
    }
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

static void mcu_reset_now(mcu_state *st, int power_on);
static int level_of(const esk_pin *p, int dflt);

/* this is the circuit's only core: no other core's pin changes to wait for */
static int solo_core(const mcu_state *st)
{
    const esk_engine *e = st->c->e;
    size_t i;
    for (i = 0; i < e->comps.len; i++)
        if (e->comps.data[i] != st->c && esk_mcu_is(e->comps.data[i]))
            return 0;
    return 1;
}

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
    if (now < st->settle_to) {
        /* an earlier quantum (a wake-up) comes before the circuit has its
         * pins' last changes: it waits for them, as one more change to a
         * pin would take the place of one not yet applied */
        schedule_at(st, st->settle_to);
        return;
    }
    if (now > st->settle_to)
        catch_up(st, now);
    st->settle_to = 0;
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
    if (st->m.reset_req) {
        /* a reset from outside the core's run (a pin's edge ending an
         * STM32's Standby) */
        mcu_reset_now(st, 0);
        return;
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
     * this quantum's end instead) or another core's quantum, and the
     * quantum's bound: no further than a quantum past the core furthest
     * behind, so each core sees the others' pin changes by its next
     * quantum boundary (SPEC-002 §5) without the two stepping each other
     * an instruction at a time */
    next = esk_sched_next_time_without(&e->sched, 1u << ESK_EV_ANALOG | 1u << ESK_EV_CORE);
    {
        es_time_t peer = peers_behind(st);
        if (peer != ES_TIME_MAX && peer + st->quantum_ps < next)
            next = peer + st->quantum_ps;
    }
    limit = next;
    {
        es_time_t q = st->quantum_ps;
        if (q < MCU_QUIET_QUANTUM_PS && st->m.lazy && !st->steps && solo_core(st) && esk_engine_analog_quiet(e))
            q = MCU_QUIET_QUANTUM_PS;
        if (limit == ES_TIME_MAX || limit - now > q)
            limit = now + q;
    }
    hz = esk_mach_core_hz(&st->m);
    budget = ps_to_cycles(limit > st->t ? limit - st->t : 0, hz);
    if (!budget)
        budget = 1; /* progress: the instruction may overshoot the next event */
    st->in_quantum = 1;
    st->q_t = st->t;
    st->q_cyc = st->m.cycles;
    st->q_hz = hz;
    st->pin_changed = 0;
    st->settle_ps = 0;
    st->m.return_on_sleep = 1;
    {
        u64 insns0 = st->m.insns, stepping = st->steps;
        used = esk_mach_run(&st->m, budget, stepping);
        st->in_quantum = 0;
        advance_time(st, used, hz);
        st->sup.run_ps += st->t > st->q_t ? st->t - st->q_t : 0;
        supply_update(st, 0);
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
        mcu_reset_now(st, 0);
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
    if (st->pin_changed) {
        /* the circuit sees the change before the core goes on; an edge on an
         * analog island first ramps to its level, so a device it clocks
         * answers (a comparator crosses) before the core's next instruction
         * can look at the answer: up to a cycle, as no instruction is
         * shorter */
        es_time_t gap = st->settle_ps ? st->settle_ps : 1, cycle = cycles_to_ps(1, hz, NULL);
        if (gap >= cycle)
            gap = cycle > 1 ? cycle - 1 : 1;
        st->settle_to = st->t + gap;
        schedule_at(st, st->settle_to);
        return;
    }
    schedule_at(st, st->t);
}

/* ---- pins ------------------------------------------------------------------ */

typedef struct pin_apply {
    mcu_state *st;
    int port, bit;
} pin_apply;

/* io.electrical's rout or slew_v_per_us: one value for every speed, or one
 * per speed setting in the order the GPIO model numbers them */
static void speed_table(const esk_json *v, double out[4])
{
    double d;
    size_t i;
    if (v && esk_json_to_double(v, &d) == 0) {
        for (i = 0; i < 4; i++)
            out[i] = d;
        return;
    }
    for (i = 0; i < 4 && i < esk_json_len(v); i++)
        if (esk_json_to_double(esk_json_at(v, i), &d) == 0)
            out[i] = d;
}

static void apply_cfg(mcu_state *st, int port, int bit)
{
    esk_pin *p = st->gpio[port][bit];
    const esk_mach_pin *cfg = &st->pq[port][bit].cfg;
    if (!p)
        return;
    p->model.pull = cfg->pull == 1 ? ESK_PULL_UP : cfg->pull == 2 ? ESK_PULL_DOWN : ESK_PULL_NONE;
    p->model.drive = cfg->open_drain ? ESK_DRIVE_OPEN_DRAIN : ESK_DRIVE_PUSH_PULL;
    if (cfg->aout) {
        /* a DAC: its voltage behind its output resistance, both levels the
         * same so the net settles there (as a 1 or 0 on a digital net) */
        p->model.voc_set = 3;
        p->model.voc_high = p->model.voc_low = cfg->volts;
        p->model.rout = cfg->aout_r > 0.0 ? cfg->aout_r : 1.0;
        p->model.slew_v_per_us = 1.0;
        esk_pin_output(p, cfg->volts > (p->model.vih + p->model.vil) / 2.0 ? ESK_L1 : ESK_L0, ESK_STR_STRONG);
        esk_pin_refresh(p);
        return;
    }
    p->model.voc_set = p->model0.voc_set;
    p->model.voc_high = p->model0.voc_high;
    p->model.voc_low = p->model0.voc_low;
    if (cfg->speed < 1) {
        p->model.rout = p->model0.rout;
        p->model.slew_v_per_us = p->model0.slew_v_per_us;
    }
    if (cfg->speed >= 1 && cfg->speed <= 4) {
        if (st->rout[cfg->speed - 1] > 0.0)
            p->model.rout = st->rout[cfg->speed - 1];
        if (st->slew[cfg->speed - 1] > 0.0)
            p->model.slew_v_per_us = st->slew[cfg->speed - 1];
    }
    /* an open-drain output's 1 lets the pin go: it is an input then, whose
     * level (what an I2C slave's ACK makes of it) the program reads */
    if (cfg->drive >= 0 && !(cfg->open_drain && cfg->drive)) {
        esk_pin_output(p, cfg->drive ? ESK_L1 : ESK_L0, ESK_STR_STRONG);
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

static void map_rx_pins(mcu_state *st);

static void h_pin_cfg(void *ctx, int port, int bit, const esk_mach_pin *cfg)
{
    mcu_state *st = (mcu_state *)ctx;
    esk_engine *e = st->c->e;
    mcu_pinq *q;
    es_time_t at;
    int was_af;
    if (port < 0 || port >= MCU_PORTS || bit < 0 || bit > 15 || !st->gpio[port][bit])
        return;
    q = &st->pq[port][bit];
    was_af = q->known && q->cfg.af;
    q->cfg = *cfg;
    q->known = 1;
    if (was_af != (cfg->af != 0))
        map_rx_pins(st); /* a UART's RX may have come to this pin or left it */
    at = local_now(st);
    if (st->m.output_lag) {
        es_time_t lag = cycles_to_ps(st->m.output_lag, esk_mach_core_hz(&st->m), NULL);
        at = at > lag ? at - lag : 0;
    }
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
        esk_pin *p = st->gpio[port][bit];
        st->m.yield = 1;
        st->pin_changed = 1;
        if (p->net && p->net->island && p->model.slew_v_per_us > 0.0) {
            /* the whole swing between the driver's open-circuit levels
             * (esk_pin_bridge_target's) bounds the ramp to any threshold */
            const esk_pin_model *pm = &p->model;
            double hi = (pm->voc_set & 1) ? pm->voc_high : pm->rails && pm->nominal_v > 0.0 ? pm->nominal_v : pm->voh;
            double lo = (pm->voc_set & 2) ? pm->voc_low : pm->rails ? 0.0 : pm->vol;
            double us = (hi > lo ? hi - lo : lo - hi) / pm->slew_v_per_us;
            es_time_t ramp = us > 0.0 ? (es_time_t)(us * 1e6) + 1 : 1;
            if (ramp > st->settle_ps)
                st->settle_ps = ramp;
        }
    }
}

static int h_pin_in(void *ctx, int port, int bit, int *level)
{
    mcu_state *st = (mcu_state *)ctx;
    esk_pin *p;
    esk_logic v;
    if (port < 0 || port >= MCU_PORTS || bit < 0 || bit > 15 || !(p = st->gpio[port][bit]))
        return 0;
    if (p->net && p->net->island) {
        /* the node's voltage against the pin's thresholds: an output's
         * comparator is not kept (an open-drain line a slave pulls low,
         * an I2C master reading back its own SDA); between them, the
         * comparator's state */
        double vn = p->net->voltage;
        if (vn >= p->model.vih) {
            *level = 1;
            return 1;
        }
        if (vn <= p->model.vil) {
            *level = 0;
            return 1;
        }
    }
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

static int h_temperature(void *ctx, double *celsius)
{
    mcu_state *st = (mcu_state *)ctx;
    const esk_world *w = &st->c->e->world;
    if (!(w->set & (1u << ESK_WF_TEMPERATURE)))
        return 0;
    *celsius = w->value[ESK_WF_TEMPERATURE];
    return 1;
}

static int h_pin_watch(void *ctx, int port, int bit, int on, double v)
{
    mcu_state *st = (mcu_state *)ctx;
    esk_pin *p;
    if (port < 0 || port >= MCU_PORTS || bit < 0 || bit > 15 || !(p = st->gpio[port][bit]))
        return 0;
    if (!on) {
        esk_pin_compare_off(p);
        return 0;
    }
    return esk_pin_compare(p, v);
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
    u->text_total++;
}

static void tx_bit_cb(void *ctx, void *arg, es_time_t now);

/* the next queued frame onto the line, no earlier than its time and the end
 * of the frame before it */
static void tx_next(mcu_state *st, mcu_uart *u)
{
    esk_engine *e = st->c->e;
    es_time_t at = u->txq[u->txq_head].at;
    int halves = u->txq[u->txq_head].halves;
    u->frame = u->txq[u->txq_head].frame;
    u->bit_ps = u->txq[u->txq_head].bit_ps;
    u->nbits = u->txq[u->txq_head].nbits;
    u->txq_head = (u->txq_head + 1) % MCU_TXQ_CAP;
    u->txq_len--;
    u->bit = 0;
    if (at < u->tx_free)
        at = u->tx_free;
    if (at < e->sched.now)
        at = e->sched.now;
    u->tx_free = at + (es_time_t)halves * u->bit_ps / 2;
    (void)esk_sched_at(&e->sched, at, ESK_EV_SIGNAL, tx_bit_cb, st, (void *)(uintptr_t)(u - st->uart), &u->tx_ev);
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
    else if (u->txq_len)
        tx_next(st, u);
}

static void h_uart_frame(void *ctx, const char *unit, int c, u32 bits, int nbits, int stop_halves,
                         u32 bit_cycles)
{
    mcu_state *st = (mcu_state *)ctx;
    esk_engine *e = st->c->e;
    mcu_uart *u = uart_named(st, unit, 1);
    int port = 0, bit = 0;
    unsigned slot;
    es_time_t bit_ps;
    if (!u)
        return;
    if (nbits < 1 || nbits > 16)
        nbits = 8;
    if (stop_halves < 1 || stop_halves > 4)
        stop_halves = 2;
    if (c >= 0) {
        u->tx_bytes++;
        text_append(u, c);
    }
    /* the waveform, when the TX pin is the UART's */
    if (!(st->m.uart_pin && st->m.uart_pin(&st->m, unit, 1, &port, &bit) == 0 && port < MCU_PORTS &&
          st->gpio[port][bit] && st->pq[port][bit].cfg.af && st->pq[port][bit].cfg.drive >= 0)) {
        u->tx_pin = NULL;
        u->txq_len = 0;
        return;
    }
    if (u->tx_pin != st->gpio[port][bit]) {
        if (u->tx_ev) {
            esk_sched_cancel(&e->sched, u->tx_ev);
            u->tx_ev = 0;
        }
        u->tx_pin = st->gpio[port][bit];
        u->txq_len = 0;
        u->tx_free = 0;
    }
    bit_ps = cycles_to_ps(bit_cycles, st->in_quantum ? st->q_hz : esk_mach_core_hz(&st->m), NULL);
    if (u->txq_len == MCU_TXQ_CAP) { /* not drained: the oldest goes */
        u->txq_head = (u->txq_head + 1) % MCU_TXQ_CAP;
        u->txq_len--;
    }
    slot = (u->txq_head + u->txq_len) % MCU_TXQ_CAP;
    u->txq[slot].at = local_now(st);
    u->txq[slot].bit_ps = bit_ps ? bit_ps : 1;
    /* start 0, the data, a stop 1 held for the stop bits' length */
    u->txq[slot].frame = (unsigned)((bits & ((1u << nbits) - 1)) << 1 | 1u << (nbits + 1));
    u->txq[slot].nbits = nbits + 2;
    u->txq[slot].halves = 2 * (nbits + 1) + stop_halves;
    u->txq_len++;
    if (!u->tx_ev)
        tx_next(st, u);
}

static void h_uart_tx(void *ctx, const char *unit, int c, u32 bit_cycles)
{
    h_uart_frame(ctx, unit, c, (u32)c & 0xffu, 8, 2, bit_cycles);
}

/* the receiver's frame: its data bits and stop half bits (8N1 without a
 * format) */
static void rx_format(const mcu_uart *u, int *data, int *halves)
{
    u32 f = u->rx && u->rx->format ? u->rx->format(u->rx->ctx) : 0;
    *data = f & 0xff ? (int)(f & 0xff) : 8;
    *halves = f >> 8 & 0xff ? (int)(f >> 8 & 0xff) : 2;
    if (*data > 16)
        *data = 16;
}

static es_time_t frame_ps(mcu_state *st, mcu_uart *u)
{
    u32 bc = u->rx && u->rx->bit_cycles ? u->rx->bit_cycles(u->rx->ctx) : 1;
    int data, halves;
    es_time_t ps;
    rx_format(u, &data, &halves);
    ps = cycles_to_ps((u64)bc * (u64)(2 * (data + 1) + halves) / 2u, esk_mach_core_hz(&st->m), NULL);
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
    esk_mach_outside(&st->m);
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
    const char *text = esk_json_str(value), *b64 = esk_json_get_str(value, "base64", NULL);
    mcu_uart *u = uart_named(st, unit, 1);
    unsigned char bytes[1024];
    const unsigned char *data = (const unsigned char *)text;
    size_t i, n;
    /* text, or {"base64": ...} for any byte (a host port's binary data) */
    if ((!text && !b64) || !u || !u->rx)
        return ES_EINVAL;
    if (text) {
        n = strlen(text);
    } else {
        n = esk_base64_decode(b64, strlen(b64), bytes, sizeof bytes);
        if (n == (size_t)-1)
            return ES_EINVAL;
        data = bytes;
    }
    for (i = 0; i < n && u->inq_len < MCU_INQ_CAP; i++) {
        u->inq[(u->inq_head + u->inq_len) % MCU_INQ_CAP] = data[i];
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
    int data, halves;
    u->rx_ev = 0;
    if (!u->rx_busy || !u->rx_pin)
        return;
    v = esk_pin_level(u->rx_pin);
    rx_format(u, &data, &halves);
    if (u->rx_bit < data) {
        if (v == ESK_L1)
            u->rx_shift |= 1u << u->rx_bit;
        u->rx_bit++;
        (void)esk_sched_at(&e->sched, now + u->rx_bit_ps, ESK_EV_PERIPHERAL, rx_bit_cb, st, arg, &u->rx_ev);
        return;
    }
    /* the middle of the first stop bit */
    u->rx_busy = 0;
    catch_up(st, now);
    esk_mach_outside(&st->m);
    if (!runnable(st) || !u->rx || !u->rx->room(u->rx->ctx))
        return;
    if (u->rx->put_frame) {
        u->rx->put_frame(u->rx->ctx, u->rx_shift, v != ESK_L1);
        u->rx_bytes++;
        wake(st);
    } else if (v == ESK_L1) {
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
        if (u->rx->started) {
            catch_up(st, e->sched.now);
            esk_mach_outside(&st->m);
            u->rx->started(u->rx->ctx);
        }
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

/* a core enters an exception or interrupt: the session's, at the core's time */
static void h_exc(void *ctx, int core, int n)
{
    mcu_state *st = (mcu_state *)ctx;
    esk_engine *e = st->c->e;
    if (e->on_irq)
        e->on_irq(e->on_irq_ctx, st->c, core, esk_cortexm_cores(&st->m), n, local_now(st));
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
/* the part comes out of reset with the image loaded again. RAM keeps its
 * contents through a reset from the part itself (a watchdog's, a system
 * reset request) or from its reset pin, as an SRAM does while it is
 * powered; at power-on (the session's start or reset, a new image) it is
 * erased */
static void mcu_reset_now(mcu_state *st, int power_on)
{
    esk_engine *e = st->c->e;
    u32 entry = 0;
    char why[160];
    int r;
    if (!power_on && st->m.reset_cpu_only) {
        /* the core alone starts again: memory and devices keep their state */
        st->m.reset_req = 0;
        st->m.reset_cpu_only = 0;
        st->m.asleep = 0;
        st->m.yield = 0;
        st->m.cpu->ops->reset(st->m.cpu);
        return;
    }
    if (st->ev) {
        esk_sched_cancel(&e->sched, st->ev);
        st->ev = 0;
    }
    if (power_on)
        st->m.reset_cause = ESK_RESET_POWER;
    for (r = 0; r < st->m.bus.nrg; r++)
        if (!st->m.bus.rg[r].mirror && (power_on || st->m.bus.rg[r].kind != ESK_MEM_RAM))
            esk_bus_erase(&st->m.bus.rg[r]);
    if (!st->image && !esk_mach_store(&st->m, "flash")) {
        /* no image yet: the part sits blank, its pins inputs (a part that
         * boots its ROM from a flash it reaches through a device, as the
         * ESP32-C3, runs it on whatever the flash holds) */
        esk_mach_reset(&st->m);
        st->m.reset_cause = ESK_RESET_POWER;
        st->boot_fail = 1;
        st->t = e->sched.now;
        return;
    }
    if (st->image && esk_mach_load_image(&st->m, st->image, st->image_len, &entry, why, sizeof why) != ES_OK)
        raise(st, ES_SEV_ERROR, "mcu.image", "%s: %s", st->image_name, why);
    st->in_quantum = 0;
    esk_mach_reset(&st->m);
    st->m.reset_cause = ESK_RESET_POWER; /* what a model does not say next */
    if (st->an)
        esk_an_restart(st->an);
    st->stopped = 0;
    st->ended_reported = 0;
    st->boot_fail = 0;
    st->t = e->sched.now;
    st->rem = 0;
    map_rx_pins(st);
    st->fresh = 1;
    schedule_at(st, e->sched.now + 1);
}

/* ---- the radio ----------------------------------------------------------------- */

static uint32_t air_tuned(void *ctx, int kind)
{
    mcu_state *st = (mcu_state *)ctx;
    if (!runnable(st) && !(st->attached && st->m.asleep))
        return 0;
    return st->m.radio.tuned ? st->m.radio.tuned(st->m.radio.ctx, kind) : 0;
}

static int air_rx(void *ctx, const esk_air_frame *f, int rssi, es_time_t at)
{
    mcu_state *st = (mcu_state *)ctx;
    int r;
    (void)at; /* a BLE packet's end: the radio takes it from its access address on */
    catch_up(st, st->c->e->sched.now);
    esk_mach_outside(&st->m);
    r = st->m.radio.rx(st->m.radio.ctx, f->data, f->len, f->mhz, rssi, f->rate_kbps, f->kind);
    wake(st);
    return r;
}

static void air_tx_done(void *ctx, uint32_t token, int acked, es_time_t at)
{
    mcu_state *st = (mcu_state *)ctx;
    st->sup.tx_a = 0.0;
    supply_update(st, 0);
    if (!st->m.radio.tx_done)
        return;
    catch_up(st, at);
    esk_mach_outside(&st->m);
    st->m.radio.tx_done(st->m.radio.ctx, token, acked);
    wake(st);
}

static int h_radio_tx(void *ctx, const u8 *frame, u32 len, u32 mhz, int dbm, u32 rate_kbps, u64 airtime_ps,
                      int kind, u32 token)
{
    mcu_state *st = (mcu_state *)ctx;
    esk_air_frame f;
    if (!st->on_air)
        return -1;
    f.data = frame;
    f.len = len;
    f.mhz = mhz;
    f.dbm = (double)dbm;
    f.rate_kbps = rate_kbps;
    f.kind = kind;
    if (!airtime_ps)
        airtime_ps = kind == ESK_AIR_BLE ? esk_air_ble_airtime(len, rate_kbps) : esk_air_wifi_airtime(len, rate_kbps);
    if (esk_air_send(st->c->e, &st->air, &f, local_now(st), airtime_ps, token) != ES_OK)
        return -1;
    /* the power amplifier draws while the frame is on the air */
    st->sup.tx_a = supply_tx(&st->sup, kind, rate_kbps);
    supply_update(st, 0);
    return 0;
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
    if (st->on_air)
        esk_air_detach(e, &st->air);
    st->on_air = 0;
    esk_an_free(st->an);
    st->an = NULL;
    st->m.an = NULL;
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
        u->txq_len = u->txq_head = 0;
        u->tx_free = 0;
        u->tx_pin = NULL;
        u->text_len = 0;
        u->text[0] = 0;
        u->text_total = 0;
        u->tx_bytes = u->rx_bytes = 0;
        u->inq_len = u->inq_head = 0;
        u->rx_busy = 0;
    }
    st->held = 0;
    st->steps = 0;
    mcu_reset_now(st, 1);
    /* the core starts running: its first window counts from here */
    st->sup.win_t = 0;
    st->sup.run_ps = 0;
    st->sup.tx_a = 0.0;
    st->sup.cpu_a = supply_at(st->sup.run, st->sup.nrun, (double)esk_mach_core_hz(&st->m));
    st->sup.set_a = -1.0;
    supply_update(st, 1);
}

static void mcu_on_time(esk_comp *c, uint32_t token)
{
    mcu_state *st = state_of(c);
    if (token == MCU_WAKE_SUPPLY && st->attached)
        supply_update(st, 0);
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
            if (st->m.pin_reset)
                st->m.pin_reset(&st->m, 0);
            st->m.reset_cause = ESK_RESET_PIN;
            esk_mach_reset(&st->m);
            st->sup.tx_a = 0.0;
            supply_update(st, 1);
        } else if (!low && st->held) {
            st->held = 0;
            st->m.reset_cause = ESK_RESET_PIN;
            mcu_reset_now(st, 0);
            if (st->m.pin_reset)
                st->m.pin_reset(&st->m, 1);
            supply_update(st, 1);
        }
        return;
    }
    if (st->held)
        return;
    if (st->m.pins_changed) {
        es_time_t now = e->sched.now;
        st->m.input_lead = st->t > now ? (u32)ps_to_cycles(st->t - now, esk_mach_core_hz(&st->m)) : 0;
        esk_mach_outside(&st->m);
        st->m.pins_changed(&st->m);
        st->m.input_lead = 0;
    }
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
        st->m.reset_cause = ESK_RESET_PIN; /* the board's reset button */
        mcu_reset_now(st, 0);
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
    state = !st->image && !esk_mach_store(&st->m, "flash") ? "no_image"
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
                                     mcu_on_input, mcu_on_time, mcu_on_set, mcu_observe, NULL, 0 };

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
        char again[160]; /* the old image back; `why` keeps the new one's reason */
        esk_free(e->a, copy, len);
        if (st->image)
            (void)esk_mach_load_image(&st->m, st->image, st->image_len, &entry, again, sizeof again);
        return ES_EINVAL;
    }
    if (st->image)
        esk_free(e->a, st->image, st->image_len);
    st->image = copy;
    st->image_len = len;
    if (st->dwarf.a)
        esk_dwarf_free(&st->dwarf);
    (void)esk_dwarf_load(&st->dwarf, e->a, st->image, st->image_len);
    if (st->an_what)
        (void)esk_mcu_analyze(c, st->an_what, why, cap); /* the new image's functions */
    catch_up(st, e->sched.now);
    mcu_reset_now(st, 1);
    return ES_OK;
}

es_status esk_mcu_analyze(esk_comp *c, unsigned what, char *why, size_t cap)
{
    mcu_state *st = esk_mcu_is(c) ? state_of(c) : NULL;
    es_status r;
    if (!st || !st->attached)
        return ES_EINVAL;
    esk_an_free(st->an);
    st->an = NULL;
    st->m.an = NULL;
    st->an_what = what;
    if (!what)
        return ES_OK;
    if (!st->image) {
        esk_snprintf(why, cap, "the board has no firmware image");
        return ES_EINVAL;
    }
    r = esk_an_create(&st->m, what, st->image, st->image_len, st->dwarf.nrows ? &st->dwarf : NULL,
                      esk_cortexm_cores(&st->m), &st->an);
    if (r == ES_EINVAL)
        esk_snprintf(why, cap, "%s has no function symbols (an ELF image with its symbol table is needed)",
                     st->image_name);
    else if (r != ES_OK)
        esk_snprintf(why, cap, "out of memory");
    st->m.an = st->an;
    return r;
}

esk_an *esk_mcu_analysis(const esk_comp *c)
{
    const mcu_state *st = esk_mcu_is(c) ? state_of(c) : NULL;
    return st ? st->an : NULL;
}

const char *esk_mcu_console(esk_comp *c, size_t *len, u64 *total)
{
    mcu_state *st = esk_mcu_is(c) ? state_of(c) : NULL;
    mcu_uart *u = st && st->attached ? uart_named(st, st->console, 0) : NULL;
    *len = u ? u->text_len : 0;
    *total = u ? u->text_total : 0;
    return u ? u->text : "";
}

uint32_t esk_mcu_console_baud(esk_comp *c)
{
    mcu_state *st = esk_mcu_is(c) ? state_of(c) : NULL;
    mcu_uart *u = st && st->attached ? uart_named(st, st->console, 0) : NULL;
    u64 hz;
    u32 bc;
    if (!u || !u->rx || !u->rx->bit_cycles)
        return 0;
    bc = u->rx->bit_cycles(u->rx->ctx);
    hz = esk_mach_core_hz(&st->m);
    return bc ? (uint32_t)((hz + bc / 2) / bc) : 0;
}

const char *esk_mcu_image_name(const esk_comp *c)
{
    const mcu_state *st = esk_mcu_is(c) ? state_of(c) : NULL;
    return st ? st->image_name : NULL;
}

es_status esk_mcu_attach(esk_comp *c, const esk_mcu_setup *su, char *why, size_t cap)
{
    mcu_state *st = state_of(c);
    esk_engine *e = c->e;
    es_status st2;
    int port, bit, i;
    char name[12];
    if (!esk_mcu_is(c))
        return ES_EINVAL;
    esk_mach_init(&st->m, e->a);
    if (su->mac) {
        memcpy(st->m.factory_mac, su->mac, 6);
        st->m.has_factory_mac = 1;
    }
    st->attached = 1;
    st->m.host.ctx = st;
    st->m.host.out = h_out;
    st->m.host.pin_cfg = h_pin_cfg;
    st->m.host.pin_in = h_pin_in;
    st->m.host.uart_tx = h_uart_tx;
    st->m.host.uart_frame = h_uart_frame;
    st->m.host.diag = h_diag;
    st->m.host.exc = h_exc;
    st->m.host.pin_volts = h_pin_volts;
    st->m.host.temperature = h_temperature;
    st->m.host.pin_watch = h_pin_watch;
    st->m.host.radio_tx = h_radio_tx;
    st->quantum_ps = su->quantum_ps ? su->quantum_ps : MCU_DEFAULT_QUANTUM_PS;
    supply_setup(st, su->mcu);
    speed_table(esk_json_get(esk_json_get(esk_json_get(su->mcu, "io"), "electrical"), "rout"), st->rout);
    speed_table(esk_json_get(esk_json_get(esk_json_get(su->mcu, "io"), "electrical"), "slew_v_per_us"),
                st->slew);
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
            if (!st->gpio[port][bit]) {
                /* a PIC's: RA0, RD6 */
                snprintf(name, sizeof name, "R%c%d", 'A' + port, bit);
                st->gpio[port][bit] = esk_comp_pin(c, name);
            }
            if (st->gpio[port][bit] && st->gpio[port][bit]->model.kind == ESK_PIN_DIGITAL_IO)
                esk_pin_watch(st->gpio[port][bit], 1);
            else
                st->gpio[port][bit] = NULL;
        }
    st->nrst = esk_comp_pin(c, "NRST");
    if (!st->nrst) /* an ESP32-C3's CHIP_EN: low, the chip is off and resets */
        st->nrst = esk_comp_pin(c, "CHIP_EN");
    st->boot0 = esk_comp_pin(c, "BOOT0");
    if (st->nrst)
        esk_pin_watch(st->nrst, 1);
    st->m.hse_clock = su->hse_clock;
    st->m.lse_hz = su->lse_hz;
    st2 = esk_mach_build(&st->m, su->mcu, su->regs, su->hse_hz, why, cap);
    if (st2 != ES_OK)
        return st2;
    for (i = 0; i < su->nfiles; i++)
        if (esk_mach_factory(&st->m, su->files[i].base, su->files[i].data, su->files[i].len) != ES_OK) {
            snprintf(why, cap, "the memory file at 0x%08x does not fit its region", (unsigned)su->files[i].base);
            return ES_ESCHEMA;
        }
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
    if (st->m.radio.rx && !st->on_air) {
        /* its radio joins the session's air */
        st->air.ctx = st;
        st->air.comp = c;
        st->air.kind = st->m.radio.kinds;
        st->air.tuned = air_tuned;
        st->air.rx = air_rx;
        st->air.tx_done = air_tx_done;
        if (esk_air_attach(e, &st->air) != ES_OK)
            return ES_ENOMEM;
        st->on_air = 1;
    }
    return ES_OK;
}
