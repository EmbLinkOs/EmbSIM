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
/* the pseudo-field a PIR sensor binds: a warm body moving in its field of view */
#define ESK_WF_MOTION (ESK_WF_COUNT + 1)

#define ESK_BODY_PATH_MAX 16

typedef struct esk_body {
    char *id;
    int sphere;     /* a sphere of diameter size[0]; else a box */
    double at[3];   /* centre when set, mm: x right, y towards the viewer, z up */
    double size[3]; /* width, depth, height, mm */
    /* motion in virtual time (presentation and sensors alike): a constant
     * velocity from `at`, set at `since`; with `travel` > 0 the body goes
     * back and forth along it over that distance (a hand, a door, a cart) */
    double vel[3];  /* mm/s */
    double travel;  /* mm, 0: straight on */
    es_time_t since;
    /* or along a path of waypoints (bench mm) at `speed` mm/s, round again (`loop`) or back */
    double path[ESK_BODY_PATH_MAX][3];
    int npath;
    double speed;
    int loop;
    int warm;       /* warmer than the room (a person, an animal, a hand): a PIR sees it move */
    /* or let go: it falls under gravity from `at` with `vel` as its throw, bounces off the table's
     * top or the floor losing speed by `restitution` (0: it stops dead), and comes to rest */
    int falls;
    double restitution;
} esk_body;

/* the room's furniture the engine knows, as the bench draws it (ui/packages/bench/src/room.ts):
 * a 1800 x 900 mm table centred under the bench, its top at z = 0, the floor 750 mm below */
#define ESK_TABLE_WIDTH 1800.0
#define ESK_TABLE_DEPTH 900.0
#define ESK_FLOOR_Z (-750.0)
/* standard gravity, mm/s^2 */
#define ESK_GRAVITY 9806.65

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
/* a body from {shape, at, size, velocity_mm_s, travel_mm, path_mm, speed_mm_s,
 * loop, warm, falls, restitution, since_ps}, set at `now` (its motion counts
 * from since_ps when given, else from now);
 * NULL (or a JSON null) removes it */
es_status esk_world_set_body(esk_alloc *a, esk_world *w, const char *id, const esk_json *spec, es_time_t now);
/* where a body's centre is at `now` */
void esk_world_body_at(const esk_body *b, es_time_t now, double out[3]);
/* whether a body is moving at `now` (a velocity, a path, or falling and not yet at rest) */
int esk_world_body_moving(const esk_body *b, es_time_t now);
/* the distance (mm) from `o` along the unit vector `d` to the nearest
 * body where the bodies are at `now`, or -1 when the ray meets none */
double esk_world_ray(const esk_world *w, const double o[3], const double d[3], es_time_t now);
/* whether a PIR at `o` looking along the unit axis `d` sees a change at `now`:
 * a warm body within `range_mm` and `half_angle_deg` of the axis whose image
 * crossed from one of the lens's view cones (`zone_deg` wide; 0: no lens, any
 * warm body moving) into another since `now - look`, or into or out of view */
int esk_world_motion(const esk_world *w, const double o[3], const double d[3], double half_angle_deg, double range_mm,
                     double zone_deg, es_time_t look, es_time_t now);
/* the world as JSON at `now`: {fields: {name: {value, unit, set}}, bodies: [...]} */
esk_json *esk_world_json(esk_alloc *a, const esk_world *w, es_time_t now);

#endif
