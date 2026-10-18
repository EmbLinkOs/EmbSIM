/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The world's fields and bodies, and the rays distance sensors cast (ADR-026). */
#include <math.h>
#include <string.h>

#include "engine/world.h"
#include "util/num.h"
#include "util/str.h"

static const char *const NAMES[ESK_WF_COUNT] = { "temperature", "humidity", "pressure", "illuminance", "sound", "wind" };
static const char *const UNITS[ESK_WF_COUNT] = { "degC", "%", "hPa", "lx", "dB", "m/s" };
/* a room at the bench: 22 C, half humid, sea-level pressure, office light, quiet, still */
static const double DEFAULTS[ESK_WF_COUNT] = { 22.0, 50.0, 1013.25, 300.0, 40.0, 0.0 };

int esk_world_field_index(const char *name)
{
    int i;
    if (!name)
        return -1;
    for (i = 0; i < ESK_WF_COUNT; i++)
        if (esk_str_eq(name, NAMES[i]))
            return i;
    return esk_str_eq(name, "distance") ? ESK_WF_DISTANCE : -1;
}

const char *esk_world_field_name(int field) { return field >= 0 && field < ESK_WF_COUNT ? NAMES[field] : field == ESK_WF_DISTANCE ? "distance" : "?"; }

const char *esk_world_field_unit(int field) { return field >= 0 && field < ESK_WF_COUNT ? UNITS[field] : field == ESK_WF_DISTANCE ? "mm" : ""; }

void esk_world_init(esk_world *w)
{
    int i;
    memset(w, 0, sizeof *w);
    for (i = 0; i < ESK_WF_COUNT; i++)
        w->value[i] = DEFAULTS[i];
}

void esk_world_free(esk_alloc *a, esk_world *w)
{
    size_t i;
    for (i = 0; i < w->bodies.len; i++)
        esk_strfree(a, w->bodies.data[i].id);
    esk_vec_free(a, &w->bodies);
    esk_world_init(w);
}

static void vec3(const esk_json *arr, double out[3], double def)
{
    size_t i;
    for (i = 0; i < 3; i++) {
        double v = def;
        const esk_json *j = esk_json_at(arr, i);
        if (j)
            (void)esk_json_to_double(j, &v);
        out[i] = v;
    }
}

es_status esk_world_set_body(esk_alloc *a, esk_world *w, const char *id, const esk_json *spec)
{
    size_t i;
    esk_body b;
    if (!id || !*id)
        return ES_EINVAL;
    for (i = 0; i < w->bodies.len; i++)
        if (esk_str_eq(w->bodies.data[i].id, id))
            break;
    if (!spec || spec->type == ESK_JSON_NULL) {
        if (i == w->bodies.len)
            return ES_ENOTFOUND;
        esk_strfree(a, w->bodies.data[i].id);
        memmove(&w->bodies.data[i], &w->bodies.data[i + 1], (w->bodies.len - i - 1) * sizeof b);
        w->bodies.len--;
        return ES_OK;
    }
    if (spec->type != ESK_JSON_OBJECT)
        return ES_EINVAL;
    memset(&b, 0, sizeof b);
    b.sphere = esk_str_eq(esk_json_get_str(spec, "shape", "box"), "sphere");
    vec3(esk_json_get(spec, "at"), b.at, 0.0);
    vec3(esk_json_get(spec, "size"), b.size, 50.0);
    if (i < w->bodies.len) {
        b.id = w->bodies.data[i].id;
        w->bodies.data[i] = b;
        return ES_OK;
    }
    b.id = esk_strdup(a, id);
    if (!b.id || esk_vec_push(a, &w->bodies, b)) {
        esk_strfree(a, b.id);
        return ES_ENOMEM;
    }
    return ES_OK;
}

/* the slab method: the ray against an axis-aligned box */
static double ray_box(const esk_body *b, const double o[3], const double d[3])
{
    double t0 = 0.0, t1 = HUGE_VAL;
    int k;
    for (k = 0; k < 3; k++) {
        double lo = b->at[k] - b->size[k] / 2, hi = b->at[k] + b->size[k] / 2;
        if (fabs(d[k]) < 1e-12) {
            if (o[k] < lo || o[k] > hi)
                return -1.0;
        } else {
            double a = (lo - o[k]) / d[k], c = (hi - o[k]) / d[k];
            if (a > c) {
                double t = a;
                a = c;
                c = t;
            }
            if (a > t0)
                t0 = a;
            if (c < t1)
                t1 = c;
            if (t0 > t1)
                return -1.0;
        }
    }
    return t0;
}

static double ray_sphere(const esk_body *b, const double o[3], const double d[3])
{
    double r = b->size[0] / 2, oc[3], bq = 0, c = 0, disc, t;
    int k;
    for (k = 0; k < 3; k++) {
        oc[k] = o[k] - b->at[k];
        bq += oc[k] * d[k];
        c += oc[k] * oc[k];
    }
    c -= r * r;
    disc = bq * bq - c;
    if (disc < 0)
        return -1.0;
    t = -bq - sqrt(disc);
    if (t < 0)
        t = -bq + sqrt(disc);
    return t < 0 ? -1.0 : (c <= 0 ? 0.0 : t);
}

double esk_world_ray(const esk_world *w, const double o[3], const double d[3])
{
    double best = -1.0;
    size_t i;
    for (i = 0; i < w->bodies.len; i++) {
        const esk_body *b = &w->bodies.data[i];
        double t = b->sphere ? ray_sphere(b, o, d) : ray_box(b, o, d);
        if (t >= 0 && (best < 0 || t < best))
            best = t;
    }
    return best;
}

esk_json *esk_world_json(esk_alloc *a, const esk_world *w)
{
    esk_json *o = esk_json_new_object(a), *fields = esk_json_new_object(a), *bodies = esk_json_new_array(a);
    size_t i;
    int k;
    if (!o || !fields || !bodies) {
        esk_json_free(a, o);
        esk_json_free(a, fields);
        esk_json_free(a, bodies);
        return NULL;
    }
    for (k = 0; k < ESK_WF_COUNT; k++) {
        esk_json *f = esk_json_new_object(a);
        if (!f)
            continue;
        esk_json_set_number(a, f, "value", w->value[k]);
        esk_json_set_string(a, f, "unit", UNITS[k]);
        esk_json_set_bool(a, f, "set", (w->set >> k) & 1u);
        esk_json_object_set(a, fields, NAMES[k], f);
    }
    for (i = 0; i < w->bodies.len; i++) {
        const esk_body *b = &w->bodies.data[i];
        esk_json *j = esk_json_new_object(a), *at = esk_json_new_array(a), *size = esk_json_new_array(a);
        int m;
        if (!j || !at || !size) {
            esk_json_free(a, j);
            esk_json_free(a, at);
            esk_json_free(a, size);
            continue;
        }
        for (m = 0; m < 3; m++) {
            esk_json_array_push(a, at, esk_json_new_number(a, b->at[m]));
            esk_json_array_push(a, size, esk_json_new_number(a, b->size[m]));
        }
        esk_json_set_string(a, j, "id", b->id);
        esk_json_set_string(a, j, "shape", b->sphere ? "sphere" : "box");
        esk_json_object_set(a, j, "at", at);
        esk_json_object_set(a, j, "size", size);
        esk_json_array_push(a, bodies, j);
    }
    esk_json_object_set(a, o, "fields", fields);
    esk_json_object_set(a, o, "bodies", bodies);
    return o;
}
