/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* loader.c -- from a project description to a wired engine (02 §5, SPEC-005,
 * SPEC-006 §3, SPEC-007 §1): libraries, components with parameters and pin
 * models, boards as composites with a placeholder MCU (no runtime in M0),
 * nets merged through board ports and functional wires, probes, settings. */
#include "project/loader.h"
#include "project/hierarchy.h"
#include "project/library_lock.h"
#include "ext/lua_model.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "host/host.h"
#include "mcu/mcu.h"
#include "util/num.h"
#include "util/sha256.h"
#include "util/str.h"

/* ---- diagnostics --------------------------------------------------------- */

static void ld_diag(es_session *s, es_severity_t sev, const char *code, const char *subject, const char *fmt, const char *a1, const char *a2)
{
    esk_diag_raise(&s->diag, sev, code, 0, "component", subject, s->project_path, 0, fmt, a1, a2);
}

int esk_split_pin_ref(const char *ref, char *inst, size_t icap, char *pin, size_t pcap)
{
    const char *dot = strrchr(ref, '.');
    size_t n;
    if (!dot || dot == ref || !dot[1])
        return -1;
    n = (size_t)(dot - ref);
    if (n + 1 > icap || strlen(dot + 1) + 1 > pcap)
        return -1;
    memcpy(inst, ref, n);
    inst[n] = 0;
    esk_snprintf(pin, pcap, "%s", dot + 1);
    return 0;
}

/* ---- the placeholder MCU (M0: no runtime) ---------------------------------- */

static const esk_model_ops mcu_placeholder_ops = { "mcu_placeholder", 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0 };
/* a composite's shell: its pins redirect to internal pins through the ports (SPEC-005 §5) */
static const esk_model_ops composite_shell_ops = { "composite", 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0 };

typedef struct graph graph;
static es_status instantiate(es_session *s, graph *g, const char *id, const esk_catalog_item *item, const esk_json *inst_params, int depth, esk_comp **out);
static esk_pin *resolve_pin_ref(es_session *s, const char *full, int depth);
static int resolve_via_port; /* set by resolve_pin_ref when the reference went through a composite port */
/* connect a pin to a net record; a pin reached through a composite port whose internal net already
 * holds it merges the two records instead (the port is the same node seen from both sides) */
static es_status connect_or_union(graph *g, esk_pin *pin, int rec, int via_port);
static void rec_union(graph *g, int x, int y);
static int rec_get(graph *g, const char *name, int project_level);

/* an MCU description's io.electrical: its supply sets the GPIO pins' high
 * level and pulls (a 3.0 V nRF52840 on its DK); the logic family's levels
 * and thresholds stay, since the datasheets' VOH and VOL are worst cases
 * at full load. Scalar resistance/slew describe every GPIO setting; arrays
 * remain the runtime's per-speed overrides. */
static void mcu_electrical(const esk_json *el, esk_pin_model *m)
{
    double v;
    if (el && esk_json_get(el, "vdd") && esk_json_to_double(esk_json_get(el, "vdd"), &v) == 0 && v > 0)
        m->nominal_v = v;
    if (esk_json_to_double(esk_json_get(el, "rout"), &v) == 0 && isfinite(v) && v > 0)
        m->rout = v;
    if (esk_json_to_double(esk_json_get(el, "slew_v_per_us"), &v) == 0 && isfinite(v) && v > 0)
        m->slew_v_per_us = v;
}

static void mcu_signal_model(const char *sig, const char *family, esk_pin_model *m)
{
    esk_pin_model_family(family, m);
    if (!strncmp(sig, "VSS", 3) || !strcmp(sig, "GND") || !strcmp(sig, "AVSS")) {
        m->kind = ESK_PIN_POWER; m->power_ground = 1; m->nominal_v = 0.0;
    } else if (!strncmp(sig, "VDD", 3) || !strcmp(sig, "VBAT") || !strcmp(sig, "VCC") || !strcmp(sig, "VREF+") ||
               !strcmp(sig, "AVCC") || !strcmp(sig, "AVDD") || !strcmp(sig, "VUSB3V3") || !strcmp(sig, "VCAP")) {
        m->kind = ESK_PIN_POWER; m->power_ground = 0;
    } else if ((sig[0] == 'P' && sig[1] >= 'A' && sig[1] <= 'Z' && sig[2] >= '0' && sig[2] <= '9') ||
               (sig[0] == 'R' && sig[1] >= 'A' && sig[1] <= 'K' && sig[2] >= '0' && sig[2] <= '9') ||
               (!strncmp(sig, "GPIO", 4) && sig[4] >= '0' && sig[4] <= '9')) {
        m->kind = ESK_PIN_DIGITAL_IO; m->pull = ESK_PULL_NONE;
    } else if (!strncmp(sig, "OSC", 3) || !strncmp(sig, "XTAL", 4) || !strcmp(sig, "AREF") ||
               !strncmp(sig, "VCAP_", 5)) {
        /* VCAP_1, VCAP_2: the core regulator's capacitors, the regulator not modelled */
        m->kind = ESK_PIN_ANALOG;
    } else if (!strcmp(sig, "NC")) {
        m->kind = ESK_PIN_NC;
    } else if (!strcmp(sig, "NRST")) {
        m->kind = ESK_PIN_DIGITAL_IN; m->pull = ESK_PULL_UP; /* its internal pull-up */
    } else {
        m->kind = ESK_PIN_DIGITAL_IN; m->pull = ESK_PULL_NONE; /* BOOT0 and the like: the board sets them */
    }
}

/* ---- parameters ------------------------------------------------------------ */

/* a resolved number parameter: an integer parameter stays an integer (scripted
 * models see 4, not 4.0) */
static esk_json *num_param(esk_alloc *a, const char *type, double d)
{
    if (!strcmp(type, "integer") && d >= -9.0e15 && d <= 9.0e15 && d == (double)(int64_t)d)
        return esk_json_new_integer(a, (int64_t)d);
    return esk_json_new_number(a, d);
}

static es_status build_params(es_session *s, const char *id, const esk_json *desc, const esk_json *inst_params, esk_json **out)
{
    esk_json *params = esk_json_new_object(s->a);
    const esk_json *pdefs = esk_json_get(desc, "params");
    size_t i;
    es_status st = ES_OK;
    if (!params)
        return ES_ENOMEM;
    /* defaults */
    for (i = 0; i < esk_json_len(pdefs); i++) {
        const esk_json *pd = esk_json_at(pdefs, i);
        const esk_json *def = esk_json_get(pd, "default");
        if (def) {
            esk_json *v;
            double d;
            const char *type = esk_json_get_str(pd, "type", "number");
            if ((!strcmp(type, "number") || !strcmp(type, "integer")) && esk_json_to_double(def, &d) == 0)
                v = num_param(s->a, type, d);
            else
                v = esk_json_clone(s->a, def);
            if (!v || esk_json_object_set(s->a, params, pd->key, v)) {
                esk_json_free(s->a, v);
                st = ES_ENOMEM;
            }
        }
    }
    /* instance values */
    for (i = 0; i < esk_json_len(inst_params); i++) {
        const esk_json *iv = esk_json_at(inst_params, i);
        const esk_json *pd = esk_json_get(pdefs, iv->key);
        esk_json *v = NULL;
        const char *type;
        double d;
        if (!pd) {
            ld_diag(s, ES_SEV_ERROR, "schema.unknown_field", id, "%s has no parameter named %s", esk_json_get_str(desc, "id", "?"), iv->key);
            st = ES_ESCHEMA;
            continue;
        }
        type = esk_json_get_str(pd, "type", "number");
        if (!strcmp(type, "number") || !strcmp(type, "integer")) {
            double mn, mx;
            if (esk_json_to_double(iv, &d) != 0) {
                ld_diag(s, ES_SEV_ERROR, "schema.type", id, "parameter %s of %s must be a number", iv->key, id);
                st = ES_ESCHEMA;
                continue;
            }
            if ((esk_json_to_double(esk_json_get(pd, "min"), &mn) == 0 && d < mn) ||
                (esk_json_to_double(esk_json_get(pd, "max"), &mx) == 0 && d > mx)) {
                ld_diag(s, ES_SEV_ERROR, "analog.param_out_of_range", id, "parameter %s of %s is outside its declared range", iv->key, id);
                st = ES_ESCHEMA;
                continue;
            }
            v = num_param(s->a, type, d);
        } else if (!strcmp(type, "boolean")) {
            if (iv->type != ESK_JSON_BOOL) {
                ld_diag(s, ES_SEV_ERROR, "schema.type", id, "parameter %s of %s must be a boolean", iv->key, id);
                st = ES_ESCHEMA;
                continue;
            }
            v = esk_json_clone(s->a, iv);
        } else if (!strcmp(type, "enum")) {
            const esk_json *en = esk_json_get(pd, "enum");
            size_t k;
            int ok = 0;
            for (k = 0; k < esk_json_len(en); k++)
                if (esk_str_eq(esk_json_str(esk_json_at(en, k)), esk_json_str(iv)))
                    ok = 1;
            if (!ok) {
                ld_diag(s, ES_SEV_ERROR, "schema.type", id, "parameter %s of %s is not one of its enum values", iv->key, id);
                st = ES_ESCHEMA;
                continue;
            }
            v = esk_json_clone(s->a, iv);
        } else {
            v = esk_json_clone(s->a, iv);
        }
        if (!v || esk_json_object_set(s->a, params, iv->key, v)) {
            esk_json_free(s->a, v);
            st = ES_ENOMEM;
        }
    }
    /* required parameters */
    for (i = 0; i < esk_json_len(pdefs); i++) {
        const esk_json *pd = esk_json_at(pdefs, i);
        if (esk_json_get_bool(pd, "required", 1) && !esk_json_get(pd, "default") && !esk_json_get(params, pd->key)) {
            ld_diag(s, ES_SEV_ERROR, "schema.missing_field", id, "parameter %s of %s is required", pd->key, id);
            st = ES_ESCHEMA;
        }
    }
    /* enum-driven presets: a param definition may carry "presets": {value: {param: value}};
     * a parameter the instance gives itself is kept (a "generic" part with its own vout) */
    for (i = 0; i < esk_json_len(pdefs); i++) {
        const esk_json *pd = esk_json_at(pdefs, i);
        const esk_json *presets = esk_json_get(pd, "presets");
        const char *cur = esk_json_get_str(params, pd->key, NULL);
        const esk_json *preset = cur ? esk_json_get(presets, cur) : NULL;
        size_t k;
        for (k = 0; k < esk_json_len(preset); k++) {
            const esk_json *pv = esk_json_at(preset, k);
            esk_json *v;
            if (inst_params && esk_json_get(inst_params, pv->key))
                continue;
            v = esk_json_clone(s->a, pv);
            if (!v || esk_json_object_set(s->a, params, pv->key, v)) {
                esk_json_free(s->a, v);
                st = ES_ENOMEM;
            }
        }
    }
    if (st != ES_OK) {
        esk_json_free(s->a, params);
        return st;
    }
    *out = params;
    return ES_OK;
}

/* choose the model binding for the policy; returns the primitive name or NULL */
static const esk_json *choose_model(es_session *s, const esk_json *desc, const char **primitive)
{
    const esk_json *model = esk_json_get(desc, "model");
    const esk_json *functional = esk_json_get(desc, "functional");
    const esk_json *chosen;
    if (s->eng.policy == ESK_POLICY_ELECTRICAL)
        chosen = model ? model : functional;
    else
        chosen = functional ? functional : model;
    if (!chosen || chosen->type != ESK_JSON_OBJECT)
        return NULL;
    *primitive = esk_json_get_str(chosen, "primitive", NULL);
    return chosen;
}

/* substitute "$name" strings in a pin model object from params */
static esk_json *resolve_pin_model(es_session *s, const esk_json *model, const esk_json *params)
{
    esk_json *copy = esk_json_clone(s->a, model);
    size_t i;
    if (!copy)
        return NULL;
    for (i = 0; i < esk_json_len(copy); i++) {
        esk_json *m = copy->u.array.items[i];
        const char *str = esk_json_str(m);
        if (str && str[0] == '$') {
            const esk_json *pv = esk_json_get(params, str + 1);
            if (pv) {
                esk_json *v = esk_json_clone(s->a, pv);
                if (v)
                    esk_json_object_set(s->a, copy, m->key, v);
            }
        }
    }
    return copy;
}

/* substitute "$name" strings in an instance-parameter object from the parent's params */
static esk_json *subst_params(es_session *s, const esk_json *inst, const esk_json *parent)
{
    esk_json *copy = inst ? esk_json_clone(s->a, inst) : esk_json_new_object(s->a);
    size_t i;
    if (!copy)
        return NULL;
    for (i = 0; i < esk_json_len(copy); i++) {
        esk_json *m = copy->u.array.items[i];
        const char *str = esk_json_str(m);
        if (str && str[0] == '$') {
            const esk_json *pv = esk_json_get(parent, str + 1);
            esk_json *v = pv ? esk_json_clone(s->a, pv) : NULL;
            if (v)
                esk_json_object_set(s->a, copy, m->key, v);
        }
    }
    return copy;
}

/* expand a composite (SPEC-005 §5): internal components scoped under the shell's id, internal nets,
 * and a shell component whose pins redirect to internal pins through the ports */
static es_status expand_composite(es_session *s, graph *g, const char *id, const esk_catalog_item *item,
                                  const esk_json *composite, const esk_json *params, int depth, esk_comp **out)
{
    const esk_json *comps = esk_json_get(composite, "components");
    const esk_json *nets = esk_json_get(composite, "nets");
    const esk_json *ports = esk_json_get(composite, "ports");
    const esk_json *pins = esk_json_get(item->desc, "pins");
    size_t i, k, np = esk_json_len(pins);
    char scoped[256];
    es_status st = ES_OK;
    if (depth > 8) {
        ld_diag(s, ES_SEV_ERROR, "catalog.composite_depth", id, "%s nests composites deeper than 8 levels%s", item->id, "");
        return ES_ESCHEMA;
    }
    if (!g) {
        ld_diag(s, ES_SEV_ERROR, "catalog.model_unsupported", id, "%s is a composite and cannot be instantiated here%s", item->id, "");
        return ES_EUNSUPPORTED;
    }
    for (i = 0; i < esk_json_len(comps) && st == ES_OK; i++) {
        const esk_json *ce = esk_json_at(comps, i);
        const char *cid = esk_json_get_str(ce, "id", NULL);
        const char *ref = esk_json_get_str(ce, "component", NULL);
        const esk_catalog_item *ci = esk_project_component(s, ref);
        esk_json *sub_params;
        esk_comp *c = NULL;
        if (!cid || !ci) {
            ld_diag(s, ES_SEV_ERROR, "catalog.not_found", id, "composite member %s (%s) is not in any library", cid ? cid : "?", ref ? ref : "?");
            return ES_ENOTFOUND;
        }
        esk_snprintf(scoped, sizeof scoped, "%s.%s", id, cid);
        sub_params = subst_params(s, esk_json_get(ce, "params"), params);
        if (!sub_params)
            return ES_ENOMEM;
        st = instantiate(s, g, scoped, ci, sub_params, depth + 1, &c);
        esk_json_free(s->a, sub_params);
    }
    for (i = 0; i < esk_json_len(nets) && st == ES_OK; i++) {
        const esk_json *ne = esk_json_at(nets, i);
        int rec;
        esk_snprintf(scoped, sizeof scoped, "%s.%s", id, ne->key);
        rec = rec_get(g, scoped, 0);
        if (rec < 0)
            return ES_ENOMEM;
        for (k = 0; k < esk_json_len(ne) && st == ES_OK; k++) {
            const char *ref = esk_json_str(esk_json_at(ne, k));
            esk_pin *p;
            if (!ref) {
                st = ES_ESCHEMA;
                break;
            }
            esk_snprintf(scoped, sizeof scoped, "%s.%s", id, ref);
            p = resolve_pin_ref(s, scoped, 0);
            if (!p) {
                ld_diag(s, ES_SEV_ERROR, "catalog.pin_unknown", id, "unknown pin %s in composite net %s", ref, ne->key);
                st = ES_ESCHEMA;
                break;
            }
            st = connect_or_union(g, p, rec, resolve_via_port);
        }
    }
    if (st != ES_OK)
        return st;
    /* the shell: one NC pin per declared pin; ports are looked up on demand */
    {
        const char **names = (const char **)esk_calloc(s->a, np ? np : 1, sizeof *names);
        esk_pin_model *models = (esk_pin_model *)esk_calloc(s->a, np ? np : 1, sizeof *models);
        if (!names || !models) {
            esk_free(s->a, names, (np ? np : 1) * sizeof *names);
            esk_free(s->a, models, (np ? np : 1) * sizeof *models);
            return ES_ENOMEM;
        }
        for (i = 0; i < np; i++) {
            names[i] = esk_json_get_str(esk_json_at(pins, i), "name", "?");
            esk_pin_model_family(NULL, &models[i]);
            models[i].kind = ESK_PIN_NC;
            if (!esk_json_get(ports, names[i]))
                ld_diag(s, ES_SEV_WARNING, "catalog.port_missing", id, "composite %s has no port for pin %s", item->id, names[i]);
        }
        st = esk_engine_add_comp(&s->eng, id, item->id, &composite_shell_ops, names, models, (uint32_t)np, params, item->desc, out);
        esk_free(s->a, names, (np ? np : 1) * sizeof *names);
        esk_free(s->a, models, (np ? np : 1) * sizeof *models);
    }
    return st;
}

static esk_pin *resolve_pin_ref(es_session *s, const char *full, int depth)
{
    char inst[160], pin[64], internal[256];
    esk_comp *c;
    if (depth == 0)
        resolve_via_port = 0;
    if (depth > 8 || esk_split_pin_ref(full, inst, sizeof inst, pin, sizeof pin))
        return NULL;
    c = esk_engine_comp(&s->eng, inst);
    if (!c)
        return NULL;
    if (c->ops == &composite_shell_ops) {
        resolve_via_port = 1;
        const char *prim = NULL;
        const esk_json *binding = choose_model(s, c->desc, &prim);
        const esk_json *ports = esk_json_get(esk_json_get(binding, "composite"), "ports");
        const char *target = esk_json_get_str(ports, pin, NULL);
        if (!target) {
            /* the pin may be an alias */
            esk_pin *alias = esk_comp_pin(c, pin);
            target = alias ? esk_json_get_str(ports, alias->name, NULL) : NULL;
        }
        if (!target)
            return NULL;
        esk_snprintf(internal, sizeof internal, "%s.%s", inst, target);
        return resolve_pin_ref(s, internal, depth + 1);
    }
    return esk_comp_pin(c, pin);
}

esk_pin *esk_project_pin(es_session *s, const char *ref)
{
    return resolve_pin_ref(s, ref, 0);
}

static es_status instantiate(es_session *s, graph *g, const char *id, const esk_catalog_item *item, const esk_json *inst_params, int depth, esk_comp **out)
{
    const esk_json *desc = item->desc;
    const esk_json *pins = esk_json_get(desc, "pins");
    const char *family = esk_json_get_str(desc, "family", NULL);
    const char *primitive = NULL;
    const esk_json *binding = choose_model(s, desc, &primitive);
    const esk_model_ops *ops;
    esk_json *params = NULL;
    const char **names = NULL;
    esk_pin_model *models = NULL;
    size_t np = esk_json_len(pins), i;
    es_status st;
    if (!binding) {
        ld_diag(s, ES_SEV_ERROR, "catalog.model_unsupported", id, "%s declares no model usable under this policy%s", item->id, "");
        return ES_ESCHEMA;
    }
    if (!primitive) {
        const esk_json *comp = esk_json_get(binding, "composite");
        if (comp && esk_json_len(esk_json_get(comp, "components")) == 0) {
            /* an empty composite is a pin-only part (a supply symbol, a header): no behaviour */
            primitive = "none";
        } else if (comp) {
            st = build_params(s, id, desc, inst_params, &params);
            if (st != ES_OK)
                return st;
            st = expand_composite(s, g, id, item, comp, params, depth, out);
            esk_json_free(s->a, params);
            return st;
        } else if (esk_json_get_str(binding, "script", NULL)) {
            const char *script = esk_json_get_str(binding, "script", NULL);
            char why[512], name[256];
            size_t len = 0;
            char *src = esk_catalog_read_item_file(s->catalog, item, script, &len);
            if (!src) {
                ld_diag(s, ES_SEV_ERROR, "catalog.file_missing", id, "%s names the script %s, which is not in its directory", item->id, script);
                return ES_ENOTFOUND;
            }
            esk_snprintf(name, sizeof name, "@%s/%s", item->id, script);
            st = esk_lua_register(&s->eng, item->id, src, len, name, why, sizeof why);
            esk_free(s->catalog->a, src, len + 1);
            if (st != ES_OK) {
                ld_diag(s, ES_SEV_ERROR, st == ES_EUNSUPPORTED ? "catalog.model_unsupported" : "ext.error", id, "%s: %s", item->id, why);
                return st;
            }
            primitive = "lua";
        } else {
            ld_diag(s, ES_SEV_ERROR, "catalog.model_unsupported", id, "%s uses a native model, which this version does not load%s", item->id, "");
            return ES_EUNSUPPORTED;
        }
    }
    ops = esk_str_eq(primitive, "lua") && esk_json_get_str(binding, "script", NULL) ? esk_lua_ops() : esk_registry_find(&s->reg, primitive);
    if (!ops) {
        ld_diag(s, ES_SEV_ERROR, "catalog.model_unsupported", id, "%s binds unknown primitive %s", item->id, primitive);
        return ES_EUNSUPPORTED;
    }
    if (s->eng.policy == ESK_POLICY_FUNCTIONAL && esk_model_is_electrical_only(ops)) {
        ld_diag(s, ES_SEV_ERROR, "island.requires_electrical", id, "%s has no functional model (primitive %s is electrical)", item->id, primitive);
        return ES_EUNSUPPORTED;
    }
    st = build_params(s, id, desc, inst_params, &params);
    if (st != ES_OK)
        return st;
    /* bind: primitive_name <- component_param */
    {
        const esk_json *bind = esk_json_get(binding, "bind");
        for (i = 0; i < esk_json_len(bind); i++) {
            const esk_json *b = esk_json_at(bind, i);
            const esk_json *src = esk_json_get(params, esk_json_str(b));
            if (src) {
                esk_json *v = esk_json_clone(s->a, src);
                if (v)
                    esk_json_object_set(s->a, params, b->key, v);
            }
        }
        /* primitive-level constants in the binding: {"primitive": "pull", "params": {"level": 1}} */
        {
            const esk_json *cst = esk_json_get(binding, "params");
            for (i = 0; i < esk_json_len(cst); i++) {
                esk_json *v = esk_json_clone(s->a, esk_json_at(cst, i));
                if (v)
                    esk_json_object_set(s->a, params, esk_json_at(cst, i)->key, v);
            }
        }
    }
    /* timed section: tpd etc. become params under timed/electrical policies */
    if (s->eng.policy != ESK_POLICY_FUNCTIONAL) {
        const esk_json *timed = esk_json_get(desc, "timed");
        for (i = 0; i < esk_json_len(timed); i++) {
            const esk_json *tv = esk_json_at(timed, i);
            if (!esk_json_get(params, tv->key)) {
                esk_json *v = esk_json_clone(s->a, tv);
                if (v)
                    esk_json_object_set(s->a, params, tv->key, v);
            }
        }
    }
    names = (const char **)esk_calloc(s->a, np ? np : 1, sizeof *names);
    models = (esk_pin_model *)esk_calloc(s->a, np ? np : 1, sizeof *models);
    if (!names || !models) {
        st = ES_ENOMEM;
        goto done;
    }
    for (i = 0; i < np; i++) {
        const esk_json *pd = esk_json_at(pins, i);
        const esk_json *pm = esk_json_get(pd, "model");
        esk_json *resolved;
        names[i] = esk_json_get_str(pd, "name", NULL);
        if (!names[i] || !pm) {
            ld_diag(s, ES_SEV_ERROR, "schema.missing_field", id, "pin %u of %s needs a name and a model", "", item->id);
            st = ES_ESCHEMA;
            goto done;
        }
        resolved = resolve_pin_model(s, pm, params);
        if (!resolved || esk_pin_model_parse(resolved, family, &models[i]) != 0) {
            esk_json_free(s->a, resolved);
            ld_diag(s, ES_SEV_ERROR, "schema.type", id, "pin %s of %s has an invalid pin model", names[i], item->id);
            st = ES_ESCHEMA;
            goto done;
        }
        esk_json_free(s->a, resolved);
    }
    st = esk_engine_add_comp(&s->eng, id, item->id, ops, names, models, (uint32_t)np, params, desc, out);
    if (st != ES_OK) {
        ld_diag(s, ES_SEV_ERROR, "catalog.instantiate", id, "cannot instantiate %s: %s", item->id, es_strerror(st));
        goto done;
    }
    /* interactive input defaults; a parameter initially_NAME gives the input
     * NAME its first value on this instance (a jumper fitted at the factory) */
    {
        const esk_json *inputs = esk_json_get(desc, "inputs");
        for (i = 0; i < esk_json_len(inputs); i++) {
            const esk_json *in = esk_json_at(inputs, i);
            const esk_json *def = esk_json_get(in, "default");
            const char *name = esk_json_get_str(in, "name", NULL);
            char initially[96];
            if (name && esk_snprintf(initially, sizeof initially, "initially_%s", name) < sizeof initially &&
                esk_json_get(params, initially))
                def = esk_json_get(params, initially);
            /* an input bound to the world stays unset: it reads the world, or its default */
            if (name && def && !esk_json_get(in, "world")) {
                esk_json *v = esk_json_clone(s->a, def);
                if (v)
                    esk_json_object_set(s->a, (*out)->inputs, name, v);
            }
        }
    }
done:
    esk_json_free(s->a, params);
    esk_free(s->a, names, (np ? np : 1) * sizeof *names);
    esk_free(s->a, models, (np ? np : 1) * sizeof *models);
    return st;
}

/* ---- the net graph under construction -------------------------------------- */

typedef struct netrec { char *name; int project_level; int parent; } netrec;
typedef struct conn { esk_pin *pin; int rec; } conn;

struct graph {
    es_session *s;
    ESK_VEC(netrec) recs;
    ESK_VEC(conn) conns;
    esk_map rec_by_name;
};

static int rec_find(graph *g, int i)
{
    while (g->recs.data[i].parent != i) {
        g->recs.data[i].parent = g->recs.data[g->recs.data[i].parent].parent;
        i = g->recs.data[i].parent;
    }
    return i;
}

static void rec_union(graph *g, int x, int y)
{
    x = rec_find(g, x);
    y = rec_find(g, y);
    if (x == y)
        return;
    /* keep the project-level name as the root when there is one */
    if (g->recs.data[y].project_level && !g->recs.data[x].project_level)
        g->recs.data[x].parent = y;
    else
        g->recs.data[y].parent = x;
}

static int rec_get(graph *g, const char *name, int project_level)
{
    void *v = esk_map_get(&g->rec_by_name, name);
    netrec r;
    if (v)
        return (int)(intptr_t)v - 1;
    r.name = esk_strdup(g->s->a, name);
    r.project_level = project_level;
    r.parent = (int)g->recs.len;
    if (!r.name || esk_vec_push(g->s->a, &g->recs, r))
        return -1;
    esk_map_set(&g->rec_by_name, name, (void *)(intptr_t)g->recs.len);
    return (int)g->recs.len - 1;
}

static void graph_free(graph *g)
{
    size_t i;
    for (i = 0; i < g->recs.len; i++)
        esk_strfree(g->s->a, g->recs.data[i].name);
    esk_vec_free(g->s->a, &g->recs);
    esk_vec_free(g->s->a, &g->conns);
    esk_map_free(&g->rec_by_name);
}

static es_status connect_or_union(graph *g, esk_pin *pin, int rec, int via_port)
{
    conn c;
    size_t i;
    for (i = 0; i < g->conns.len; i++) {
        if (g->conns.data[i].pin == pin) {
            if (via_port) {
                rec_union(g, rec, g->conns.data[i].rec);
                return ES_OK;
            }
            ld_diag(g->s, ES_SEV_ERROR, "catalog.pin_twice", pin->comp->id, "pin %s.%s appears in more than one net", pin->comp->id, pin->name);
            return ES_ESCHEMA;
        }
    }
    c.pin = pin;
    c.rec = rec;
    return esk_vec_push(g->s->a, &g->conns, c) ? ES_ENOMEM : ES_OK;
}


/* the nets of each tie's connected pins become one; a tie names the part's
 * own pins, so a pin left unconnected costs nothing */
static es_status join_ties(graph *g, esk_comp *c)
{
    const esk_json *ties = esk_json_get(c->desc, "ties");
    int *first;
    size_t k, t, nt = esk_json_len(ties);
    if (!nt || c->ops == &composite_shell_ops)
        return ES_OK;
    first = (int *)esk_calloc(g->s->a, nt, sizeof *first);
    if (!first)
        return ES_ENOMEM;
    for (t = 0; t < nt; t++)
        first[t] = -1;
    for (k = 0; k < g->conns.len; k++) {
        const esk_pin *p = g->conns.data[k].pin;
        if (p->comp != c)
            continue;
        for (t = 0; t < nt; t++) {
            const esk_json *tie = esk_json_at(ties, t);
            size_t m;
            for (m = 0; m < esk_json_len(tie); m++)
                if (esk_str_eq(esk_json_str(esk_json_at(tie, m)), p->name))
                    break;
            if (m == esk_json_len(tie))
                continue;
            if (first[t] < 0)
                first[t] = g->conns.data[k].rec;
            else
                rec_union(g, first[t], g->conns.data[k].rec);
            break;
        }
    }
    esk_free(g->s->a, first, nt * sizeof *first);
    return ES_OK;
}

/* ---- boards (SPEC-006 §3) --------------------------------------------------- */

/* the board's firmware on its MCU's runtime (SPEC-009): the image, the register
 * description beside the MCU description, the board's oscillator */
#define LD_MAX_FILES 4

/* the MCU's memory regions' factory files (a boot ROM's image), checked
 * against their SHA-256: n of them in files[], their bytes to be freed */
static int read_memory_files(es_session *s, const esk_board_inst *bi, const esk_catalog_item *mi,
                             esk_mcu_file *files)
{
    const esk_json *mem = esk_json_get(mi->desc, "memory");
    size_t i;
    int n = 0;
    for (i = 0; i < esk_json_len(mem) && n < LD_MAX_FILES; i++) {
        const esk_json *r = esk_json_at(mem, i);
        const char *rel = esk_json_get_str(r, "file", NULL), *want = esk_json_get_str(r, "sha256", NULL);
        char *path, *data = NULL, got[72];
        size_t len = 0;
        u64 base = 0;
        if (!rel)
            continue;
        path = esk_host_path_join(s->a, mi->dir, rel);
        if (!path || esk_host_read_file(s->a, path, &data, &len) != ES_OK) {
            esk_diag_raise(&s->diag, ES_SEV_ERROR, "mcu.memory_file", 0, "board", bi->id, NULL, 0,
                           "%s: %s cannot be read", bi->id, path ? path : rel);
            esk_strfree(s->a, path);
            continue;
        }
        esk_sha256_text(data, len, got);
        if (want && strcmp(got + 7, want)) {
            esk_diag_raise(&s->diag, ES_SEV_ERROR, "mcu.memory_file", 0, "board", bi->id, NULL, 0,
                           "%s: %s is not the file the description names (sha256 %s)", bi->id, path, got + 7);
            esk_free(s->a, data, len + 1);
            esk_strfree(s->a, path);
            continue;
        }
        esk_strfree(s->a, path);
        (void)esk_json_to_u64(esk_json_get(r, "base"), &base);
        files[n].base = (u32)base;
        files[n].data = (const unsigned char *)data;
        files[n].len = len;
        n++;
    }
    return n;
}

static es_status attach_firmware(es_session *s, esk_board_inst *bi, const esk_catalog_item *board,
                                 const esk_catalog_item *mi, esk_comp *mc)
{
    const char *rel = esk_json_get_str(bi->firmware, "image", NULL);
    const esk_json *rfd = esk_json_get(mi->desc, "register_file");
    const esk_json *hse = esk_json_get(esk_json_get(board->desc, "oscillators"), "hse");
    const esk_json *lse = esk_json_get(esk_json_get(board->desc, "oscillators"), "lse");
    char *path = NULL, *rpath = NULL, *img = NULL, *rtext = NULL, why[256];
    size_t len = 0, rlen = 0;
    esk_json *regs = NULL;
    esk_mcu_setup su;
    esk_mcu_file files[LD_MAX_FILES];
    unsigned char mac_bytes[6];
    es_status st = ES_OK;
    int line = 0, nfiles = 0, k;
    const char *err = NULL;
    why[0] = 0;
    memset(&su, 0, sizeof su);
    if (!rel) {
        ld_diag(s, ES_SEV_ERROR, "schema.missing_field", bi->id, "the firmware of %s has no image%s", bi->id, "");
        return ES_ESCHEMA;
    }
    path = esk_host_path_join(s->a, s->base_dir, rel);
    if (!path)
        return ES_ENOMEM;
    if (esk_host_read_file(s->a, path, &img, &len) != ES_OK) {
        /* not built yet: the MCU waits for its image (embsim build) */
        esk_diag_raise(&s->diag, ES_SEV_WARNING, "mcu.no_image", 0, "board", bi->id, NULL, 0,
                       "%s: %s cannot be read; build the firmware (embsim build) to run it", bi->id, path);
        img = NULL;
        len = 0;
    }
    if (esk_str_eq(esk_json_get_str(rfd, "format", ""), "embsim")) {
        rpath = esk_host_path_join(s->a, mi->dir, esk_json_get_str(rfd, "file", "registers.json"));
        if (rpath && esk_host_read_file(s->a, rpath, &rtext, &rlen) == ES_OK) {
            regs = esk_json_parse(s->a, rtext, rlen, &line, &err);
            esk_free(s->a, rtext, rlen + 1);
        }
        if (!regs)
            esk_diag_raise(&s->diag, ES_SEV_ERROR, "mcu.register_file", 0, "board", bi->id, NULL, 0,
                           "%s: the register description %s cannot be read", bi->id, rpath ? rpath : "?");
    } else {
        esk_diag_raise(&s->diag, ES_SEV_ERROR, "mcu.register_file", 0, "board", bi->id, NULL, 0,
                       "%s: register files of format %s are converted first (tools/importers/svd2regs.py)", bi->id,
                       esk_json_get_str(rfd, "format", "?"));
    }
    if (regs) {
        su.mcu = mi->desc;
        su.regs = regs;
        su.hse_hz = (u32)esk_json_get_int(hse, "hz", 0);
        /* a clock or an oscillator module drives OSC_IN: no crystal to start */
        su.hse_clock = esk_str_eq(esk_json_get_str(hse, "source", ""), "external_clock") ||
                       esk_str_eq(esk_json_get_str(hse, "source", ""), "oscillator_module");
        su.lse_hz = (u32)esk_json_get_int(lse, "hz", 0);
        su.image = (const unsigned char *)img;
        su.image_len = len;
        su.image_name = rel;
        su.console = esk_json_get_str(esk_json_get(mi->desc, "console"), "default", "USART1");
        su.quantum_ps = s->core_quantum_ps;
        nfiles = read_memory_files(s, bi, mi, files);
        su.files = files;
        su.nfiles = nfiles;
        {
            unsigned v[6];
            const char *mac = bi->settings ? esk_json_get_str(bi->settings, "mac", NULL) : NULL;
            if (mac && sscanf(mac, "%2x:%2x:%2x:%2x:%2x:%2x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6) {
                for (k = 0; k < 6; k++)
                    mac_bytes[k] = (unsigned char)v[k];
                su.mac = mac_bytes;
            }
        }
        st = esk_mcu_attach(mc, &su, why, sizeof why);
        if (st != ES_OK)
            esk_diag_raise(&s->diag, ES_SEV_ERROR, "mcu.image", 0, "board", bi->id, NULL, 0, "%s: %s", bi->id, why);
    } else {
        st = ES_ESCHEMA;
    }
    esk_json_free(s->a, regs);
    for (k = 0; k < nfiles; k++)
        esk_free(s->a, (void *)files[k].data, files[k].len + 1);
    if (img)
        esk_free(s->a, img, len + 1);
    esk_strfree(s->a, path);
    esk_strfree(s->a, rpath);
    return st;
}

static es_status load_board(es_session *s, graph *g, const esk_json *entry, const esk_json *components_in_project)
{
    const char *bid = esk_json_get_str(entry, "id", NULL);
    const char *board_id = esk_json_get_str(entry, "board", NULL);
    const esk_catalog_item *item = esk_catalog_find(s->catalog, "board", board_id);
    esk_board_inst *bi;
    const esk_json *comps, *nets, *mcu;
    size_t i;
    es_status st = ES_OK;
    char scoped[192];
    ES_UNUSED(components_in_project);
    if (!bid || !board_id) {
        ld_diag(s, ES_SEV_ERROR, "schema.missing_field", "boards", "a board entry needs id and board%s%s", "", "");
        return ES_ESCHEMA;
    }
    if (!item) {
        ld_diag(s, ES_SEV_ERROR, "catalog.not_found", bid, "board %s is not in any library%s", board_id, "");
        return ES_ENOTFOUND;
    }
    bi = (esk_board_inst *)esk_calloc(s->a, 1, sizeof *bi);
    if (!bi)
        return ES_ENOMEM;
    bi->id = esk_strdup(s->a, bid);
    bi->board_id = esk_strdup(s->a, board_id);
    bi->item = item;
    bi->firmware = esk_json_get(entry, "firmware") ? esk_json_clone(s->a, esk_json_get(entry, "firmware")) : NULL;
    bi->settings = esk_json_get(entry, "settings") ? esk_json_clone(s->a, esk_json_get(entry, "settings")) : NULL;
    if (esk_vec_push(s->a, &s->boards, bi))
        return ES_ENOMEM;
    /* the MCU as a placeholder with the package's pins */
    mcu = esk_json_get(item->desc, "mcu");
    if (mcu) {
        const char *mref = esk_json_get_str(mcu, "ref", NULL);
        const char *pkg = esk_json_get_str(mcu, "package", NULL);
        const char *uid = esk_json_get_str(mcu, "instance", "U1");
        const esk_catalog_item *mi = esk_catalog_find(s->catalog, "mcu", mref);
        const esk_catalog_item *pi = NULL;
        const esk_json *pins;
        if (!mi) {
            ld_diag(s, ES_SEV_ERROR, "catalog.not_found", bid, "MCU %s of board %s is not in any library", mref ? mref : "?", board_id);
            return ES_ENOTFOUND;
        }
        {
            char pid[192];
            esk_snprintf(pid, sizeof pid, "%s/%s", mref, pkg ? pkg : "");
            pi = esk_catalog_find(s->catalog, "package", pid);
        }
        pins = pi ? esk_json_get(pi->desc, "pins") : NULL;
        if (!pins) {
            ld_diag(s, ES_SEV_ERROR, "catalog.not_found", bid, "package %s of MCU %s not found", pkg ? pkg : "?", mref);
            return ES_ENOTFOUND;
        }
        {
            /* unique signals */
            size_t np = esk_json_len(pins), k, n = 0;
            const char **names = (const char **)esk_calloc(s->a, np ? np : 1, sizeof *names);
            esk_pin_model *models = (esk_pin_model *)esk_calloc(s->a, np ? np : 1, sizeof *models);
            const char *family = esk_json_get_str(esk_json_get(mi->desc, "io"), "family", "cmos_3v3");
            esk_comp *mc = NULL;
            if (!names || !models)
                return ES_ENOMEM;
            for (k = 0; k < np; k++) {
                const char *sig = esk_json_get_str(esk_json_at(pins, k), "signal", NULL);
                size_t m;
                int dup = 0;
                if (!sig || !strcmp(sig, "NC"))
                    continue;
                for (m = 0; m < n; m++)
                    if (!strcmp(names[m], sig))
                        dup = 1;
                if (dup)
                    continue;
                names[n] = sig;
                mcu_signal_model(sig, family, &models[n]);
                if (models[n].kind == ESK_PIN_DIGITAL_IO)
                    mcu_electrical(esk_json_get(esk_json_get(mi->desc, "io"), "electrical"), &models[n]);
                n++;
            }
            esk_snprintf(scoped, sizeof scoped, "%s.%s", bid, uid);
            st = esk_engine_add_comp(&s->eng, scoped, mref, bi->firmware ? &esk_mcu_ops : &mcu_placeholder_ops, names, models,
                                     (uint32_t)n, NULL, mi->desc, &mc);
            esk_free(s->a, names, (np ? np : 1) * sizeof *names);
            esk_free(s->a, models, (np ? np : 1) * sizeof *models);
            if (st != ES_OK)
                return st;
            bi->mcu = mc;
            if (bi->firmware)
                st = attach_firmware(s, bi, item, mi, mc);
            else
                esk_diag_raise(&s->diag, ES_SEV_INFO, "mcu.no_firmware", 0, "board", bid, NULL, 0,
                               "%s has no firmware: its MCU's pins stay inputs, as a blank part's do", bid);
            if (st != ES_OK)
                return st;
        }
    }
    /* on-board components */
    comps = esk_json_get(item->desc, "components");
    for (i = 0; i < esk_json_len(comps) && st == ES_OK; i++) {
        const esk_json *ce = esk_json_at(comps, i);
        const char *cid = esk_json_get_str(ce, "id", NULL);
        const char *ref = esk_json_get_str(ce, "component", NULL);
        const esk_catalog_item *ci = esk_project_component(s, ref);
        esk_comp *c = NULL;
        if (!cid || !ci) {
            ld_diag(s, ES_SEV_ERROR, "catalog.not_found", bid, "board component %s (%s) is not in any library", cid ? cid : "?", ref ? ref : "?");
            st = ES_ENOTFOUND;
            break;
        }
        esk_snprintf(scoped, sizeof scoped, "%s.%s", bid, cid);
        st = instantiate(s, g, scoped, ci, esk_json_get(ce, "params"), 0, &c);
    }
    if (st != ES_OK)
        return st;
    /* board nets */
    nets = esk_json_get(item->desc, "nets");
    for (i = 0; i < esk_json_len(nets) && st == ES_OK; i++) {
        const esk_json *ne = esk_json_at(nets, i);
        int rec;
        size_t k;
        esk_snprintf(scoped, sizeof scoped, "%s.%s", bid, ne->key);
        rec = rec_get(g, scoped, 0);
        if (rec < 0)
            return ES_ENOMEM;
        for (k = 0; k < esk_json_len(ne) && st == ES_OK; k++) {
            const char *ref = esk_json_str(esk_json_at(ne, k));
            char full[256];
            esk_pin *p;
            if (!ref) {
                st = ES_ESCHEMA;
                break;
            }
            esk_snprintf(full, sizeof full, "%s.%s", bid, ref);
            p = resolve_pin_ref(s, full, 0);
            if (!p) {
                ld_diag(s, ES_SEV_ERROR, "catalog.pin_unknown", bid, "unknown pin %s in board net %s", ref, ne->key);
                st = ES_ESCHEMA;
                break;
            }
            st = connect_or_union(g, p, rec, resolve_via_port);
        }
    }
    return st;
}

/* resolve "BOARD.PORT" to a net record; -1 if not a board port */
static int board_port_rec(es_session *s, graph *g, const char *ref)
{
    size_t i;
    for (i = 0; i < s->boards.len; i++) {
        esk_board_inst *bi = s->boards.data[i];
        size_t bl = strlen(bi->id);
        const esk_json *ports, *port;
        char scoped[192];
        if (strncmp(ref, bi->id, bl) || ref[bl] != '.')
            continue;
        ports = esk_json_get(bi->item->desc, "ports");
        port = esk_json_get(ports, ref + bl + 1);
        if (!port)
            return -1;
        esk_snprintf(scoped, sizeof scoped, "%s.%s", bi->id, esk_json_get_str(port, "net", ""));
        if (!esk_map_get(&g->rec_by_name, scoped))
            return -1;
        return rec_get(g, scoped, 0);
    }
    return -1;
}

/* ---- the project --------------------------------------------------------- */

static es_status apply_settings(es_session *s, const esk_json *settings)
{
    es_time_t t;
    int64_t n;
    if (esk_json_to_time(esk_json_get(settings, "core_quantum_ps"), &t) == 0)
        s->core_quantum_ps = t;
    if (esk_json_to_time(esk_json_get(settings, "crossing_resolution_ps"), &t) == 0)
        s->eng.crossing_resolution_ps = t;
    n = esk_json_get_int(settings, "max_delta_cycles", 0);
    if (n > 0)
        s->eng.sched.max_delta_cycles = (uint32_t)n;
    n = esk_json_get_int(settings, "max_pending_events", 0);
    if (n > 0)
        s->eng.sched.max = (size_t)n;
    return ES_OK;
}

es_status esk_project_load(es_session *s, const esk_json *project, const char *base_dir)
{
    graph g;
    const esk_json *libs, *boards, *comps, *nets, *probes;
    const char *policy;
    size_t i;
    es_status st = ES_OK;
    if (!project || project->type != ESK_JSON_OBJECT)
        return ES_ESCHEMA;
    if (esk_json_get_int(project, "schema_version", 0) != 1) {
        ld_diag(s, ES_SEV_ERROR, "schema.version", "project", "unsupported schema_version; expected 1%s%s", "", "");
        return ES_ESCHEMA;
    }
    if (!esk_str_eq(esk_json_get_str(project, "kind", "project"), "project")) {
        ld_diag(s, ES_SEV_ERROR, "schema.type", "project", "kind must be \"project\"%s%s", "", "");
        return ES_ESCHEMA;
    }
    {
        char why[256];
        if (esk_project_validate_organization(project, why, sizeof why)) {
            ld_diag(s, ES_SEV_ERROR, "schema.layout", "project", "%s%s", why, "");
            return ES_ESCHEMA;
        }
    }
    {
        /* copied before the unload: `project` or `base_dir` may be the
         * session's own (a reload of the project as it stands) */
        esk_json *copy = esk_json_clone(s->a, project);
        char *dir = esk_strdup(s->a, base_dir ? base_dir : ".");
        esk_project_unload(s);
        s->project = copy;
        s->base_dir = dir;
    }
    project = s->project;
    s->name = project ? esk_strdup(s->a, esk_json_get_str(project, "name", "project")) : NULL;
    if (!s->project || !s->base_dir || !s->name)
        return ES_ENOMEM;
    policy = esk_json_get_str(project, "policy", NULL);
    if (policy && !s->policy_forced) {
        if (!strcmp(policy, "functional")) s->eng.policy = ESK_POLICY_FUNCTIONAL;
        else if (!strcmp(policy, "timed")) s->eng.policy = ESK_POLICY_TIMED;
        else if (!strcmp(policy, "electrical")) s->eng.policy = ESK_POLICY_ELECTRICAL;
        else {
            ld_diag(s, ES_SEV_ERROR, "schema.type", "project", "policy must be functional, timed or electrical%s%s", "", "");
            return ES_ESCHEMA;
        }
    }
    /* libraries */
    libs = esk_json_get(project, "libraries");
    for (i = 0; i < esk_json_len(libs); i++) {
        const char *rel = esk_json_str(esk_json_at(libs, i));
        char *path = rel ? esk_host_path_join(s->a, s->base_dir, rel) : NULL;
        if (path) {
            esk_catalog_load_library(s->catalog, path, &s->diag);
            esk_strfree(s->a, path);
        }
    }
    {
        char why[256];
        if (esk_project_validate_bench(s->catalog, project, why, sizeof why)) {
            ld_diag(s, ES_SEV_ERROR, "bench.invalid", "project", "%s%s", why, "");
            return ES_ESCHEMA;
        }
    }
    st = esk_library_lock_enforce(s, project);
    if (st != ES_OK) return st;
    st = esk_project_blocks_prepare(s);
    if (st != ES_OK) return st;
    memset(&g, 0, sizeof g);
    g.s = s;
    esk_map_init(&g.rec_by_name, s->a);
    /* settings first: the MCU runtime reads its quantum when a board is loaded */
    apply_settings(s, esk_json_get(project, "settings"));
    /* boards */
    boards = esk_json_get(project, "boards");
    comps = esk_json_get(project, "components");
    for (i = 0; i < esk_json_len(boards) && st == ES_OK; i++)
        st = load_board(s, &g, esk_json_at(boards, i), comps);
    /* components */
    for (i = 0; i < esk_json_len(comps) && st == ES_OK; i++) {
        const esk_json *ce = esk_json_at(comps, i);
        const char *cid = esk_json_get_str(ce, "id", NULL);
        const char *ref = esk_json_get_str(ce, "component", NULL);
        const esk_catalog_item *ci = esk_project_component(s, ref);
        esk_comp *c = NULL;
        if (!cid || !esk_is_identifier(cid)) {
            ld_diag(s, ES_SEV_ERROR, "schema.type", cid ? cid : "components", "component ids must be identifiers%s%s", "", "");
            st = ES_ESCHEMA;
            break;
        }
        if (!ci) {
            ld_diag(s, ES_SEV_ERROR, "catalog.not_found", cid, "component %s is not in any library%s", ref ? ref : "?", "");
            st = ES_ENOTFOUND;
            break;
        }
        st = instantiate(s, &g, cid, ci, esk_json_get(ce, "params"), 0, &c);
    }
    /* project nets */
    nets = esk_json_get(project, "nets");
    for (i = 0; i < esk_json_len(nets) && st == ES_OK; i++) {
        const esk_json *ne = esk_json_at(nets, i);
        int rec = rec_get(&g, ne->key, 1);
        size_t k;
        if (rec < 0) {
            st = ES_ENOMEM;
            break;
        }
        for (k = 0; k < esk_json_len(ne) && st == ES_OK; k++) {
            const char *ref = esk_json_str(esk_json_at(ne, k));
            esk_pin *p;
            int prec;
            if (!ref) {
                st = ES_ESCHEMA;
                break;
            }
            prec = board_port_rec(s, &g, ref);
            if (prec >= 0) {
                rec_union(&g, rec, prec);
                continue;
            }
            p = resolve_pin_ref(s, ref, 0);
            if (!p) {
                ld_diag(s, ES_SEV_ERROR, "catalog.pin_unknown", ne->key, "unknown pin %s in net %s", ref, ne->key);
                st = ES_ESCHEMA;
                break;
            }
            st = connect_or_union(&g, p, rec, resolve_via_port);
        }
    }
    /* functional wires join their pins' nets */
    for (i = 0; i < s->eng.comps.len && st == ES_OK; i++) {
        esk_comp *c = s->eng.comps.data[i];
        size_t k;
        int first = -1;
        if (!esk_str_eq(c->ops->name, "wire"))
            continue;
        for (k = 0; k < g.conns.len; k++) {
            if (g.conns.data[k].pin->comp != c)
                continue;
            if (first < 0)
                first = g.conns.data[k].rec;
            else
                rec_union(&g, first, g.conns.data[k].rec);
        }
    }
    /* a part's ties (a breadboard's strips, a connector's shorted contacts)
     * join the nets of their pins (SPEC-005 §2) */
    for (i = 0; i < s->eng.comps.len && st == ES_OK; i++)
        st = join_ties(&g, s->eng.comps.data[i]);
    /* create engine nets per root */
    for (i = 0; i < g.recs.len && st == ES_OK; i++) {
        int root = rec_find(&g, (int)i);
        esk_net *n;
        if (root != (int)i) {
            /* alias onto the root's net (created when the root was visited, roots have index <= any? not necessarily) */
            continue;
        }
        st = esk_engine_add_net(&s->eng, g.recs.data[i].name, &n);
    }
    for (i = 0; i < g.recs.len && st == ES_OK; i++) {
        int root = rec_find(&g, (int)i);
        if (root != (int)i) {
            esk_net *n = esk_engine_net(&s->eng, g.recs.data[root].name);
            if (n)
                esk_engine_net_alias(&s->eng, n, g.recs.data[i].name);
        }
    }
    for (i = 0; i < g.conns.len && st == ES_OK; i++) {
        int root = rec_find(&g, g.conns.data[i].rec);
        esk_net *n = esk_engine_net(&s->eng, g.recs.data[root].name);
        if (!n) {
            st = ES_EINTERNAL;
            break;
        }
        st = esk_engine_connect(&s->eng, g.conns.data[i].pin, n);
    }
    graph_free(&g);
    if (st != ES_OK)
        return st;
    /* unconnected required pins */
    for (i = 0; i < s->eng.comps.len; i++) {
        esk_comp *c = s->eng.comps.data[i];
        uint32_t k;
        if (c->ops == &mcu_placeholder_ops || c->ops == &composite_shell_ops)
            continue;
        for (k = 0; k < c->npins; k++) {
            if (!c->pins[k].net && c->pins[k].model.kind != ESK_PIN_NC) {
                const esk_json *pins = esk_json_get(c->desc, "pins");
                const esk_json *pd = esk_json_at(pins, k);
                if (esk_json_get_bool(pd, "required", 1))
                    esk_diag_raise(&s->diag, ES_SEV_WARNING, "net.unconnected_required_pin", 0, "pin", c->pins[k].name, NULL, 0,
                                   "%s.%s is not connected", c->id, c->pins[k].name);
            }
        }
    }
    st = esk_engine_partition(&s->eng);
    if (st != ES_OK)
        return st;
    /* probes */
    probes = esk_json_get(project, "probes");
    for (i = 0; i < esk_json_len(probes); i++) {
        const esk_json *pe = esk_json_at(probes, i);
        esk_probe *p = NULL;
        es_status ps = esk_session_probe_add(s, esk_json_get_str(pe, "id", NULL), esk_json_get_str(pe, "target", ""),
                                             esk_json_get_str(pe, "kind", NULL), esk_json_get_num(pe, "rate_hz", 0), &p);
        if (ps != ES_OK)
            ld_diag(s, ES_SEV_WARNING, "probe.invalid", esk_json_get_str(pe, "id", "probe"), "probe %s could not be created: %s",
                    esk_json_get_str(pe, "target", "?"), es_strerror(ps));
    }
    s->loaded = 1;
    esk_session_world_load(s);
    esk_session_analyses_apply(s);
    return ES_OK;
}

void esk_project_unload(es_session *s)
{
    size_t i;
    for (i = 0; i < s->probes.len; i++) {
        esk_probe *p = s->probes.data[i];
        esk_strfree(s->a, p->name);
        esk_strfree(s->a, p->target);
        esk_strfree(s->a, p->quantity);
        esk_free(s->a, p->ring, p->cap * sizeof *p->ring);
        esk_free(s->a, p, sizeof *p);
    }
    esk_vec_clear(&s->probes);
    for (i = 0; i < s->boards.len; i++) {
        esk_board_inst *bi = s->boards.data[i];
        esk_strfree(s->a, bi->id);
        esk_strfree(s->a, bi->board_id);
        esk_json_free(s->a, bi->firmware);
        esk_json_free(s->a, bi->settings);
        esk_free(s->a, bi, sizeof *bi);
    }
    esk_vec_clear(&s->boards);
    esk_engine_free(&s->eng);
    esk_project_blocks_free(s);
    esk_engine_init(&s->eng, s->a, &s->diag, s->eng.policy);
    s->eng.on_net_change = esk_session_on_net_change;
    s->eng.on_net_change_ctx = s;
    s->eng.on_irq = esk_record_irq;
    s->eng.on_irq_ctx = s;
    esk_json_free(s->a, s->project);
    s->project = NULL;
    esk_strfree(s->a, s->base_dir);
    esk_strfree(s->a, s->name);
    s->base_dir = s->name = NULL;
    if (s->inputs_log)
        esk_json_free(s->a, s->inputs_log);
    s->inputs_log = esk_json_new_array(s->a);
    s->input_seq = 0;
    s->loaded = 0;
    s->running = 0;
}
