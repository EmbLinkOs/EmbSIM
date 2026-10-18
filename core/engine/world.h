/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * The world (ADR-026, SPEC-003 §8): the room the circuit sits in. Fields
 * are ambient quantities that sensors read (temperature, humidity,
 * pressure, light, sound, wind); bodies are objects on the bench that
 * distance sensors see. A field takes effect once declared (by the
 * project's `world`, a client or a scenario): until then each sensor keeps
 * its own default, so a project without a world behaves as before. The
 * world changes only through inputs, in virtual time, recorded and
 * replayed like any other.
 */
#ifndef ESK_ENGINE_WORLD_H
#define ESK_ENGINE_WORLD_H

#include "embsim/types.h"
#include "util/alloc.h"
#include "util/json.h"
#include "util/vec.h"

typedef enum {
    ESK_WF_TEMPERATURE, /* degC */
    ESK_WF_HUMIDITY,    /* % relative humidity */
    ESK_WF_PRESSURE,    /* hPa */
    ESK_WF_ILLUMINANCE, /* lx */
    ESK_WF_SOUND,       /* dB SPL */
    ESK_WF_WIND,        /* m/s */
    ESK_WF_COUNT
} esk_world_field;

/* the pseudo-field a distance sensor binds: the nearest body along its beam */
#define ESK_WF_DISTANCE ESK_WF_COUNT

typedef struct esk_body {
    char *id;
    int sphere;     /* a sphere of diameter size[0]; else a box */
    double at[3];   /* centre, mm: x right, y towards the viewer, z up */
    double size[3]; /* width, depth, height, mm */
} esk_body;

typedef struct esk_world {
    double value[ESK_WF_COUNT];
    unsigned set; /* bit per field: declared */
    ESK_VEC(esk_body) bodies;
} esk_world;

/* the field a name names (ESK_WF_DISTANCE for "distance"), -1 if none */
int esk_world_field_index(const char *name);
const char *esk_world_field_name(int field);
const char *esk_world_field_unit(int field);
void esk_world_init(esk_world *w);
void esk_world_free(esk_alloc *a, esk_world *w);
/* a body from {shape, at, size}; NULL (or a JSON null) removes it */
es_status esk_world_set_body(esk_alloc *a, esk_world *w, const char *id, const esk_json *spec);
/* the distance (mm) from `o` along the unit vector `d` to the nearest
 * body, or -1 when the ray meets none */
double esk_world_ray(const esk_world *w, const double o[3], const double d[3]);
/* the world as JSON: {fields: {name: {value, unit, set}}, bodies: [...]} */
esk_json *esk_world_json(esk_alloc *a, const esk_world *w);

#endif
