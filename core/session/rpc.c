/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* rpc.c -- the session protocol's method table (SPEC-008 §5) over the
 * in-process transport. Every transport ends here. */
#include "session/session_priv.h"
#include <string.h>
#include "host/host.h"
#include "util/num.h"
#include "util/str.h"

#define PROTOCOL_VERSION "1.0"

typedef struct rpc_ctx {
    es_session *s;
    const esk_json *params;
    esk_json *result;   /* set by a handler */
    int err_code;
    char err_msg[200];
} rpc_ctx;

static es_status fail(rpc_ctx *r, int code, const char *msg)
{
    r->err_code = code;
    esk_snprintf(r->err_msg, sizeof r->err_msg, "%s", msg);
    return ES_EPROTO;
}

static es_status fail_status(rpc_ctx *r, es_status st)
{
    r->err_code = -32000 + st; /* -32000 - |st| */
    esk_snprintf(r->err_msg, sizeof r->err_msg, "%s", es_strerror(st));
    return st;
}

static esk_json *obj(rpc_ctx *r) { return esk_json_new_object(r->s->a); }
static const char *pstr(rpc_ctx *r, const char *k, const char *def) { return esk_json_get_str(r->params, k, def); }

static int ptime(rpc_ctx *r, const char *k, es_time_t *out)
{
    const esk_json *j = esk_json_get(r->params, k);
    return j ? esk_json_to_time(j, out) : -1;
}

/* ---- session ------------------------------------------------------------------ */

static es_status m_session_open(rpc_ctx *r)
{
    es_session *s = r->s;
    esk_json *caps;
    const char *client = pstr(r, "client", NULL);
    const char *proto = pstr(r, "protocol", PROTOCOL_VERSION);
    if (strncmp(proto, "1.", 2) != 0)
        return fail(r, -32001, "protocol major version mismatch; this engine speaks 1.x");
    esk_strfree(s->a, s->client_name);
    s->client_name = client ? esk_strdup(s->a, client) : NULL;
    s->opened = 1;
    r->result = obj(r);
    caps = esk_json_new_array(s->a);
    if (!r->result || !caps)
        return ES_ENOMEM;
    esk_json_set_string(s->a, r->result, "protocol", PROTOCOL_VERSION);
    esk_json_set_string(s->a, r->result, "engine", es_version().string);
    esk_json_set_integer(s->a, r->result, "abi", es_abi_version());
    {
        static const char *const names[] = { "analog", "digital", "catalog", "scenario", "record.inputs", "streams.json", "observe.subscribe" };
        size_t i;
        for (i = 0; i < ES_COUNTOF(names); i++)
            esk_json_array_push(s->a, caps, esk_json_new_string(s->a, names[i]));
    }
    esk_json_object_set(s->a, r->result, "capabilities", caps);
    return ES_OK;
}

static es_status m_session_close(rpc_ctx *r)
{
    r->s->running = 0;
    r->s->opened = 0;
    r->result = obj(r);
    return r->result ? ES_OK : ES_ENOMEM;
}

static const char *policy_name(esk_policy p)
{
    return p == ESK_POLICY_FUNCTIONAL ? "functional" : p == ESK_POLICY_TIMED ? "timed" : "electrical";
}

static es_status m_session_info(rpc_ctx *r)
{
    es_session *s = r->s;
    esk_json *boards, *limits, *tiers;
    size_t i;
    r->result = obj(r);
    boards = esk_json_new_array(s->a);
    limits = obj(r);
    tiers = obj(r);
    if (!r->result || !boards || !limits || !tiers)
        return ES_ENOMEM;
    esk_json_set_string(s->a, r->result, "engine", es_version().string);
    esk_json_set_string(s->a, r->result, "protocol", PROTOCOL_VERSION);
    esk_json_set_string(s->a, r->result, "project", s->name ? s->name : "");
    esk_json_set_string(s->a, r->result, "project_path", s->project_path ? s->project_path : "");
    esk_json_set_bool(s->a, r->result, "loaded", s->loaded);
    esk_json_set_string(s->a, r->result, "policy", policy_name(s->eng.policy));
    esk_json_set_u64_string(s->a, r->result, "now_ps", s->eng.sched.now);
    esk_json_set_bool(s->a, r->result, "running", s->running);
    esk_json_set_number(s->a, r->result, "speed", s->speed);
    for (i = 0; i < s->boards.len; i++) {
        esk_json *b = obj(r);
        if (!b) break;
        esk_json_set_string(s->a, b, "id", s->boards.data[i]->id);
        esk_json_set_string(s->a, b, "board", s->boards.data[i]->board_id);
        esk_json_set_string(s->a, b, "runtime", "none");
        esk_json_array_push(s->a, boards, b);
        esk_json_set_string(s->a, tiers, s->boards.data[i]->id, "unmodelled");
    }
    esk_json_object_set(s->a, r->result, "boards", boards);
    esk_json_object_set(s->a, r->result, "timing_tiers", tiers);
    esk_json_set_integer(s->a, r->result, "islands", (int64_t)s->eng.islands.len);
    esk_json_set_integer(s->a, r->result, "nets", (int64_t)s->eng.nets.len);
    esk_json_set_integer(s->a, r->result, "components", (int64_t)s->eng.comps.len);
    esk_json_set_integer(s->a, limits, "max_nets", (int64_t)s->eng.max_nets);
    esk_json_set_integer(s->a, limits, "max_components", (int64_t)s->eng.max_comps);
    esk_json_set_integer(s->a, limits, "max_pending_events", (int64_t)s->eng.sched.max);
    esk_json_set_integer(s->a, limits, "max_delta_cycles", (int64_t)s->eng.sched.max_delta_cycles);
    esk_json_set_u64_string(s->a, limits, "core_quantum_ps", s->core_quantum_ps);
    esk_json_set_u64_string(s->a, limits, "crossing_resolution_ps", s->eng.crossing_resolution_ps);
    esk_json_object_set(s->a, r->result, "limits", limits);
    esk_json_object_set(s->a, r->result, "libraries", esk_json_clone(s->a, s->catalog->libraries));
    esk_json_set_integer(s->a, r->result, "events_processed", (int64_t)s->eng.sched.processed);
    return ES_OK;
}

static es_status m_session_policy(rpc_ctx *r)
{
    es_session *s = r->s;
    const char *p = pstr(r, "policy", NULL);
    esk_policy np;
    es_status st;
    if (s->running)
        return fail_status(r, ES_ESTATE);
    if (!p) return fail(r, -32602, "policy is required");
    if (!strcmp(p, "functional")) np = ESK_POLICY_FUNCTIONAL;
    else if (!strcmp(p, "timed")) np = ESK_POLICY_TIMED;
    else if (!strcmp(p, "electrical")) np = ESK_POLICY_ELECTRICAL;
    else return fail(r, -32602, "policy must be functional, timed or electrical");
    if (np != s->eng.policy) {
        /* models were chosen for the previous policy: reload the project */
        esk_json *proj = s->project ? esk_json_clone(s->a, s->project) : NULL;
        char *base = s->base_dir ? esk_strdup(s->a, s->base_dir) : NULL;
        s->eng.policy = np;
        s->policy_forced = 1;
        if (proj) {
            st = esk_project_load(s, proj, base);
            esk_json_free(s->a, proj);
            esk_strfree(s->a, base);
            if (st == ES_OK)
                st = esk_session_do_reset(s);
            if (st != ES_OK)
                return fail_status(r, st);
        }
    }
    r->result = obj(r);
    if (!r->result) return ES_ENOMEM;
    esk_json_set_string(s->a, r->result, "policy", policy_name(s->eng.policy));
    return ES_OK;
}

/* ---- project -------------------------------------------------------------------- */

static es_status m_project_load(rpc_ctx *r)
{
    es_session *s = r->s;
    const char *path = pstr(r, "path", NULL);
    const esk_json *json = esk_json_get(r->params, "json");
    es_status st;
    size_t before = s->diag.len;
    if (path) {
        char *full = s->base_dir && path[0] != '/' ? esk_host_path_join(s->a, s->base_dir, path) : esk_strdup(s->a, path);
        st = es_session_load_file(s, esk_host_file_exists(path) ? path : full ? full : path);
        esk_strfree(s->a, full);
    } else if (json && json->type == ESK_JSON_OBJECT) {
        st = esk_project_load(s, json, pstr(r, "base_dir", "."));
        if (st == ES_OK)
            st = esk_session_do_reset(s);
    } else {
        return fail(r, -32602, "path or json is required");
    }
    r->result = obj(r);
    if (!r->result) return ES_ENOMEM;
    esk_json_set_string(s->a, r->result, "name", s->name ? s->name : "");
    esk_json_set_bool(s->a, r->result, "ok", st == ES_OK);
    {
        esk_json *all = esk_diag_to_json(&s->diag), *recent = esk_json_new_array(s->a);
        size_t i;
        for (i = before; all && recent && i < esk_json_len(all); i++)
            esk_json_array_push(s->a, recent, esk_json_clone(s->a, esk_json_at(all, i)));
        esk_json_free(s->a, all);
        esk_json_object_set(s->a, r->result, "diagnostics", recent);
    }
    if (st != ES_OK)
        return fail_status(r, st);
    return ES_OK;
}

static es_status m_project_get(rpc_ctx *r)
{
    r->result = obj(r);
    if (!r->result) return ES_ENOMEM;
    esk_json_object_set(r->s->a, r->result, "project", r->s->project ? esk_json_clone(r->s->a, r->s->project) : esk_json_new_null(r->s->a));
    return ES_OK;
}

static es_status m_project_validate(rpc_ctx *r)
{
    es_session *s = r->s;
    es_session *tmp = NULL;
    es_session_config_t cfg;
    const char *path = pstr(r, "path", NULL);
    const esk_json *json = esk_json_get(r->params, "json");
    es_status st;
    memset(&cfg, 0, sizeof cfg);
    cfg.catalog = s->catalog;
    st = es_session_create(&cfg, &tmp);
    if (st != ES_OK) return fail_status(r, st);
    if (path)
        st = es_session_load_file(tmp, path);
    else if (json)
        st = esk_project_load(tmp, json, pstr(r, "base_dir", "."));
    else {
        es_session_destroy(tmp);
        return fail(r, -32602, "path or json is required");
    }
    r->result = obj(r);
    if (!r->result) {
        es_session_destroy(tmp);
        return ES_ENOMEM;
    }
    esk_json_set_bool(s->a, r->result, "ok", st == ES_OK && esk_diag_count(&tmp->diag, ES_SEV_ERROR) == 0);
    {
        esk_json *d = esk_diag_to_json(&tmp->diag);
        esk_json *copy = d ? esk_json_clone(s->a, d) : NULL;
        esk_json_free(tmp->a, d);
        esk_json_object_set(s->a, r->result, "diagnostics", copy ? copy : esk_json_new_array(s->a));
    }
    es_session_destroy(tmp);
    return ES_OK;
}

static es_status m_project_topology(rpc_ctx *r)
{
    r->result = esk_session_topology(r->s);
    return r->result ? ES_OK : ES_ENOMEM;
}

/* ---- control --------------------------------------------------------------------- */

static es_status m_control_run(rpc_ctx *r)
{
    es_session *s = r->s;
    es_time_t until;
    const esk_json *sp = esk_json_get(r->params, "speed");
    if (!s->loaded) return fail_status(r, ES_ESTATE);
    if (sp && sp->type == ESK_JSON_NUMBER && sp->u.number.value >= 0)
        s->speed = sp->u.number.value;
    s->run_until = ptime(r, "until", &until) == 0 ? until : ES_TIME_MAX;
    esk_debug_resume_all(s);
    s->running = 1;
    s->last_host_ms = esk_host_monotonic_ms();
    r->result = obj(r);
    if (!r->result) return ES_ENOMEM;
    esk_json_set_bool(s->a, r->result, "running", 1);
    esk_json_set_u64_string(s->a, r->result, "now_ps", s->eng.sched.now);
    return ES_OK;
}

static es_status m_control_pause(rpc_ctx *r)
{
    r->s->running = 0;
    r->result = obj(r);
    if (!r->result) return ES_ENOMEM;
    esk_json_set_u64_string(r->s->a, r->result, "now_ps", r->s->eng.sched.now);
    esk_session_emit_observations(r->s, 1);
    return ES_OK;
}

static es_status m_control_step(rpc_ctx *r)
{
    const char *mode = pstr(r, "mode", "event");
    es_status st;
    if (!r->s->loaded) return fail_status(r, ES_ESTATE);
    if (!strcmp(mode, "instruction")) {
        int code = 0;
        st = esk_debug_call(r->s, "debug.step", r->params, &r->result, &code, r->err_msg, sizeof r->err_msg);
        if (code)
            r->err_code = code;
        return st;
    }
    st = es_session_step(r->s);
    if (st != ES_OK) return fail_status(r, st);
    r->result = obj(r);
    if (!r->result) return ES_ENOMEM;
    esk_json_set_u64_string(r->s->a, r->result, "now_ps", r->s->eng.sched.now);
    esk_session_emit_observations(r->s, 1);
    return ES_OK;
}

static es_status m_control_advance(rpc_ctx *r)
{
    es_session *s = r->s;
    es_time_t until, by;
    es_status st;
    if (!s->loaded) return fail_status(r, ES_ESTATE);
    if (ptime(r, "until", &until) != 0) {
        if (ptime(r, "by", &by) != 0)
            return fail(r, -32602, "until or by is required");
        if (esk_time_add(s->eng.sched.now, by, &until))
            return fail_status(r, ES_ETIME);
    }
    esk_debug_resume_all(s);
    st = esk_session_do_advance(s, until);
    if (st != ES_OK) return fail_status(r, st);
    r->result = obj(r);
    if (!r->result) return ES_ENOMEM;
    esk_json_set_u64_string(s->a, r->result, "now_ps", s->eng.sched.now);
    {
        esk_json *info = NULL;
        if (esk_debug_stopped_board(s, &info) && info)
            esk_json_object_set(s->a, r->result, "stopped", info);
    }
    esk_session_emit_observations(s, 1);
    return ES_OK;
}

static es_status m_control_reset(rpc_ctx *r)
{
    es_status st = esk_session_do_reset(r->s);
    if (st != ES_OK) return fail_status(r, st);
    r->result = obj(r);
    if (!r->result) return ES_ENOMEM;
    esk_json_set_u64_string(r->s->a, r->result, "now_ps", r->s->eng.sched.now);
    esk_session_emit_observations(r->s, 1);
    return ES_OK;
}

static es_status m_control_speed(rpc_ctx *r)
{
    const esk_json *sp = esk_json_get(r->params, "speed");
    if (!sp || sp->type != ESK_JSON_NUMBER || sp->u.number.value < 0)
        return fail(r, -32602, "speed must be a non-negative number");
    r->s->speed = sp->u.number.value;
    r->result = obj(r);
    if (!r->result) return ES_ENOMEM;
    esk_json_set_number(r->s->a, r->result, "speed", r->s->speed);
    return ES_OK;
}

/* ---- inputs ------------------------------------------------------------------------ */

static es_status input_common(rpc_ctx *r, const char *target, const char *input, const esk_json *value)
{
    es_time_t at = r->s->eng.sched.now, applied;
    uint64_t seq;
    es_status st;
    if (!target || !input || !value) return fail(r, -32602, "target, input and value are required");
    if (ptime(r, "at", &at) != 0)
        at = r->s->eng.sched.now;
    st = esk_session_apply_input(r->s, target, input, value, at, &applied, &seq);
    if (st != ES_OK) return fail_status(r, st);
    r->result = obj(r);
    if (!r->result) return ES_ENOMEM;
    esk_json_set_u64_string(r->s->a, r->result, "at_ps", applied);
    esk_json_set_integer(r->s->a, r->result, "seq", (int64_t)seq);
    if (!r->s->running)
        esk_session_emit_observations(r->s, 1);
    return ES_OK;
}

static es_status m_input_set(rpc_ctx *r)
{
    return input_common(r, pstr(r, "target", NULL), pstr(r, "input", NULL), esk_json_get(r->params, "value"));
}

static es_status m_input_press(rpc_ctx *r)
{
    esk_json t;
    memset(&t, 0, sizeof t);
    t.type = ESK_JSON_BOOL;
    t.u.boolean = 1;
    return input_common(r, pstr(r, "target", NULL), "pressed", &t);
}

static es_status m_input_release(rpc_ctx *r)
{
    esk_json t;
    memset(&t, 0, sizeof t);
    t.type = ESK_JSON_BOOL;
    t.u.boolean = 0;
    return input_common(r, pstr(r, "target", NULL), "pressed", &t);
}

static es_status m_input_serial(rpc_ctx *r)
{
    const esk_json *text = esk_json_get(r->params, "text");
    if (!text)
        text = esk_json_get(r->params, "bytes");
    return input_common(r, pstr(r, "target", NULL), "tx_text", text);
}

static es_status m_input_list(rpc_ctx *r)
{
    es_session *s = r->s;
    esk_json *arr = esk_json_new_array(s->a);
    size_t i, k;
    r->result = obj(r);
    if (!r->result || !arr) {
        esk_json_free(s->a, arr);
        return ES_ENOMEM;
    }
    for (i = 0; i < s->eng.comps.len; i++) {
        esk_comp *c = s->eng.comps.data[i];
        const esk_json *inputs = esk_json_get(c->desc, "inputs");
        for (k = 0; k < esk_json_len(inputs); k++) {
            const esk_json *in = esk_json_at(inputs, k);
            esk_json *o = obj(r);
            if (!o) break;
            esk_json_set_string(s->a, o, "target", c->id);
            esk_json_set_string(s->a, o, "input", esk_json_get_str(in, "name", ""));
            esk_json_set_string(s->a, o, "type", esk_json_get_str(in, "type", "boolean"));
            esk_json_set_string(s->a, o, "doc", esk_json_get_str(in, "doc", ""));
            if (esk_json_get(in, "min")) esk_json_object_set(s->a, o, "min", esk_json_clone(s->a, esk_json_get(in, "min")));
            if (esk_json_get(in, "max")) esk_json_object_set(s->a, o, "max", esk_json_clone(s->a, esk_json_get(in, "max")));
            {
                const esk_json *cur = esk_json_get(c->inputs, esk_json_get_str(in, "name", ""));
                if (cur) esk_json_object_set(s->a, o, "value", esk_json_clone(s->a, cur));
            }
            esk_json_array_push(s->a, arr, o);
        }
    }
    esk_json_object_set(s->a, r->result, "inputs", arr);
    return ES_OK;
}

/* ---- probes and waveforms ------------------------------------------------------------- */

static es_status m_probe_add(rpc_ctx *r)
{
    esk_probe *p = NULL;
    es_status st = esk_session_probe_add(r->s, pstr(r, "id", NULL), pstr(r, "target", NULL), pstr(r, "kind", NULL),
                                         esk_json_get_num(r->params, "rate_hz", 0), &p);
    if (st != ES_OK) return fail_status(r, st);
    r->result = obj(r);
    if (!r->result) return ES_ENOMEM;
    esk_json_set_integer(r->s->a, r->result, "probe", (int64_t)p->id);
    esk_json_set_string(r->s->a, r->result, "id", p->name);
    return ES_OK;
}

static es_status m_probe_remove(rpc_ctx *r)
{
    const esk_json *pj = esk_json_get(r->params, "probe");
    esk_probe *p = pj && pj->type == ESK_JSON_NUMBER ? NULL : esk_session_probe_find(r->s, esk_json_str(pj));
    uint32_t id = p ? p->id : (uint32_t)esk_json_get_int(r->params, "probe", 0);
    if (!id) return fail_status(r, ES_ENOTFOUND);
    esk_session_probe_remove(r->s, id);
    r->result = obj(r);
    return r->result ? ES_OK : ES_ENOMEM;
}

static es_status m_probe_list(rpc_ctx *r)
{
    esk_json *inc = esk_json_new_array(r->s->a);
    esk_json *snap;
    if (!inc) return ES_ENOMEM;
    esk_json_array_push(r->s->a, inc, esk_json_new_string(r->s->a, "probes"));
    snap = esk_session_snapshot(r->s, inc);
    esk_json_free(r->s->a, inc);
    if (!snap) return ES_ENOMEM;
    r->result = obj(r);
    if (!r->result) { esk_json_free(r->s->a, snap); return ES_ENOMEM; }
    esk_json_object_set(r->s->a, r->result, "probes", esk_json_clone(r->s->a, esk_json_get(snap, "probes")));
    esk_json_free(r->s->a, snap);
    return ES_OK;
}

static esk_probe *probe_param(rpc_ctx *r, const char *key)
{
    const esk_json *pj = esk_json_get(r->params, key);
    char buf[32];
    if (!pj) return NULL;
    if (pj->type == ESK_JSON_NUMBER) {
        esk_snprintf(buf, sizeof buf, "%lld", (long long)pj->u.number.integer);
        return esk_session_probe_find(r->s, buf);
    }
    return esk_session_probe_find(r->s, esk_json_str(pj));
}

static es_status m_probe_read(rpc_ctx *r)
{
    esk_probe *p = probe_param(r, "probe");
    es_time_t from = 0, to = ES_TIME_MAX;
    if (!p) return fail_status(r, ES_ENOTFOUND);
    ptime(r, "from_ps", &from);
    ptime(r, "to_ps", &to);
    r->result = obj(r);
    if (!r->result) return ES_ENOMEM;
    if (p->kind == ESK_PROBE_SERIAL) {
        esk_json *out = obj(r);
        if (out && p->comp->ops->observe) {
            p->comp->ops->observe(p->comp, out);
            esk_json_set_string(r->s->a, r->result, "text", esk_json_get_str(out, p->quantity ? p->quantity : "rx_text", ""));
        }
        esk_json_free(r->s->a, out);
        return ES_OK;
    }
    esk_json_object_set(r->s->a, r->result, "samples", esk_session_probe_samples(r->s, p, from, to, (size_t)esk_json_get_int(r->params, "max", 0)));
    esk_json_set_integer(r->s->a, r->result, "dropped", (int64_t)p->dropped);
    esk_json_set_string(r->s->a, r->result, "kind", p->kind == ESK_PROBE_DIGITAL ? "digital" : p->kind == ESK_PROBE_ANALOG ? "analog" : "value");
    return ES_OK;
}

static es_status m_waveform_subscribe(rpc_ctx *r)
{
    const esk_json *probes = esk_json_get(r->params, "probes");
    esk_json *streams = esk_json_new_array(r->s->a);
    size_t i;
    r->result = obj(r);
    if (!r->result || !streams) { esk_json_free(r->s->a, streams); return ES_ENOMEM; }
    for (i = 0; i < esk_json_len(probes); i++) {
        const esk_json *pj = esk_json_at(probes, i);
        esk_probe *p;
        char buf[32];
        if (pj->type == ESK_JSON_NUMBER) {
            esk_snprintf(buf, sizeof buf, "%lld", (long long)pj->u.number.integer);
            p = esk_session_probe_find(r->s, buf);
        } else {
            p = esk_session_probe_find(r->s, esk_json_str(pj));
        }
        if (p && p->kind != ESK_PROBE_SERIAL) {
            p->subscribed = 1;
            p->next_sample = 0;
            esk_json_array_push(r->s->a, streams, esk_json_new_integer(r->s->a, (int64_t)p->id));
        }
    }
    esk_json_object_set(r->s->a, r->result, "streams", streams);
    esk_json_set_string(r->s->a, r->result, "encoding", "json");
    esk_session_emit_observations(r->s, 1);
    return ES_OK;
}

static es_status m_waveform_unsubscribe(rpc_ctx *r)
{
    esk_probe *p = probe_param(r, "stream");
    if (p) p->subscribed = 0;
    r->result = obj(r);
    return r->result ? ES_OK : ES_ENOMEM;
}

/* ---- observe ------------------------------------------------------------------------- */

static es_status m_observe_snapshot(rpc_ctx *r)
{
    r->result = esk_session_snapshot(r->s, esk_json_get(r->params, "include"));
    return r->result ? ES_OK : ES_ENOMEM;
}

static es_status m_observe_subscribe(rpc_ctx *r)
{
    double rate = esk_json_get_num(r->params, "max_rate_hz", 30.0);
    r->s->observe_subscribed = 1;
    r->s->observe_rate_hz = rate > 0 ? rate : 30.0;
    r->result = obj(r);
    if (!r->result) return ES_ENOMEM;
    esk_session_emit_observations(r->s, 1);
    return ES_OK;
}

static es_status m_observe_unsubscribe(rpc_ctx *r)
{
    r->s->observe_subscribed = 0;
    r->result = obj(r);
    return r->result ? ES_OK : ES_ENOMEM;
}

/* ---- catalog ------------------------------------------------------------------------- */

static es_status m_catalog_list(rpc_ctx *r)
{
    r->result = obj(r);
    if (!r->result) return ES_ENOMEM;
    esk_json_object_set(r->s->a, r->result, "items", esk_catalog_list(r->s->catalog, pstr(r, "kind", "component"), pstr(r, "category", NULL), pstr(r, "query", NULL)));
    return ES_OK;
}

static es_status m_catalog_get(rpc_ctx *r)
{
    const char *id = pstr(r, "id", NULL);
    const char *kind = pstr(r, "kind", "component");
    const esk_catalog_item *it;
    esk_json *files;
    if (!id) return fail(r, -32602, "id is required");
    it = esk_catalog_find(r->s->catalog, kind, id);
    if (!it) return fail_status(r, ES_ENOTFOUND);
    r->result = obj(r);
    files = obj(r);
    if (!r->result || !files) { esk_json_free(r->s->a, files); return ES_ENOMEM; }
    esk_json_object_set(r->s->a, r->result, "description", esk_json_clone(r->s->a, it->desc));
    esk_json_set_string(r->s->a, r->result, "dir", it->dir);
    {
        static const char *const keys[2][2] = { { "symbol", "svg" }, { "artwork", "svg" } };
        size_t i;
        for (i = 0; i < 2; i++) {
            const char *rel = esk_json_get_str(esk_json_get(it->desc, keys[i][0]), keys[i][1], NULL);
            if (rel) {
                size_t len;
                char *text = esk_catalog_read_item_file(r->s->catalog, it, rel, &len);
                if (text) {
                    esk_json_set_string(r->s->a, files, keys[i][0], text);
                    esk_free(r->s->catalog->a, text, len + 1);
                }
            }
        }
    }
    esk_json_object_set(r->s->a, r->result, "files", files);
    return ES_OK;
}

static es_status m_catalog_libraries(rpc_ctx *r)
{
    r->result = obj(r);
    if (!r->result) return ES_ENOMEM;
    esk_json_object_set(r->s->a, r->result, "libraries", esk_json_clone(r->s->a, r->s->catalog->libraries));
    return ES_OK;
}

static es_status m_catalog_models(rpc_ctx *r)
{
    r->result = obj(r);
    if (!r->result) return ES_ENOMEM;
    esk_json_object_set(r->s->a, r->result, "models", esk_registry_list(&r->s->reg));
    return ES_OK;
}

/* ---- record, scenario, diagnostics -------------------------------------------------------- */

static es_status m_record_start(rpc_ctx *r)
{
    r->s->recording = 1;
    esk_strfree(r->s->a, r->s->record_path);
    r->s->record_path = pstr(r, "path", NULL) ? esk_strdup(r->s->a, pstr(r, "path", NULL)) : NULL;
    r->result = obj(r);
    return r->result ? ES_OK : ES_ENOMEM;
}

static es_status m_record_stop(rpc_ctx *r)
{
    es_session *s = r->s;
    r->s->recording = 0;
    r->result = obj(r);
    if (!r->result) return ES_ENOMEM;
    esk_json_object_set(s->a, r->result, "inputs", esk_json_clone(s->a, s->inputs_log));
    esk_json_set_integer(s->a, r->result, "count", (int64_t)esk_json_len(s->inputs_log));
    if (s->record_path) {
        esk_json *rec = obj(r);
        if (rec) {
            esk_json *eng = obj(r);
            char *text;
            size_t len;
            esk_json_set_integer(s->a, rec, "schema_version", 1);
            esk_json_set_string(s->a, rec, "kind", "recording");
            if (eng) {
                esk_json_set_string(s->a, eng, "version", es_version().string);
                esk_json_set_integer(s->a, eng, "abi", es_abi_version());
                esk_json_object_set(s->a, rec, "engine", eng);
            }
            esk_json_set_string(s->a, rec, "project", s->project_path ? s->project_path : "");
            esk_json_set_string(s->a, rec, "policy", policy_name(s->eng.policy));
            esk_json_object_set(s->a, rec, "inputs", esk_json_clone(s->a, s->inputs_log));
            esk_json_set_u64_string(s->a, rec, "end_ps", s->eng.sched.now);
            text = esk_json_format(s->a, rec, 2, &len);
            if (text) {
                esk_host_write_file(s->record_path, text, len);
                esk_free(s->a, text, len + 1);
            }
            esk_json_free(s->a, rec);
            esk_json_set_string(s->a, r->result, "path", s->record_path);
        }
    }
    return ES_OK;
}

static es_status m_scenario_run(rpc_ctx *r)
{
    const char *path = pstr(r, "path", NULL);
    const esk_json *sc = esk_json_get(r->params, "scenario");
    esk_json *loaded = NULL;
    es_time_t until = 0;
    char *dir = NULL;
    if (path) {
        char *text;
        size_t len;
        int line;
        const char *err;
        char *full = r->s->base_dir && !esk_host_file_exists(path) ? esk_host_path_join(r->s->a, r->s->base_dir, path) : esk_strdup(r->s->a, path);
        if (!full || esk_host_read_file(r->s->a, full, &text, &len) != ES_OK) {
            esk_strfree(r->s->a, full);
            return fail_status(r, ES_EIO);
        }
        loaded = esk_json_parse(r->s->a, text, len, &line, &err);
        esk_free(r->s->a, text, len + 1);
        dir = esk_host_dirname(r->s->a, full);
        esk_strfree(r->s->a, full);
        if (!loaded) {
            esk_strfree(r->s->a, dir);
            return fail(r, -32602, err);
        }
        sc = loaded;
    }
    if (!sc) return fail(r, -32602, "path or scenario is required");
    ptime(r, "until", &until);
    r->result = esk_scenario_run(r->s, sc, dir ? dir : r->s->base_dir, until);
    esk_json_free(r->s->a, loaded);
    esk_strfree(r->s->a, dir);
    return r->result ? ES_OK : ES_ENOMEM;
}

static es_status m_diagnostics_list(rpc_ctx *r)
{
    r->result = obj(r);
    if (!r->result) return ES_ENOMEM;
    esk_json_object_set(r->s->a, r->result, "records", esk_diag_to_json(&r->s->diag));
    return ES_OK;
}

static es_status m_unavailable(rpc_ctx *r)
{
    return fail(r, -32601, "this method needs a subsystem this version does not include (see docs/STATUS.md)");
}

/* ---- the table ------------------------------------------------------------------------ */

typedef struct method { const char *name; es_status (*fn)(rpc_ctx *); } method;

static const method methods[] = {
    { "session.open", m_session_open }, { "session.close", m_session_close }, { "session.info", m_session_info },
    { "session.policy", m_session_policy },
    { "project.load", m_project_load }, { "project.get", m_project_get }, { "project.validate", m_project_validate },
    { "project.topology", m_project_topology }, { "project.transaction", m_unavailable }, { "project.save", m_unavailable },
    { "control.run", m_control_run }, { "control.pause", m_control_pause }, { "control.step", m_control_step },
    { "control.advance", m_control_advance }, { "control.reset", m_control_reset }, { "control.speed", m_control_speed },
    { "input.set", m_input_set }, { "input.press", m_input_press }, { "input.release", m_input_release },
    { "input.serial", m_input_serial }, { "input.list", m_input_list },
    { "probe.add", m_probe_add }, { "probe.remove", m_probe_remove }, { "probe.list", m_probe_list }, { "probe.read", m_probe_read },
    { "waveform.subscribe", m_waveform_subscribe }, { "waveform.unsubscribe", m_waveform_unsubscribe },
    { "observe.snapshot", m_observe_snapshot }, { "observe.subscribe", m_observe_subscribe }, { "observe.unsubscribe", m_observe_unsubscribe },
    { "catalog.list", m_catalog_list }, { "catalog.get", m_catalog_get }, { "catalog.libraries", m_catalog_libraries },
    { "catalog.models", m_catalog_models }, { "catalog.validate", m_unavailable },
    { "record.start", m_record_start }, { "record.stop", m_record_stop }, { "record.replay", m_unavailable },
    { "scenario.run", m_scenario_run }, { "diagnostics.list", m_diagnostics_list },
    { "toolchain.detect", m_unavailable }, { "toolchain.build", m_unavailable },
};

es_status esk_rpc_dispatch(es_session *s, const esk_json *req, esk_json **response)
{
    rpc_ctx r;
    esk_json *resp = esk_json_new_object(s->a);
    const esk_json *id = esk_json_get(req, "id");
    const char *name = esk_json_get_str(req, "method", NULL);
    size_t i;
    es_status st = ES_OK;
    int found = 0;
    if (!resp)
        return ES_ENOMEM;
    memset(&r, 0, sizeof r);
    r.s = s;
    r.params = esk_json_get(req, "params");
    esk_json_set_string(s->a, resp, "jsonrpc", "2.0");
    esk_json_object_set(s->a, resp, "id", id ? esk_json_clone(s->a, id) : esk_json_new_null(s->a));
    if (!name) {
        r.err_code = -32600;
        esk_snprintf(r.err_msg, sizeof r.err_msg, "%s", "method is required");
    } else {
        for (i = 0; i < ES_COUNTOF(methods); i++) {
            if (!strcmp(methods[i].name, name)) {
                found = 1;
                st = methods[i].fn(&r);
                break;
            }
        }
        if (!found && esk_debug_has(name)) {
            found = 1;
            st = esk_debug_call(s, name, r.params, &r.result, &r.err_code, r.err_msg, sizeof r.err_msg);
        }
        if (!found) {
            if (esk_str_has_prefix(name, "debug.") || esk_str_has_prefix(name, "toolchain."))
                m_unavailable(&r);
            else {
                r.err_code = -32601;
                esk_snprintf(r.err_msg, sizeof r.err_msg, "unknown method %s", name);
            }
        }
    }
    if (r.err_code) {
        esk_json *e = esk_json_new_object(s->a);
        esk_json_free(s->a, r.result);
        if (e) {
            esk_json_set_integer(s->a, e, "code", r.err_code);
            esk_json_set_string(s->a, e, "message", r.err_msg);
            if (s->diag.len) {
                /* the most recent diagnostic as data */
                esk_json *all = esk_diag_to_json(&s->diag);
                if (all && esk_json_len(all))
                    esk_json_object_set(s->a, e, "data", esk_json_clone(s->a, esk_json_at(all, esk_json_len(all) - 1)));
                esk_json_free(s->a, all);
            }
            esk_json_object_set(s->a, resp, "error", e);
        }
    } else if (st != ES_OK || !r.result) {
        esk_json *e = esk_json_new_object(s->a);
        esk_json_free(s->a, r.result);
        if (e) {
            esk_json_set_integer(s->a, e, "code", -32000 + (st != ES_OK ? st : ES_ENOMEM));
            esk_json_set_string(s->a, e, "message", es_strerror(st != ES_OK ? st : ES_ENOMEM));
            esk_json_object_set(s->a, resp, "error", e);
        }
    } else {
        esk_json_object_set(s->a, resp, "result", r.result);
    }
    *response = resp;
    return ES_OK;
}
