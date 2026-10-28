/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* perf.c -- what the circuit cost a run (issues #86, #87): the analog
 * islands by solve time, the nets by edges, the parts by the events they
 * made, and the two diagnostics that name a cost a user can remove:
 * perf.hot_net (one island takes most of the analog steps) and
 * perf.island_size (a switching signal joined to a large island, the usual
 * case being a pull-up resistor drawn to a board's regulated rail). */
#include "session/session_priv.h"
#include "analog/analog.h"
#include <string.h>

#define PERF_TOP 25
#define PERF_HOT_MIN_STEPS 100000u /* below this a run is too short to matter */
#define PERF_HOT_SHARE 0.8
#define PERF_BIG_ISLAND 16          /* nodes */
#define PERF_BUSY_EDGES 1000u

static double sim_seconds(const es_session *s)
{
    return (double)s->eng.sched.now / (double)ES_PS_PER_S;
}

/* the island's busiest net that is no supply or ground, or NULL */
static esk_net *busiest_net(const esk_island *isl)
{
    esk_net *best = NULL;
    size_t i;
    for (i = 0; i < isl->nets.len; i++) {
        esk_net *n = isl->nets.data[i];
        if (n && !n->is_ground && !n->is_supply && n->edges && (!best || n->edges > best->edges))
            best = n;
    }
    return best;
}

/* the name an island goes by: its supply rail when it has one (B1.3V3),
 * else its net with the most pins */
static const char *island_name(const esk_island *isl)
{
    const esk_net *best = NULL;
    size_t i;
    for (i = 0; i < isl->nets.len; i++) {
        const esk_net *n = isl->nets.data[i];
        if (!n || n->is_ground)
            continue;
        if (!best || (n->is_supply && !best->is_supply) ||
            (n->is_supply == best->is_supply && n->pins.len > best->pins.len))
            best = n;
    }
    return best && best->name ? best->name : "?";
}

/* the rail a net is pulled to: the net is joined to its island only by
 * two-terminal parts (resistors) whose other side is a supply net, and its
 * other pins are digital, so its edges are a logic signal's, not analog
 * work (#86). The rail's net, or NULL */
static const esk_net *pulled_to_rail(const esk_net *n)
{
    const esk_net *rail = NULL;
    size_t k;
    for (k = 0; k < n->pins.len; k++) {
        const esk_pin *p = n->pins.data[k];
        const esk_comp *c = p->comp;
        switch (p->model.kind) {
        case ESK_PIN_DIGITAL_OUT: case ESK_PIN_DIGITAL_IN: case ESK_PIN_DIGITAL_IO: case ESK_PIN_NC:
            continue;
        default:
            break;
        }
        if (c && !c->adev && !(c->ops && c->ops->joins_nets))
            continue; /* a header's pin: no load, no join */
        if (c && c->ops && c->ops->joins_nets && c->npins == 2) {
            const esk_pin *o = &c->pins[&c->pins[0] == p ? 1 : 0];
            if (o->net && o->net->is_supply && !o->net->is_ground) {
                rail = o->net;
                continue;
            }
        }
        return NULL; /* an analog load: the island's work is real */
    }
    return rail;
}

void esk_session_perf_check(es_session *s)
{
    uint64_t total = 0;
    size_t i, k;
    double secs = sim_seconds(s);
    for (i = 0; i < s->eng.islands.len; i++)
        total += s->eng.islands.data[i]->steps;
    for (i = 0; i < s->eng.islands.len; i++) {
        esk_island *isl = s->eng.islands.data[i];
        esk_net *hot = busiest_net(isl);
        int said = 0;
        /* one net's edges explain most of the run's analog steps */
        if (!s->perf_hot_said && hot && total >= PERF_HOT_MIN_STEPS &&
            (double)isl->steps >= PERF_HOT_SHARE * (double)total && 5.0 * (double)hot->edges >= (double)isl->steps &&
            secs > 0) {
            s->perf_hot_said = 1;
            esk_diag_raise(&s->diag, ES_SEV_INFO, "perf.hot_net", s->eng.sched.now, "net", hot->name, NULL, 0,
                           "%s takes %.0f%% of the analog steps: each of its %llu edges (%.4g Hz) re-solves its "
                           "%u-node island of %s (embsim run --circuit-profile shows the rest)",
                           hot->name, 100.0 * (double)isl->steps / (double)total, (unsigned long long)hot->edges,
                           (double)hot->edges / 2.0 / secs, (unsigned)isl->nets.len, island_name(isl));
        }
        if ((int)isl->nets.len < PERF_BIG_ISLAND)
            continue;
        for (k = 0; k < isl->nets.len && said < 3; k++) {
            esk_net *n = isl->nets.data[k];
            const esk_net *rail;
            if (!n || n->is_ground || n->is_supply || n->edges < PERF_BUSY_EDGES || n->perf_said)
                continue;
            rail = pulled_to_rail(n);
            if (!rail)
                continue;
            n->perf_said = 1;
            esk_diag_raise(&s->diag, ES_SEV_WARNING, "perf.island_size", s->eng.sched.now, "net", n->name, NULL, 0,
                           "%s joins the %u-node island of %s through its pull-up: each of its %llu edges costs a "
                           "solve of the whole island (emblink/pull_up to the rail's voltage keeps the net on its "
                           "own, with the same levels)",
                           n->name, (unsigned)isl->nets.len, rail->name, (unsigned long long)n->edges);
            said++;
        }
    }
}

/* insertion sort of indices by a key, largest first */
static void rank(size_t *idx, const double *key, size_t n)
{
    size_t i, j;
    for (i = 1; i < n; i++) {
        size_t v = idx[i];
        for (j = i; j > 0 && key[idx[j - 1]] < key[v]; j--)
            idx[j] = idx[j - 1];
        idx[j] = v;
    }
}

static size_t *ranked(es_session *s, const double *key, size_t n)
{
    size_t *idx = n ? (size_t *)esk_calloc(s->a, n, sizeof *idx) : NULL;
    size_t i;
    for (i = 0; idx && i < n; i++)
        idx[i] = i;
    if (idx)
        rank(idx, key, n);
    return idx;
}

esk_json *esk_session_circuit_profile(es_session *s)
{
    esk_alloc *a = s->a;
    esk_json *root = esk_json_new_object(a), *isls = esk_json_new_array(a), *nets = esk_json_new_array(a),
             *comps = esk_json_new_array(a);
    size_t ni = s->eng.islands.len, nn = s->eng.nets.len, nc = s->eng.comps.len, i, k;
    double *ki = (double *)esk_calloc(a, ni + 1, sizeof *ki), *kn = (double *)esk_calloc(a, nn + 1, sizeof *kn),
           *kc = (double *)esk_calloc(a, nc + 1, sizeof *kc);
    size_t *oi = NULL, *on = NULL, *oc = NULL;
    uint64_t steps = 0, solve = 0;
    double secs = sim_seconds(s);
    if (!root || !isls || !nets || !comps || !ki || !kn || !kc)
        goto out;
    for (i = 0; i < ni; i++) {
        steps += s->eng.islands.data[i]->steps;
        solve += s->eng.islands.data[i]->solve_ns;
        ki[i] = (double)s->eng.islands.data[i]->solve_ns + (double)s->eng.islands.data[i]->steps * 1e-3;
    }
    for (i = 0; i < nn; i++)
        kn[i] = (double)s->eng.nets.data[i]->edges;
    for (i = 0; i < nc; i++) {
        const esk_comp *c = s->eng.comps.data[i];
        kc[i] = (double)(c->edges + c->wakes + c->heard);
    }
    oi = ranked(s, ki, ni);
    on = ranked(s, kn, nn);
    oc = ranked(s, kc, nc);
    for (k = 0; k < ni && k < PERF_TOP && oi; k++) {
        const esk_island *isl = s->eng.islands.data[oi[k]];
        esk_json *o = esk_json_new_object(a), *names = esk_json_new_array(a);
        size_t j;
        const esk_net *hot = busiest_net(isl);
        for (j = 0; j < isl->nets.len && j < 8; j++)
            if (isl->nets.data[j] && isl->nets.data[j]->name)
                esk_json_array_push(a, names, esk_json_new_string(a, isl->nets.data[j]->name));
        esk_json_set_string(a, o, "name", island_name(isl));
        esk_json_set_integer(a, o, "nodes", (int64_t)isl->nets.len);
        esk_json_set_integer(a, o, "devices", (int64_t)isl->devs.len);
        esk_json_set_integer(a, o, "steps", (int64_t)isl->steps);
        esk_json_set_integer(a, o, "rejections", (int64_t)isl->rejections);
        esk_json_set_integer(a, o, "newton_iterations", (int64_t)isl->newton_iters);
        esk_json_set_number(a, o, "solve_s", (double)isl->solve_ns / 1e9);
        esk_json_set_number(a, o, "steps_share", steps ? (double)isl->steps / (double)steps : 0.0);
        esk_json_set_number(a, o, "solve_share", solve ? (double)isl->solve_ns / (double)solve : 0.0);
        if (hot)
            esk_json_set_string(a, o, "busiest_net", hot->name);
        esk_json_object_set(a, o, "nets", names);
        esk_json_array_push(a, isls, o);
    }
    for (k = 0; k < nn && k < PERF_TOP && on; k++) {
        const esk_net *n = s->eng.nets.data[on[k]];
        esk_json *o;
        if (!n->edges)
            break;
        o = esk_json_new_object(a);
        esk_json_set_string(a, o, "name", n->name);
        esk_json_set_integer(a, o, "edges", (int64_t)n->edges);
        esk_json_set_number(a, o, "hz", secs > 0 ? (double)n->edges / 2.0 / secs : 0.0);
        esk_json_set_integer(a, o, "island_nodes", n->island ? (int64_t)n->island->nets.len : 0);
        esk_json_array_push(a, nets, o);
    }
    for (k = 0; k < nc && k < PERF_TOP && oc; k++) {
        const esk_comp *c = s->eng.comps.data[oc[k]];
        esk_json *o;
        if (!kc[oc[k]])
            break;
        o = esk_json_new_object(a);
        esk_json_set_string(a, o, "id", c->id);
        esk_json_set_string(a, o, "kind", c->kind ? c->kind : "");
        esk_json_set_integer(a, o, "edges", (int64_t)c->edges);
        esk_json_set_integer(a, o, "wakes", (int64_t)c->wakes);
        esk_json_set_integer(a, o, "inputs", (int64_t)c->heard);
        esk_json_array_push(a, comps, o);
    }
    esk_json_set_number(a, root, "simulated_s", secs);
    esk_json_set_number(a, root, "wall_time_s", s->wall_time_s);
    esk_json_set_integer(a, root, "analog_steps", (int64_t)steps);
    esk_json_set_number(a, root, "solve_s", (double)solve / 1e9);
    esk_json_set_integer(a, root, "events", (int64_t)s->eng.sched.processed);
    esk_json_object_set(a, root, "islands", isls);
    esk_json_object_set(a, root, "nets", nets);
    esk_json_object_set(a, root, "components", comps);
    isls = nets = comps = NULL;
out:
    esk_json_free(a, isls);
    esk_json_free(a, nets);
    esk_json_free(a, comps);
    if (ki) esk_free(a, ki, (ni + 1) * sizeof *ki);
    if (kn) esk_free(a, kn, (nn + 1) * sizeof *kn);
    if (kc) esk_free(a, kc, (nc + 1) * sizeof *kc);
    if (oi) esk_free(a, oi, ni * sizeof *oi);
    if (on) esk_free(a, on, nn * sizeof *on);
    if (oc) esk_free(a, oc, nc * sizeof *oc);
    return root;
}
