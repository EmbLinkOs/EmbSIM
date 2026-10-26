/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* rpc.c -- the session protocol's method table (SPEC-008 §5) over the
 * in-process transport. Every transport ends here. */
#include "session/session_priv.h"
#include "mcu/analysis.h"
#include "mcu/mcu.h"
#include <stdlib.h>
#include <string.h>
#include "host/host.h"
#include "util/num.h"
#include "util/sha256.h"
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
        static const char *const names[] = { "analog", "digital", "catalog", "scenario", "scenario.edit", "record", "record.inputs",
                                             "record.replay", "streams.json", "observe.subscribe", "mcu", "debug", "toolchain",
                                             "analysis" };
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
        esk_board_inst *bi = s->boards.data[i];
        int live = bi->mcu && esk_mcu_machine(bi->mcu) != NULL;
        esk_json_set_string(s->a, b, "id", bi->id);
        esk_json_set_string(s->a, b, "board", bi->board_id);
        if (bi->mcu)
            esk_json_set_string(s->a, b, "mcu", bi->mcu->kind);
        esk_json_set_string(s->a, b, "runtime", live ? "cortex-m" : "none");
        if (bi->mcu)
            esk_json_set_string(s->a, b, "component", bi->mcu->id);
        if (bi->firmware)
            esk_json_set_string(s->a, b, "image", esk_json_get_str(bi->firmware, "image", ""));
        esk_json_set_string(s->a, b, "timing_tier", live ? "instruction" : "none");
        esk_json_array_push(s->a, boards, b);
        esk_json_set_string(s->a, tiers, bi->id, live ? "instruction" : "none");
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

/* the world as it is now (ADR-026): its fields (value, unit, declared) and bodies */
static es_status m_world_get(rpc_ctx *r)
{
    r->result = esk_world_json(r->s->a, &r->s->eng.world);
    return r->result ? ES_OK : ES_ENOMEM;
}

/* a board's console or a terminal part as a serial port on the host (ADR-031) */
static es_status m_port_open(rpc_ctx *r)
{
    const char *target = pstr(r, "target", NULL);
    const struct esk_host_port *hp;
    char why[160];
    es_status st;
    if (!target)
        return fail(r, -32602, "target is required");
    why[0] = 0;
    st = esk_session_port_open(r->s, target, &hp, why, sizeof why);
    if (st != ES_OK) {
        fail_status(r, st);
        if (why[0])
            esk_snprintf(r->err_msg, sizeof r->err_msg, "%s", why);
        return st;
    }
    r->result = esk_session_port_json(r->s, hp);
    return r->result ? ES_OK : ES_ENOMEM;
}

static es_status m_port_close(rpc_ctx *r)
{
    const char *target = pstr(r, "target", NULL);
    if (!target)
        return fail(r, -32602, "target is required");
    if (esk_session_port_close(r->s, target) != ES_OK)
        return fail_status(r, ES_ENOTFOUND);
    r->result = obj(r);
    return r->result ? ES_OK : ES_ENOMEM;
}

static es_status m_port_list(rpc_ctx *r)
{
    esk_json *arr = esk_json_new_array(r->s->a);
    size_t i;
    r->result = obj(r);
    if (!r->result || !arr) {
        esk_json_free(r->s->a, arr);
        return ES_ENOMEM;
    }
    for (i = 0; i < r->s->host_ports.len; i++)
        esk_json_array_push(r->s->a, arr, esk_session_port_json(r->s, r->s->host_ports.data[i]));
    esk_json_object_set(r->s->a, r->result, "ports", arr);
    return ES_OK;
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
                double w;
                if (cur) esk_json_object_set(s->a, o, "value", esk_json_clone(s->a, cur));
                else if (esk_comp_world_input(c, esk_json_get_str(in, "name", ""), &w)) {
                    /* read from the world (ADR-026) */
                    esk_json_set_number(s->a, o, "value", w);
                    esk_json_set_string(s->a, o, "source", "world");
                }
                if (esk_json_get(in, "world")) esk_json_set_string(s->a, o, "world", esk_json_get_str(in, "world", ""));
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
    const char *kind = pstr(r, "kind", NULL);
    const esk_catalog_item *it;
    esk_json *files;
    if (!id) return fail(r, -32602, "id is required");
    /* without a kind: a component, else a board, else an MCU */
    it = esk_catalog_find(r->s->catalog, kind ? kind : "component", id);
    if (!it && !kind)
        it = esk_catalog_find(r->s->catalog, "board", id);
    if (!it && !kind)
        it = esk_catalog_find(r->s->catalog, "mcu", id);
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

/* ---- the firmware's coverage and profile (SPEC-008 §5 analysis) ---------------------------- */

static es_status m_analysis_start(rpc_ctx *r)
{
    es_session *s = r->s;
    char why[160];
    unsigned what = (esk_json_get_bool(r->params, "coverage", 1) ? ESK_AN_COVERAGE : 0u) |
                    (esk_json_get_bool(r->params, "profile", 1) ? ESK_AN_PROFILE : 0u) |
                    (esk_json_get_bool(r->params, "stack", 0) ? ESK_AN_STACK : 0u) |
                    (esk_json_get_bool(r->params, "faults", 0) ? ESK_AN_FAULT : 0u);
    es_status st;
    if (!s->loaded)
        return fail(r, -32001, "no project is loaded");
    if (!what)
        return fail(r, -32602, "analysis.start needs coverage, profile, stack or faults");
    st = esk_session_analyze(s, pstr(r, "board", NULL), what, why, sizeof why);
    if (st == ES_ENOMEM)
        return st;
    if (st != ES_OK)
        return fail(r, -32602, why);
    r->result = obj(r);
    if (r->result) {
        esk_json_set_bool(s->a, r->result, "coverage", (what & ESK_AN_COVERAGE) != 0);
        esk_json_set_bool(s->a, r->result, "profile", (what & ESK_AN_PROFILE) != 0);
        esk_json_set_bool(s->a, r->result, "stack", (what & ESK_AN_STACK) != 0);
        esk_json_set_bool(s->a, r->result, "faults", (what & ESK_AN_FAULT) != 0);
    }
    return r->result ? ES_OK : ES_ENOMEM;
}

static es_status m_analysis_stop(rpc_ctx *r)
{
    char why[160];
    (void)esk_session_analyze(r->s, NULL, 0, why, sizeof why);
    r->result = obj(r);
    return r->result ? ES_OK : ES_ENOMEM;
}

/* analysis.coverage, analysis.profile, analysis.stack and analysis.faults:
 * JSON, or a report as text */
static es_status analysis_report(rpc_ctx *r, unsigned bit)
{
    es_session *s = r->s;
    const char *fmt = pstr(r, "format", "json"), *board = pstr(r, "board", NULL);
    int coverage = bit == ESK_AN_COVERAGE, profile = bit == ESK_AN_PROFILE;
    int f;
    if (!(s->an_what & bit)) {
        char why[64];
        esk_snprintf(why, sizeof why, "%s is not on (analysis.start)", esk_session_analysis_name(bit));
        return fail(r, -32001, why);
    }
    if (esk_str_eq(fmt, "json")) {
        r->result = obj(r);
        if (r->result)
            esk_json_object_set(s->a, r->result, "boards",
                                esk_session_analysis_json(s, board, bit, esk_json_get_bool(r->params, "detail", 1)));
        return r->result ? ES_OK : ES_ENOMEM;
    }
    f = esk_str_eq(fmt, "text") ? ESK_AN_TEXT
      : coverage && esk_str_eq(fmt, "lcov") ? ESK_AN_LCOV
      : profile && esk_str_eq(fmt, "collapsed") ? ESK_AN_COLLAPSED
      : profile && esk_str_eq(fmt, "collapsed-insns") ? ESK_AN_COLLAPSED_INSNS : -1;
    if (f < 0)
        return fail(r, -32602, "no such format");
    {
        esk_strbuf b;
        char why[160];
        es_status st;
        esk_strbuf_init(&b, s->a);
        st = esk_session_analysis_text(s, board, bit, f, &b, why, sizeof why);
        if (st == ES_OK && !b.oom) {
            r->result = obj(r);
            if (r->result)
                esk_json_set_string(s->a, r->result, "text", b.data ? b.data : "");
        }
        esk_strbuf_free(&b);
        if (st != ES_OK && st != ES_ENOMEM)
            return fail(r, -32602, why);
    }
    return r->result ? ES_OK : ES_ENOMEM;
}

static es_status m_analysis_coverage(rpc_ctx *r) { return analysis_report(r, ESK_AN_COVERAGE); }
static es_status m_analysis_profile(rpc_ctx *r) { return analysis_report(r, ESK_AN_PROFILE); }
static es_status m_analysis_stack(rpc_ctx *r) { return analysis_report(r, ESK_AN_STACK); }
static es_status m_analysis_faults(rpc_ctx *r) { return analysis_report(r, ESK_AN_FAULT); }

/* ---- record, scenario, diagnostics -------------------------------------------------------- */

static es_status m_record_start(rpc_ctx *r)
{
    es_session *s = r->s;
    esk_record_start(s);
    if (s->eng.sched.now == 0 && !s->input_seq) {
        /* nothing has run: from reset, as a replay starts (the reset logs the
         * probes' levels) */
        (void)esk_session_do_reset(s);
    } else {
        s->record_from = s->eng.sched.now; /* replay compares from here */
        s->record_from_seq = s->input_seq;
        esk_record_levels(s, s->record_from); /* the digital probes' levels as it starts */
    }
    esk_strfree(s->a, s->record_path);
    s->record_path = pstr(r, "path", NULL) ? esk_strdup(s->a, pstr(r, "path", NULL)) : NULL;
    r->result = obj(r);
    if (r->result)
        esk_json_set_u64_string(s->a, r->result, "from_ps", s->record_from);
    return r->result ? ES_OK : ES_ENOMEM;
}

static es_status m_record_stop(rpc_ctx *r)
{
    es_session *s = r->s;
    esk_json *report, *rec;
    r->result = obj(r);
    if (!r->result) return ES_ENOMEM;
    report = esk_session_report(s, "stopped", NULL);
    rec = report ? esk_record_document(s, s->project_path, NULL, report, NULL) : NULL;
    esk_json_free(s->a, report);
    s->recording = 0;
    if (!rec) return ES_ENOMEM;
    esk_json_object_set(s->a, r->result, "inputs", esk_json_clone(s->a, s->inputs_log));
    esk_json_set_integer(s->a, r->result, "count", (int64_t)esk_json_len(s->inputs_log));
    esk_json_set_integer(s->a, r->result, "observations", (int64_t)esk_json_len(esk_json_get(rec, "observations")));
    if (s->record_path) {
        size_t len;
        char *text = esk_json_format(s->a, rec, 1, &len), h[72];
        if (text) {
            esk_sha256_text(text, len, h);
            if (esk_host_write_file(s->record_path, text, len) == ES_OK) {
                esk_json_set_string(s->a, r->result, "path", s->record_path);
                esk_json_set_string(s->a, r->result, "hash", h);
            }
            esk_free(s->a, text, len + 1);
        }
    } else {
        esk_json_object_set(s->a, r->result, "recording", rec);
        rec = NULL;
    }
    esk_json_free(s->a, rec);
    return ES_OK;
}

/* the recording at `path`: its project loaded into this session, replayed */
/* the recording a request names: {path} read from a file, or {recording}
 * itself; NULL after fail() */
static esk_json *request_recording(rpc_ctx *r)
{
    es_session *s = r->s;
    const char *path = pstr(r, "path", NULL);
    const esk_json *inline_rec = esk_json_get(r->params, "recording");
    char *text = NULL;
    size_t len = 0;
    int line;
    const char *err;
    esk_json *rec;
    if (inline_rec) {
        rec = esk_json_clone(s->a, inline_rec);
    } else {
        if (!path) {
            fail(r, -32602, "a recording's path or the recording is required");
            return NULL;
        }
        if (esk_host_read_file(s->a, path, &text, &len) != ES_OK) {
            fail(r, -32602, "the recording cannot be read");
            return NULL;
        }
        rec = esk_json_parse(s->a, text, len, &line, &err);
        esk_free(s->a, text, len + 1);
    }
    if (!rec || !esk_str_eq(esk_json_get_str(rec, "kind", ""), "recording")) {
        esk_json_free(s->a, rec);
        fail(r, -32602, "not a recording");
        return NULL;
    }
    return rec;
}

static es_status m_record_to_scenario(rpc_ctx *r)
{
    es_session *s = r->s;
    esk_json *rec = request_recording(r), *sc;
    const esk_json *what = esk_json_get(r->params, "assert");
    int probes = 1, serial = 1;
    size_t i;
    if (!rec)
        return ES_EPROTO;
    if (what) {
        probes = serial = 0;
        for (i = 0; i < esk_json_len(what); i++) {
            probes |= esk_str_eq(esk_json_str(esk_json_at(what, i)), "probes");
            serial |= esk_str_eq(esk_json_str(esk_json_at(what, i)), "serial");
        }
    }
    sc = esk_record_to_scenario(s->a, rec, pstr(r, "name", NULL), probes, serial);
    esk_json_free(s->a, rec);
    if (!sc)
        return ES_ENOMEM;
    r->result = obj(r);
    if (!r->result) {
        esk_json_free(s->a, sc);
        return ES_ENOMEM;
    }
    esk_json_object_set(s->a, r->result, "scenario", sc);
    return ES_OK;
}

static es_status m_record_replay(rpc_ctx *r)
{
    es_session *s = r->s;
    const char *path = pstr(r, "path", NULL);
    char *dir, *proj;
    esk_json *rec = request_recording(r);
    es_status st;
    if (!rec)
        return ES_EPROTO;
    proj = esk_strdup(s->a, esk_json_get_str(esk_json_get(rec, "project"), "path", ""));
    if (proj && path && !esk_host_file_exists(proj)) {
        dir = esk_host_dirname(s->a, path);
        esk_strfree(s->a, proj);
        proj = esk_host_path_join(s->a, dir ? dir : ".", esk_json_get_str(esk_json_get(rec, "project"), "path", ""));
        esk_strfree(s->a, dir);
    }
    st = proj ? es_session_load_file(s, proj) : ES_ENOMEM;
    esk_strfree(s->a, proj);
    if (st != ES_OK) {
        esk_json_free(s->a, rec);
        return fail(r, -32000, "the recording's project does not load");
    }
    st = esk_record_replay(s, rec, esk_json_get_bool(r->params, "allow_drift", 0), &r->result);
    esk_json_free(s->a, rec);
    if (st == ES_ESTATE)
        return ES_OK; /* the result says the project or an image changed */
    return st;
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

/* a scenario file of the project: a relative path inside the project's
 * folder (no "..", not absolute), as its `scenarios` list names them */
static char *scenario_path(rpc_ctx *r, const char *path)
{
    const char *p;
    if (!path || !path[0] || path[0] == '/' || path[0] == '\\' || strchr(path, ':'))
        return NULL;
    for (p = path; *p; p++)
        if (p[0] == '.' && p[1] == '.' && (p == path || p[-1] == '/' || p[-1] == '\\'))
            return NULL;
    return esk_host_path_join(r->s->a, r->s->base_dir ? r->s->base_dir : ".", path);
}

static es_status m_scenario_get(rpc_ctx *r)
{
    es_session *s = r->s;
    const char *path = pstr(r, "path", NULL);
    char *full = scenario_path(r, path), *text = NULL;
    size_t len = 0;
    int line;
    const char *err;
    esk_json *sc;
    if (!full)
        return fail(r, -32602, "a scenario path inside the project's folder is required");
    if (esk_host_read_file(s->a, full, &text, &len) != ES_OK) {
        esk_strfree(s->a, full);
        return fail_status(r, ES_EIO);
    }
    esk_strfree(s->a, full);
    sc = esk_json_parse(s->a, text, len, &line, &err);
    esk_free(s->a, text, len + 1);
    if (!sc)
        return fail(r, -32602, err);
    r->result = obj(r);
    if (!r->result) {
        esk_json_free(s->a, sc);
        return ES_ENOMEM;
    }
    esk_json_set_string(s->a, r->result, "path", path);
    esk_json_object_set(s->a, r->result, "scenario", sc);
    return ES_OK;
}

/* scenario.save({path, scenario, add?}): written with two-space indents; with
 * `add`, the project's `scenarios` list gains the path (project.save keeps it) */
static es_status m_scenario_save(rpc_ctx *r)
{
    es_session *s = r->s;
    const char *path = pstr(r, "path", NULL);
    const esk_json *sc = esk_json_get(r->params, "scenario");
    char *full, *text;
    size_t len = 0, i;
    es_status st;
    esk_strbuf b;
    if (!sc || !esk_json_is(sc, ESK_JSON_OBJECT))
        return fail(r, -32602, "scenario is required");
    full = scenario_path(r, path);
    if (!full)
        return fail(r, -32602, "a scenario path inside the project's folder is required");
    esk_strbuf_init(&b, s->a);
    text = esk_json_write(&b, sc, 2) == 0 && esk_strbuf_putc(&b, '\n') == 0 ? esk_strbuf_detach(&b, &len) : NULL;
    if (!text) {
        esk_strbuf_free(&b);
        esk_strfree(s->a, full);
        return ES_ENOMEM;
    }
    st = esk_host_write_file(full, text, len);
    esk_free(s->a, text, len + 1);
    esk_strfree(s->a, full);
    if (st != ES_OK)
        return fail_status(r, ES_EIO);
    if (esk_json_get_bool(r->params, "add", 0) && s->project) {
        esk_json *list = (esk_json *)esk_json_get(s->project, "scenarios");
        int found = 0;
        if (!list) {
            esk_json_object_set(s->a, s->project, "scenarios", esk_json_new_array(s->a));
            list = (esk_json *)esk_json_get(s->project, "scenarios");
        }
        for (i = 0; list && i < esk_json_len(list); i++)
            found |= esk_str_eq(esk_json_str(esk_json_at(list, i)), path);
        if (list && !found)
            esk_json_array_push(s->a, list, esk_json_new_string(s->a, path));
    }
    r->result = obj(r);
    if (r->result)
        esk_json_set_string(s->a, r->result, "path", path);
    return r->result ? ES_OK : ES_ENOMEM;
}

static es_status m_diagnostics_list(rpc_ctx *r)
{
    r->result = obj(r);
    if (!r->result) return ES_ENOMEM;
    esk_json_object_set(r->s->a, r->result, "records", esk_diag_to_json(&r->s->diag));
    return ES_OK;
}

static es_status m_toolchain_detect(rpc_ctx *r)
{
    esk_json *arr = NULL;
    es_status st = esk_toolchain_detect(r->s, &arr);
    if (st != ES_OK) return fail_status(r, st);
    r->result = obj(r);
    if (!r->result) {
        esk_json_free(r->s->a, arr);
        return ES_ENOMEM;
    }
    esk_json_object_set(r->s->a, r->result, "profiles", arr);
    return ES_OK;
}

static es_status m_toolchain_build(rpc_ctx *r)
{
    es_status st = esk_toolchain_build(r->s, pstr(r, "board", NULL), pstr(r, "profile", NULL), &r->result);
    if (st == ES_ENOTFOUND) return fail(r, -32602, "no such board with firmware, or no such toolchain profile");
    if (st != ES_OK) return fail_status(r, st);
    esk_session_emit_observations(r->s, 1);
    return ES_OK;
}

/* ---- project editing ------------------------------------------------------------------ */

/* the diagnostics raised since `before`, as a JSON list */
static esk_json *diags_since(es_session *s, size_t before)
{
    esk_json *all = esk_diag_to_json(&s->diag), *recent = esk_json_new_array(s->a);
    size_t i;
    for (i = before; all && recent && i < esk_json_len(all); i++)
        esk_json_array_push(s->a, recent, esk_json_clone(s->a, esk_json_at(all, i)));
    esk_json_free(s->a, all);
    return recent;
}

/* begin, edit, commit, abort (SPEC-008 §5). Edits collect on a copy of
 * the project; a commit that only moves or draws keeps the run going, one
 * that changes the circuit is checked in a scratch session first, then
 * rebuilds this one from the edited project and restarts it at time 0. */
static es_status m_project_transaction(rpc_ctx *r)
{
    es_session *s = r->s;
    const char *op = pstr(r, "op", "");
    const esk_json *txj = esk_json_get(r->params, "tx");
    /* the id is a string (SPEC-008); a number is accepted too */
    int tx = txj && txj->type == ESK_JSON_STRING ? atoi(esk_json_str(txj)) : (int)esk_json_get_int(r->params, "tx", 0);
    if (esk_str_eq(op, "begin")) {
        if (!s->loaded || !s->project)
            return fail_status(r, ES_ESTATE);
        if (s->tx_project)
            return fail_status(r, ES_ESTATE); /* one transaction at a time */
        s->tx_project = esk_json_clone(s->a, s->project);
        if (!s->tx_project)
            return ES_ENOMEM;
        s->tx_id = ++s->tx_last;
        s->tx_structural = 0;
        r->result = obj(r);
        if (!r->result) return ES_ENOMEM;
        {
            char id[16];
            esk_snprintf(id, sizeof id, "%d", s->tx_id);
            esk_json_set_string(s->a, r->result, "tx", id);
        }
        return ES_OK;
    }
    if (!s->tx_project || tx != s->tx_id)
        return fail(r, -32602, "no such open transaction");
    if (esk_str_eq(op, "edit")) {
        const esk_json *edits = esk_json_get(r->params, "edits");
        char why[256];
        size_t i;
        if (!edits || edits->type != ESK_JSON_ARRAY)
            return fail(r, -32602, "edits is a list");
        for (i = 0; i < esk_json_len(edits); i++) {
            why[0] = 0;
            if (esk_project_apply_edit(s->a, s->tx_project, esk_json_at(edits, i), &s->tx_structural, why, sizeof why))
                return fail(r, -32602, why[0] ? why : "the edit could not be applied");
        }
        r->result = obj(r);
        return r->result ? ES_OK : ES_ENOMEM;
    }
    if (esk_str_eq(op, "abort")) {
        esk_json_free(s->a, s->tx_project);
        s->tx_project = NULL;
        r->result = obj(r);
        return r->result ? ES_OK : ES_ENOMEM;
    }
    if (esk_str_eq(op, "commit")) {
        esk_json *edited = s->tx_project;
        size_t before = s->diag.len;
        int applied = 1, restarted = 0;
        s->tx_project = NULL;
        {
            char why[256];
            if (esk_project_validate_organization(edited, why, sizeof why) ||
                esk_project_validate_bench(s->catalog, edited, why, sizeof why)) {
                esk_diag_raise(&s->diag, ES_SEV_ERROR, "bench.invalid", s->eng.sched.now,
                               "project", s->name ? s->name : "", NULL, 0, "%s", why);
                esk_json_free(s->a, edited);
                return fail_status(r, ES_ESCHEMA);
            }
        }
        if (!s->tx_structural) {
            esk_json *was = s->project;
            s->project = edited;
            edited = NULL;
            esk_session_world_sync(s, was);
            esk_json_free(s->a, was);
        } else {
            es_session *tmp = NULL;
            es_session_config_t cfg;
            es_status st;
            memset(&cfg, 0, sizeof cfg);
            cfg.catalog = s->catalog;
            st = es_session_create(&cfg, &tmp);
            if (st == ES_OK)
                st = esk_project_load(tmp, edited, s->base_dir);
            if (st != ES_OK || esk_diag_count(&tmp->diag, ES_SEV_ERROR) > 0) {
                /* the edited project does not load: report why, keep the old one */
                esk_json *d = tmp ? esk_diag_to_json(&tmp->diag) : NULL;
                size_t i;
                applied = 0;
                for (i = 0; d && i < esk_json_len(d); i++) {
                    const esk_json *x = esk_json_at(d, i);
                    if (esk_str_eq(esk_json_get_str(x, "severity", ""), "error"))
                        esk_diag_raise(&s->diag, ES_SEV_ERROR, esk_json_get_str(x, "code", "project.edit"), s->eng.sched.now,
                                       "project", s->name ? s->name : "", NULL, 0, "%s", esk_json_get_str(x, "message", ""));
                }
                esk_json_free(tmp ? tmp->a : s->a, d);
            }
            es_session_destroy(tmp);
            if (applied) {
                s->running = 0;
                st = esk_project_load(s, edited, s->base_dir);
                if (st == ES_OK)
                    st = esk_session_do_reset(s);
                if (st != ES_OK) {
                    esk_json_free(s->a, edited);
                    return fail_status(r, st);
                }
                restarted = 1;
            }
        }
        esk_json_free(s->a, edited);
        r->result = obj(r);
        if (!r->result) return ES_ENOMEM;
        esk_json_set_bool(s->a, r->result, "applied", applied);
        esk_json_set_bool(s->a, r->result, "restarted", restarted);
        esk_json_object_set(s->a, r->result, "diagnostics", diags_since(s, before));
        esk_session_emit_observations(s, 1);
        if (!applied)
            return fail_status(r, ES_ESCHEMA); /* the edited project does not load: its diagnostics say why */
        return ES_OK;
    }
    return fail(r, -32602, "op is begin, edit, commit or abort");
}

/* writes the project's JSON to `path` (relative to the project's folder) or
 * to the file it was loaded from */
static es_status m_project_save(rpc_ctx *r)
{
    es_session *s = r->s;
    const char *path = pstr(r, "path", NULL);
    char *full, *text;
    size_t len = 0;
    es_status st;
    if (!s->project)
        return fail_status(r, ES_ESTATE);
    if (!path)
        path = s->project_path;
    if (!path)
        return fail(r, -32602, "the project has no file: give a path");
    full = path[0] != '/' && s->base_dir && path != s->project_path ? esk_host_path_join(s->a, s->base_dir, path)
                                                                     : esk_strdup(s->a, path);
    {
        esk_strbuf b;
        esk_strbuf_init(&b, s->a);
        text = full && esk_json_write(&b, s->project, 2) == 0 && esk_strbuf_putc(&b, '\n') == 0 ? esk_strbuf_detach(&b, &len) : NULL;
        if (!text)
            esk_strbuf_free(&b);
    }
    if (!text) {
        esk_strfree(s->a, full);
        return ES_ENOMEM;
    }
    st = esk_host_write_file(full, text, len);
    esk_free(s->a, text, len + 1);
    if (st != ES_OK) {
        esk_strfree(s->a, full);
        return fail_status(r, ES_EIO);
    }
    r->result = obj(r);
    if (r->result) esk_json_set_string(s->a, r->result, "path", full);
    esk_strfree(s->a, full);
    return r->result ? ES_OK : ES_ENOMEM;
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
    { "project.topology", m_project_topology }, { "project.transaction", m_project_transaction }, { "project.save", m_project_save },
    { "control.run", m_control_run }, { "control.pause", m_control_pause }, { "control.step", m_control_step },
    { "control.advance", m_control_advance }, { "control.reset", m_control_reset }, { "control.speed", m_control_speed },
    { "world.get", m_world_get },
    { "port.open", m_port_open }, { "port.close", m_port_close }, { "port.list", m_port_list },
    { "input.set", m_input_set }, { "input.press", m_input_press }, { "input.release", m_input_release },
    { "input.serial", m_input_serial }, { "input.list", m_input_list },
    { "probe.add", m_probe_add }, { "probe.remove", m_probe_remove }, { "probe.list", m_probe_list }, { "probe.read", m_probe_read },
    { "waveform.subscribe", m_waveform_subscribe }, { "waveform.unsubscribe", m_waveform_unsubscribe },
    { "observe.snapshot", m_observe_snapshot }, { "observe.subscribe", m_observe_subscribe }, { "observe.unsubscribe", m_observe_unsubscribe },
    { "catalog.list", m_catalog_list }, { "catalog.get", m_catalog_get }, { "catalog.libraries", m_catalog_libraries },
    { "catalog.models", m_catalog_models }, { "catalog.validate", m_unavailable },
    { "analysis.start", m_analysis_start }, { "analysis.stop", m_analysis_stop },
    { "analysis.coverage", m_analysis_coverage }, { "analysis.profile", m_analysis_profile },
    { "analysis.stack", m_analysis_stack }, { "analysis.faults", m_analysis_faults },
    { "record.start", m_record_start }, { "record.stop", m_record_stop }, { "record.replay", m_record_replay },
    { "record.to_scenario", m_record_to_scenario },
    { "scenario.run", m_scenario_run }, { "scenario.get", m_scenario_get }, { "scenario.save", m_scenario_save },
    { "diagnostics.list", m_diagnostics_list },
    { "toolchain.detect", m_toolchain_detect }, { "toolchain.build", m_toolchain_build },
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
