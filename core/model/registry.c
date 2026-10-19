/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "model/registry.h"
#include <string.h>
#include "analog/analog.h"

/* generic behaviour for a component whose model is an analog device: an
 * interactive input touches the island so the step restarts from now */
static es_status adev_on_set(esk_comp *c, const char *input, const esk_json *value)
{
    ES_UNUSED(input);
    ES_UNUSED(value);
    if (c->adev && c->adev->isl)
        esk_island_touch(c->adev->isl, c->e->sched.now);
    return ES_OK;
}

/* a wake the device asked for (esk_comp_wake_at) */
static void adev_on_time(esk_comp *c, uint32_t token)
{
    ES_UNUSED(token);
    if (c->adev && c->adev->isl)
        esk_adev_on_time(c->adev, c->e->sched.now);
}

static void adev_observe(esk_comp *c, esk_json *out)
{
    if (c->adev && c->adev->ops->observe)
        c->adev->ops->observe(c->adev, out);
}

es_status esk_registry_init(esk_registry *r, esk_alloc *a)
{
    size_t n, i;
    const esk_model_ops *const *dig;
    const esk_adev_ops *const *adevs;
    memset(r, 0, sizeof *r);
    r->a = a;
    esk_map_init(&r->by_name, a);
    dig = esk_digital_models(&n);
    for (i = 0; i < n; i++)
        if (esk_map_set(&r->by_name, dig[i]->name, (void *)dig[i]))
            return ES_ENOMEM;
    adevs = esk_adev_table(&n);
    r->wrappers = (esk_model_ops *)esk_calloc(a, n ? n : 1, sizeof *r->wrappers);
    if (!r->wrappers)
        return ES_ENOMEM;
    r->nwrappers = n;
    for (i = 0; i < n; i++) {
        esk_model_ops *w = &r->wrappers[i];
        const esk_model_ops *dig_same = (const esk_model_ops *)esk_map_get(&r->by_name, adevs[i]->name);
        if (dig_same) {
            /* one name, two tiers (a pull is a weak driver digitally and a resistor electrically):
             * keep the digital behaviour and attach the device */
            *w = *dig_same;
            if (!w->on_set)
                w->on_set = adev_on_set;
            if (!w->observe)
                w->observe = adev_observe;
            if (!w->on_time)
                w->on_time = adev_on_time;
        } else {
            w->name = adevs[i]->name;
            w->state_size = 0;
            w->on_set = adev_on_set;
            w->observe = adev_observe;
            w->on_time = adev_on_time;
        }
        w->adev = adevs[i];
        w->joins_nets = 1;
        if (esk_map_set(&r->by_name, w->name, w))
            return ES_ENOMEM;
    }
    return ES_OK;
}

void esk_registry_free(esk_registry *r)
{
    esk_map_free(&r->by_name);
    esk_free(r->a, r->wrappers, (r->nwrappers ? r->nwrappers : 1) * sizeof *r->wrappers);
    r->wrappers = NULL;
}

const esk_model_ops *esk_registry_find(const esk_registry *r, const char *name)
{
    return (const esk_model_ops *)esk_map_get(&r->by_name, name);
}

esk_json *esk_registry_list(const esk_registry *r)
{
    esk_json *arr = esk_json_new_array(r->a);
    size_t i = 0;
    const char *k;
    void *v;
    if (!arr)
        return NULL;
    while (esk_map_next(&r->by_name, &i, &k, &v)) {
        const esk_model_ops *ops = (const esk_model_ops *)v;
        esk_json *o = esk_json_new_object(r->a);
        if (!o)
            break;
        esk_json_set_string(r->a, o, "name", k);
        esk_json_set_string(r->a, o, "kind", ops->adev ? "electrical" : "digital");
        esk_json_array_push(r->a, arr, o);
    }
    return arr;
}
