/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The world's fields and bodies, and the rays distance sensors cast (ADR-026). */
#include <math.h>
#include <stdlib.h>
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
    return esk_str_eq(name, "distance") ? ESK_WF_DISTANCE : esk_str_eq(name, "motion") ? ESK_WF_MOTION : -1;
}

const char *esk_world_field_name(int field)
{
    return field >= 0 && field < ESK_WF_COUNT ? NAMES[field] : field == ESK_WF_DISTANCE ? "distance" : field == ESK_WF_MOTION ? "motion" : "?";
}

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

es_status esk_world_set_body(esk_alloc *a, esk_world *w, const char *id, const esk_json *spec, es_time_t now)
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
    vec3(esk_json_get(spec, "velocity_mm_s"), b.vel, 0.0);
    b.travel = esk_json_get_num(spec, "travel_mm", 0.0);
    b.warm = esk_json_get_bool(spec, "warm", 0);
    b.falls = esk_json_get_bool(spec, "falls", 0);
    b.restitution = esk_json_get_num(spec, "restitution", 0.5);
    if (b.restitution < 0)
        b.restitution = 0;
    if (b.restitution > 0.95)
        b.restitution = 0.95; /* nothing bounces back higher, or for ever */
    {
        const esk_json *path = esk_json_get(spec, "path_mm");
        size_t k, n = esk_json_len(path);
        for (k = 0; k < n && k < ESK_BODY_PATH_MAX; k++)
            vec3(esk_json_at(path, k), b.path[k], 0.0);
        b.npath = (int)(n < ESK_BODY_PATH_MAX ? n : ESK_BODY_PATH_MAX);
        b.speed = esk_json_get_num(spec, "speed_mm_s", 0.0);
        b.loop = esk_json_get_bool(spec, "loop", 0);
        if (b.npath >= 2) {
            for (k = 0; k < 3; k++)
                b.at[k] = b.path[0][k]; /* it starts at the first waypoint */
        }
    }
    if (b.travel < 0)
        b.travel = 0;
    /* the motion counts from `since_ps` when the spec says (a project's bodies: from 0), else from now */
    {
        const char *since = esk_json_get_str(spec, "since_ps", NULL);
        b.since = since ? (es_time_t)strtoull(since, NULL, 10) : now;
    }
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

static double seg_len(const double a[3], const double b[3])
{
    double dx = b[0] - a[0], dy = b[1] - a[1], dz = b[2] - a[2];
    return sqrt(dx * dx + dy * dy + dz * dz);
}

/* where a body walking its path at its speed is after `s` mm (looped, or there and back) */
static void path_at(const esk_body *b, double s, double out[3])
{
    double total = 0, t;
    int i, segs = b->loop ? b->npath : b->npath - 1, k;
    for (i = 0; i < segs; i++)
        total += seg_len(b->path[i], b->path[(i + 1) % b->npath]);
    if (total <= 0) {
        for (k = 0; k < 3; k++)
            out[k] = b->path[0][k];
        return;
    }
    if (b->loop)
        s = fmod(s, total);
    else {
        s = fmod(s, 2 * total);
        if (s > total)
            s = 2 * total - s;
    }
    for (i = 0; i < segs; i++) {
        const double *p = b->path[i], *q = b->path[(i + 1) % b->npath];
        double len = seg_len(p, q);
        if (s <= len || i == segs - 1) {
            t = len > 0 ? (s > len ? 1.0 : s / len) : 0.0;
            for (k = 0; k < 3; k++)
                out[k] = p[k] + (q[k] - p[k]) * t;
            return;
        }
        s -= len;
    }
}

/* the half height of a body: how far its centre stands above what it rests on */
static double half_height(const esk_body *b)
{
    return (b->sphere ? b->size[0] : b->size[2]) / 2.0;
}

static int over_table(const double p[3])
{
    return fabs(p[0]) <= ESK_TABLE_WIDTH / 2 && fabs(p[1]) <= ESK_TABLE_DEPTH / 2;
}

/*
 * A body let go: flights under gravity, each ending on the table's top (when it comes down over
 * it from above) or on the floor, the bounce sending it up again at `restitution` times the speed
 * it came down at, sideways speed kept, until a bounce would rise less than a tenth of a
 * millimetre: then it rests there. Each flight is solved, not stepped, so the answer is the same
 * whenever it is asked. Returns 1 when at rest by `dt`.
 */
static int fall_at(const esk_body *b, double dt, double out[3])
{
    const double g = ESK_GRAVITY, hh = half_height(b);
    double p[3], v[3];
    int k, n;
    for (k = 0; k < 3; k++) {
        p[k] = b->at[k];
        v[k] = b->vel[k];
    }
    if (p[2] - hh < ESK_FLOOR_Z)
        p[2] = ESK_FLOOR_Z + hh; /* never under the floor */
    for (n = 0; n < 512; n++) {
        double zs, t, rise, land[3];
        /* where it would land on the table's top, if it comes down onto it from above */
        zs = ESK_FLOOR_Z;
        if (p[2] - hh >= -1e-9) {
            rise = v[2] * v[2] + 2 * g * (p[2] - hh);
            t = (v[2] + sqrt(rise > 0 ? rise : 0)) / g;
            land[0] = p[0] + v[0] * t;
            land[1] = p[1] + v[1] * t;
            land[2] = 0;
            if (over_table(land))
                zs = 0;
        }
        rise = v[2] * v[2] + 2 * g * (p[2] - hh - zs);
        t = (v[2] + sqrt(rise > 0 ? rise : 0)) / g;
        if (t >= dt) {
            for (k = 0; k < 2; k++)
                out[k] = p[k] + v[k] * dt;
            out[2] = p[2] + v[2] * dt - g * dt * dt / 2;
            return 0;
        }
        for (k = 0; k < 2; k++)
            p[k] += v[k] * t;
        p[2] = zs + hh;
        v[2] = -(v[2] - g * t) * b->restitution;
        dt -= t;
        if (v[2] * v[2] / (2 * g) < 0.1)
            break; /* the bounce would rise less than 0.1 mm: it rests (and stops sliding) */
    }
    for (k = 0; k < 3; k++)
        out[k] = p[k];
    return 1;
}

int esk_world_body_moving(const esk_body *b, es_time_t now)
{
    double p[3];
    if (b->falls)
        return !fall_at(b, now > b->since ? (double)(now - b->since) / 1e12 : 0.0, p);
    return b->vel[0] != 0 || b->vel[1] != 0 || b->vel[2] != 0 || (b->npath >= 2 && b->speed > 0);
}

void esk_world_body_at(const esk_body *b, es_time_t now, double out[3])
{
    double speed = sqrt(b->vel[0] * b->vel[0] + b->vel[1] * b->vel[1] + b->vel[2] * b->vel[2]);
    double dt = now > b->since ? (double)(now - b->since) / 1e12 : 0.0, s;
    int k;
    if (b->falls) {
        (void)fall_at(b, dt, out);
        return;
    }
    if (b->npath >= 2 && b->speed > 0) {
        path_at(b, b->speed * dt, out);
        return;
    }
    if (speed <= 0 || dt <= 0) {
        for (k = 0; k < 3; k++)
            out[k] = b->at[k];
        return;
    }
    s = speed * dt;
    if (b->travel > 0) {
        /* back and forth: out along the velocity for `travel`, then back */
        s = fmod(s, 2 * b->travel);
        if (s > b->travel)
            s = 2 * b->travel - s;
    }
    for (k = 0; k < 3; k++)
        out[k] = b->at[k] + b->vel[k] / speed * s;
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

double esk_world_ray(const esk_world *w, const double o[3], const double d[3], es_time_t now)
{
    double best = -1.0;
    size_t i;
    for (i = 0; i < w->bodies.len; i++) {
        esk_body b = w->bodies.data[i];
        double t;
        esk_world_body_at(&w->bodies.data[i], now, b.at);
        t = b.sphere ? ray_sphere(&b, o, d) : ray_box(&b, o, d);
        if (t >= 0 && (best < 0 || t < best))
            best = t;
    }
    return best;
}

/*
 * Which of the lens's view cones a point is in, seen from `o` along `d` (z up): the Fresnel lens
 * divides the field into cones `zone_deg` wide across and up, the optical axis on a boundary (the
 * dual element's two halves side by side). Returns 0 outside the field of view or range.
 */
static int lens_zone(const double p[3], const double o[3], const double d[3], double cos_half, double range_mm,
                     double zone_deg, long *ix, long *iy)
{
    const double rad = 180.0 / 3.14159265358979323846;
    double r[3], right[3], up[3], dist, along, n;
    int k;
    for (k = 0; k < 3; k++)
        r[k] = p[k] - o[k];
    dist = sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
    along = r[0] * d[0] + r[1] * d[1] + r[2] * d[2];
    if (dist > range_mm || dist <= 0 || along < cos_half * dist)
        return 0;
    /* the lens's own across (right) and up, from the axis and the room's up */
    right[0] = d[1];
    right[1] = -d[0];
    right[2] = 0;
    n = sqrt(right[0] * right[0] + right[1] * right[1]);
    if (n < 1e-9) {
        right[0] = 1;
        right[1] = 0;
        n = 1;
    }
    right[0] /= n;
    right[1] /= n;
    up[0] = right[1] * d[2] - right[2] * d[1];
    up[1] = right[2] * d[0] - right[0] * d[2];
    up[2] = right[0] * d[1] - right[1] * d[0];
    *ix = (long)floor(atan2(r[0] * right[0] + r[1] * right[1] + r[2] * right[2], along) * rad / zone_deg);
    *iy = (long)floor(atan2(r[0] * up[0] + r[1] * up[1] + r[2] * up[2], along) * rad / zone_deg);
    return 1;
}

int esk_world_motion(const esk_world *w, const double o[3], const double d[3], double half_angle_deg, double range_mm,
                     double zone_deg, es_time_t look, es_time_t now)
{
    double cos_half = cos(half_angle_deg * 3.14159265358979323846 / 180.0);
    es_time_t then = now > look ? now - look : 0;
    size_t i;
    for (i = 0; i < w->bodies.len; i++) {
        const esk_body *b = &w->bodies.data[i];
        double p[3], q[3];
        long ax = 0, ay = 0, bx = 0, by = 0;
        int in_now, in_then;
        if (!b->warm || (!esk_world_body_moving(b, now) && !esk_world_body_moving(b, then)))
            continue; /* at the room's temperature, or still: nothing changes in the infrared */
        esk_world_body_at(b, now, p);
        esk_world_body_at(b, then, q);
        in_now = lens_zone(p, o, d, cos_half, range_mm, zone_deg, &ax, &ay);
        in_then = lens_zone(q, o, d, cos_half, range_mm, zone_deg, &bx, &by);
        if (zone_deg <= 0) {
            if (in_now)
                return 1; /* no lens: any warm body moving in the field */
            continue;
        }
        /* the element sees a change when the body's image moves from one cone to the next, or into
         * or out of the field */
        if (in_now != in_then || (in_now && (ax != bx || ay != by)))
            return 1;
    }
    return 0;
}

esk_json *esk_world_json(esk_alloc *a, const esk_world *w, es_time_t now)
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
        double pos[3];
        int m, moving = (b->vel[0] != 0 || b->vel[1] != 0 || b->vel[2] != 0) && !b->falls;
        if (!j || !at || !size) {
            esk_json_free(a, j);
            esk_json_free(a, at);
            esk_json_free(a, size);
            continue;
        }
        esk_world_body_at(b, now, pos);
        for (m = 0; m < 3; m++) {
            esk_json_array_push(a, at, esk_json_new_number(a, pos[m]));
            esk_json_array_push(a, size, esk_json_new_number(a, b->size[m]));
        }
        if (b->npath >= 2) {
            esk_json *path = esk_json_new_array(a);
            int q;
            for (q = 0; q < b->npath && path; q++) {
                esk_json *pt = esk_json_new_array(a);
                for (m = 0; m < 3 && pt; m++)
                    esk_json_array_push(a, pt, esk_json_new_number(a, b->path[q][m]));
                if (pt)
                    esk_json_array_push(a, path, pt);
            }
            if (path)
                esk_json_object_set(a, j, "path_mm", path);
            esk_json_set_number(a, j, "speed_mm_s", b->speed);
            if (b->loop)
                esk_json_set_bool(a, j, "loop", 1);
        }
        if (moving) {
            esk_json *v = esk_json_new_array(a);
            for (m = 0; m < 3 && v; m++)
                esk_json_array_push(a, v, esk_json_new_number(a, b->vel[m]));
            if (v)
                esk_json_object_set(a, j, "velocity_mm_s", v);
            if (b->travel > 0)
                esk_json_set_number(a, j, "travel_mm", b->travel);
        }
        esk_json_set_string(a, j, "id", b->id);
        esk_json_set_string(a, j, "shape", b->sphere ? "sphere" : "box");
        if (b->warm)
            esk_json_set_bool(a, j, "warm", 1);
        if (b->falls) {
            esk_json_set_bool(a, j, "falls", 1);
            esk_json_set_number(a, j, "restitution", b->restitution);
            esk_json_set_bool(a, j, "resting", !esk_world_body_moving(b, now));
            if (b->vel[0] != 0 || b->vel[1] != 0 || b->vel[2] != 0) {
                esk_json *v = esk_json_new_array(a);
                for (m = 0; m < 3 && v; m++)
                    esk_json_array_push(a, v, esk_json_new_number(a, b->vel[m]));
                if (v)
                    esk_json_object_set(a, j, "velocity_mm_s", v);
            }
        }
        esk_json_object_set(a, j, "at", at);
        esk_json_object_set(a, j, "size", size);
        esk_json_array_push(a, bodies, j);
    }
    esk_json_object_set(a, o, "fields", fields);
    esk_json_object_set(a, o, "bodies", bodies);
    return o;
}
