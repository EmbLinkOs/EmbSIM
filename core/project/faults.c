/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* faults.c -- the fault mode (ADR-034): cuts, shorts and failed parts put
 * into a circuit on purpose, to see what the firmware and the circuit do
 * when a wire breaks, a solder bridge forms or a part dies.
 *
 * A project's `faults` list them; each is a part (emblink/fault, a switch)
 * the loader wires in, and its input `active` turns it on or off at any
 * time (a scenario, the workbench, a script), `active` its state at the
 * start:
 *
 *   {"id": "F1", "kind": "open", "pin": "R1.1"}      the pin cut from its net
 *   {"id": "F2", "kind": "short", "nets": ["A","B"]} the nets bridged
 *   {"id": "F3", "kind": "short", "pins": ["U1.3","U1.4"]}
 *   {"id": "F4", "kind": "fail_open", "part": "D1"}  a two-terminal part open
 *   {"id": "F5", "kind": "fail_short", "part": "C1"} ...or shorted
 *
 * `resistance` is the cut's or short's resistance while it conducts (1 mOhm
 * by default: a solder bridge; a cut wire's intact copper).
 *
 * Inside: the project's components and nets are rewritten for the load
 * only (the project as saved keeps its `faults` list, not the switches):
 * a cut pin moves to a net of its own, joined to its old net through the
 * fault's switch. */
#include "project/loader.h"
#include "util/json.h"
#include "util/str.h"
#include <stdio.h>
#include <string.h>

/* the name of the net holding `pin`, or NULL */
static const char *net_of(const esk_json *nets, const char *pin)
{
    size_t i, k;
    for (i = 0; i < esk_json_len(nets); i++) {
        const esk_json *n = esk_json_at(nets, i);
        for (k = 0; k < esk_json_len(n); k++)
            if (esk_str_eq(esk_json_str(esk_json_at(n, k)), pin))
                return n->key;
    }
    return NULL;
}

/* the connected pins of part `id` ("D1.A", "D1.K"), at most `cap` */
static int pins_of(const esk_json *nets, const char *id, const char **out, int cap)
{
    size_t i, k, len = strlen(id);
    int n = 0;
    for (i = 0; i < esk_json_len(nets); i++) {
        const esk_json *net = esk_json_at(nets, i);
        for (k = 0; k < esk_json_len(net); k++) {
            const char *p = esk_json_str(esk_json_at(net, k));
            if (p && !strncmp(p, id, len) && p[len] == '.') {
                if (n < cap)
                    out[n] = p;
                n++;
            }
        }
    }
    return n;
}

/* nets with `pin` moved out of its net into `fresh` */
static int move_pin(esk_alloc *a, esk_json *nets, const char *pin, const char *fresh, const char *stub)
{
    size_t i, k;
    for (i = 0; i < esk_json_len(nets); i++) {
        esk_json *n = (esk_json *)esk_json_at(nets, i), *kept = esk_json_new_array(a);
        int found = 0;
        if (!kept)
            return -1;
        for (k = 0; k < esk_json_len(n); k++) {
            const char *p = esk_json_str(esk_json_at(n, k));
            if (esk_str_eq(p, pin))
                found = 1;
            else
                (void)esk_json_array_push(a, kept, esk_json_new_string(a, p ? p : ""));
        }
        if (found) {
            esk_json *own = esk_json_new_array(a);
            char key[160];
            (void)esk_json_array_push(a, kept, esk_json_new_string(a, stub));
            snprintf(key, sizeof key, "%s", n->key);
            (void)esk_json_object_set(a, nets, key, kept);
            if (!own)
                return -1;
            (void)esk_json_array_push(a, own, esk_json_new_string(a, pin));
            return esk_json_object_set(a, nets, fresh, own);
        }
        esk_json_free(a, kept);
    }
    return -1;
}

static void add_to_net(esk_alloc *a, esk_json *nets, const char *net, const char *pin)
{
    esk_json *n = (esk_json *)esk_json_get(nets, net);
    if (n)
        (void)esk_json_array_push(a, n, esk_json_new_string(a, pin));
}

/* the fault's switch, `kind` "open" or "short" */
static int add_switch(esk_alloc *a, esk_json *comps, const esk_json *f, const char *id, const char *kind)
{
    esk_json *c = esk_json_new_object(a), *params = esk_json_new_object(a);
    const esk_json *r = esk_json_get(f, "resistance");
    if (!c || !params)
        return -1;
    (void)esk_json_set_string(a, c, "id", id);
    (void)esk_json_set_string(a, c, "component", "emblink/fault");
    (void)esk_json_set_string(a, params, "fault", kind);
    (void)esk_json_set_bool(a, params, "initially_active", esk_json_get_bool(f, "active", 0));
    if (r)
        (void)esk_json_object_set(a, params, "ron", esk_json_clone(a, r));
    (void)esk_json_object_set(a, c, "params", params);
    return esk_json_array_push(a, comps, c);
}

esk_json *esk_project_with_faults(esk_alloc *a, const esk_json *project, char *why, size_t cap)
{
    const esk_json *faults = esk_json_get(project, "faults");
    esk_json *view, *comps, *nets;
    size_t i;
    why[0] = 0;
    if (!esk_json_len(faults))
        return NULL;
    view = esk_json_new_object(a);
    comps = esk_json_clone(a, esk_json_get(project, "components"));
    nets = esk_json_clone(a, esk_json_get(project, "nets"));
    if (!comps)
        comps = esk_json_new_array(a);
    if (!nets)
        nets = esk_json_new_object(a);
    if (!view || !comps || !nets) {
        esk_json_free(a, view);
        esk_json_free(a, comps);
        esk_json_free(a, nets);
        snprintf(why, cap, "out of memory");
        return NULL;
    }
    (void)esk_json_object_set(a, view, "components", comps);
    (void)esk_json_object_set(a, view, "nets", nets);
    for (i = 0; i < esk_json_len(faults) && !why[0]; i++) {
        const esk_json *f = esk_json_at(faults, i);
        const char *id = esk_json_get_str(f, "id", NULL), *kind = esk_json_get_str(f, "kind", "");
        char stub[96], fresh[128];
        if (!id || !esk_is_identifier(id)) {
            snprintf(why, cap, "fault %u: an id (an identifier) is needed", (unsigned)i);
            break;
        }
        snprintf(stub, sizeof stub, "%s.2", id);
        if (esk_str_eq(kind, "open") || esk_str_eq(kind, "fail_open")) {
            const char *pin = esk_json_get_str(f, "pin", NULL), *pins[3];
            if (esk_str_eq(kind, "fail_open")) {
                const char *part = esk_json_get_str(f, "part", "");
                if (pins_of(nets, part, pins, 3) != 2) {
                    snprintf(why, cap, "fault %s: fail_open takes a two-terminal part with both pins connected (%s); "
                                       "cut a pin with an open fault",
                             id, part);
                    break;
                }
                pin = pins[0];
            }
            if (!pin || !net_of(nets, pin)) {
                snprintf(why, cap, "fault %s: the pin %s is in no net", id, pin ? pin : "(none)");
                break;
            }
            snprintf(fresh, sizeof fresh, "%s_cut", id);
            {
                char keep[96];
                snprintf(keep, sizeof keep, "%s", pin); /* pin points into nets, which move_pin rewrites */
                if (move_pin(a, nets, keep, fresh, stub) || add_switch(a, comps, f, id, "open")) {
                    snprintf(why, cap, "fault %s: out of memory", id);
                    break;
                }
            }
            snprintf(stub, sizeof stub, "%s.1", id);
            add_to_net(a, nets, fresh, stub);
        } else if (esk_str_eq(kind, "short") || esk_str_eq(kind, "fail_short")) {
            const char *na = NULL, *nb = NULL;
            const esk_json *list = esk_json_get(f, "nets");
            char an[128], bn[128];
            if (esk_str_eq(kind, "fail_short")) {
                const char *pins[3], *part = esk_json_get_str(f, "part", "");
                if (pins_of(nets, part, pins, 3) != 2) {
                    snprintf(why, cap, "fault %s: fail_short takes a two-terminal part with both pins connected (%s)", id,
                             part);
                    break;
                }
                na = net_of(nets, pins[0]);
                nb = net_of(nets, pins[1]);
            } else if (esk_json_len(list) == 2) {
                na = esk_json_str(esk_json_at(list, 0));
                nb = esk_json_str(esk_json_at(list, 1));
                if (!esk_json_get(nets, na ? na : "") || !esk_json_get(nets, nb ? nb : "")) {
                    snprintf(why, cap, "fault %s: a short's nets must be the project's", id);
                    break;
                }
            } else if (esk_json_len(esk_json_get(f, "pins")) == 2) {
                const esk_json *pl = esk_json_get(f, "pins");
                na = net_of(nets, esk_json_str(esk_json_at(pl, 0)) ? esk_json_str(esk_json_at(pl, 0)) : "");
                nb = net_of(nets, esk_json_str(esk_json_at(pl, 1)) ? esk_json_str(esk_json_at(pl, 1)) : "");
            }
            if (!na || !nb) {
                snprintf(why, cap, "fault %s: a short names two nets, two connected pins, or a part", id);
                break;
            }
            snprintf(an, sizeof an, "%s", na);
            snprintf(bn, sizeof bn, "%s", nb);
            if (add_switch(a, comps, f, id, "short")) {
                snprintf(why, cap, "fault %s: out of memory", id);
                break;
            }
            snprintf(stub, sizeof stub, "%s.1", id);
            add_to_net(a, nets, an, stub);
            snprintf(stub, sizeof stub, "%s.2", id);
            add_to_net(a, nets, bn, stub);
        } else {
            snprintf(why, cap, "fault %s: kind must be open, short, fail_open or fail_short", id);
        }
    }
    if (why[0]) {
        esk_json_free(a, view);
        return NULL;
    }
    return view;
}
