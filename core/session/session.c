/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* session.c -- the session (SPEC-001 §6, SPEC-002 §8, SPEC-008): lifecycle,
 * the command path that stamps inputs with virtual time, probes, snapshots,
 * pacing, notifications, and the public C API. */
#include "session/session_priv.h"
#include "mcu/mcu.h"
#include <stdlib.h>
#include <string.h>
#include "analog/analog.h"
#include "host/host.h"
#include "project/loader.h"
#include "util/num.h"
#include "util/str.h"

#ifndef EMBSIM_VERSION_STRING
#define EMBSIM_VERSION_STRING "0.0.0"
#endif
#ifndef EMBSIM_VERSION_MAJOR
#define EMBSIM_VERSION_MAJOR 0
#define EMBSIM_VERSION_MINOR 0
#define EMBSIM_VERSION_PATCH 0
#endif
#define ES_ABI_VERSION 1
#define PROBE_DEFAULT_CAP 65536

/* ---- version and errors ---------------------------------------------------- */

es_version_t es_version(void)
{
    es_version_t v;
    v.major = EMBSIM_VERSION_MAJOR;
    v.minor = EMBSIM_VERSION_MINOR;
    v.patch = EMBSIM_VERSION_PATCH;
    v.string = EMBSIM_VERSION_STRING;
    return v;
}

int es_abi_version(void) { return ES_ABI_VERSION; }

const char *es_strerror(es_status st)
{
    switch (st) {
    case ES_OK: return "ok";
    case ES_EINVAL: return "invalid argument or identifier";
    case ES_ENOMEM: return "out of memory";
    case ES_ELIMIT: return "a configured capacity was exceeded";
    case ES_ETIME: return "time cannot move backwards or past the horizon";
    case ES_ENOTFOUND: return "not found";
    case ES_EPARSE: return "parse error";
    case ES_ESCHEMA: return "description failed validation";
    case ES_EUNSUPPORTED: return "unsupported under the session policy";
    case ES_ECONVERGE: return "the analog engine failed to converge";
    case ES_ESTATE: return "not valid in the current state";
    case ES_EIO: return "host I/O failed";
    case ES_EPROTO: return "protocol error";
    case ES_EINTERNAL: return "internal error";
    default: return "unknown error";
    }
}

void es_free(void *p) { free(p); }

/* ---- notifications ------------------------------------------------------------ */

void esk_session_notify(es_session *s, const char *method, esk_json *params)
{
    if (s->notify && params) {
        size_t len;
        char *text = esk_json_format(s->a, params, 0, &len);
        if (text) {
            s->notify(s->notify_ctx, method, text, len);
            esk_free(s->a, text, len + 1);
        }
    }
    esk_json_free(s->a, params);
}

static void diag_forward(void *ctx, const es_diagnostic_t *d)
{
    es_session *s = (es_session *)ctx;
    esk_json *o;
    if (!s->notify)
        return;
    o = esk_json_new_object(s->a);
    if (!o)
        return;
    esk_json_set_string(s->a, o, "severity", d->severity == ES_SEV_INFO ? "info" : d->severity == ES_SEV_WARNING ? "warning" : d->severity == ES_SEV_ERROR ? "error" : "fatal");
    esk_json_set_string(s->a, o, "code", d->code);
    esk_json_set_string(s->a, o, "message", d->message);
    esk_json_set_u64_string(s->a, o, "time_ps", d->time_ps);
    if (d->subject_kind) {
        esk_json *sub = esk_json_new_object(s->a);
        if (sub) {
            esk_json_set_string(s->a, sub, "kind", d->subject_kind);
            esk_json_set_string(s->a, sub, "name", d->subject ? d->subject : "");
            esk_json_object_set(s->a, o, "subject", sub);
        }
    }
    {
        esk_json *params = esk_json_new_object(s->a);
        esk_json *arr = esk_json_new_array(s->a);
        if (params && arr) {
            esk_json_array_push(s->a, arr, o);
            esk_json_object_set(s->a, params, "records", arr);
            esk_session_notify(s, "diagnostics", params);
        } else {
            esk_json_free(s->a, o);
            esk_json_free(s->a, params);
            esk_json_free(s->a, arr);
        }
    }
}

/* ---- lifecycle ------------------------------------------------------------------ */

static esk_policy policy_from(const char *p, int *ok)
{
    *ok = 1;
    if (!p || !strcmp(p, "electrical")) return ESK_POLICY_ELECTRICAL;
    if (!strcmp(p, "functional")) return ESK_POLICY_FUNCTIONAL;
    if (!strcmp(p, "timed")) return ESK_POLICY_TIMED;
    *ok = 0;
    return ESK_POLICY_ELECTRICAL;
}

es_status es_session_create(const es_session_config_t *cfg, es_session **out)
{
    es_session *s;
    es_status st;
    int ok;
    esk_alloc tmp;
    if (!out)
        return ES_EINVAL;
    esk_alloc_init(&tmp, cfg ? cfg->allocator : NULL);
    s = (es_session *)esk_calloc(&tmp, 1, sizeof *s);
    if (!s)
        return ES_ENOMEM;
    s->alloc = tmp;
    s->a = &s->alloc;
    st = esk_diag_init(&s->diag, s->a, 1024);
    if (st != ES_OK) {
        esk_free(&tmp, s, sizeof *s);
        return st;
    }
    s->diag.fn = diag_forward;
    s->diag.ctx = s;
    st = esk_engine_init(&s->eng, s->a, &s->diag, policy_from(cfg ? cfg->policy : NULL, &ok));
    if (st == ES_OK && !ok)
        st = ES_EINVAL;
    if (st == ES_OK)
        st = esk_registry_init(&s->reg, s->a);
    if (st == ES_OK) {
        if (cfg && cfg->catalog) {
            s->catalog = cfg->catalog;
        } else {
            st = es_catalog_create(&s->catalog);
            s->owns_catalog = 1;
            if (st == ES_OK) {
                s->catalog_dir = esk_catalog_bundled_dir(s->a, NULL);
                if (s->catalog_dir)
                    esk_catalog_load_library(s->catalog, s->catalog_dir, &s->diag);
                /* then the user's installed libraries (SPEC-014 §1) */
                {
                    char *user = esk_catalog_user_dir(s->a);
                    esk_catalog_load_user_libraries(s->catalog, user, &s->diag);
                    esk_strfree(s->a, user);
                }
            }
        }
    }
    if (st != ES_OK) {
        es_session_destroy(s);
        return st;
    }
    s->policy_forced = cfg && cfg->policy;
    if (cfg) {
        if (cfg->max_nets) s->eng.max_nets = cfg->max_nets;
        if (cfg->max_components) s->eng.max_comps = cfg->max_components;
        if (cfg->max_pending_events) s->eng.sched.max = cfg->max_pending_events;
        if (cfg->max_delta_cycles) s->eng.sched.max_delta_cycles = cfg->max_delta_cycles;
        if (cfg->crossing_resolution_ps) s->eng.crossing_resolution_ps = cfg->crossing_resolution_ps;
        if (cfg->core_quantum_ps) s->core_quantum_ps = cfg->core_quantum_ps;
        if (cfg->probe_memory_bytes) s->probe_memory_bytes = cfg->probe_memory_bytes;
    }
    if (!s->core_quantum_ps) s->core_quantum_ps = ES_PS_PER_US;
    if (!s->probe_memory_bytes) s->probe_memory_bytes = (size_t)64 << 20;
    s->run_slice_ps = 10 * ES_PS_PER_MS;
    s->speed = 1.0;
    s->run_until = ES_TIME_MAX;
    s->observe_rate_hz = 30.0;
    s->eng.on_net_change = esk_session_on_net_change;
    s->eng.on_net_change_ctx = s;
    s->eng.on_irq = esk_record_irq;
    s->eng.on_irq_ctx = s;
    s->inputs_log = esk_json_new_array(s->a);
    *out = s;
    return ES_OK;
}

void es_session_destroy(es_session *s)
{
    esk_alloc tmp;
    if (!s)
        return;
    esk_project_unload(s);
    esk_engine_free(&s->eng);
    esk_vec_free(s->a, &s->boards);
    esk_vec_free(s->a, &s->probes);
    esk_registry_free(&s->reg);
    if (s->owns_catalog)
        es_catalog_destroy(s->catalog);
    esk_strfree(s->a, s->catalog_dir);
    esk_strfree(s->a, s->project_path);
    esk_strfree(s->a, s->client_name);
    esk_strfree(s->a, s->record_path);
    esk_json_free(s->a, s->inputs_log);
    esk_json_free(s->a, s->observations);
    esk_json_free(s->a, s->tx_project);
    while (s->n_allow_origin > 0)
        esk_strfree(s->a, s->allow_origin[--s->n_allow_origin]);
    esk_diag_free(&s->diag);
    tmp = s->alloc;
    esk_free(&tmp, s, sizeof *s);
}

es_status es_session_load_json(es_session *s, const char *json, size_t len, const char *base_dir)
{
    esk_json *j;
    int line;
    const char *err;
    es_status st;
    if (!s || !json)
        return ES_EINVAL;
    j = esk_json_parse(s->a, json, len, &line, &err);
    if (!j) {
        esk_diag_raise(&s->diag, ES_SEV_ERROR, "schema.parse", 0, "file", s->project_path ? s->project_path : "project",
                       s->project_path, line, "%s", err);
        return ES_EPARSE;
    }
    st = esk_project_load(s, j, base_dir);
    esk_json_free(s->a, j);
    if (st == ES_OK)
        st = esk_session_do_reset(s);
    return st;
}

es_status es_session_load_file(es_session *s, const char *path)
{
    char *text, *dir;
    size_t len;
    es_status st;
    if (!s || !path)
        return ES_EINVAL;
    st = esk_host_read_file(s->a, path, &text, &len);
    if (st != ES_OK) {
        esk_diag_raise(&s->diag, ES_SEV_ERROR, "io.read", 0, "file", path, path, 0, "cannot read %s", path);
        return st;
    }
    esk_strfree(s->a, s->project_path);
    s->project_path = esk_strdup(s->a, path);
    dir = esk_host_dirname(s->a, path);
    st = es_session_load_json(s, text, len, dir ? dir : ".");
    esk_strfree(s->a, dir);
    esk_free(s->a, text, len + 1);
    return st;
}

/* ---- probes ------------------------------------------------------------------- */

static void probe_push(es_session *s, esk_probe *p, es_time_t t, double v)
{
    size_t idx;
    if (s->recording)
        esk_record_probe(s, p, t, v);
    if (!p->cap)
        return;
    if (p->len == p->cap) {
        p->head = (p->head + 1) % p->cap;
        p->dropped++;
    } else {
        p->len++;
    }
    idx = (p->head + p->len - 1) % p->cap;
    p->ring[idx].time_ps = t;
    p->ring[idx].value = v;
    p->last = v;
    p->has_last = 1;
}

static double probe_value(es_session *s, esk_probe *p)
{
    esk_json *out;
    double v = 0.0;
    switch (p->kind) {
    case ESK_PROBE_DIGITAL:
        if (p->net && p->net->island) {
            /* threshold with the default family's levels and hysteresis */
            double volts = p->net->voltage;
            if (!p->has_last)
                return volts >= 1.4 ? 1.0 : 0.0;
            if (p->last >= 1.0)
                return volts < 0.8 ? 0.0 : 1.0;
            return volts > 2.0 ? 1.0 : 0.0;
        }
        return p->net ? (double)esk_net_value(p->net) : 2.0;
    case ESK_PROBE_ANALOG:
        if (p->net)
            return p->net->island ? p->net->voltage : (esk_net_value(p->net) == ESK_L1 ? s->eng.default_supply_v : 0.0);
        /* a device quantity: same as a value probe */
        /* fall through */
    case ESK_PROBE_VALUE:
    default:
        if (p->comp && p->comp->ops->observe) {
            out = esk_json_new_object(s->a);
            if (out) {
                const esk_json *q;
                p->comp->ops->observe(p->comp, out);
                q = esk_json_get(out, p->quantity ? p->quantity : "i");
                if (q && q->type == ESK_JSON_NUMBER)
                    v = q->u.number.value;
                else if (q && q->type == ESK_JSON_BOOL)
                    v = q->u.boolean ? 1.0 : 0.0;
                else if (q && q->type == ESK_JSON_STRING && q->u.string.len == 1 && strchr("01ZXzx", q->u.string.data[0]))
                    v = (double)esk_logic_from_name(q->u.string.data); /* a logic level: 0, 1, 2 (Z), 3 (X) */
                esk_json_free(s->a, out);
            }
        }
        return v;
    }
}

static void probe_sample_cb(void *ctx, void *arg, es_time_t now)
{
    es_session *s = (es_session *)ctx;
    esk_probe *p = (esk_probe *)arg;
    p->sample_event = 0;
    {
        double v = probe_value(s, p);
        if (!p->threshold || !p->has_last || v != p->last)
            probe_push(s, p, now, v);
    }
    if (p->period_ps && now <= ES_TIME_MAX - p->period_ps)
        esk_sched_at(&s->eng.sched, now + p->period_ps, ESK_EV_PROBE, probe_sample_cb, s, p, &p->sample_event);
}

static void probe_start(es_session *s, esk_probe *p)
{
    /* a digital probe on a net that became analog thresholds the voltage (SPEC-003 §4) */
    if (p->kind == ESK_PROBE_DIGITAL && p->net && p->net->island && !p->threshold) {
        p->threshold = 1;
        if (!p->period_ps)
            p->period_ps = (es_time_t)(1e12 / (p->rate_hz > 0 ? p->rate_hz : 100000.0));
    }
    p->len = p->head = 0;
    p->has_last = 0;
    p->dropped = 0;
    p->sample_event = 0;
    probe_push(s, p, s->eng.sched.now, probe_value(s, p));
    if (p->period_ps)
        esk_sched_at(&s->eng.sched, s->eng.sched.now + p->period_ps, ESK_EV_PROBE, probe_sample_cb, s, p, &p->sample_event);
}

void esk_session_on_net_change(void *ctx, esk_net *net)
{
    es_session *s = (es_session *)ctx;
    size_t i;
    for (i = 0; i < s->probes.len; i++) {
        esk_probe *p = s->probes.data[i];
        if (p->kind == ESK_PROBE_DIGITAL && p->net == net)
            probe_push(s, p, s->eng.sched.now, (double)esk_net_value(net));
    }
}

es_status esk_session_probe_add(es_session *s, const char *name, const char *target, const char *kind, double rate_hz, esk_probe **out)
{
    esk_probe *p;
    const char *colon = target ? strchr(target, ':') : NULL;
    char head[32], rest[192];
    size_t hl;
    if (!colon)
        return ES_EINVAL;
    hl = (size_t)(colon - target);
    if (hl >= sizeof head || strlen(colon + 1) >= sizeof rest)
        return ES_EINVAL;
    memcpy(head, target, hl);
    head[hl] = 0;
    esk_snprintf(rest, sizeof rest, "%s", colon + 1);
    p = (esk_probe *)esk_calloc(s->a, 1, sizeof *p);
    if (!p)
        return ES_ENOMEM;
    p->id = ++s->next_probe_id;
    p->target = esk_strdup(s->a, target);
    if (name && *name)
        p->name = esk_strdup(s->a, name);
    else {
        char buf[32];
        esk_snprintf(buf, sizeof buf, "p%u", (unsigned)p->id);
        p->name = esk_strdup(s->a, buf);
    }
    if (!strcmp(head, "net")) {
        p->net = esk_engine_net(&s->eng, rest);
        if (!p->net) goto notfound;
        p->kind = (kind && !strcmp(kind, "analog")) ? ESK_PROBE_ANALOG : (!kind && p->net->island) ? ESK_PROBE_ANALOG : ESK_PROBE_DIGITAL;
    } else if (!strcmp(head, "pin")) {
        char inst[128], pin[64];
        esk_comp *c;
        esk_pin *pp;
        if (esk_split_pin_ref(rest, inst, sizeof inst, pin, sizeof pin)) goto notfound;
        c = esk_engine_comp(&s->eng, inst);
        pp = c ? esk_comp_pin(c, pin) : NULL;
        if (!pp || !pp->net) goto notfound;
        p->net = pp->net;
        p->kind = (kind && !strcmp(kind, "analog")) ? ESK_PROBE_ANALOG : ESK_PROBE_DIGITAL;
    } else if (!strcmp(head, "device") || !strcmp(head, "output")) {
        char inst[128], q[64];
        if (esk_split_pin_ref(rest, inst, sizeof inst, q, sizeof q)) goto notfound;
        p->comp = esk_engine_comp(&s->eng, inst);
        if (!p->comp) goto notfound;
        p->quantity = esk_strdup(s->a, q);
        p->kind = ESK_PROBE_VALUE;
    } else if (!strcmp(head, "serial")) {
        size_t i;
        p->comp = esk_engine_comp(&s->eng, rest);
        p->quantity = NULL;
        /* a board's MCU: "B1" its console, "B1.USART2" one of its UARTs */
        for (i = 0; !p->comp && i < s->boards.len; i++) {
            esk_board_inst *bi = s->boards.data[i];
            size_t n = strlen(bi->id);
            if (!bi->mcu || !esk_str_has_prefix(rest, bi->id) || (rest[n] && rest[n] != '.'))
                continue;
            p->comp = bi->mcu;
            if (rest[n] == '.') {
                char q[64];
                esk_snprintf(q, sizeof q, "serial.%s", rest + n + 1);
                p->quantity = esk_strdup(s->a, q);
            }
        }
        if (!p->comp) goto notfound;
        if (!p->quantity)
            p->quantity = esk_strdup(s->a, "rx_text");
        p->kind = ESK_PROBE_SERIAL;
    } else {
        goto notfound;
    }
    p->rate_hz = rate_hz;
    if (p->kind == ESK_PROBE_DIGITAL && p->net && p->net->island)
        p->threshold = 1;
    if (p->kind == ESK_PROBE_ANALOG || p->kind == ESK_PROBE_VALUE || p->threshold) {
        if (rate_hz <= 0)
            rate_hz = p->kind == ESK_PROBE_ANALOG ? 100000.0 : 10000.0;
        p->period_ps = (es_time_t)(1e12 / rate_hz);
        if (!p->period_ps) p->period_ps = 1;
    }
    p->cap = PROBE_DEFAULT_CAP;
    if (p->kind != ESK_PROBE_SERIAL) {
        p->ring = (es_sample_t *)esk_calloc(s->a, p->cap, sizeof *p->ring);
        if (!p->ring) {
            p->cap = 0;
            esk_strfree(s->a, p->name); esk_strfree(s->a, p->target); esk_strfree(s->a, p->quantity);
            esk_free(s->a, p, sizeof *p);
            return ES_ENOMEM;
        }
    } else {
        p->cap = 0;
    }
    if (esk_vec_push(s->a, &s->probes, p)) {
        esk_free(s->a, p->ring, p->cap * sizeof *p->ring);
        esk_strfree(s->a, p->name); esk_strfree(s->a, p->target); esk_strfree(s->a, p->quantity);
        esk_free(s->a, p, sizeof *p);
        return ES_ENOMEM;
    }
    if (s->loaded && p->cap)
        probe_start(s, p);
    if (out)
        *out = p;
    return ES_OK;
notfound:
    esk_strfree(s->a, p->name); esk_strfree(s->a, p->target); esk_strfree(s->a, p->quantity);
    esk_free(s->a, p, sizeof *p);
    return ES_ENOTFOUND;
}

void esk_session_probe_remove(es_session *s, uint32_t id)
{
    size_t i;
    for (i = 0; i < s->probes.len; i++) {
        esk_probe *p = s->probes.data[i];
        if (p->id == id) {
            if (p->sample_event)
                esk_sched_cancel(&s->eng.sched, p->sample_event);
            esk_strfree(s->a, p->name);
            esk_strfree(s->a, p->target);
            esk_strfree(s->a, p->quantity);
            esk_free(s->a, p->ring, p->cap * sizeof *p->ring);
            esk_free(s->a, p, sizeof *p);
            esk_vec_remove(&s->probes, i);
            return;
        }
    }
}

esk_probe *esk_session_probe_find(es_session *s, const char *name_or_id)
{
    size_t i;
    char *end;
    unsigned long id;
    if (!name_or_id)
        return NULL;
    for (i = 0; i < s->probes.len; i++)
        if (esk_str_eq(s->probes.data[i]->name, name_or_id) || esk_str_eq(s->probes.data[i]->target, name_or_id))
            return s->probes.data[i];
    id = strtoul(name_or_id, &end, 10);
    if (*end == 0 && end != name_or_id)
        for (i = 0; i < s->probes.len; i++)
            if (s->probes.data[i]->id == id)
                return s->probes.data[i];
    return NULL;
}

esk_json *esk_session_probe_samples(es_session *s, esk_probe *p, es_time_t from, es_time_t to, size_t max)
{
    esk_json *arr = esk_json_new_array(s->a);
    size_t i, n = 0;
    if (!arr)
        return NULL;
    if (!max)
        max = 100000;
    for (i = 0; i < p->len && n < max; i++) {
        const es_sample_t *smp = &p->ring[(p->head + i) % p->cap];
        esk_json *pair;
        if (smp->time_ps < from || smp->time_ps > to)
            continue;
        pair = esk_json_new_array(s->a);
        if (!pair)
            break;
        {
            char buf[32];
            esk_snprintf(buf, sizeof buf, "%llu", (unsigned long long)smp->time_ps);
            esk_json_array_push(s->a, pair, esk_json_new_string(s->a, buf));
        }
        esk_json_array_push(s->a, pair, esk_json_new_number(s->a, smp->value));
        esk_json_array_push(s->a, arr, pair);
        n++;
    }
    return arr;
}

es_status es_session_probe_read(es_session *s, uint32_t probe, es_sample_t *buf, size_t cap, size_t *n)
{
    size_t i;
    esk_probe *p = NULL;
    if (!s || !buf || !n)
        return ES_EINVAL;
    for (i = 0; i < s->probes.len; i++)
        if (s->probes.data[i]->id == probe)
            p = s->probes.data[i];
    if (!p)
        return ES_ENOTFOUND;
    *n = p->len < cap ? p->len : cap;
    for (i = 0; i < *n; i++)
        buf[i] = p->ring[(p->head + p->len - *n + i) % p->cap];
    return ES_OK;
}

/* ---- inputs (the command path) ----------------------------------------------- */

typedef struct pending_input {
    es_session *s;
    esk_comp *comp;
    char *input;
    esk_json *value;
} pending_input;

static void apply_input_now(es_session *s, esk_comp *c, const char *input, const esk_json *value)
{
    es_status st = esk_engine_set_input(&s->eng, c, input, value);
    if (st != ES_OK)
        esk_diag_raise(&s->diag, ES_SEV_WARNING, "input.rejected", s->eng.sched.now, "component", c->id, NULL, 0,
                       "%s does not accept input %s", c->id, input);
}

static void pending_input_cb(void *ctx, void *arg, es_time_t now)
{
    pending_input *pi = (pending_input *)arg;
    ES_UNUSED(ctx);
    ES_UNUSED(now);
    apply_input_now(pi->s, pi->comp, pi->input, pi->value);
    esk_strfree(pi->s->a, pi->input);
    esk_json_free(pi->s->a, pi->value);
    esk_free(pi->s->a, pi, sizeof *pi);
}

es_status esk_session_apply_input(es_session *s, const char *target, const char *input, const esk_json *value,
                                  es_time_t at, es_time_t *applied_at, uint64_t *seq)
{
    esk_comp *c;
    char unit_input[64];
    esk_json *rec;
    size_t i;
    if (!s->loaded || !target || !input || !value)
        return ES_ESTATE;
    if (at < s->eng.sched.now)
        return ES_ETIME;
    c = esk_engine_comp(&s->eng, target);
    if (!c) {
        /* a board ("B1": its MCU's reset and console) or one of its MCU's UARTs ("B1.USART2") */
        for (i = 0; i < s->boards.len; i++) {
            esk_board_inst *bi = s->boards.data[i];
            size_t n = strlen(bi->id);
            if (!esk_str_has_prefix(target, bi->id) || (target[n] && target[n] != '.'))
                continue;
            if (!bi->mcu || !esk_mcu_machine(bi->mcu)) {
                esk_diag_raise(&s->diag, ES_SEV_WARNING, "mcu.no_firmware", at, "board", target, NULL, 0,
                               "input %s to %s needs firmware on the board's MCU", input, target);
                return ES_EUNSUPPORTED;
            }
            c = bi->mcu;
            if (target[n] == '.' && (esk_str_eq(input, "tx_text") || esk_str_eq(input, "serial"))) {
                esk_snprintf(unit_input, sizeof unit_input, "serial:%s", target + n + 1);
                input = unit_input;
            }
            break;
        }
        if (!c)
            return ES_ENOTFOUND;
    }
    if (at == s->eng.sched.now) {
        apply_input_now(s, c, input, value);
    } else {
        pending_input *pi = (pending_input *)esk_calloc(s->a, 1, sizeof *pi);
        es_status st;
        if (!pi)
            return ES_ENOMEM;
        pi->s = s;
        pi->comp = c;
        pi->input = esk_strdup(s->a, input);
        pi->value = esk_json_clone(s->a, value);
        if (!pi->input || !pi->value) {
            esk_strfree(s->a, pi->input);
            esk_json_free(s->a, pi->value);
            esk_free(s->a, pi, sizeof *pi);
            return ES_ENOMEM;
        }
        st = esk_sched_at(&s->eng.sched, at, ESK_EV_EXTERNAL, pending_input_cb, s, pi, NULL);
        if (st != ES_OK) {
            esk_strfree(s->a, pi->input);
            esk_json_free(s->a, pi->value);
            esk_free(s->a, pi, sizeof *pi);
            return st;
        }
    }
    s->input_seq++;
    rec = esk_json_new_object(s->a);
    if (rec) {
        esk_json_set_u64_string(s->a, rec, "at_ps", at);
        esk_json_set_integer(s->a, rec, "seq", (int64_t)s->input_seq);
        esk_json_set_string(s->a, rec, "target", target);
        esk_json_set_string(s->a, rec, "input", input);
        esk_json_object_set(s->a, rec, "value", esk_json_clone(s->a, value));
        if (s->inputs_log)
            esk_json_array_push(s->a, s->inputs_log, rec);
        if (s->notify) {
            esk_json *copy = esk_json_clone(s->a, rec);
            if (copy)
                esk_session_notify(s, "record.input", copy);
        }
    }
    if (applied_at)
        *applied_at = at;
    if (seq)
        *seq = s->input_seq;
    return ES_OK;
}

/* ---- snapshots ----------------------------------------------------------------- */

static int want(const esk_json *include, const char *what)
{
    size_t i;
    if (!include || include->type != ESK_JSON_ARRAY)
        return 1;
    for (i = 0; i < esk_json_len(include); i++)
        if (esk_str_eq(esk_json_str(esk_json_at(include, i)), what))
            return 1;
    return 0;
}

static const char *strength_name(esk_strength s)
{
    switch (s) {
    case ESK_STR_SUPPLY: return "supply";
    case ESK_STR_STRONG: return "strong";
    case ESK_STR_WEAK: return "weak";
    default: return "hiz";
    }
}

static esk_json *net_state(es_session *s, const esk_net *n)
{
    esk_json *o = esk_json_new_object(s->a);
    size_t k;
    if (!o)
        return NULL;
    if (n->island) {
        esk_json_set_string(s->a, o, "kind", "analog");
        esk_json_set_number(s->a, o, "voltage", n->voltage);
        esk_json_set_integer(s->a, o, "island", (int64_t)n->island->id);
    } else {
        esk_json_set_string(s->a, o, "kind", "digital");
        esk_json_set_string(s->a, o, "logic", esk_logic_name(n->value));
        esk_json_set_string(s->a, o, "strength", strength_name(n->strength));
    }
    esk_json_set_u64_string(s->a, o, "changed_at_ps", n->changed_at);
    if (n->aliases.len) {
        esk_json *al = esk_json_new_array(s->a);
        if (al) {
            for (k = 0; k < n->aliases.len; k++)
                esk_json_array_push(s->a, al, esk_json_new_string(s->a, n->aliases.data[k]));
            esk_json_object_set(s->a, o, "aliases", al);
        }
    }
    return o;
}

esk_json *esk_session_snapshot(es_session *s, const esk_json *include)
{
    esk_json *root = esk_json_new_object(s->a);
    size_t i;
    if (!root)
        return NULL;
    esk_json_set_string(s->a, root, "name", s->name ? s->name : "");
    esk_json_set_u64_string(s->a, root, "now_ps", s->eng.sched.now);
    esk_json_set_string(s->a, root, "policy", s->eng.policy == ESK_POLICY_FUNCTIONAL ? "functional" : s->eng.policy == ESK_POLICY_TIMED ? "timed" : "electrical");
    esk_json_set_bool(s->a, root, "running", s->running);
    esk_json_set_number(s->a, root, "speed", s->speed);
    if (want(include, "nets")) {
        esk_json *nets = esk_json_new_object(s->a);
        for (i = 0; nets && i < s->eng.nets.len; i++) {
            const esk_net *n = s->eng.nets.data[i];
            esk_json_object_set(s->a, nets, n->name, net_state(s, n));
        }
        esk_json_object_set(s->a, root, "nets", nets);
    }
    if (want(include, "components") || want(include, "outputs")) {
        esk_json *comps = esk_json_new_object(s->a);
        esk_json *outs = want(include, "outputs") ? esk_json_new_object(s->a) : NULL;
        for (i = 0; comps && i < s->eng.comps.len; i++) {
            esk_comp *c = s->eng.comps.data[i];
            esk_json *o = esk_json_new_object(s->a);
            esk_json *outputs = esk_json_new_object(s->a);
            uint32_t k;
            if (!o || !outputs) {
                esk_json_free(s->a, o);
                esk_json_free(s->a, outputs);
                break;
            }
            esk_json_set_string(s->a, o, "kind", c->kind);
            esk_json_set_string(s->a, o, "model", c->ops->name);
            if (c->ops->observe)
                c->ops->observe(c, outputs);
            if (outs && esk_json_len(outputs))
                esk_json_object_set(s->a, outs, c->id, esk_json_clone(s->a, outputs));
            esk_json_object_set(s->a, o, "outputs", outputs);
            if (want(include, "components")) {
                esk_json *pins = esk_json_new_object(s->a);
                esk_json_object_set(s->a, o, "inputs", esk_json_clone(s->a, c->inputs));
                esk_json_object_set(s->a, o, "params", esk_json_clone(s->a, c->params));
                for (k = 0; pins && k < c->npins; k++) {
                    esk_pin *p = &c->pins[k];
                    esk_json *po = esk_json_new_object(s->a);
                    if (!po) break;
                    esk_json_set_string(s->a, po, "net", p->net ? p->net->name : "");
                    esk_json_set_string(s->a, po, "kind", esk_pin_kind_name(p->model.kind));
                    if (p->net && p->net->island) {
                        esk_json_set_number(s->a, po, "voltage", p->net->voltage);
                        if (p->bridge)
                            esk_json_set_string(s->a, po, "logic", esk_logic_name(p->sampled == ESK_LZ ? ESK_L0 : p->sampled));
                    } else {
                        esk_json_set_string(s->a, po, "logic", esk_logic_name(p->net ? p->net->value : ESK_LZ));
                    }
                    esk_json_object_set(s->a, pins, p->name, po);
                }
                esk_json_object_set(s->a, o, "pins", pins);
            }
            esk_json_object_set(s->a, comps, c->id, o);
        }
        if (want(include, "components"))
            esk_json_object_set(s->a, root, "components", comps);
        else
            esk_json_free(s->a, comps);
        if (outs)
            esk_json_object_set(s->a, root, "outputs", outs);
    }
    if (want(include, "boards")) {
        esk_json *arr = esk_json_new_array(s->a);
        for (i = 0; arr && i < s->boards.len; i++) {
            esk_board_inst *b = s->boards.data[i];
            esk_json *o = esk_json_new_object(s->a);
            if (!o) break;
            esk_json_set_string(s->a, o, "id", b->id);
            esk_json_set_string(s->a, o, "board", b->board_id);
            esk_json_set_string(s->a, o, "name", esk_json_get_str(b->item->desc, "name", b->board_id));
            esk_json_set_string(s->a, o, "runtime", "none");
            esk_json_set_string(s->a, o, "timing_tier", "unmodelled");
            if (b->firmware)
                esk_json_object_set(s->a, o, "firmware", esk_json_clone(s->a, b->firmware));
            esk_json_array_push(s->a, arr, o);
        }
        esk_json_object_set(s->a, root, "boards", arr);
    }
    if (want(include, "probes")) {
        esk_json *arr = esk_json_new_array(s->a);
        for (i = 0; arr && i < s->probes.len; i++) {
            esk_probe *p = s->probes.data[i];
            esk_json *o = esk_json_new_object(s->a);
            if (!o) break;
            esk_json_set_integer(s->a, o, "probe", (int64_t)p->id);
            esk_json_set_string(s->a, o, "id", p->name);
            esk_json_set_string(s->a, o, "target", p->target);
            esk_json_set_string(s->a, o, "kind", p->kind == ESK_PROBE_DIGITAL ? "digital" : p->kind == ESK_PROBE_ANALOG ? "analog" : p->kind == ESK_PROBE_VALUE ? "value" : "serial");
            esk_json_set_integer(s->a, o, "samples", (int64_t)p->len);
            if (p->has_last)
                esk_json_set_number(s->a, o, "last", p->last);
            esk_json_array_push(s->a, arr, o);
        }
        esk_json_object_set(s->a, root, "probes", arr);
    }
    {
        esk_json *d = esk_json_new_object(s->a);
        if (d) {
            esk_json_set_integer(s->a, d, "info", (int64_t)esk_diag_count(&s->diag, ES_SEV_INFO));
            esk_json_set_integer(s->a, d, "warning", (int64_t)esk_diag_count(&s->diag, ES_SEV_WARNING));
            esk_json_set_integer(s->a, d, "error", (int64_t)esk_diag_count(&s->diag, ES_SEV_ERROR));
            esk_json_set_integer(s->a, d, "fatal", (int64_t)esk_diag_count(&s->diag, ES_SEV_FATAL));
            esk_json_object_set(s->a, root, "diagnostics", d);
        }
    }
    return root;
}

esk_json *esk_session_topology(es_session *s)
{
    esk_json *root = esk_json_new_object(s->a);
    esk_json *nets = esk_json_new_array(s->a);
    esk_json *islands = esk_json_new_array(s->a);
    size_t i, k;
    if (!root || !nets || !islands) {
        esk_json_free(s->a, root); esk_json_free(s->a, nets); esk_json_free(s->a, islands);
        return NULL;
    }
    for (i = 0; i < s->eng.nets.len; i++) {
        const esk_net *n = s->eng.nets.data[i];
        esk_json *o = esk_json_new_object(s->a);
        esk_json *pins = esk_json_new_array(s->a);
        if (!o || !pins) break;
        esk_json_set_string(s->a, o, "name", n->name);
        esk_json_set_integer(s->a, o, "island", n->island ? (int64_t)n->island->id : -1);
        for (k = 0; k < n->pins.len; k++) {
            char buf[256];
            esk_snprintf(buf, sizeof buf, "%s.%s", n->pins.data[k]->comp->id, n->pins.data[k]->name);
            esk_json_array_push(s->a, pins, esk_json_new_string(s->a, buf));
        }
        esk_json_object_set(s->a, o, "pins", pins);
        esk_json_object_set(s->a, o, "state", net_state(s, n));
        esk_json_array_push(s->a, nets, o);
    }
    for (i = 0; i < s->eng.islands.len; i++) {
        esk_island *isl = s->eng.islands.data[i];
        esk_json *o = esk_json_new_object(s->a);
        esk_json *inets = esk_json_new_array(s->a);
        if (!o || !inets) break;
        esk_json_set_integer(s->a, o, "id", (int64_t)isl->id);
        esk_json_set_string(s->a, o, "kind", "analog");
        for (k = 0; k < isl->nets.len; k++)
            if (isl->nets.data[k])
                esk_json_array_push(s->a, inets, esk_json_new_string(s->a, isl->nets.data[k]->name));
        esk_json_object_set(s->a, o, "nets", inets);
        esk_json_set_integer(s->a, o, "unknowns", isl->nunknowns);
        esk_json_set_integer(s->a, o, "devices", (int64_t)isl->devs.len);
        esk_json_set_integer(s->a, o, "steps", (int64_t)isl->steps);
        esk_json_array_push(s->a, islands, o);
    }
    esk_json_object_set(s->a, root, "nets", nets);
    esk_json_object_set(s->a, root, "islands", islands);
    return root;
}

/* ---- observations and pacing ---------------------------------------------------- */

void esk_session_emit_observations(es_session *s, int force)
{
    uint64_t now_ms;
    size_t i;
    if (!s->notify)
        return;
    now_ms = esk_host_monotonic_ms();
    if (!force && s->observe_rate_hz > 0 && now_ms - s->last_observe_ms < (uint64_t)(1000.0 / s->observe_rate_hz))
        return;
    s->last_observe_ms = now_ms;
    if (s->observe_subscribed) {
        esk_json *snap = esk_session_snapshot(s, NULL);
        if (snap)
            esk_session_notify(s, "observe.update", snap);
    }
    for (i = 0; i < s->probes.len; i++) {
        esk_probe *p = s->probes.data[i];
        esk_json *params, *samples;
        es_time_t from;
        if (!p->subscribed || !p->len)
            continue;
        from = p->next_sample;
        samples = esk_session_probe_samples(s, p, from, ES_TIME_MAX, 20000);
        if (!samples || !esk_json_len(samples)) {
            esk_json_free(s->a, samples);
            continue;
        }
        params = esk_json_new_object(s->a);
        if (!params) {
            esk_json_free(s->a, samples);
            continue;
        }
        esk_json_set_integer(s->a, params, "stream", (int64_t)p->id);
        esk_json_set_string(s->a, params, "probe", p->name);
        esk_json_set_integer(s->a, params, "seq", (int64_t)p->stream_seq++);
        esk_json_set_string(s->a, params, "encoding", "json");
        esk_json_set_string(s->a, params, "kind", p->kind == ESK_PROBE_DIGITAL ? "digital" : p->kind == ESK_PROBE_ANALOG ? "analog" : "value");
        esk_json_object_set(s->a, params, "samples", samples);
        p->next_sample = p->ring[(p->head + p->len - 1) % p->cap].time_ps + 1;
        esk_session_notify(s, "waveform.samples", params);
    }
}

es_status esk_session_do_reset(es_session *s)
{
    es_status st;
    size_t i;
    if (!s->loaded)
        return ES_ESTATE;
    s->running = 0;
    esk_diag_reset(&s->diag);
    st = esk_engine_reset(&s->eng);
    if (s->recording)
        esk_record_start(s); /* a reset starts the recording's observations again */
    for (i = 0; i < s->probes.len; i++) {
        s->probes.data[i]->next_sample = 0;
        s->probes.data[i]->stream_seq = 0;
        if (s->probes.data[i]->cap)
            probe_start(s, s->probes.data[i]);
    }
    esk_json_free(s->a, s->inputs_log);
    s->inputs_log = esk_json_new_array(s->a);
    s->input_seq = 0;
    return st;
}

static void probes_sample_now(es_session *s)
{
    size_t i;
    for (i = 0; i < s->probes.len; i++) {
        esk_probe *p = s->probes.data[i];
        if (p->cap && (p->kind != ESK_PROBE_DIGITAL || p->threshold)) {
            es_time_t last = p->len ? p->ring[(p->head + p->len - 1) % p->cap].time_ps : 0;
            if (!p->len || last < s->eng.sched.now)
                probe_push(s, p, s->eng.sched.now, probe_value(s, p));
        }
    }
}

es_status esk_session_do_advance(es_session *s, es_time_t until)
{
    es_status st;
    if (!s->loaded)
        return ES_ESTATE;
    st = esk_engine_advance(&s->eng, until);
    if (s->eng.sched.stop_requested) {
        /* a core stopped (a breakpoint): the session stops with it */
        esk_json *info = NULL;
        s->eng.sched.stop_requested = 0;
        if (esk_debug_stopped_board(s, &info) && info)
            esk_session_notify(s, "debug.stopped", info);
    }
    probes_sample_now(s);
    return st;
}

static void control_state(es_session *s, const char *reason)
{
    esk_json *o = esk_json_new_object(s->a);
    if (!o)
        return;
    esk_json_set_bool(s->a, o, "running", s->running);
    esk_json_set_u64_string(s->a, o, "now_ps", s->eng.sched.now);
    if (reason)
        esk_json_set_string(s->a, o, "reason", reason);
    esk_session_notify(s, "control.state", o);
}

es_status esk_session_run_slice(es_session *s)
{
    es_time_t until, delta;
    uint64_t now_ms;
    es_status st;
    if (!s->running)
        return ES_OK;
    now_ms = esk_host_monotonic_ms();
    if (s->speed > 0) {
        uint64_t elapsed = now_ms > s->last_host_ms ? now_ms - s->last_host_ms : 0;
        if (elapsed == 0)
            return ES_OK;
        if (elapsed > 200)
            elapsed = 200; /* never try to catch up more than 200 ms of host time */
        delta = (es_time_t)((double)elapsed * (double)ES_PS_PER_MS * s->speed);
        if (delta > 100 * ES_PS_PER_MS)
            delta = 100 * ES_PS_PER_MS;
    } else {
        delta = s->run_slice_ps;
    }
    s->last_host_ms = now_ms;
    if (esk_time_add(s->eng.sched.now, delta, &until))
        until = ES_TIME_MAX - 1;
    if (until >= s->run_until)
        until = s->run_until;
    st = esk_session_do_advance(s, until);
    if (st != ES_OK || s->diag.fatal) {
        s->running = 0;
        control_state(s, s->diag.fatal ? s->diag.fatal_code : es_strerror(st));
    } else if (esk_debug_stopped_board(s, NULL)) {
        s->running = 0;
        control_state(s, "stopped");
    } else if (until >= s->run_until) {
        s->running = 0;
        control_state(s, "until");
    }
    esk_session_emit_observations(s, !s->running);
    return st;
}

/* ---- public control ---------------------------------------------------------------- */

es_status es_session_reset(es_session *s) { return s ? esk_session_do_reset(s) : ES_EINVAL; }

es_status es_session_advance(es_session *s, es_time_t until)
{
    if (!s)
        return ES_EINVAL;
    return esk_session_do_advance(s, until);
}

es_status es_session_step(es_session *s)
{
    int r;
    if (!s || !s->loaded)
        return ES_ESTATE;
    r = esk_sched_run_one(&s->eng.sched, ES_TIME_MAX);
    if (r < 0)
        return ES_ELIMIT;
    probes_sample_now(s);
    return ES_OK;
}

es_time_t es_session_now(const es_session *s) { return s ? s->eng.sched.now : 0; }

es_status es_session_set_diagnostic_handler(es_session *s, es_diagnostic_fn fn, void *ctx)
{
    if (!s)
        return ES_EINVAL;
    /* the session keeps its own forwarder; chain the user's handler */
    s->diag.fn = fn ? fn : diag_forward;
    s->diag.ctx = fn ? ctx : s;
    return ES_OK;
}

es_status es_session_set_notification_handler(es_session *s, es_notification_fn fn, void *ctx)
{
    if (!s)
        return ES_EINVAL;
    s->notify = fn;
    s->notify_ctx = ctx;
    return ES_OK;
}

es_status es_session_rpc(es_session *s, const char *req, size_t len, char **res, size_t *res_len)
{
    esk_json *request, *response = NULL;
    int line;
    const char *err;
    es_status st;
    char *text;
    size_t tlen;
    if (!s || !req || !res)
        return ES_EINVAL;
    s->rpc_count++;
    request = esk_json_parse(s->a, req, len, &line, &err);
    if (!request) {
        esk_json *e = esk_json_new_object(s->a), *eo = esk_json_new_object(s->a);
        if (!e || !eo) {
            esk_json_free(s->a, e);
            esk_json_free(s->a, eo);
            return ES_ENOMEM;
        }
        esk_json_set_string(s->a, e, "jsonrpc", "2.0");
        esk_json_object_set(s->a, e, "id", esk_json_new_null(s->a));
        esk_json_set_integer(s->a, eo, "code", -32700);
        esk_json_set_string(s->a, eo, "message", err);
        esk_json_object_set(s->a, e, "error", eo);
        response = e;
        st = ES_EPROTO;
    } else {
        st = esk_rpc_dispatch(s, request, &response);
        esk_json_free(s->a, request);
    }
    if (!response)
        return ES_ENOMEM;
    text = esk_json_format(s->a, response, 0, &tlen);
    esk_json_free(s->a, response);
    if (!text)
        return ES_ENOMEM;
    *res = (char *)malloc(tlen + 1);
    if (!*res) {
        esk_free(s->a, text, tlen + 1);
        return ES_ENOMEM;
    }
    memcpy(*res, text, tlen + 1);
    esk_free(s->a, text, tlen + 1);
    if (res_len)
        *res_len = tlen;
    ES_UNUSED(st);
    return ES_OK;
}

/* ---- catalog public API ------------------------------------------------------------ */

es_status es_catalog_create(es_catalog **out)
{
    es_catalog *c;
    es_status st;
    if (!out)
        return ES_EINVAL;
    c = (es_catalog *)malloc(sizeof *c);
    if (!c)
        return ES_ENOMEM;
    st = esk_catalog_init(c);
    if (st != ES_OK) {
        free(c);
        return st;
    }
    *out = c;
    return ES_OK;
}

void es_catalog_destroy(es_catalog *c)
{
    if (!c)
        return;
    esk_catalog_free(c);
    free(c);
}

es_status es_catalog_add_library(es_catalog *c, const char *path)
{
    if (!c || !path)
        return ES_EINVAL;
    return esk_catalog_load_library(c, path, NULL);
}

ES_API es_status es_serve_allow_origin(es_session *s, const char *origin)
{
    char *copy;
    if (!s || !origin || !*origin)
        return ES_EINVAL;
    if (s->n_allow_origin >= (int)ES_COUNTOF(s->allow_origin))
        return ES_ELIMIT;
    copy = esk_strdup(s->a, origin);
    if (!copy)
        return ES_ENOMEM;
    s->allow_origin[s->n_allow_origin++] = copy;
    return ES_OK;
}
