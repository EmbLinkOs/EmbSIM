/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* engine.c -- components, pins, nets, digital resolution (SPEC-003 §3),
 * islands and bridges (SPEC-003 §4), and the model API (SPEC-003 §5). */
#include "engine/engine.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "analog/analog.h"
#include "util/num.h"
#include "util/str.h"

#define MAX_PINS_PER_NET 4096

/* ---- names --------------------------------------------------------------- */

const char *esk_logic_name(esk_logic v)
{
    switch (v) {
    case ESK_L0: return "0";
    case ESK_L1: return "1";
    case ESK_LZ: return "Z";
    default: return "X";
    }
}

esk_logic esk_logic_from_name(const char *s)
{
    if (!s) return ESK_LX;
    if (!strcmp(s, "0")) return ESK_L0;
    if (!strcmp(s, "1")) return ESK_L1;
    if (!strcmp(s, "Z") || !strcmp(s, "z")) return ESK_LZ;
    return ESK_LX;
}

const char *esk_pin_kind_name(esk_pin_kind k)
{
    switch (k) {
    case ESK_PIN_DIGITAL_OUT: return "digital_out";
    case ESK_PIN_DIGITAL_IN: return "digital_in";
    case ESK_PIN_DIGITAL_IO: return "digital_io";
    case ESK_PIN_ANALOG: return "analog";
    case ESK_PIN_POWER: return "power";
    case ESK_PIN_PASSIVE: return "passive";
    default: return "nc";
    }
}

/* ---- pin models ---------------------------------------------------------- */

void esk_pin_model_family(const char *family, esk_pin_model *m)
{
    memset(m, 0, sizeof *m);
    m->kind = ESK_PIN_PASSIVE;
    m->drive = ESK_DRIVE_PUSH_PULL;
    m->strength = ESK_STR_STRONG;
    m->slew_v_per_us = 500.0;
    m->rpull = 40000.0;
    m->leak_a = 1e-6;
    if (!family || !strcmp(family, "cmos_3v3")) {
        m->voh = 2.9; m->vol = 0.4; m->rout = 25.0; m->vih = 2.0; m->vil = 0.8; m->hyst = 0.2; m->nominal_v = 3.3;
        m->has_electrical = family != NULL;
    } else if (!strcmp(family, "cmos_5v")) {
        m->voh = 4.4; m->vol = 0.5; m->rout = 25.0; m->vih = 3.0; m->vil = 1.5; m->hyst = 0.4; m->nominal_v = 5.0; m->has_electrical = 1;
    } else if (!strcmp(family, "ttl_5v")) {
        m->voh = 2.7; m->vol = 0.4; m->rout = 50.0; m->vih = 2.0; m->vil = 0.8; m->hyst = 0.0; m->nominal_v = 5.0; m->has_electrical = 1;
    } else if (!strcmp(family, "lvcmos_1v8")) {
        m->voh = 1.5; m->vol = 0.3; m->rout = 30.0; m->vih = 1.17; m->vil = 0.63; m->hyst = 0.1; m->nominal_v = 1.8; m->has_electrical = 1;
    } else {
        m->voh = 2.9; m->vol = 0.4; m->rout = 25.0; m->vih = 2.0; m->vil = 0.8; m->hyst = 0.2; m->nominal_v = 3.3;
    }
    /* a CMOS output's transistors pull to its rails: voh and vol are the
     * levels at the rated load, which rout gives; a TTL totem pole's open
     * high level is below the supply, so its sources are voh and vol */
    m->rails = !family || strcmp(family, "ttl_5v") != 0;
}

static int get_d(const esk_json *o, const char *k, double *v)
{
    const esk_json *j = esk_json_get(o, k);
    return j && esk_json_to_double(j, v) == 0;
}

/* the electrical fields of a pin model (or of a digital_io's nested out or
 * in object, which has no kind of its own) */
static int parse_fields(const esk_json *obj, esk_pin_model *out)
{
    const char *s;
    double v;
    s = esk_json_get_str(obj, "drive", NULL);
    if (s) {
        if (!strcmp(s, "push_pull")) out->drive = ESK_DRIVE_PUSH_PULL;
        else if (!strcmp(s, "open_drain")) out->drive = ESK_DRIVE_OPEN_DRAIN;
        else if (!strcmp(s, "open_source")) out->drive = ESK_DRIVE_OPEN_SOURCE;
        else if (!strcmp(s, "tristate")) out->drive = ESK_DRIVE_TRISTATE;
        else return -1;
    }
    s = esk_json_get_str(obj, "strength", NULL);
    if (s) {
        if (!strcmp(s, "strong")) out->strength = ESK_STR_STRONG;
        else if (!strcmp(s, "weak")) out->strength = ESK_STR_WEAK;
        else if (!strcmp(s, "supply")) out->strength = ESK_STR_SUPPLY;
        else return -1;
    }
    s = esk_json_get_str(obj, "pull", NULL);
    if (s) {
        if (!strcmp(s, "none")) out->pull = ESK_PULL_NONE;
        else if (!strcmp(s, "up")) out->pull = ESK_PULL_UP;
        else if (!strcmp(s, "down")) out->pull = ESK_PULL_DOWN;
        else return -1;
    }
    s = esk_json_get_str(obj, "role", NULL);
    if (s) {
        if (!strcmp(s, "ground")) out->power_ground = 1;
        else if (!strcmp(s, "supply")) out->power_ground = 0;
        else return -1;
    }
    if (get_d(obj, "voc_high", &v)) { out->voc_high = v; out->voc_set |= 1; out->has_electrical = 1; }
    if (get_d(obj, "voc_low", &v)) { out->voc_low = v; out->voc_set |= 2; out->has_electrical = 1; }
    if (get_d(obj, "voh", &v)) { out->voh = v; out->has_electrical = 1; }
    if (get_d(obj, "vol", &v)) { out->vol = v; out->has_electrical = 1; }
    if (get_d(obj, "rout", &v)) { out->rout = v; out->has_electrical = 1; }
    if (get_d(obj, "slew_v_per_us", &v)) out->slew_v_per_us = v;
    if (get_d(obj, "vih", &v)) { out->vih = v; out->has_electrical = 1; }
    if (get_d(obj, "vil", &v)) { out->vil = v; out->has_electrical = 1; }
    if (get_d(obj, "hysteresis", &v)) out->hyst = v;
    if (get_d(obj, "rpull", &v)) out->rpull = v;
    if (get_d(obj, "leak_a", &v)) out->leak_a = v;
    if (get_d(obj, "nominal_v", &v)) { out->nominal_v = v; out->has_electrical = 1; }
    if (get_d(obj, "tolerance", &v)) out->tolerance = v;
    if (out->kind == ESK_PIN_POWER && out->power_ground)
        out->nominal_v = 0.0;
    return 0;
}

int esk_pin_model_parse(const esk_json *obj, const char *family, esk_pin_model *out)
{
    const char *kind;
    esk_pin_model_family(family, out);
    if (!obj || obj->type != ESK_JSON_OBJECT)
        return -1;
    kind = esk_json_get_str(obj, "kind", NULL);
    if (!kind) return -1;
    if (!strcmp(kind, "digital_out")) out->kind = ESK_PIN_DIGITAL_OUT;
    else if (!strcmp(kind, "digital_in")) out->kind = ESK_PIN_DIGITAL_IN;
    else if (!strcmp(kind, "digital_io")) out->kind = ESK_PIN_DIGITAL_IO;
    else if (!strcmp(kind, "analog")) out->kind = ESK_PIN_ANALOG;
    else if (!strcmp(kind, "power")) out->kind = ESK_PIN_POWER;
    else if (!strcmp(kind, "passive")) out->kind = ESK_PIN_PASSIVE;
    else if (!strcmp(kind, "nc")) out->kind = ESK_PIN_NC;
    else return -1;
    if (out->kind == ESK_PIN_DIGITAL_IO) {
        /* nested out/in objects or flat fields */
        const esk_json *o2 = esk_json_get(obj, "out"), *i2 = esk_json_get(obj, "in");
        if (o2 && o2->type == ESK_JSON_OBJECT) { esk_pin_model tmp = *out;
            if (parse_fields(o2, &tmp) == 0) { out->drive = tmp.drive; out->strength = tmp.strength; out->voh = tmp.voh; out->vol = tmp.vol; out->rout = tmp.rout; out->voc_set = tmp.voc_set; out->voc_high = tmp.voc_high; out->voc_low = tmp.voc_low; out->slew_v_per_us = tmp.slew_v_per_us; out->has_electrical |= tmp.has_electrical; } }
        if (i2 && i2->type == ESK_JSON_OBJECT) { esk_pin_model tmp = *out;
            if (parse_fields(i2, &tmp) == 0) { out->vih = tmp.vih; out->vil = tmp.vil; out->hyst = tmp.hyst; out->pull = tmp.pull; out->rpull = tmp.rpull; out->leak_a = tmp.leak_a; out->has_electrical |= tmp.has_electrical; } }
    }
    return parse_fields(obj, out);
}

/* ---- lifecycle ----------------------------------------------------------- */

es_status esk_engine_init(esk_engine *e, esk_alloc *a, esk_diag *diag, esk_policy policy)
{
    memset(e, 0, sizeof *e);
    e->a = a;
    e->diag = diag;
    e->policy = policy;
    e->max_nets = 65536;
    e->max_comps = 65536;
    e->crossing_resolution_ps = 100;
    e->default_supply_v = 3.3;
    esk_map_init(&e->comp_by_id, a);
    esk_map_init(&e->net_by_name, a);
    esk_world_init(&e->world);
    esk_air_init(&e->air);
    esk_usb_init(&e->usb);
    esk_inet_init(&e->inet);
    return esk_sched_init(&e->sched, a, 0, 0);
}

static void comp_free(esk_engine *e, esk_comp *c)
{
    uint32_t i;
    if (!c)
        return;
    if (c->ops && c->ops->destroy && c->state)
        c->ops->destroy(c);
    for (i = 0; i < c->npins; i++)
        esk_strfree(e->a, c->pins[i].name);
    esk_free(e->a, c->pins, c->npins * sizeof *c->pins);
    if (c->ops)
        esk_free(e->a, c->state, c->ops->state_size);
    esk_json_free(e->a, c->params);
    esk_json_free(e->a, c->inputs);
    esk_strfree(e->a, c->id);
    esk_strfree(e->a, c->kind);
    esk_free(e->a, c, sizeof *c);
}

static void net_free(esk_engine *e, esk_net *n)
{
    size_t i;
    if (!n)
        return;
    for (i = 0; i < n->aliases.len; i++)
        esk_strfree(e->a, n->aliases.data[i]);
    esk_vec_free(e->a, &n->aliases);
    esk_vec_free(e->a, &n->pins);
    esk_strfree(e->a, n->name);
    esk_free(e->a, n, sizeof *n);
}

static void islands_free(esk_engine *e)
{
    size_t i;
    for (i = 0; i < e->islands.len; i++)
        esk_island_destroy(e->islands.data[i]);
    esk_vec_clear(&e->islands);
    for (i = 0; i < e->nets.len; i++) {
        e->nets.data[i]->island = NULL;
        e->nets.data[i]->node = -1;
    }
    for (i = 0; i < e->comps.len; i++) {
        esk_comp *c = e->comps.data[i];
        uint32_t k;
        c->adev = NULL;
        for (k = 0; k < c->npins; k++) {
            c->pins[k].analog_node = -1;
            c->pins[k].bridge = NULL;
        }
    }
    e->partitioned = 0;
}

void esk_engine_free(esk_engine *e)
{
    size_t i;
    islands_free(e);
    esk_vec_free(e->a, &e->islands);
    for (i = 0; i < e->comps.len; i++)
        comp_free(e, e->comps.data[i]);
    esk_vec_free(e->a, &e->comps);
    for (i = 0; i < e->nets.len; i++)
        net_free(e, e->nets.data[i]);
    esk_vec_free(e->a, &e->nets);
    esk_map_free(&e->comp_by_id);
    esk_map_free(&e->net_by_name);
    esk_world_free(e->a, &e->world);
    esk_air_free(e->a, &e->air);
    esk_usb_free(e->a, &e->usb);
    esk_inet_free(e->a, &e->inet);
    esk_sched_free(&e->sched);
    if (e->ext && e->ext_free)
        e->ext_free(e->ext);
    e->ext = NULL;
}

es_status esk_engine_add_net(esk_engine *e, const char *name, esk_net **out)
{
    esk_net *n;
    if (!name || !*name)
        return ES_EINVAL;
    if (esk_map_has(&e->net_by_name, name))
        return ES_EINVAL;
    if (e->nets.len >= e->max_nets)
        return ES_ELIMIT;
    n = (esk_net *)esk_calloc(e->a, 1, sizeof *n);
    if (!n)
        return ES_ENOMEM;
    n->name = esk_strdup(e->a, name);
    if (!n->name) {
        esk_free(e->a, n, sizeof *n);
        return ES_ENOMEM;
    }
    n->index = (uint32_t)e->nets.len;
    n->value = ESK_LZ;
    n->node = -1;
    if (esk_vec_push(e->a, &e->nets, n) || esk_map_set(&e->net_by_name, name, n)) {
        net_free(e, n);
        return ES_ENOMEM;
    }
    if (out)
        *out = n;
    return ES_OK;
}

es_status esk_engine_net_alias(esk_engine *e, esk_net *net, const char *alias)
{
    char *s;
    if (esk_map_has(&e->net_by_name, alias))
        return ES_EINVAL;
    s = esk_strdup(e->a, alias);
    if (!s)
        return ES_ENOMEM;
    if (esk_vec_push(e->a, &net->aliases, s) || esk_map_set(&e->net_by_name, alias, net)) {
        esk_strfree(e->a, s);
        return ES_ENOMEM;
    }
    return ES_OK;
}

es_status esk_engine_add_comp(esk_engine *e, const char *id, const char *kind, const esk_model_ops *ops,
                              const char *const *pin_names, const esk_pin_model *pin_models, uint32_t npins,
                              const esk_json *params, const esk_json *desc, esk_comp **out)
{
    esk_comp *c;
    uint32_t i;
    es_status st;
    if (!id || !ops || (npins && (!pin_names || !pin_models)))
        return ES_EINVAL;
    if (esk_map_has(&e->comp_by_id, id))
        return ES_EINVAL;
    if (e->comps.len >= e->max_comps)
        return ES_ELIMIT;
    c = (esk_comp *)esk_calloc(e->a, 1, sizeof *c);
    if (!c)
        return ES_ENOMEM;
    c->e = e;
    c->ops = ops;
    c->desc = desc;
    c->id = esk_strdup(e->a, id);
    c->kind = esk_strdup(e->a, kind ? kind : ops->name);
    c->params = params ? esk_json_clone(e->a, params) : esk_json_new_object(e->a);
    c->inputs = esk_json_new_object(e->a);
    c->state = ops->state_size ? esk_calloc(e->a, 1, ops->state_size) : NULL;
    c->pins = npins ? (esk_pin *)esk_calloc(e->a, npins, sizeof *c->pins) : NULL;
    if (!c->id || !c->kind || !c->params || !c->inputs || (ops->state_size && !c->state) || (npins && !c->pins)) {
        c->npins = 0;
        comp_free(e, c);
        return ES_ENOMEM;
    }
    c->npins = npins;
    for (i = 0; i < npins; i++) {
        esk_pin *p = &c->pins[i];
        p->comp = c;
        p->index = i;
        p->name = esk_strdup(e->a, pin_names[i]);
        p->model = pin_models[i];
        p->model0 = pin_models[i];
        p->drive_value = ESK_LZ;
        p->drive_strength = ESK_STR_HIZ;
        p->pending_value = ESK_LZ;
        p->sampled = ESK_LZ;
        p->analog_node = -1;
        p->is_output = p->model.kind == ESK_PIN_DIGITAL_OUT || p->model.kind == ESK_PIN_POWER;
        if (!p->name) {
            comp_free(e, c);
            return ES_ENOMEM;
        }
    }
    if (esk_vec_push(e->a, &e->comps, c) || esk_map_set(&e->comp_by_id, id, c)) {
        comp_free(e, c);
        return ES_ENOMEM;
    }
    if (ops->create) {
        st = ops->create(c);
        if (st != ES_OK) {
            /* remove again */
            e->comps.len--;
            esk_map_set(&e->comp_by_id, id, NULL);
            comp_free(e, c);
            return st;
        }
    }
    if (out)
        *out = c;
    return ES_OK;
}

es_status esk_engine_connect(esk_engine *e, esk_pin *pin, esk_net *net)
{
    if (!pin || !net)
        return ES_EINVAL;
    if (pin->net)
        return ES_EINVAL; /* catalog.pin_twice is raised by the loader */
    if (net->pins.len >= MAX_PINS_PER_NET)
        return ES_ELIMIT;
    if (esk_vec_push(e->a, &net->pins, pin))
        return ES_ENOMEM;
    pin->net = net;
    return ES_OK;
}

esk_comp *esk_engine_comp(const esk_engine *e, const char *id) { return (esk_comp *)esk_map_get(&e->comp_by_id, id); }
esk_net *esk_engine_net(const esk_engine *e, const char *name) { return (esk_net *)esk_map_get(&e->net_by_name, name); }

esk_pin *esk_comp_pin(esk_comp *c, const char *name)
{
    uint32_t i;
    for (i = 0; i < c->npins; i++)
        if (!strcmp(c->pins[i].name, name))
            return &c->pins[i];
    /* aliases from the description */
    if (c->desc) {
        const esk_json *pins = esk_json_get(c->desc, "pins");
        size_t k;
        for (k = 0; k < esk_json_len(pins); k++) {
            const esk_json *pd = esk_json_at(pins, k);
            const esk_json *al = esk_json_get(pd, "aliases");
            size_t m;
            for (m = 0; m < esk_json_len(al); m++)
                if (esk_str_eq(esk_json_str(esk_json_at(al, m)), name)) {
                    const char *real = esk_json_get_str(pd, "name", NULL);
                    for (i = 0; real && i < c->npins; i++)
                        if (!strcmp(c->pins[i].name, real))
                            return &c->pins[i];
                }
        }
    }
    return NULL;
}

/* ---- diagnostics --------------------------------------------------------- */

void esk_comp_diag(esk_comp *c, es_severity_t sev, const char *code, const char *fmt, ...)
{
    char msg[240];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    esk_diag_raise(c->e->diag, sev, code, c->e->sched.now, "component", c->id, NULL, 0, "%s", msg);
}

es_time_t esk_comp_now(const esk_comp *c) { return c->e->sched.now; }

/* ---- digital resolution (SPEC-003 §3) ------------------------------------ */

static void pin_contribution(const esk_pin *p, esk_logic *v, esk_strength *s)
{
    const esk_pin_model *m = &p->model;
    *v = ESK_LZ;
    *s = ESK_STR_HIZ;
    switch (m->kind) {
    case ESK_PIN_POWER:
        *v = m->power_ground ? ESK_L0 : ESK_L1;
        *s = ESK_STR_SUPPLY;
        return;
    case ESK_PIN_PASSIVE:
    case ESK_PIN_ANALOG:
    case ESK_PIN_DIGITAL_OUT:
    case ESK_PIN_DIGITAL_IO:
        if (m->kind == ESK_PIN_DIGITAL_IO && !p->is_output)
            break;
        if (p->drive_value == ESK_LZ)
            break;
        if (m->drive == ESK_DRIVE_OPEN_DRAIN && p->drive_value == ESK_L1)
            break; /* open drain never drives 1 */
        if (m->drive == ESK_DRIVE_OPEN_SOURCE && p->drive_value == ESK_L0)
            break;
        *v = p->drive_value;
        *s = p->drive_strength;
        return;
    default:
        break;
    }
    /* an input's pull is a weak driver */
    if ((m->kind == ESK_PIN_DIGITAL_IN || (m->kind == ESK_PIN_DIGITAL_IO && !p->is_output)) && m->pull != ESK_PULL_NONE) {
        *v = m->pull == ESK_PULL_UP ? ESK_L1 : ESK_L0;
        *s = ESK_STR_WEAK;
    }
}

static int contention_drivers(const esk_net *n, const esk_pin **low, const esk_pin **high)
{
    size_t i;
    esk_strength strongest = ESK_STR_HIZ;
    *low = *high = NULL;
    for (i = 0; i < n->pins.len; i++) {
        const esk_pin *p = n->pins.data[i];
        esk_logic v;
        esk_strength s;
        /* Power pins on analog islands may be supply inputs with disabled
         * bridges. Restrict analog intent conflicts to active digital outputs. */
        if (n->island && p->model.kind != ESK_PIN_DIGITAL_OUT &&
            !(p->model.kind == ESK_PIN_DIGITAL_IO && p->is_output))
            continue;
        pin_contribution(p, &v, &s);
        if (s < ESK_STR_STRONG)
            continue;
        if (!n->island && s > strongest) {
            strongest = s;
            *low = *high = NULL;
        }
        if (!n->island && s < strongest)
            continue;
        if (v == ESK_L0 && !*low) *low = p;
        if (v == ESK_L1 && !*high) *high = p;
    }
    return *low && *high;
}

int esk_net_has_contention(const esk_net *n)
{
    const esk_pin *low, *high;
    return contention_drivers(n, &low, &high);
}

static void resolve_net(esk_engine *e, esk_net *n, esk_logic *out_v, esk_strength *out_s, const esk_pin **a, const esk_pin **b)
{
    esk_logic v = ESK_LZ;
    esk_strength s = ESK_STR_HIZ;
    size_t i;
    *a = *b = NULL;
    for (i = 0; i < n->pins.len; i++) {
        const esk_pin *p = n->pins.data[i];
        esk_logic pv;
        esk_strength ps;
        pin_contribution(p, &pv, &ps);
        if (pv == ESK_LZ)
            continue;
        if (ps > s) {
            s = ps;
            v = pv;
            *a = p;
            *b = NULL;
        } else if (ps == s) {
            if (pv != v) {
                if (v != ESK_LX)
                    *b = p;
                v = ESK_LX;
            }
        }
    }
    ES_UNUSED(e);
    *out_v = v;
    *out_s = s;
}

void esk_net_resolve_excluding(const esk_net *n, const esk_comp *excluded, esk_logic *out_v, esk_strength *out_s)
{
    esk_logic v = ESK_LZ;
    esk_strength s = ESK_STR_HIZ;
    size_t i;
    for (i = 0; i < n->pins.len; i++) {
        const esk_pin *p = n->pins.data[i];
        esk_logic pv;
        esk_strength ps;
        if (p->comp == excluded)
            continue;
        pin_contribution(p, &pv, &ps);
        if (pv == ESK_LZ)
            continue;
        if (ps > s) {
            s = ps;
            v = pv;
        } else if (ps == s && pv != v) {
            v = ESK_LX;
        }
    }
    *out_v = v;
    *out_s = s;
}

static void notify_net(esk_engine *e, esk_net *n)
{
    size_t i;
    for (i = 0; i < n->pins.len; i++) {
        esk_pin *p = n->pins.data[i];
        if (p->watched && p->comp->ops->on_input) {
            p->comp->heard++;
            p->comp->ops->on_input(p->comp, p);
        }
    }
    if (e->on_net_change)
        e->on_net_change(e->on_net_change_ctx, n);
}

static void update_net(esk_engine *e, esk_net *n)
{
    esk_logic v;
    esk_strength s;
    const esk_pin *a, *b;
    int contention;
    if (n->island) {
        /* analog net: the digital drivers are bridges; the island is told by the bridge */
        return;
    }
    resolve_net(e, n, &v, &s, &a, &b);
    contention = contention_drivers(n, &a, &b);
    /* two pulls that disagree are a divider, not a fight: X, but no
     * diagnostic (a pad's pull-down meeting a terminal's pull-up) */
    if (contention && !n->contention_reported) {
        n->contention_reported = 1;
        esk_diag_raise(e->diag, ES_SEV_WARNING, "net.contention", e->sched.now, "net", n->name, NULL, 0,
                       "drivers %s.%s and %s.%s disagree at the strongest active drive strength", a->comp->id, a->name, b->comp->id, b->name);
    } else if (!contention) {
        n->contention_reported = 0;
    }
    if (v != n->value || s != n->strength) {
        n->value = v;
        n->strength = s;
        n->changed_at = e->sched.now;
        notify_net(e, n);
    }
}

static void apply_pending(void *ctx, void *arg, es_time_t now)
{
    esk_engine *e = (esk_engine *)ctx;
    esk_pin *p = (esk_pin *)arg;
    ES_UNUSED(now);
    p->pending = 0;
    if (p->drive_value == p->pending_value)
        return;
    p->drive_value = p->pending_value;
    e->signal_events++;
    p->comp->edges++;
    if (p->net) {
        p->net->edges++;
        if (p->bridge && p->bridge->ops->set_logic)
            p->bridge->ops->set_logic(p->bridge, p->drive_value, now);
        else
            update_net(e, p->net);
    }
}

void esk_pin_drive(esk_pin *p, esk_logic value, esk_strength strength, es_time_t delay_ps)
{
    esk_engine *e = p->comp->e;
    es_time_t at;
    if (p->model.kind == ESK_PIN_DIGITAL_IN || p->model.kind == ESK_PIN_POWER || p->model.kind == ESK_PIN_NC)
        return;
    p->drive_strength = strength;
    /* inertial delay: a newer change supersedes a pending one */
    if (p->pending) {
        esk_sched_cancel(&e->sched, p->pending);
        p->pending = 0;
    }
    p->pending_value = value;
    if (esk_time_add(e->sched.now, delay_ps, &at)) {
        esk_comp_diag(p->comp, ES_SEV_ERROR, "sched.time_overflow", "delay overflows the time horizon");
        return;
    }
    if (esk_sched_at(&e->sched, at, ESK_EV_SIGNAL, apply_pending, e, p, &p->pending) != ES_OK)
        esk_comp_diag(p->comp, ES_SEV_FATAL, "sched.queue_full", "cannot schedule a pin change");
}

void esk_pin_release(esk_pin *p) { esk_pin_drive(p, ESK_LZ, ESK_STR_HIZ, 0); }

esk_logic esk_net_value(const esk_net *n) { return n ? n->value : ESK_LZ; }

static esk_logic bridge_in_value(const esk_pin *p);

esk_logic esk_pin_read(esk_pin *p)
{
    esk_engine *e = p->comp->e;
    esk_logic v;
    if (!p->net)
        v = ESK_LZ;
    else if (p->net->island && p->bridge)
        v = bridge_in_value(p);
    else
        v = p->net->value;
    if (v == ESK_LZ) {
        /* an input with a pull reads its pull */
        if (p->model.pull == ESK_PULL_UP) v = ESK_L1;
        else if (p->model.pull == ESK_PULL_DOWN) v = ESK_L0;
        else if (!p->floating_reported && p->net && !e->in_reset) {
            p->floating_reported = 1;
            esk_diag_raise(e->diag, ES_SEV_WARNING, "net.floating_input", e->sched.now, "pin", p->name, NULL, 0,
                           "%s.%s sampled net %s with no driver and no pull", p->comp->id, p->name, p->net->name);
        }
    } else {
        p->floating_reported = 0;
    }
    if (v == ESK_LX) {
        if (!p->unknown_reported && p->net && !e->in_reset) {
            p->unknown_reported = 1;
            esk_diag_raise(e->diag, ES_SEV_WARNING, "net.unknown_input", e->sched.now, "pin", p->name, NULL, 0,
                           "%s.%s sampled X on net %s", p->comp->id, p->name, p->net->name);
        }
    } else {
        p->unknown_reported = 0;
    }
    p->sampled = v;
    return v;
}

void esk_pin_set_direction(esk_pin *p, int output)
{
    if (p->model.kind != ESK_PIN_DIGITAL_IO || p->is_output == (output != 0))
        return;
    p->is_output = output != 0;
    if (p->net) {
        if (p->bridge && p->bridge->ops->set_logic)
            p->bridge->ops->set_logic(p->bridge, output ? p->drive_value : ESK_LZ, p->comp->e->sched.now);
        else
            update_net(p->comp->e, p->net);
    }
}

void esk_pin_output(esk_pin *p, esk_logic value, esk_strength strength)
{
    if (p->model.kind != ESK_PIN_DIGITAL_IO || p->is_output) {
        esk_pin_drive(p, value, strength, 0);
        return;
    }
    if (p->pending) {
        esk_sched_cancel(&p->comp->e->sched, p->pending);
        p->pending = 0;
    }
    p->drive_strength = strength;
    if (p->drive_value != value) {
        p->comp->e->signal_events++;
        p->comp->edges++;
        if (p->net)
            p->net->edges++;
    }
    p->drive_value = p->pending_value = value;
    esk_pin_set_direction(p, 1);
}

void esk_pin_watch(esk_pin *p, int watch) { p->watched = watch != 0; }

/* ---- wake-ups ------------------------------------------------------------ */

static void wake_fn(void *ctx, void *arg, es_time_t now)
{
    esk_comp *c = (esk_comp *)ctx;
    uint32_t token = (uint32_t)(uintptr_t)arg;
    ES_UNUSED(now);
    if (token < ES_COUNTOF(c->wake))
        c->wake[token] = 0;
    c->wakes++;
    if (c->ops->on_time)
        c->ops->on_time(c, token);
}

es_status esk_comp_wake_at(esk_comp *c, es_time_t t, uint32_t token)
{
    es_status st;
    esk_event_handle h = 0;
    if (token < ES_COUNTOF(c->wake) && c->wake[token]) {
        esk_sched_cancel(&c->e->sched, c->wake[token]);
        c->wake[token] = 0;
    }
    st = esk_sched_at(&c->e->sched, t, ESK_EV_PERIPHERAL, wake_fn, c, (void *)(uintptr_t)token, &h);
    if (st == ES_OK && token < ES_COUNTOF(c->wake))
        c->wake[token] = h;
    return st;
}

void esk_comp_set_supply(esk_comp *c, double amps, es_time_t at)
{
    uint32_t k, j;
    if (!(amps >= 0.0))
        amps = 0.0;
    if (amps == c->supply_a)
        return;
    /* each island its power pins are in integrates up to `at` with the
     * current as it was, once */
    for (k = 0; k < c->npins; k++) {
        esk_island *isl = c->pins[k].net ? c->pins[k].net->island : NULL;
        int seen = 0;
        if (!isl || c->pins[k].model.kind != ESK_PIN_POWER || !c->pins[k].bridge)
            continue;
        for (j = 0; j < k && !seen; j++)
            seen = c->pins[j].model.kind == ESK_PIN_POWER && c->pins[j].bridge && c->pins[j].net &&
                   c->pins[j].net->island == isl;
        if (!seen)
            esk_island_touch(isl, at);
    }
    c->supply_a = amps;
}

void esk_comp_wake_cancel(esk_comp *c, uint32_t token)
{
    if (token < ES_COUNTOF(c->wake) && c->wake[token]) {
        esk_sched_cancel(&c->e->sched, c->wake[token]);
        c->wake[token] = 0;
    }
}

/* ---- parameters and inputs ----------------------------------------------- */

int esk_comp_param_double(const esk_comp *c, const char *name, double *out)
{
    const esk_json *j = esk_json_get(c->params, name);
    return j ? esk_json_to_double(j, out) : -1;
}

int esk_comp_param_int(const esk_comp *c, const char *name, int64_t *out)
{
    const esk_json *j = esk_json_get(c->params, name);
    double d;
    if (!j)
        return -1;
    if (j->type == ESK_JSON_NUMBER && j->u.number.is_integer) {
        *out = j->u.number.integer;
        return 0;
    }
    if (esk_json_to_double(j, &d))
        return -1;
    *out = (int64_t)d;
    return 0;
}

int esk_comp_param_bool(const esk_comp *c, const char *name, int *out)
{
    const esk_json *j = esk_json_get(c->params, name);
    if (!j || j->type != ESK_JSON_BOOL)
        return -1;
    *out = j->u.boolean;
    return 0;
}

const char *esk_comp_param_str(const esk_comp *c, const char *name, const char *def)
{
    return esk_json_get_str(c->params, name, def);
}

int esk_comp_input_bool(const esk_comp *c, const char *name, int def) { return esk_json_get_bool(c->inputs, name, def); }

double esk_comp_input_double(const esk_comp *c, const char *name, double def)
{
    const esk_json *j = esk_json_get(c->inputs, name);
    double v;
    if (j && esk_json_to_double(j, &v) == 0)
        return v;
    if (!j && esk_comp_world_input(c, name, &v))
        return v;
    return def;
}

/* the field an input of the description is bound to, -1 if none */
static int input_world(const esk_comp *c, const char *name, const esk_json **desc)
{
    const esk_json *inputs = esk_json_get(c->desc, "inputs");
    size_t i;
    for (i = 0; i < esk_json_len(inputs); i++) {
        const esk_json *in = esk_json_at(inputs, i);
        if (!esk_str_eq(esk_json_get_str(in, "name", NULL), name))
            continue;
        if (desc)
            *desc = in;
        return esk_world_field_index(esk_json_get_str(in, "world", NULL));
    }
    return -1;
}

/* the beam of a distance sensor on the bench: from its bench.sense.at along
 * bench.sense.dir (part coordinates, mm: x right, y towards the viewer, z up),
 * turned with the part */
static void beam(const esk_comp *c, double o[3], double d[3])
{
    const esk_json *sense = esk_json_get(esk_json_get(c->desc, "bench"), "sense");
    const esk_json *at = esk_json_get(sense, "at"), *dir = esk_json_get(sense, "dir");
    double la[3] = { 0, 0, 10 }, ld[3] = { 0, -1, 0 }, rot = c->pose[2] * 3.14159265358979323846 / 180.0, n;
    int k;
    for (k = 0; k < 3; k++) {
        if (esk_json_at(at, (size_t)k))
            (void)esk_json_to_double(esk_json_at(at, (size_t)k), &la[k]);
        if (esk_json_at(dir, (size_t)k))
            (void)esk_json_to_double(esk_json_at(dir, (size_t)k), &ld[k]);
    }
    /* clockwise seen from above with y towards the viewer: (x, y) -> (x cos - y sin, x sin + y cos) */
    o[0] = c->pose[0] + la[0] * cos(rot) - la[1] * sin(rot);
    o[1] = c->pose[1] + la[0] * sin(rot) + la[1] * cos(rot);
    o[2] = la[2];
    d[0] = ld[0] * cos(rot) - ld[1] * sin(rot);
    d[1] = ld[0] * sin(rot) + ld[1] * cos(rot);
    d[2] = ld[2];
    n = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (n > 0)
        for (k = 0; k < 3; k++)
            d[k] /= n;
}

int esk_comp_world_input(const esk_comp *c, const char *input, double *out)
{
    const esk_json *in = NULL;
    const esk_world *w = &c->e->world;
    int f = input_world(c, input, &in);
    if (f < 0)
        return 0;
    if (f == ESK_WF_DISTANCE) {
        double o[3], d[3], mm;
        const char *unit = esk_json_get_str(in, "unit", "mm");
        if (!w->bodies.len)
            goto fallback;
        beam(c, o, d);
        mm = esk_world_ray(w, o, d, esk_engine_now(c->e));
        if (mm < 0)
            mm = 10000.0; /* nothing in the beam: far beyond any sensor's range */
        *out = esk_str_eq(unit, "cm") ? mm / 10.0 : esk_str_eq(unit, "m") ? mm / 1000.0 : mm;
        return 1;
    }
    if (f == ESK_WF_MOTION) {
        /* a PIR: a warm body crossing the lens's view cones within its range and field of view
         * (params fov, deg, range, m, zone, deg), since the model's last look 100 ms before */
        double o[3], d[3], fov = 110.0, range = 7.0, zone = 7.5;
        if (!w->bodies.len)
            goto fallback;
        (void)esk_comp_param_double(c, "fov", &fov);
        (void)esk_comp_param_double(c, "range", &range);
        (void)esk_comp_param_double(c, "zone", &zone);
        beam(c, o, d);
        *out = esk_world_motion(w, o, d, fov / 2.0, range * 1000.0, zone, 100 * ES_PS_PER_MS, esk_engine_now(c->e)) ? 1.0 : 0.0;
        return 1;
    }
    if (!((w->set >> f) & 1u))
        goto fallback;
    *out = w->value[f];
    return 1;
fallback:
    /* a world that does not have it: the input's own default, as before the world */
    return esk_json_get(in, "default") && esk_json_to_double(esk_json_get(in, "default"), out) == 0;
}

void esk_engine_world_changed(esk_engine *e, int field)
{
    size_t i, k;
    for (i = 0; i < e->comps.len; i++) {
        esk_comp *c = e->comps.data[i];
        const esk_json *inputs = esk_json_get(c->desc, "inputs");
        if (!c->ops->on_set)
            continue;
        for (k = 0; k < esk_json_len(inputs); k++) {
            const esk_json *in = esk_json_at(inputs, k);
            const char *name = esk_json_get_str(in, "name", NULL);
            esk_json v;
            double x;
            if (!name || esk_world_field_index(esk_json_get_str(in, "world", NULL)) != field)
                continue;
            if (esk_json_get(c->inputs, name) || !esk_comp_world_input(c, name, &x))
                continue; /* set by the user, or not given by the world */
            memset(&v, 0, sizeof v);
            v.type = ESK_JSON_NUMBER;
            v.u.number.value = x;
            (void)c->ops->on_set(c, name, &v);
        }
    }
}

es_status esk_engine_set_input(esk_engine *e, esk_comp *c, const char *input, const esk_json *value)
{
    esk_json *copy;
    es_status st;
    if (!c || !input || !value)
        return ES_EINVAL;
    copy = esk_json_clone(e->a, value);
    if (!copy)
        return ES_ENOMEM;
    if (esk_json_object_set(e->a, c->inputs, input, copy)) {
        esk_json_free(e->a, copy);
        return ES_ENOMEM;
    }
    st = c->ops->on_set ? c->ops->on_set(c, input, value) : ES_OK;
    return st;
}

/* ---- islands (SPEC-003 §4) ----------------------------------------------- */

static int comp_is_electrical(const esk_comp *c) { return c->ops->adev != NULL && !c->functional_only; }

static int find_root(int *parent, int i)
{
    while (parent[i] != i) {
        parent[i] = parent[parent[i]];
        i = parent[i];
    }
    return i;
}

static void unite(int *parent, int x, int y)
{
    x = find_root(parent, x);
    y = find_root(parent, y);
    if (x != y)
        parent[y] = x;
}

static esk_logic bridge_in_value(const esk_pin *p)
{
    /* the comparator's state is the pin's sampled value, maintained by the bridge through crossings */
    return p->sampled == ESK_LZ ? ESK_L0 : p->sampled;
}

static void bridge_notify(esk_engine *e, esk_pin *p)
{
    if (p->watched && p->comp->ops->on_input) {
        p->comp->heard++;
        p->comp->ops->on_input(p->comp, p);
    }
    if (e->on_net_change && p->net)
        e->on_net_change(e->on_net_change_ctx, p->net);
}

static void bridge_crossed_cb(void *ctx, void *arg, es_time_t now)
{
    esk_pin *p = (esk_pin *)ctx;
    ES_UNUSED(arg);
    ES_UNUSED(now);
    bridge_notify(p->comp->e, p);
}

void esk_engine_bridge_crossed(esk_engine *e, esk_pin *p, esk_logic value, es_time_t at)
{
    esk_event_handle h = 0;
    if (p->sampled == value)
        return;
    /* the comparator's state now, so the island does not find the crossing
     * again; its model hears of it at the crossing's time, which a step
     * bisecting ahead of the scheduler reaches later (a SIGNAL event runs
     * before the island's next step at the same time) */
    p->sampled = value;
    if (at > e->sched.now && esk_sched_at(&e->sched, at, ESK_EV_SIGNAL, bridge_crossed_cb, p, NULL, &h) == ES_OK)
        return;
    bridge_notify(e, p);
}

void esk_engine_watch_crossed(esk_engine *e, esk_pin *p, esk_logic value, es_time_t at)
{
    esk_event_handle h = 0;
    if (!p->watch_on || p->watch_state == value)
        return;
    p->watch_state = value;
    if (at > e->sched.now && esk_sched_at(&e->sched, at, ESK_EV_SIGNAL, bridge_crossed_cb, p, NULL, &h) == ES_OK)
        return;
    bridge_notify(e, p);
}

int esk_pin_compare(esk_pin *p, double v)
{
    if (!p->net || !p->net->island || !p->bridge) {
        p->watch_on = 0;
        return 0;
    }
    p->watch_on = 1;
    p->watch_v = v;
    p->watch_state = p->net->voltage > v ? ESK_L1 : ESK_L0;
    return 1;
}

void esk_pin_compare_off(esk_pin *p) { p->watch_on = 0; }

void esk_pin_bridge_target(const esk_pin *p, double *v, double *r, int *open)
{
    const esk_pin_model *m = &p->model;
    esk_logic dv = (m->kind == ESK_PIN_DIGITAL_IO && !p->is_output) ? ESK_LZ : p->drive_value;
    *r = m->rout > 0 ? m->rout : 25.0;
    *open = 0;
    if (m->kind == ESK_PIN_POWER) {
        *v = m->nominal_v;
        *r = 0.0;
        return;
    }
    if (dv == ESK_L1 && m->drive != ESK_DRIVE_OPEN_DRAIN) {
        *v = (m->voc_set & 1) ? m->voc_high : m->rails && m->nominal_v > 0.0 ? m->nominal_v : m->voh;
    } else if (dv == ESK_L0 && m->drive != ESK_DRIVE_OPEN_SOURCE) {
        *v = (m->voc_set & 2) ? m->voc_low : m->rails ? 0.0 : m->vol;
    } else {
        *open = 1;
        *v = 0.0;
        return;
    }
    /* a weak drive (an MCU's pull, a bus keeper) is the pull resistance */
    if (p->drive_strength == ESK_STR_WEAK)
        *r = m->rpull > 0 ? m->rpull : 40000.0;
}

esk_logic esk_pin_level(const esk_pin *p)
{
    if (!p->net)
        return ESK_LZ;
    if (p->net->island && p->bridge)
        return p->sampled == ESK_LZ ? ESK_L0 : p->sampled;
    return p->net->value;
}

void esk_pin_refresh(esk_pin *p)
{
    esk_engine *e = p->comp->e;
    if (!p->net)
        return;
    if (p->bridge && p->net->island) {
        if ((!p->is_output || p->drive_value == ESK_LZ) && p->model.kind == ESK_PIN_DIGITAL_IO) {
            /* back to an input: the comparator starts from where the node sits */
            double vn = p->net->voltage, mid = (p->model.vih + p->model.vil) / 2.0;
            p->sampled = vn > mid ? ESK_L1 : ESK_L0;
        }
        if (p->bridge->ops->set_logic)
            p->bridge->ops->set_logic(p->bridge, p->is_output ? p->drive_value : ESK_LZ, e->sched.now);
    } else {
        update_net(e, p->net);
    }
}

static void island_step_cb(void *ctx, void *arg, es_time_t now);

static es_status schedule_island(esk_engine *e, esk_island *isl, es_time_t at)
{
    if (isl->step_event) {
        esk_sched_cancel(&e->sched, isl->step_event);
        isl->step_event = 0;
    }
    return esk_sched_at(&e->sched, at, ESK_EV_ANALOG, island_step_cb, e, isl, &isl->step_event);
}

static void island_step_cb(void *ctx, void *arg, es_time_t now)
{
    esk_engine *e = (esk_engine *)ctx;
    esk_island *isl = (esk_island *)arg;
    /* clip to the next event that can change this island's sources: other islands' step
     * events cannot (crossings arrive as SIGNAL events), so they are ignored */
    es_time_t next = esk_sched_next_time_excluding(&e->sched, ESK_EV_ANALOG), until, reached = now;
    es_status st;
    isl->step_event = 0;
    if (isl->failed)
        return;
    until = now;
    if (esk_time_add(now, isl->h_next_ps ? isl->h_next_ps : isl->p.h_max_ps, &until))
        until = ES_TIME_MAX - 1;
    if (next < until)
        until = next;
    if (until <= now)
        until = now + 1;
    st = esk_island_step(isl, until, &reached);
    if (st != ES_OK) {
        isl->failed = 1;
        return;
    }
    if (reached < now)
        reached = now;
    {
        /* a steady island steps again only when something touches it, or
         * at its sources' next breakpoint */
        es_time_t bp;
        if (esk_island_quiet(isl, &bp)) {
            if (bp != ES_TIME_MAX)
                schedule_island(e, isl, bp > reached ? bp : reached);
            return;
        }
    }
    schedule_island(e, isl, reached);
}

int esk_engine_analog_quiet(esk_engine *e)
{
    size_t i;
    for (i = 0; i < e->islands.len; i++) {
        es_time_t bp;
        if (!esk_island_quiet(e->islands.data[i], &bp) || bp <= e->sched.now)
            return 0;
    }
    return 1;
}

static void island_wake(esk_island *isl, es_time_t at)
{
    esk_engine *e = isl->e;
    if (at < e->sched.now)
        at = e->sched.now;
    schedule_island(e, isl, at);
}

es_status esk_engine_partition(esk_engine *e)
{
    size_t i, k;
    int *parent = NULL;
    size_t nn = e->nets.len;
    es_status st = ES_OK;
    islands_free(e);
    if (e->policy == ESK_POLICY_FUNCTIONAL) {
        /* everything digital; components without a functional model are
         * refused. Under `timed` the electrical devices still form analog
         * islands (SPEC-003 §4); the loader binds functional models first. */
        for (i = 0; i < e->comps.len; i++) {
            esk_comp *c = e->comps.data[i];
            if (esk_model_is_electrical_only(c->ops)) {
                esk_comp_diag(c, ES_SEV_ERROR, "island.requires_electrical", "%s has no functional model", c->kind);
                st = ES_EUNSUPPORTED;
            }
        }
        e->partitioned = 1;
        return st;
    }
    parent = (int *)esk_malloc(e->a, (nn ? nn : 1) * sizeof *parent);
    if (!parent)
        return ES_ENOMEM;
    for (i = 0; i < nn; i++)
        parent[i] = (int)i;
    /* nets of an electrical component's analog/passive pins join one island */
    for (i = 0; i < e->comps.len; i++) {
        esk_comp *c = e->comps.data[i];
        int first = -1;
        if (!comp_is_electrical(c))
            continue;
        for (k = 0; k < c->npins; k++) {
            esk_pin *p = &c->pins[k];
            if (!p->net || p->model.kind == ESK_PIN_NC)
                continue;
            if (first < 0)
                first = (int)p->net->index;
            else
                unite(parent, first, (int)p->net->index);
        }
    }
    /* mark analog nets: any net touched by an electrical component or an analog pin */
    {
        unsigned char *analog = (unsigned char *)esk_calloc(e->a, nn ? nn : 1, 1);
        if (!analog) {
            esk_free(e->a, parent, (nn ? nn : 1) * sizeof *parent);
            return ES_ENOMEM;
        }
        for (i = 0; i < e->comps.len; i++) {
            esk_comp *c = e->comps.data[i];
            for (k = 0; k < c->npins; k++) {
                esk_pin *p = &c->pins[k];
                if (p->net && (comp_is_electrical(c) || p->model.kind == ESK_PIN_ANALOG) && p->model.kind != ESK_PIN_NC)
                    analog[find_root(parent, (int)p->net->index)] = 1;
            }
        }
        /* create islands per root */
        for (i = 0; i < nn; i++) {
            int root = find_root(parent, (int)i);
            esk_net *n = e->nets.data[i];
            esk_island *isl;
            if (!analog[root])
                continue;
            isl = e->nets.data[root]->island;
            if (!isl) {
                isl = esk_island_create(e, (uint32_t)e->islands.len);
                if (!isl || esk_vec_push(e->a, &e->islands, isl)) {
                    if (isl) esk_island_destroy(isl);
                    st = ES_ENOMEM;
                    break;
                }
                isl->wake = island_wake;
                e->nets.data[root]->island = isl;
                /* add the root net first so a ground root becomes node 0 */
            }
            n->island = isl;
        }
        if (st == ES_OK) {
            /* decide ground nets: a power pin with role ground, or a net named GND/VSS */
            for (i = 0; i < nn; i++) {
                esk_net *n = e->nets.data[i];
                n->is_ground = n->is_supply = 0;
                for (k = 0; k < n->pins.len; k++) {
                    const esk_pin *p = n->pins.data[k];
                    if (p->model.kind == ESK_PIN_POWER) {
                        if (p->model.power_ground) n->is_ground = 1;
                        else { n->is_supply = 1; n->supply_v = p->model.nominal_v; }
                    }
                }
            }
            /* add nets to islands: grounds first */
            for (i = 0; i < nn && st == ES_OK; i++) {
                esk_net *n = e->nets.data[i];
                if (n->island && n->is_ground) {
                    int node = esk_island_add_net(n->island, n, 1);
                    if (node < 0) st = ES_ENOMEM;
                    n->node = node;
                }
            }
            for (i = 0; i < nn && st == ES_OK; i++) {
                esk_net *n = e->nets.data[i];
                if (n->island && !n->is_ground) {
                    int node = esk_island_add_net(n->island, n, 0);
                    if (node < 0) st = ES_ENOMEM;
                    n->node = node;
                }
            }
        }
        esk_free(e->a, analog, nn ? nn : 1);
    }
    esk_free(e->a, parent, (nn ? nn : 1) * sizeof *parent);
    if (st != ES_OK)
        return st;
    /* devices and bridges */
    for (i = 0; i < e->comps.len && st == ES_OK; i++) {
        esk_comp *c = e->comps.data[i];
        esk_pin *pins[8];
        int np = 0;
        esk_island *isl = NULL;
        for (k = 0; k < c->npins; k++) {
            esk_pin *p = &c->pins[k];
            if (p->net && p->net->island) {
                if (comp_is_electrical(c)) {
                    if (np < 8) pins[np++] = p;
                    isl = p->net->island;
                    p->analog_node = p->net->node;
                } else if (p->model.kind == ESK_PIN_DIGITAL_OUT || p->model.kind == ESK_PIN_DIGITAL_IN ||
                           p->model.kind == ESK_PIN_DIGITAL_IO || p->model.kind == ESK_PIN_POWER) {
                    st = esk_island_add_bridge(p->net->island, p, &p->bridge);
                    p->analog_node = p->net->node;
                }
            }
        }
        if (st == ES_OK && isl && comp_is_electrical(c)) {
            /* by name when every pin the primitive names is here and connected:
             * a description may list them in any order */
            int nnames, all = 1, named = 0;
            const char *const *names = esk_adev_pin_names(c->ops->adev, &nnames);
            esk_pin *byname[8];
            for (k = 0; names && k < (size_t)nnames; k++) {
                esk_pin *p = esk_comp_pin(c, names[k]);
                all = all && p && p->net && p->net->island == isl;
                byname[k] = p;
                if (p) named++;
            }
            /* Indicators, instrument inputs and voltage sources retain their
             * terminal identities with open leads. Their missing nodes float
             * in mna.c; no detached return becomes an implicit ground. */
            int preserve_open = esk_adev_preserves_open_pins(c->ops->adev);
            if (preserve_open && !named) {
                /* Low-level embedders may use positional p0/p1 names. Keep
                 * their slots too, including a missing trailing terminal. */
                for (k = 0; k < (size_t)nnames; k++)
                    byname[k] = k < c->npins ? &c->pins[k] : NULL;
            }
            if (names && ((all && nnames >= np) || preserve_open))
                st = esk_island_add_device(isl, c, c->ops->adev, byname, nnames, &c->adev);
            else
                st = esk_island_add_device(isl, c, c->ops->adev, pins, np, &c->adev);
        }
    }
    e->partitioned = 1;
    return st;
}

/* ---- reset and advance --------------------------------------------------- */

es_status esk_engine_reset(esk_engine *e)
{
    size_t i;
    uint32_t k;
    es_status st = ES_OK;
    esk_sched_reset(&e->sched);
    esk_air_reset(&e->air);
    esk_inet_reset(&e->inet);
    e->signal_events = 0;
    for (i = 0; i < e->nets.len; i++) {
        esk_net *n = e->nets.data[i];
        n->value = ESK_LZ;
        n->strength = ESK_STR_HIZ;
        n->changed_at = 0;
        n->contention_reported = 0;
        n->voltage = 0.0;
    }
    for (i = 0; i < e->comps.len; i++) {
        esk_comp *c = e->comps.data[i];
        for (k = 0; k < c->npins; k++) {
            esk_pin *p = &c->pins[k];
            p->model = p->model0; /* a run's configuration ends with it */
            p->drive_value = ESK_LZ;
            p->drive_strength = ESK_STR_HIZ;
            p->pending = 0;
            p->pending_value = ESK_LZ;
            p->sampled = ESK_LZ;
            p->floating_reported = p->unknown_reported = 0;
            p->is_output = p->model.kind == ESK_PIN_DIGITAL_OUT || p->model.kind == ESK_PIN_POWER;
        }
        for (k = 0; k < ES_COUNTOF(c->wake); k++)
            c->wake[k] = 0;
        c->supply_a = 0.0;
    }
    if (!e->partitioned) {
        st = esk_engine_partition(e);
        if (st != ES_OK)
            return st;
    }
    /* static contributions (power pins, pulls) resolve before any model looks */
    e->in_reset = 1;
    for (i = 0; i < e->nets.len; i++)
        if (!e->nets.data[i]->island)
            update_net(e, e->nets.data[i]);
    for (i = 0; i < e->comps.len; i++) {
        esk_comp *c = e->comps.data[i];
        if (c->ops->reset)
            c->ops->reset(c);
    }
    /* apply pending drives at time 0 and resolve every digital net */
    st = esk_sched_advance(&e->sched, 0);
    e->in_reset = 0;
    for (i = 0; i < e->nets.len; i++)
        if (!e->nets.data[i]->island)
            update_net(e, e->nets.data[i]);
    for (i = 0; i < e->islands.len; i++) {
        esk_island *isl = e->islands.data[i];
        es_status s2 = esk_island_reset(isl);
        if (s2 != ES_OK && st == ES_OK)
            st = s2;
        if (s2 == ES_OK)
            schedule_island(e, isl, 0);
    }
    return st;
}

static void sync_cb(void *ctx, void *arg, es_time_t now)
{
    ES_UNUSED(ctx); ES_UNUSED(arg); ES_UNUSED(now);
}

es_status esk_engine_advance(esk_engine *e, es_time_t until)
{
    es_status st;
    if (until < e->sched.now)
        return ES_ETIME;
    /* a probe-class marker at `until` makes islands stop exactly there */
    if (e->islands.len)
        esk_sched_at(&e->sched, until, ESK_EV_PROBE, sync_cb, e, NULL, NULL);
    st = esk_sched_advance(&e->sched, until);
    if (st == ES_ELIMIT && e->sched.delta_overflow) {
        esk_diag_raise(e->diag, ES_SEV_FATAL, "sched.delta_overflow", e->sched.now, NULL, NULL, NULL, 0,
                       "zero-delay propagation exceeded %u delta cycles", (unsigned)e->sched.max_delta_cycles);
    }
    if (e->diag->fatal)
        return ES_EUNSUPPORTED;
    return st;
}

es_time_t esk_engine_now(const esk_engine *e) { return e->sched.now; }
