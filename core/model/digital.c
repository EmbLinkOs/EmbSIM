/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* digital.c -- the digital primitives of SPEC-003 §5: gates, flip-flop,
 * sources, pulls, switches as transmission gates, the functional indicator,
 * the logic probe and the UART virtual terminal. */
#include <string.h>
#include "engine/engine.h"
#include "model/registry.h"
#include "util/num.h"
#include "util/str.h"

/* ---- helpers --------------------------------------------------------------- */

static es_time_t tpd(const esk_comp *c)
{
    double v;
    if (c->e->policy == ESK_POLICY_FUNCTIONAL)
        return 0;
    if (esk_comp_param_double(c, "tpd_ps", &v) == 0 && v >= 0)
        return (es_time_t)v;
    if (esk_comp_param_double(c, "tpd_ns", &v) == 0 && v >= 0)
        return (es_time_t)(v * 1000.0);
    return 0;
}

static void watch_all_inputs(esk_comp *c)
{
    uint32_t i;
    for (i = 0; i < c->npins; i++)
        if (c->pins[i].model.kind == ESK_PIN_DIGITAL_IN || c->pins[i].model.kind == ESK_PIN_DIGITAL_IO)
            esk_pin_watch(&c->pins[i], 1);
}

static esk_pin *pin_or_null(esk_comp *c, const char *name) { return esk_comp_pin(c, name); }

/* ---- wire (functional stand-in for passives; nets are merged by the loader) */

static const esk_model_ops wire_ops = { "wire", 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0 };
/* a part with pins and no behaviour of its own (a supply symbol, a header, a
 * connector): its pin models do all the work; nets are not merged */
static const esk_model_ops none_ops = { "none", 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0 };

/* ---- gates ----------------------------------------------------------------- */

typedef enum { G_BUF, G_INV, G_AND, G_NAND, G_OR, G_NOR, G_XOR, G_XNOR } gate_kind;

typedef struct gate_state { gate_kind kind; esk_pin *y; } gate_state;

static esk_logic gate_eval(esk_comp *c, gate_kind k)
{
    uint32_t i;
    int n = 0, ones = 0, unknown = 0;
    for (i = 0; i < c->npins; i++) {
        esk_pin *p = &c->pins[i];
        esk_logic v;
        if (p->model.kind != ESK_PIN_DIGITAL_IN)
            continue;
        v = esk_pin_read(p);
        if (v == ESK_LX || v == ESK_LZ)
            unknown = 1;
        else if (v == ESK_L1)
            ones++;
        n++;
    }
    switch (k) {
    case G_BUF: return unknown ? ESK_LX : (ones ? ESK_L1 : ESK_L0);
    case G_INV: return unknown ? ESK_LX : (ones ? ESK_L0 : ESK_L1);
    case G_AND: if (ones < n && !unknown) return ESK_L0; return (ones == n) ? ESK_L1 : (unknown ? ESK_LX : ESK_L0);
    case G_NAND: if (ones < n && !unknown) return ESK_L1; return (ones == n) ? ESK_L0 : (unknown ? ESK_LX : ESK_L1);
    case G_OR: if (ones) return ESK_L1; return unknown ? ESK_LX : ESK_L0;
    case G_NOR: if (ones) return ESK_L0; return unknown ? ESK_LX : ESK_L1;
    case G_XOR: return unknown ? ESK_LX : ((ones & 1) ? ESK_L1 : ESK_L0);
    default: return unknown ? ESK_LX : ((ones & 1) ? ESK_L0 : ESK_L1);
    }
}

static void gate_update(esk_comp *c)
{
    gate_state *s = (gate_state *)c->state;
    if (s->y)
        esk_pin_drive(s->y, gate_eval(c, s->kind), ESK_STR_STRONG, tpd(c));
}

static es_status gate_create_kind(esk_comp *c, gate_kind k)
{
    gate_state *s = (gate_state *)c->state;
    s->kind = k;
    s->y = pin_or_null(c, "Y");
    if (!s->y)
        s->y = pin_or_null(c, "OUT");
    if (!s->y)
        return ES_ESCHEMA;
    watch_all_inputs(c);
    return ES_OK;
}

#define GATE(name, K) \
    static es_status name##_create(esk_comp *c) { return gate_create_kind(c, K); } \
    static void name##_in(esk_comp *c, esk_pin *p) { ES_UNUSED(p); gate_update(c); } \
    static void name##_reset(esk_comp *c) { gate_update(c); } \
    static const esk_model_ops name##_ops = { #name, sizeof(gate_state), name##_create, NULL, name##_reset, name##_in, NULL, NULL, NULL, NULL, 0 };

GATE(buffer, G_BUF)
GATE(inverter, G_INV)
GATE(and, G_AND)
GATE(nand, G_NAND)
GATE(or, G_OR)
GATE(nor, G_NOR)
GATE(xor, G_XOR)
GATE(xnor, G_XNOR)

/* ---- tristate buffer: A, OE (active high), Y --------------------------------- */

typedef struct tri_state { esk_pin *a, *oe, *y; } tri_state;

static void tri_update(esk_comp *c)
{
    tri_state *s = (tri_state *)c->state;
    esk_logic oe = esk_pin_read(s->oe);
    if (oe == ESK_L1)
        esk_pin_drive(s->y, esk_pin_read(s->a), ESK_STR_STRONG, tpd(c));
    else if (oe == ESK_L0)
        esk_pin_release(s->y);
    else
        esk_pin_drive(s->y, ESK_LX, ESK_STR_STRONG, tpd(c));
}
static es_status tri_create(esk_comp *c)
{
    tri_state *s = (tri_state *)c->state;
    s->a = pin_or_null(c, "A"); s->oe = pin_or_null(c, "OE"); s->y = pin_or_null(c, "Y");
    if (!s->a || !s->oe || !s->y) return ES_ESCHEMA;
    watch_all_inputs(c);
    return ES_OK;
}
static void tri_in(esk_comp *c, esk_pin *p) { ES_UNUSED(p); tri_update(c); }
static void tri_reset(esk_comp *c) { tri_update(c); }
static const esk_model_ops tristate_ops = { "tristate_buffer", sizeof(tri_state), tri_create, NULL, tri_reset, tri_in, NULL, NULL, NULL, NULL, 0 };

/* ---- D flip-flop: D, CLK, RST (active high, async), Q, QN ------------------- */

typedef struct dff_state { esk_pin *d, *clk, *rst, *q, *qn; esk_logic last_clk, value; } dff_state;

static void dff_out(esk_comp *c)
{
    dff_state *s = (dff_state *)c->state;
    esk_pin_drive(s->q, s->value, ESK_STR_STRONG, tpd(c));
    if (s->qn)
        esk_pin_drive(s->qn, s->value == ESK_L1 ? ESK_L0 : s->value == ESK_L0 ? ESK_L1 : ESK_LX, ESK_STR_STRONG, tpd(c));
}
static void dff_in(esk_comp *c, esk_pin *p)
{
    dff_state *s = (dff_state *)c->state;
    esk_logic clk;
    if (s->rst && p == s->rst) {
        if (esk_pin_read(s->rst) == ESK_L1) {
            s->value = ESK_L0;
            dff_out(c);
        }
        return;
    }
    if (p != s->clk)
        return;
    clk = esk_pin_read(s->clk);
    if (s->last_clk == ESK_L0 && clk == ESK_L1 && !(s->rst && esk_pin_read(s->rst) == ESK_L1)) {
        s->value = esk_pin_read(s->d);
        if (s->value == ESK_LZ)
            s->value = ESK_LX;
        dff_out(c);
    }
    s->last_clk = clk;
}
static es_status dff_create(esk_comp *c)
{
    dff_state *s = (dff_state *)c->state;
    s->d = pin_or_null(c, "D"); s->clk = pin_or_null(c, "CLK"); s->rst = pin_or_null(c, "RST");
    s->q = pin_or_null(c, "Q"); s->qn = pin_or_null(c, "QN");
    if (!s->d || !s->clk || !s->q) return ES_ESCHEMA;
    watch_all_inputs(c);
    return ES_OK;
}
static void dff_reset(esk_comp *c)
{
    dff_state *s = (dff_state *)c->state;
    s->value = ESK_L0;
    s->last_clk = ESK_L0;
    dff_out(c);
}
static const esk_model_ops dff_ops = { "dff", sizeof(dff_state), dff_create, NULL, dff_reset, dff_in, NULL, NULL, NULL, NULL, 0 };

/* ---- clock source: OUT; params hz (or period_ps), duty (0..1), start_high --- */

typedef struct clk_state { esk_pin *out; es_time_t high_ps, low_ps; int level; int enabled; } clk_state;

static void clk_time(esk_comp *c, uint32_t token)
{
    clk_state *s = (clk_state *)c->state;
    es_time_t next;
    ES_UNUSED(token);
    if (!s->enabled)
        return;
    s->level = !s->level;
    esk_pin_drive(s->out, s->level ? ESK_L1 : ESK_L0, ESK_STR_STRONG, 0);
    if (esk_time_add(esk_comp_now(c), s->level ? s->high_ps : s->low_ps, &next) == 0)
        esk_comp_wake_at(c, next, 0);
}
static es_status clk_create(esk_comp *c)
{
    clk_state *s = (clk_state *)c->state;
    double hz = 0, duty = 0.5, period = 0;
    s->out = pin_or_null(c, "OUT");
    if (!s->out) return ES_ESCHEMA;
    if (esk_comp_param_double(c, "period_ps", &period) != 0 || period <= 0) {
        if (esk_comp_param_double(c, "hz", &hz) != 0 || hz <= 0)
            hz = 1000.0;
        period = 1e12 / hz;
    }
    esk_comp_param_double(c, "duty", &duty);
    if (duty <= 0 || duty >= 1) duty = 0.5;
    s->high_ps = (es_time_t)(period * duty);
    s->low_ps = (es_time_t)period - s->high_ps;
    if (!s->high_ps) s->high_ps = 1;
    if (!s->low_ps) s->low_ps = 1;
    return ES_OK;
}
static void clk_reset(esk_comp *c)
{
    clk_state *s = (clk_state *)c->state;
    int start_high = 0;
    esk_comp_param_bool(c, "start_high", &start_high);
    s->enabled = esk_comp_input_bool(c, "enabled", 1);
    s->level = start_high;
    esk_pin_drive(s->out, s->level ? ESK_L1 : ESK_L0, ESK_STR_STRONG, 0);
    esk_comp_wake_at(c, s->level ? s->high_ps : s->low_ps, 0);
}
static es_status clk_set(esk_comp *c, const char *input, const esk_json *value)
{
    clk_state *s = (clk_state *)c->state;
    if (!strcmp(input, "enabled") && value->type == ESK_JSON_BOOL) {
        int was = s->enabled;
        s->enabled = value->u.boolean;
        if (!was && s->enabled)
            esk_comp_wake_at(c, esk_comp_now(c) + (s->level ? s->high_ps : s->low_ps), 0);
        return ES_OK;
    }
    return ES_EINVAL;
}
static const esk_model_ops clock_ops = { "clock_source", sizeof(clk_state), clk_create, NULL, clk_reset, NULL, clk_time, clk_set, NULL, NULL, 0 };

/* ---- pulse source (digital): OUT; params delay_ps, width_ps, level ------------ */

typedef struct pulse_state { esk_pin *out; es_time_t delay, width; int level; } pulse_state;
static es_status pulse_create(esk_comp *c)
{
    pulse_state *s = (pulse_state *)c->state;
    double d = 0, w = 1e6;
    s->out = pin_or_null(c, "OUT");
    if (!s->out) return ES_ESCHEMA;
    esk_comp_param_double(c, "delay_ps", &d);
    esk_comp_param_double(c, "width_ps", &w);
    s->delay = d > 0 ? (es_time_t)d : 0;
    s->width = w > 0 ? (es_time_t)w : 1;
    s->level = 1;
    {
        int64_t lv;
        if (esk_comp_param_int(c, "level", &lv) == 0)
            s->level = lv != 0;
    }
    return ES_OK;
}
static void pulse_reset(esk_comp *c)
{
    pulse_state *s = (pulse_state *)c->state;
    esk_pin_drive(s->out, s->level ? ESK_L0 : ESK_L1, ESK_STR_STRONG, 0);
    esk_comp_wake_at(c, s->delay, 0);
}
static void pulse_time(esk_comp *c, uint32_t token)
{
    pulse_state *s = (pulse_state *)c->state;
    if (token == 0) {
        esk_pin_drive(s->out, s->level ? ESK_L1 : ESK_L0, ESK_STR_STRONG, 0);
        esk_comp_wake_at(c, esk_comp_now(c) + s->width, 1);
    } else {
        esk_pin_drive(s->out, s->level ? ESK_L0 : ESK_L1, ESK_STR_STRONG, 0);
    }
}
static const esk_model_ops pulse_ops = { "pulse_source", sizeof(pulse_state), pulse_create, NULL, pulse_reset, NULL, pulse_time, NULL, NULL, NULL, 0 };

/* ---- logic level and pull ---------------------------------------------------- */

typedef struct level_state { esk_pin *out; int level; esk_strength strength; } level_state;
static es_status level_create_common(esk_comp *c, esk_strength strength, const char *pin)
{
    level_state *s = (level_state *)c->state;
    int64_t lv = 0;
    s->out = pin_or_null(c, pin);
    if (!s->out) s->out = c->npins ? &c->pins[0] : NULL;
    if (!s->out) return ES_ESCHEMA;
    esk_comp_param_int(c, "level", &lv);
    s->level = lv != 0;
    s->strength = strength;
    return ES_OK;
}
static es_status level_create(esk_comp *c) { return level_create_common(c, ESK_STR_STRONG, "OUT"); }
static es_status pull_create(esk_comp *c) { return level_create_common(c, ESK_STR_WEAK, "1"); }
static void level_reset(esk_comp *c)
{
    level_state *s = (level_state *)c->state;
    esk_pin_drive(s->out, s->level ? ESK_L1 : ESK_L0, s->strength, 0);
}
static es_status level_set(esk_comp *c, const char *input, const esk_json *value)
{
    level_state *s = (level_state *)c->state;
    if (!strcmp(input, "level")) {
        if (value->type == ESK_JSON_BOOL) s->level = value->u.boolean;
        else if (value->type == ESK_JSON_NUMBER) s->level = value->u.number.value != 0;
        else return ES_EINVAL;
        esk_pin_drive(s->out, s->level ? ESK_L1 : ESK_L0, s->strength, 0);
        return ES_OK;
    }
    return ES_EINVAL;
}
static const esk_model_ops level_ops = { "logic_level", sizeof(level_state), level_create, NULL, level_reset, NULL, NULL, level_set, NULL, NULL, 0 };
static const esk_model_ops pull_ops = { "pull", sizeof(level_state), pull_create, NULL, level_reset, NULL, NULL, level_set, NULL, NULL, 0 };

/* ---- switches as transmission gates (SPEC-003 §4 functional switch) ---------- */

typedef struct sw_state { esk_pin *p1, *p2; int closed; const char *input; int momentary; } sw_state;

static void sw_eval(esk_comp *c)
{
    sw_state *s = (sw_state *)c->state;
    esk_logic v1, v2;
    esk_strength s1, s2;
    if (!s->closed || !s->p1->net || !s->p2->net) {
        esk_pin_release(s->p1);
        esk_pin_release(s->p2);
        return;
    }
    esk_net_resolve_excluding(s->p1->net, c, &v1, &s1);
    esk_net_resolve_excluding(s->p2->net, c, &v2, &s2);
    if (v1 != ESK_LZ && v2 == ESK_LZ) {
        esk_pin_release(s->p1);
        esk_pin_drive(s->p2, v1, s1, 0);
    } else if (v2 != ESK_LZ && v1 == ESK_LZ) {
        esk_pin_release(s->p2);
        esk_pin_drive(s->p1, v2, s2, 0);
    } else if (v1 != ESK_LZ && v2 != ESK_LZ) {
        if (v1 == v2) {
            esk_pin_release(s->p1);
            esk_pin_release(s->p2);
        } else {
            esk_pin_drive(s->p1, v2, s2, 0);
            esk_pin_drive(s->p2, v1, s1, 0);
        }
    } else {
        esk_pin_release(s->p1);
        esk_pin_release(s->p2);
    }
}
static es_status sw_create_common(esk_comp *c, const char *input)
{
    sw_state *s = (sw_state *)c->state;
    s->p1 = pin_or_null(c, "1");
    s->p2 = pin_or_null(c, "2");
    if (!s->p1 || !s->p2) return ES_ESCHEMA;
    s->input = input;
    esk_pin_set_direction(s->p1, 1);
    esk_pin_set_direction(s->p2, 1);
    esk_pin_watch(s->p1, 1);
    esk_pin_watch(s->p2, 1);
    return ES_OK;
}
static es_status btn_create(esk_comp *c) { return sw_create_common(c, "pressed"); }
static es_status swd_create(esk_comp *c) { return sw_create_common(c, "closed"); }
static void sw_reset(esk_comp *c)
{
    sw_state *s = (sw_state *)c->state;
    int closed = 0;
    esk_comp_param_bool(c, "closed", &closed);
    s->closed = esk_comp_input_bool(c, s->input, closed);
    esk_pin_set_direction(s->p1, 1);
    esk_pin_set_direction(s->p2, 1);
    sw_eval(c);
}
static void sw_in(esk_comp *c, esk_pin *p) { ES_UNUSED(p); sw_eval(c); }
static es_status sw_set(esk_comp *c, const char *input, const esk_json *value)
{
    sw_state *s = (sw_state *)c->state;
    if (!strcmp(input, s->input) || !strcmp(input, "closed") || !strcmp(input, "pressed")) {
        if (value->type != ESK_JSON_BOOL) return ES_EINVAL;
        s->closed = value->u.boolean;
        sw_eval(c);
        return ES_OK;
    }
    return ES_EINVAL;
}
static void sw_observe(esk_comp *c, esk_json *out)
{
    sw_state *s = (sw_state *)c->state;
    esk_json_set_bool(c->e->a, out, "closed", s->closed);
}
static const esk_model_ops button_ops = { "push_button_digital", sizeof(sw_state), btn_create, NULL, sw_reset, sw_in, NULL, sw_set, sw_observe, NULL, 0 };
static const esk_model_ops switch_ops = { "switch_digital", sizeof(sw_state), swd_create, NULL, sw_reset, sw_in, NULL, sw_set, sw_observe, NULL, 0 };

/* ---- indicator (functional LED): A, K; lit when A=1 and K=0 (or K unconnected) */

typedef struct ind_state { esk_pin *a, *k; int lit; } ind_state;
static void ind_eval(esk_comp *c)
{
    ind_state *s = (ind_state *)c->state;
    esk_logic a = esk_pin_read(s->a);
    esk_logic k = (s->k && s->k->net) ? esk_pin_read(s->k) : ESK_L0;
    s->lit = a == ESK_L1 && k == ESK_L0;
}
static es_status ind_create(esk_comp *c)
{
    ind_state *s = (ind_state *)c->state;
    s->a = pin_or_null(c, "A");
    if (!s->a) s->a = c->npins ? &c->pins[0] : NULL;
    s->k = pin_or_null(c, "K");
    if (!s->a) return ES_ESCHEMA;
    esk_pin_watch(s->a, 1);
    if (s->k) esk_pin_watch(s->k, 1);
    return ES_OK;
}
static void ind_reset(esk_comp *c) { ind_eval(c); }
static void ind_in(esk_comp *c, esk_pin *p) { ES_UNUSED(p); ind_eval(c); }
static void ind_observe(esk_comp *c, esk_json *out)
{
    ind_state *s = (ind_state *)c->state;
    esk_json_set_bool(c->e->a, out, "lit", s->lit);
    esk_json_set_number(c->e->a, out, "luminance", s->lit ? 1.0 : 0.0);
    esk_json_set_string(c->e->a, out, "logic", esk_logic_name(s->a->sampled));
}
static const esk_model_ops indicator_ops = { "indicator", sizeof(ind_state), ind_create, NULL, ind_reset, ind_in, NULL, NULL, ind_observe, NULL, 0 };

/* ---- logic probe: pin 1; output logic -------------------------------------- */

typedef struct probe_state { esk_pin *p; } probe_state;
static es_status probe_create(esk_comp *c)
{
    probe_state *s = (probe_state *)c->state;
    s->p = c->npins ? &c->pins[0] : NULL;
    if (!s->p) return ES_ESCHEMA;
    esk_pin_watch(s->p, 1);
    return ES_OK;
}
static void probe_in(esk_comp *c, esk_pin *p) { ES_UNUSED(c); esk_pin_read(p); }
static void probe_reset(esk_comp *c) { esk_pin_read(((probe_state *)c->state)->p); }
static void probe_observe(esk_comp *c, esk_json *out)
{
    probe_state *s = (probe_state *)c->state;
    esk_json_set_string(c->e->a, out, "logic", esk_logic_name(s->p->net ? esk_net_value(s->p->net) : ESK_LZ));
}
static const esk_model_ops probe_ops = { "logic_probe", sizeof(probe_state), probe_create, NULL, probe_reset, probe_in, NULL, NULL, probe_observe, NULL, 0 };

/* ---- virtual terminal: RX (in), TX (out); 8N1 at `baud` ------------------------ */

#define VT_BUF 4096
typedef struct vt_state {
    esk_pin *rx, *tx;
    es_time_t bit_ps;
    int rx_busy, rx_bits;
    unsigned rx_shift;
    char rx_text[VT_BUF];
    size_t rx_len;
    unsigned char tx_queue[VT_BUF];
    size_t tx_head, tx_len;
    int tx_busy, tx_bit;
    unsigned tx_frame;
    uint64_t rx_bytes, tx_bytes;
} vt_state;

static void vt_tx_start(esk_comp *c)
{
    vt_state *s = (vt_state *)c->state;
    unsigned char byte;
    if (s->tx_busy || !s->tx_len || !s->tx)
        return;
    byte = s->tx_queue[s->tx_head];
    s->tx_head = (s->tx_head + 1) % VT_BUF;
    s->tx_len--;
    s->tx_frame = (unsigned)byte << 1 | 0x200u; /* start 0, 8 data, stop 1 */
    s->tx_bit = 0;
    s->tx_busy = 1;
    esk_pin_drive(s->tx, ESK_L0, ESK_STR_STRONG, 0);
    esk_comp_wake_at(c, esk_comp_now(c) + s->bit_ps, 1);
}
static void vt_time(esk_comp *c, uint32_t token)
{
    vt_state *s = (vt_state *)c->state;
    if (token == 0) {
        /* sample an RX bit */
        esk_logic v = esk_pin_read(s->rx);
        if (s->rx_bits < 8) {
            if (v == ESK_L1)
                s->rx_shift |= 1u << s->rx_bits;
            s->rx_bits++;
            esk_comp_wake_at(c, esk_comp_now(c) + s->bit_ps, 0);
        } else {
            /* stop bit */
            if (s->rx_len + 1 < VT_BUF)
                s->rx_text[s->rx_len++] = (char)s->rx_shift;
            s->rx_text[s->rx_len] = 0;
            s->rx_bytes++;
            s->rx_busy = 0;
        }
    } else {
        s->tx_bit++;
        if (s->tx_bit <= 9) {
            esk_pin_drive(s->tx, (s->tx_frame >> s->tx_bit) & 1u ? ESK_L1 : ESK_L0, ESK_STR_STRONG, 0);
            esk_comp_wake_at(c, esk_comp_now(c) + s->bit_ps, 1);
        } else {
            s->tx_busy = 0;
            s->tx_bytes++;
            vt_tx_start(c);
        }
    }
}
static void vt_in(esk_comp *c, esk_pin *p)
{
    vt_state *s = (vt_state *)c->state;
    if (p != s->rx || s->rx_busy)
        return;
    if (esk_pin_read(s->rx) == ESK_L0) {
        s->rx_busy = 1;
        s->rx_bits = 0;
        s->rx_shift = 0;
        esk_comp_wake_at(c, esk_comp_now(c) + s->bit_ps + s->bit_ps / 2, 0);
    }
}
static es_status vt_create(esk_comp *c)
{
    vt_state *s = (vt_state *)c->state;
    double baud = 115200;
    s->rx = pin_or_null(c, "RX");
    s->tx = pin_or_null(c, "TX");
    if (!s->rx) return ES_ESCHEMA;
    esk_comp_param_double(c, "baud", &baud);
    if (baud <= 0) baud = 115200;
    s->bit_ps = (es_time_t)(1e12 / baud);
    esk_pin_watch(s->rx, 1);
    return ES_OK;
}
static void vt_reset(esk_comp *c)
{
    vt_state *s = (vt_state *)c->state;
    s->rx_busy = 0; s->rx_len = 0; s->rx_text[0] = 0; s->tx_head = s->tx_len = 0; s->tx_busy = 0;
    s->rx_bytes = s->tx_bytes = 0;
    if (s->tx)
        esk_pin_drive(s->tx, ESK_L1, ESK_STR_STRONG, 0);
}
static es_status vt_set(esk_comp *c, const char *input, const esk_json *value)
{
    vt_state *s = (vt_state *)c->state;
    const char *text;
    size_t i, n;
    if (strcmp(input, "tx_text") && strcmp(input, "tx"))
        return ES_EINVAL;
    text = esk_json_str(value);
    if (!text) return ES_EINVAL;
    n = strlen(text);
    for (i = 0; i < n && s->tx_len < VT_BUF; i++) {
        s->tx_queue[(s->tx_head + s->tx_len) % VT_BUF] = (unsigned char)text[i];
        s->tx_len++;
    }
    vt_tx_start(c);
    return ES_OK;
}
static void vt_observe(esk_comp *c, esk_json *out)
{
    vt_state *s = (vt_state *)c->state;
    esk_json_set_string(c->e->a, out, "rx_text", s->rx_text);
    esk_json_set_integer(c->e->a, out, "rx_bytes", (int64_t)s->rx_bytes);
    esk_json_set_integer(c->e->a, out, "tx_bytes", (int64_t)s->tx_bytes);
    esk_json_set_integer(c->e->a, out, "tx_pending", (int64_t)s->tx_len + (s->tx_busy ? 1 : 0));
}
static const esk_model_ops vt_ops = { "virtual_terminal", sizeof(vt_state), vt_create, NULL, vt_reset, vt_in, vt_time, vt_set, vt_observe, NULL, 0 };

/* ---- the table ------------------------------------------------------------- */

static const esk_model_ops *const digital_table[] = {
    &wire_ops, &none_ops, &buffer_ops, &inverter_ops, &and_ops, &nand_ops, &or_ops, &nor_ops, &xor_ops, &xnor_ops,
    &tristate_ops, &dff_ops, &clock_ops, &pulse_ops, &level_ops, &pull_ops, &button_ops, &switch_ops,
    &indicator_ops, &probe_ops, &vt_ops
};

const esk_model_ops *const *esk_digital_models(size_t *n)
{
    *n = ES_COUNTOF(digital_table);
    return digital_table;
}
