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
 *   set_layout {wires?, labels?, buses?, groups?, bench?}  the drawing
 *   set_world {fields?, bodies?}    the room the circuit sits in (ADR-026)
 *
 * Everything but set_layout and set_world changes the circuit: `*structural` is set so
 * the commit rebuilds the session. A net left with no pins is removed. */
#include "session/session_priv.h"

#include <stdio.h>
#include <math.h>
#include <ctype.h>
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

static int validate_groups(const esk_json *project, const esk_json *v, char *why, size_t cap)
{
    if (!v)
        return 0;
    if (v->type != ESK_JSON_ARRAY)
        return err(why, cap, "groups must be an array%s", NULL);
    size_t gi, mi, gj, mj;
    for (gi = 0; gi < esk_json_len(v); gi++) {
        const esk_json *g = esk_json_at(v, gi), *members = esk_json_get(g, "members");
        const char *gid = esk_json_get_str(g, "id", ""), *name = esk_json_get_str(g, "name", "");
        if (!*gid || !*name || !esk_json_is(members, ESK_JSON_ARRAY) || !esk_json_len(members))
            return err(why, cap, "set_layout: invalid group %s", gid);
        for (gj = 0; gj < gi; gj++)
            if (esk_str_eq(gid, esk_json_get_str(esk_json_at(v, gj), "id", "")))
                return err(why, cap, "set_layout: duplicate group %s", gid);
        for (mi = 0; mi < esk_json_len(members); mi++) {
            const char *member = esk_json_str(esk_json_at(members, mi));
            if (!member || (find_id(esk_json_get(project, "components"), member) < 0 &&
                            find_id(esk_json_get(project, "boards"), member) < 0))
                return err(why, cap, "set_layout: unknown group member %s", member);
            for (gj = 0; gj <= gi; gj++) {
                const esk_json *previous = esk_json_get(esk_json_at(v, gj), "members");
                for (mj = 0; mj < (gj == gi ? mi : esk_json_len(previous)); mj++)
                    if (esk_str_eq(member, esk_json_str(esk_json_at(previous, mj))))
                        return err(why, cap, "set_layout: repeated group member %s", member);
            }
        }
    }
    return 0;
}

static int validate_placement(const esk_json *layout, const char *id, char *why, size_t cap)
{
    const char *const numbers[] = {"x", "y", "rotation"};
    const char *const flags[] = {"mirror", "locked"};
    size_t i;
    if (!layout)
        return 0;
    if (layout->type != ESK_JSON_OBJECT)
        return err(why, cap, "invalid placement for %s", id);
    for (i = 0; i < 3; i++) {
        const esk_json *value = esk_json_get(layout, numbers[i]);
        if (value && value->type != ESK_JSON_NUMBER)
            return err(why, cap, "invalid placement number for %s", id);
        if (value && i == 2 && value->u.number.value != 0 && value->u.number.value != 90 &&
            value->u.number.value != 180 && value->u.number.value != 270)
            return err(why, cap, "invalid placement rotation for %s", id);
    }
    for (i = 0; i < 2; i++) {
        const esk_json *value = esk_json_get(layout, flags[i]);
        if (value && value->type != ESK_JSON_BOOL)
            return err(why, cap, "invalid placement flag for %s", id);
    }
    return 0;
}

int esk_project_validate_organization(const esk_json *project, char *why, size_t cap)
{
    const char *const lists[] = {"components", "boards"};
    size_t k, i;
    for (k = 0; k < 2; k++) {
        const esk_json *parts = esk_json_get(project, lists[k]);
        for (i = 0; i < esk_json_len(parts); i++) {
            const esk_json *part = esk_json_at(parts, i);
            if (validate_placement(esk_json_get(part, "layout"), esk_json_get_str(part, "id", ""),
                                   why, cap))
                return -1;
        }
    }
    {
        const esk_json *order = esk_json_get(esk_json_get(project, "layout"), "port_order");
        const esk_json *ports = esk_json_get(project, "ports");
        if (order) {
            if (order->type != ESK_JSON_ARRAY || !ports || ports->type != ESK_JSON_OBJECT)
                return err(why, cap, "%s", "port_order requires a block's port array");
            for (i = 0; i < esk_json_len(order); i++) {
                const char *name = esk_json_str(esk_json_at(order, i));
                if (!name || !esk_json_get(ports, name)) return err(why, cap, "%s", "port_order references an unknown port");
                for (k = 0; k < i; k++)
                    if (esk_str_eq(name, esk_json_str(esk_json_at(order, k)))) return err(why, cap, "%s", "duplicate port_order entry");
            }
        }
    }
    return validate_groups(project, esk_json_get(esk_json_get(project, "layout"), "groups"), why,
                           cap);
}

/* A bench jumper represents an existing connection, never a second graph. */
static const char *bench_net(const esk_json *project, const char *ref)
{
    const esk_json *nets = esk_json_get(project, "nets");
    size_t i, j;
    for (i = 0; i < esk_json_len(nets); i++) {
        const esk_json *pins = esk_json_at(nets, i);
        for (j = 0; j < esk_json_len(pins); j++)
            if (esk_str_eq(ref, esk_json_str(esk_json_at(pins, j)))) return pins->key;
    }
    return NULL;
}

static int bench_hole(const es_catalog *catalog, const esk_json *project, const char *ref)
{
    const esk_json *parts = esk_json_get(project, "components");
    size_t i;
    const char *dot = strchr(ref, '.');
    if (!catalog || !dot) return 0;
    for (i = 0; i < esk_json_len(parts); i++) {
        const esk_json *part = esk_json_at(parts, i);
        const char *id = esk_json_get_str(part, "id", "");
        const esk_catalog_item *item;
        if (strlen(id) != (size_t)(dot-ref) || strncmp(id, ref, (size_t)(dot-ref))) continue;
        item = esk_catalog_find(catalog, "component", esk_json_get_str(part, "component", ""));
        return item && esk_str_eq(esk_json_get_str(esk_json_get(item->desc, "bench"), "package", ""), "breadboard");
    }
    return 0;
}

int esk_project_validate_bench(const es_catalog *catalog, const esk_json *project, char *why, size_t cap)
{
    const esk_json *bench = esk_json_get(esk_json_get(project, "layout"), "bench");
    const esk_json *parts = esk_json_get(bench, "parts"), *wires = esk_json_get(bench, "wires");
    size_t i, j, k;
    if (!bench) return 0;
    if (!esk_json_is(bench, ESK_JSON_OBJECT) || (parts && !esk_json_is(parts, ESK_JSON_OBJECT)) ||
        (wires && !esk_json_is(wires, ESK_JSON_ARRAY))) return err(why, cap, "invalid bench container%s", NULL);
    for (i = 0; i < esk_json_len(parts); i++) {
        const esk_json *place = esk_json_at(parts, i);
        const char *id = place->key;
        const char *fields[] = {"x", "y"};
        if (find_id(esk_json_get(project, "components"), id) < 0 && find_id(esk_json_get(project, "boards"), id) < 0)
            return err(why, cap, "unknown bench part %s", id);
        if (validate_placement(place, id, why, cap)) return -1;
        for (j = 0; j < 2; j++) {
            const esk_json *n = esk_json_get(place, fields[j]);
            if (!esk_json_is(n, ESK_JSON_NUMBER) || !isfinite(n->u.number.value) || fabs(n->u.number.value) > 1e6)
                return err(why, cap, "invalid bench position %s", id);
        }
    }
    for (i = 0; i < esk_json_len(wires); i++) {
        const esk_json *w = esk_json_at(wires, i), *points = esk_json_get(w, "points"), *rise = esk_json_get(w, "rise_mm");
        const char *from = esk_json_get_str(w, "from", ""), *to = esk_json_get_str(w, "to", "");
        const char *color = esk_json_get_str(w, "color", NULL), *net = bench_net(project, from);
        if (!*from || !*to || esk_str_eq(from, to) || !net || !esk_str_eq(net, bench_net(project, to)))
            return err(why, cap, "bench jumper endpoints must be distinct terminals of one net: %s", from);
        if (esk_json_get(w, "color")) {
            if (!color || strlen(color) != 7 || color[0] != '#') return err(why, cap, "invalid bench wire color %s", from);
            for (j = 1; j < 7; j++) if (!isxdigit((unsigned char)color[j])) return err(why, cap, "invalid bench wire color %s", from);
        }
        if (rise && (!esk_json_is(rise, ESK_JSON_NUMBER) || !isfinite(rise->u.number.value) || rise->u.number.value < 0 || rise->u.number.value > 1000))
            return err(why, cap, "invalid bench wire rise %s", from);
        if (points && (!esk_json_is(points, ESK_JSON_ARRAY) || esk_json_len(points) > 64)) return err(why, cap, "invalid bench route %s", from);
        for (j = 0; j < esk_json_len(points); j++) {
            const esk_json *point = esk_json_at(points, j);
            if (!esk_json_is(point, ESK_JSON_ARRAY) || esk_json_len(point) != 3) return err(why, cap, "invalid bench route point %s", from);
            for (k = 0; k < 3; k++) {
                const esk_json *n = esk_json_at(point, k);
                if (!esk_json_is(n, ESK_JSON_NUMBER) || !isfinite(n->u.number.value) || fabs(n->u.number.value) > 1e6 || (k == 1 && n->u.number.value < 0))
                    return err(why, cap, "invalid bench route coordinate %s", from);
            }
        }
        for (j = 0; j < i; j++) {
            const esk_json *prev = esk_json_at(wires, j);
            const char *a = esk_json_get_str(prev, "from", ""), *b = esk_json_get_str(prev, "to", "");
            if ((esk_str_eq(from, a) && esk_str_eq(to, b)) || (esk_str_eq(from, b) && esk_str_eq(to, a)))
                return err(why, cap, "duplicate bench jumper %s", from);
            if ((bench_hole(catalog, project, from) && (esk_str_eq(from, a) || esk_str_eq(from, b))) ||
                (bench_hole(catalog, project, to) && (esk_str_eq(to, a) || esk_str_eq(to, b))))
                return err(why, cap, "bench hole already occupied by a jumper: %s", from);
        }
    }
    return 0;
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

static int set_string_in(esk_alloc *a, esk_json *arr, const char *key, const char *from,
                         const char *to)
{
    size_t i;
    for (i = 0; i < esk_json_len(arr); i++) {
        esk_json *x = esk_json_at_mut(arr, i);
        if (esk_str_eq(esk_json_get_str(x, key, NULL), from) && esk_json_set_string(a, x, key, to))
            return -1;
    }
    return 0;
}

static int add_item(esk_alloc *a, esk_json *project, const char *list, const esk_json *spec,
                    const char *kind_key, char *why, size_t cap)
{
    const char *id = esk_json_get_str(spec, "id", NULL);
    esk_json *arr = array_member(a, project, list), *copy;
    if (!id || !*id || !esk_json_get_str(spec, kind_key, NULL))
        return err(why, cap, "%s needs an id and a catalog id", list);
    if (!arr)
        return -1;
    if (find_id(esk_json_get(project, "components"), id) >= 0 ||
        find_id(esk_json_get(project, "boards"), id) >= 0)
        return err(why, cap, "%s is already used", id);
    copy = esk_json_clone(a, spec);
    if (!copy || esk_json_array_push(a, arr, copy)) {
        esk_json_free(a, copy);
        return -1;
    }
    return 0;
}

/* a removed instance leaves the bench: its place and the jumpers to it */
static void prune_bench(esk_alloc *a, esk_json *project, const char *id)
{
    esk_json *bench = esk_json_get_mut(esk_json_get_mut(project, "layout"), "bench");
    esk_json *wires = esk_json_get_mut(bench, "wires");
    size_t n = strlen(id), i = esk_json_len(wires);
    esk_json_remove(a, esk_json_get_mut(bench, "parts"), id);
    while (i--) {
        const esk_json *w = esk_json_at(wires, i);
        const char *ends[2];
        int k, hit = 0;
        ends[0] = esk_json_get_str(w, "from", "");
        ends[1] = esk_json_get_str(w, "to", "");
        for (k = 0; k < 2; k++)
            if (!strncmp(ends[k], id, n) && ends[k][n] == '.')
                hit = 1;
        if (hit)
            esk_json_remove_at(a, wires, i);
    }
}

/* Geometry is presentation at every depth; moving a child must retain time. */
static void strip_geometry(esk_alloc *a, esk_json *circuit)
{
    static const char *const kinds[] = {"components", "boards"};
    esk_json *blocks;
    size_t i, k;
    if (!circuit) return;
    esk_json_remove(a, circuit, "layout");
    for (k = 0; k < 2; k++) {
        esk_json *parts = esk_json_get_mut(circuit, kinds[k]);
        for (i = 0; i < esk_json_len(parts); i++)
            esk_json_remove(a, esk_json_at_mut(parts, i), "layout");
    }
    blocks = esk_json_get_mut(circuit, "blocks");
    for (i = 0; i < esk_json_len(blocks); i++) strip_geometry(a, esk_json_at_mut(blocks, i));
}

/* JSON objects are maps: key serialization order must not restart a drawing edit. */
static int same_circuit(const esk_json *a, const esk_json *b)
{
    size_t i;
    if (!a || !b) return a == b;
    if (a->type != b->type) return 0;
    if (a->type == ESK_JSON_NULL) return 1;
    if (a->type == ESK_JSON_BOOL) return a->u.boolean == b->u.boolean;
    if (a->type == ESK_JSON_STRING)
        return a->u.string.len == b->u.string.len && !memcmp(a->u.string.data, b->u.string.data, a->u.string.len);
    if (a->type == ESK_JSON_NUMBER) {
        if (a->u.number.is_integer && b->u.number.is_integer) return a->u.number.integer == b->u.number.integer;
        if ((a->u.number.is_integer && fabs(a->u.number.value) > 9007199254740992.0) ||
            (b->u.number.is_integer && fabs(b->u.number.value) > 9007199254740992.0)) return 0;
        return a->u.number.value == b->u.number.value;
    }
    if (esk_json_len(a) != esk_json_len(b)) return 0;
    for (i = 0; i < esk_json_len(a); i++) {
        const esk_json *value = esk_json_at(a, i);
        const esk_json *other = a->type == ESK_JSON_OBJECT ? esk_json_get(b, value->key) : esk_json_at(b, i);
        if (!same_circuit(value, other)) return 0;
    }
    return 1;
}

static int replace_circuit(esk_alloc *a, esk_json *project, const esk_json *e,
                           int *structural, char *why, size_t cap)
{
    static const char *const keys[] = {"components", "nets", "blocks", "layout"};
    esk_json *before = esk_json_clone(a, project), *after;
    size_t i;
    if (!before) return -1;
    for (i = 0; i < 4; i++) {
        const esk_json *v = esk_json_get(e, keys[i]);
        if ((!v || v->type == ESK_JSON_NULL) && i == 3) { esk_json_remove(a, project, keys[i]); continue; }
        if (!v || v->type != (i == 0 ? ESK_JSON_ARRAY : ESK_JSON_OBJECT)) {
            esk_json_free(a, before);
            return err(why, cap, "Invalid circuit field %s", keys[i]);
        }
        if (esk_json_object_set(a, project, keys[i], esk_json_clone(a, v))) { esk_json_free(a, before); return -1; }
    }
    {
        const esk_json *probes = esk_json_get(e, "probes");
        const esk_json *blocks = esk_json_get(project, "blocks");
        if (probes) {
            if (probes->type != ESK_JSON_ARRAY) { esk_json_free(a, before); return err(why, cap, "Invalid %s", "probes"); }
            esk_json_object_set(a, project, "probes", esk_json_clone(a, probes));
        } else esk_json_remove(a, project, "probes");
        if (esk_project_validate_organization(project, why, cap)) { esk_json_free(a, before); return -1; }
        for (i = 0; i < esk_json_len(blocks); i++)
            if (esk_project_validate_organization(esk_json_at(blocks, i), why, cap)) { esk_json_free(a, before); return -1; }
    }
    after = esk_json_clone(a, project);
    if (!after) { esk_json_free(a, before); return -1; }
    strip_geometry(a, before); strip_geometry(a, after);
    if (!same_circuit(before, after)) *structural = 1;
    esk_json_free(a, before); esk_json_free(a, after);
    return 0;
}

int esk_project_apply_edit(esk_alloc *a, esk_json *project, const esk_json *edit, int *structural,
                           char *why, size_t cap)
{
    const esk_json *e;
    esk_json *nets;
    if (!project || !edit || edit->type != ESK_JSON_OBJECT || esk_json_len(edit) != 1)
        return err(why, cap, "an edit is an object with one key%s", NULL);
    nets = object_member(a, project, "nets");
    if (!nets)
        return -1;
    e = esk_json_at(edit, 0);
    if (esk_str_eq(e->key, "set_version")) {
        const char *id = esk_json_get_str(e, "id", "");
        const esk_json *value = esk_json_get(e, "version");
        esk_json *items = esk_json_get_mut(project, "components"), *item;
        long index = find_id(items, id);
        if (index < 0) { items = esk_json_get_mut(project, "boards"); index = find_id(items, id); }
        if (index < 0) return err(why, cap, "Unknown version target %s", id);
        if (!value || (value->type != ESK_JSON_STRING && value->type != ESK_JSON_NULL))
            return err(why, cap, "Invalid version request for %s", id);
        item = esk_json_at_mut(items, (size_t)index);
        if ((value->type == ESK_JSON_NULL && !esk_json_get(item, "version")) ||
            (value->type == ESK_JSON_STRING && esk_str_eq(esk_json_get_str(item, "version", ""), esk_json_str(value)))) return 0;
        if (value->type == ESK_JSON_NULL) esk_json_remove(a, item, "version");
        else if (esk_json_set_string(a, item, "version", esk_json_str(value))) return -1;
        *structural = 1;
        return 0;
    }
    if (esk_str_eq(e->key, "set_library_lock")) {
        if (e->type == ESK_JSON_NULL) { esk_json_remove(a, project, "library_lock"); return 0; }
        if (e->type != ESK_JSON_OBJECT) return err(why, cap, "Invalid %s", "library lock");
        {
            esk_json *copy = esk_json_clone(a, e);
            if (!copy) return -1;
            if (esk_json_object_set(a, project, "library_lock", copy)) { esk_json_free(a, copy); return -1; }
        }
        return 0; /* metadata; verified before commit, no circuit restart */
    }
    if (esk_str_eq(e->key, "set_circuit")) {
        esk_json *draft = esk_json_clone(a, project);
        int changed = *structural, result;
        if (!draft) return -1;
        result = replace_circuit(a, draft, e, &changed, why, cap);
        if (!result) {
            esk_json previous = *project;
            *project = *draft;
            *draft = previous;
            *structural = changed;
        }
        esk_json_free(a, draft);
        return result;
    }

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
        esk_json *arr =
            esk_json_get_mut(project, esk_str_eq(e->key, "remove_board") ? "boards" : "components");
        long i = find_id(arr, id);
        if (i < 0)
            return err(why, cap, "no %s to remove", id);
        *structural = 1;
        esk_json_remove_at(a, arr, (size_t)i);
        {
            esk_json *groups = esk_json_get_mut(esk_json_get_mut(project, "layout"), "groups");
            size_t gi = esk_json_len(groups);
            while (gi--) {
                esk_json *members = esk_json_get_mut(esk_json_at_mut(groups, gi), "members");
                size_t mi = esk_json_len(members);
                while (mi--)
                    if (esk_str_eq(id, esk_json_str(esk_json_at(members, mi))))
                        esk_json_remove_at(a, members, mi);
                if (!esk_json_len(members))
                    esk_json_remove_at(a, groups, gi);
            }
        }
        prune_bench(a, project, id);
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
        esk_json *layout = esk_json_get_mut(project, "layout"),
                 *probes = esk_json_get_mut(project, "probes");
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
            if (validate_placement(l, id, why, cap))
                return -1;
            {
                const esk_json *old = esk_json_get(esk_json_at(arr, (size_t)i), "layout");
                const esk_json *lock = esk_json_get(l, "locked");
                static const char *const fields[] = {"x", "y", "rotation", "mirror"};
                size_t field;
                if (lock && lock->type != ESK_JSON_BOOL)
                    return err(why, cap, "set_layout: invalid lock for %s", id);
                if (esk_json_get_bool(old, "locked", 0) && (!lock || lock->u.boolean)) {
                    for (field = 0; field < 4; field++) {
                        double before = field == 3 ? esk_json_get_bool(old, fields[field], 0)
                                                   : esk_json_get_num(old, fields[field], 0);
                        double after = field == 3 ? esk_json_get_bool(l, fields[field], 0)
                                                  : esk_json_get_num(l, fields[field], 0);
                        if (before != after)
                            return err(why, cap, "set_layout: placement is locked for %s", id);
                    }
                }
            }
            copy = esk_json_clone(a, l);
            if (copy && !esk_json_get(l, "locked") &&
                esk_json_get_bool(esk_json_get(esk_json_at(arr, (size_t)i), "layout"), "locked",
                                  0)) {
                if (esk_json_set_bool(a, copy, "locked", 1)) {
                    esk_json_free(a, copy);
                    return -1;
                }
            }
            if (!copy || esk_json_object_set(a, esk_json_at_mut(arr, (size_t)i), "layout", copy)) {
                esk_json_free(a, copy);
                return -1;
            }
            return 0;
        } else {
            static const char *const keys[] = {"wires", "labels", "buses", "groups"};
            esk_json *layout = object_member(a, project, "layout");
            const esk_json *bench = esk_json_get(e, "bench");
            size_t k;
            if (!layout)
                return -1;
            /* the bench (SPEC-007 §2): the parts' places, the jumpers, the notes */
            if (bench) {
                esk_json *copy;
                if (bench->type != ESK_JSON_OBJECT)
                    return err(why, cap, "set_layout: bench is an object%s", NULL);
                copy = esk_json_clone(a, bench);
                if (!copy || esk_json_object_set(a, layout, "bench", copy)) {
                    esk_json_free(a, copy);
                    return -1;
                }
            }
            for (k = 0; k < 4; k++) {
                const esk_json *v = esk_json_get(e, keys[k]);
                esk_json *copy;
                if (!v)
                    continue;
                if (v->type != ESK_JSON_ARRAY)
                    return err(why, cap, "set_layout: %s is a list", keys[k]);
                if (k == 3 && validate_groups(project, v, why, cap))
                    return -1;
                copy = esk_json_clone(a, v);
                if (!copy || esk_json_object_set(a, layout, keys[k], copy)) {
                    esk_json_free(a, copy);
                    return -1;
                }
            }
            return 0;
        }
    }
    if (esk_str_eq(e->key, "set_world")) {
        /* the project's world (ADR-026): its fields and bodies, as given; not
         * structural: the session applies the difference as inputs */
        esk_json *world = object_member(a, project, "world");
        static const char *const keys[] = {"fields", "bodies"};
        size_t k;
        if (!world)
            return -1;
        for (k = 0; k < 2; k++) {
            const esk_json *v = esk_json_get(e, keys[k]);
            esk_json *copy;
            if (!v)
                continue;
            if (v->type != (k ? ESK_JSON_ARRAY : ESK_JSON_OBJECT))
                return err(why, cap, k ? "set_world: %s is a list" : "set_world: %s is an object", keys[k]);
            copy = esk_json_clone(a, v);
            if (!copy || esk_json_object_set(a, world, keys[k], copy)) {
                esk_json_free(a, copy);
                return -1;
            }
        }
        return 0;
    }
    return err(why, cap, "unknown edit %s", e->key);
}
