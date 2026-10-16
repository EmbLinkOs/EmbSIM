/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* edit.c -- the edits of project.transaction (SPEC-008 §5) applied to a
 * project's JSON. Pure functions on the JSON: the transaction in rpc.c
 * collects them on a copy and commits the result.
 *
 * Edits (one key each):
 *   add_component {id, component, version?, params?, layout?}
 *   remove_component {id}           its pins leave their nets
 *   set_param {id, param, value}    value null removes the parameter
 *   connect {net, pin}              the pin leaves any other net first
 *   disconnect {pin}
 *   rename_net {from, to}           wires, labels and net probes follow
 *   add_board {id, board, layout?, firmware?}
 *   remove_board {id}
 *   set_layout {id, layout}         a component's or a board's placement
 *   set_layout {wires?, labels?, buses?}  the drawing
 *
 * Everything but set_layout changes the circuit: `*structural` is set so
 * the commit rebuilds the session. A net left with no pins is removed. */
#include "session/session_priv.h"

#include <stdio.h>
#include <string.h>

static int err(char *why, size_t cap, const char *fmt, const char *a)
{
    if (why && cap)
        snprintf(why, cap, fmt, a ? a : "");
    return -1;
}

/* the array member `key` of `obj`, created when absent */
static esk_json *array_member(esk_alloc *a, esk_json *obj, const char *key)
{
    esk_json *arr = esk_json_get_mut(obj, key);
    if (arr && arr->type == ESK_JSON_ARRAY)
        return arr;
    arr = esk_json_new_array(a);
    if (!arr || esk_json_object_set(a, obj, key, arr)) {
        esk_json_free(a, arr);
        return NULL;
    }
    return esk_json_get_mut(obj, key);
}

static esk_json *object_member(esk_alloc *a, esk_json *obj, const char *key)
{
    esk_json *o = esk_json_get_mut(obj, key);
    if (o && o->type == ESK_JSON_OBJECT)
        return o;
    o = esk_json_new_object(a);
    if (!o || esk_json_object_set(a, obj, key, o)) {
        esk_json_free(a, o);
        return NULL;
    }
    return esk_json_get_mut(obj, key);
}

/* the index of the item of `arr` whose "id" is `id`, or -1 */
static long find_id(const esk_json *arr, const char *id)
{
    size_t i;
    for (i = 0; i < esk_json_len(arr); i++)
        if (esk_str_eq(esk_json_get_str(esk_json_at(arr, i), "id", ""), id))
            return (long)i;
    return -1;
}

/* removes `pin` from every net; with `prefix`, every pin "prefix.X" */
static void drop_pins(esk_alloc *a, esk_json *nets, const char *pin, int prefix)
{
    size_t n = pin ? strlen(pin) : 0, i, j;
    for (i = esk_json_len(nets); i-- > 0;) {
        esk_json *pins = esk_json_at_mut(nets, i);
        for (j = esk_json_len(pins); j-- > 0;) {
            const char *p = esk_json_str(esk_json_at(pins, j));
            if (!p)
                continue;
            if (prefix ? (strncmp(p, pin, n) == 0 && p[n] == '.') : esk_str_eq(p, pin))
                esk_json_remove_at(a, pins, j);
        }
        if (esk_json_len(pins) == 0)
            esk_json_remove_at(a, nets, i);
    }
}

/* renames the member `from` of `obj` to `to`, keeping its place */
static int rename_member(esk_alloc *a, esk_json *obj, const char *from, const char *to)
{
    esk_json *v = esk_json_get_mut(obj, from);
    char *key;
    if (!v)
        return -1;
    key = esk_strdup(a, to);
    if (!key)
        return -1;
    esk_strfree(a, v->key);
    v->key = key;
    return 0;
}

static int set_string_in(esk_alloc *a, esk_json *arr, const char *key, const char *from, const char *to)
{
    size_t i;
    for (i = 0; i < esk_json_len(arr); i++) {
        esk_json *x = esk_json_at_mut(arr, i);
        if (esk_str_eq(esk_json_get_str(x, key, NULL), from) && esk_json_set_string(a, x, key, to))
            return -1;
    }
    return 0;
}

static int add_item(esk_alloc *a, esk_json *project, const char *list, const esk_json *spec, const char *kind_key,
                    char *why, size_t cap)
{
    const char *id = esk_json_get_str(spec, "id", NULL);
    esk_json *arr = array_member(a, project, list), *copy;
    if (!id || !*id || !esk_json_get_str(spec, kind_key, NULL))
        return err(why, cap, "%s needs an id and a catalog id", list);
    if (!arr)
        return -1;
    if (find_id(esk_json_get(project, "components"), id) >= 0 || find_id(esk_json_get(project, "boards"), id) >= 0)
        return err(why, cap, "%s is already used", id);
    copy = esk_json_clone(a, spec);
    if (!copy || esk_json_array_push(a, arr, copy)) {
        esk_json_free(a, copy);
        return -1;
    }
    return 0;
}

int esk_project_apply_edit(esk_alloc *a, esk_json *project, const esk_json *edit, int *structural, char *why,
                           size_t cap)
{
    const esk_json *e;
    esk_json *nets;
    if (!project || !edit || edit->type != ESK_JSON_OBJECT || esk_json_len(edit) != 1)
        return err(why, cap, "an edit is an object with one key%s", NULL);
    nets = object_member(a, project, "nets");
    if (!nets)
        return -1;
    e = esk_json_at(edit, 0);

    if (esk_str_eq(e->key, "add_component")) {
        *structural = 1;
        return add_item(a, project, "components", e, "component", why, cap);
    }
    if (esk_str_eq(e->key, "add_board")) {
        *structural = 1;
        return add_item(a, project, "boards", e, "board", why, cap);
    }
    if (esk_str_eq(e->key, "remove_component") || esk_str_eq(e->key, "remove_board")) {
        const char *id = esk_json_get_str(e, "id", "");
        esk_json *arr = esk_json_get_mut(project, esk_str_eq(e->key, "remove_board") ? "boards" : "components");
        long i = find_id(arr, id);
        if (i < 0)
            return err(why, cap, "no %s to remove", id);
        *structural = 1;
        esk_json_remove_at(a, arr, (size_t)i);
        drop_pins(a, nets, id, 1);
        return 0;
    }
    if (esk_str_eq(e->key, "set_param")) {
        const char *id = esk_json_get_str(e, "id", ""), *param = esk_json_get_str(e, "param", NULL);
        const esk_json *v = esk_json_get(e, "value");
        esk_json *arr = esk_json_get_mut(project, "components");
        long i = find_id(arr, id);
        esk_json *params, *copy;
        if (i < 0 || !param)
            return err(why, cap, "set_param: no component %s", id);
        *structural = 1;
        params = object_member(a, esk_json_at_mut(arr, (size_t)i), "params");
        if (!params)
            return -1;
        if (!v || v->type == ESK_JSON_NULL) {
            esk_json_remove(a, params, param);
            return 0;
        }
        copy = esk_json_clone(a, v);
        if (!copy || esk_json_object_set(a, params, param, copy)) {
            esk_json_free(a, copy);
            return -1;
        }
        return 0;
    }
    if (esk_str_eq(e->key, "connect")) {
        const char *net = esk_json_get_str(e, "net", NULL), *pin = esk_json_get_str(e, "pin", NULL);
        esk_json *pins;
        if (!net || !*net || !pin || !strchr(pin, '.'))
            return err(why, cap, "connect needs a net and a pin (INSTANCE.PIN)%s", NULL);
        *structural = 1;
        drop_pins(a, nets, pin, 0);
        pins = array_member(a, nets, net);
        if (!pins)
            return -1;
        return esk_json_array_push(a, pins, esk_json_new_string(a, pin));
    }
    if (esk_str_eq(e->key, "disconnect")) {
        const char *pin = esk_json_get_str(e, "pin", NULL);
        if (!pin)
            return err(why, cap, "disconnect needs a pin%s", NULL);
        *structural = 1;
        drop_pins(a, nets, pin, 0);
        return 0;
    }
    if (esk_str_eq(e->key, "rename_net")) {
        const char *from = esk_json_get_str(e, "from", NULL), *to = esk_json_get_str(e, "to", NULL);
        esk_json *layout = esk_json_get_mut(project, "layout"), *probes = esk_json_get_mut(project, "probes");
        size_t i;
        if (!from || !to || !*to)
            return err(why, cap, "rename_net needs from and to%s", NULL);
        if (!esk_json_get(nets, from))
            return err(why, cap, "no net %s", from);
        if (esk_json_get(nets, to))
            return err(why, cap, "a net %s exists", to);
        *structural = 1;
        if (rename_member(a, nets, from, to))
            return -1;
        if (layout && (set_string_in(a, esk_json_get_mut(layout, "wires"), "net", from, to) ||
                       set_string_in(a, esk_json_get_mut(layout, "labels"), "net", from, to)))
            return -1;
        /* a bus lists its nets by name */
        {
            esk_json *buses = layout ? esk_json_get_mut(layout, "buses") : NULL;
            for (i = 0; i < esk_json_len(buses); i++) {
                esk_json *members = esk_json_get_mut(esk_json_at_mut(buses, i), "nets");
                size_t m;
                for (m = 0; m < esk_json_len(members); m++) {
                    esk_json *x = esk_json_at_mut(members, m);
                    if (esk_str_eq(esk_json_str(x), from)) {
                        esk_json *nx = esk_json_new_string(a, to);
                        if (!nx)
                            return -1;
                        esk_strfree(a, nx->key);
                        nx->key = NULL;
                        esk_json_free(a, x);
                        members->u.array.items[m] = nx;
                    }
                }
            }
        }
        for (i = 0; i < esk_json_len(probes); i++) {
            esk_json *p = esk_json_at_mut(probes, i);
            const char *t = esk_json_get_str(p, "target", "");
            if (strncmp(t, "net:", 4) == 0 && esk_str_eq(t + 4, from)) {
                char buf[256];
                snprintf(buf, sizeof buf, "net:%s", to);
                if (esk_json_set_string(a, p, "target", buf))
                    return -1;
            }
        }
        return 0;
    }
    if (esk_str_eq(e->key, "set_layout")) {
        const char *id = esk_json_get_str(e, "id", NULL);
        if (id) {
            const esk_json *l = esk_json_get(e, "layout");
            esk_json *arr = esk_json_get_mut(project, "components"), *copy;
            long i = find_id(arr, id);
            if (i < 0) {
                arr = esk_json_get_mut(project, "boards");
                i = find_id(arr, id);
            }
            if (i < 0 || !l || l->type != ESK_JSON_OBJECT)
                return err(why, cap, "set_layout: no component or board %s", id);
            copy = esk_json_clone(a, l);
            if (!copy || esk_json_object_set(a, esk_json_at_mut(arr, (size_t)i), "layout", copy)) {
                esk_json_free(a, copy);
                return -1;
            }
            return 0;
        } else {
            static const char *const keys[] = { "wires", "labels", "buses" };
            esk_json *layout = object_member(a, project, "layout");
            size_t k;
            if (!layout)
                return -1;
            for (k = 0; k < 3; k++) {
                const esk_json *v = esk_json_get(e, keys[k]);
                esk_json *copy;
                if (!v)
                    continue;
                if (v->type != ESK_JSON_ARRAY)
                    return err(why, cap, "set_layout: %s is a list", keys[k]);
                copy = esk_json_clone(a, v);
                if (!copy || esk_json_object_set(a, layout, keys[k], copy)) {
                    esk_json_free(a, copy);
                    return -1;
                }
            }
            return 0;
        }
    }
    return err(why, cap, "unknown edit %s", e->key);
}
