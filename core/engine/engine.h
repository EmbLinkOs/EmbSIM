/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* engine.h -- the simulation world (SPEC-003): components, pins with pin
 * models, nets with digital resolution, islands, and the API models use.
 * The analog side of an island is in analog/analog.h. */
#ifndef ESK_ENGINE_H
#define ESK_ENGINE_H

#include "embsim/types.h"
#include "engine/air.h"
#include "engine/inet.h"
#include "engine/world.h"
#include "time/clock.h"
#include "time/sched.h"
#include "util/alloc.h"
#include "util/diag.h"
#include "util/json.h"
#include "util/map.h"
#include "util/vec.h"

typedef struct esk_engine esk_engine;
typedef struct esk_net esk_net;
typedef struct esk_pin esk_pin;
typedef struct esk_comp esk_comp;
typedef struct esk_island esk_island;
typedef struct esk_adev esk_adev;
typedef struct esk_adev_ops esk_adev_ops;

typedef enum esk_logic { ESK_L0 = 0, ESK_L1 = 1, ESK_LZ = 2, ESK_LX = 3 } esk_logic;
typedef enum esk_strength { ESK_STR_HIZ = 0, ESK_STR_WEAK = 1, ESK_STR_STRONG = 2, ESK_STR_SUPPLY = 3 } esk_strength;
typedef enum esk_pin_kind {
    ESK_PIN_DIGITAL_OUT, ESK_PIN_DIGITAL_IN, ESK_PIN_DIGITAL_IO, ESK_PIN_ANALOG,
    ESK_PIN_POWER, ESK_PIN_PASSIVE, ESK_PIN_NC
} esk_pin_kind;
typedef enum esk_drive { ESK_DRIVE_PUSH_PULL, ESK_DRIVE_OPEN_DRAIN, ESK_DRIVE_OPEN_SOURCE, ESK_DRIVE_TRISTATE } esk_drive;
typedef enum esk_pull { ESK_PULL_NONE, ESK_PULL_UP, ESK_PULL_DOWN } esk_pull;
typedef enum esk_policy { ESK_POLICY_FUNCTIONAL, ESK_POLICY_TIMED, ESK_POLICY_ELECTRICAL } esk_policy;

typedef struct esk_pin_model {
    esk_pin_kind kind;
    esk_drive drive;
    esk_strength strength;
    double voh, vol, rout, slew_v_per_us; /* output side */
    /* the driver's open-circuit levels behind rout (SPEC-003 §4, ADR-023): a
     * CMOS family's rails (`rails`: nominal_v and 0 V), else voh and vol;
     * voc_high and voc_low override them (`voc_set`: bit 0 high, bit 1 low) */
    int rails, voc_set;
    double voc_high, voc_low;
    double vih, vil, hyst, rpull, leak_a; /* input side */
    esk_pull pull;
    int power_ground;      /* power: 1 for ground, 0 for supply */
    double nominal_v, tolerance;
    int has_electrical;    /* electrical parameters were given or derived from a family */
} esk_pin_model;

struct esk_pin {
    esk_comp *comp;
    uint32_t index;
    char *name;
    esk_pin_model model;
    esk_pin_model model0;  /* as described: a reset restores it (an MCU's pull
                              configuration, a script's thresholds change model) */
    esk_net *net;
    int is_output;         /* digital_io direction; fixed for other kinds */
    int watched;           /* the model wants on_input when the net changes */
    esk_logic drive_value; /* this pin's current driver value */
    esk_strength drive_strength;
    esk_logic pending_value;
    esk_event_handle pending; /* an inertial-delay change not yet applied */
    esk_logic sampled;     /* for inputs: the last value the model read (hysteresis state for bridges) */
    int floating_reported, unknown_reported;
    int analog_node;       /* node in the island, -1 when digital */
    esk_adev *bridge;      /* the bridge device when a digital pin is on an analog net */
    /* a second comparator a model sets on an input on an analog net (an
     * MCU's analog comparator): its crossings of watch_v call on_input at
     * their time, the pin's own thresholds and value untouched */
    int watch_on;
    double watch_v;
    esk_logic watch_state; /* above (L1) or below (L0) watch_v */
};

struct esk_net {
    char *name;
    uint32_t index;
    ESK_VEC(esk_pin *) pins;
    ESK_VEC(char *) aliases;
    esk_logic value;
    esk_strength strength;
    es_time_t changed_at;
    esk_island *island;    /* NULL: digital */
    int node;              /* analog node index, 0 = ground */
    double voltage;        /* last accepted analog voltage */
    int contention_reported;
    int is_ground, is_supply;
    double supply_v;
};

/* The model contract (SPEC-005 §10). */
typedef struct esk_model_ops {
    const char *name;
    size_t state_size;                       /* allocated zeroed before create */
    es_status (*create)(esk_comp *c);        /* read params, set pin models if dynamic, watch pins */
    void (*destroy)(esk_comp *c);
    void (*reset)(esk_comp *c);              /* called at session reset after create */
    void (*on_input)(esk_comp *c, esk_pin *pin);
    void (*on_time)(esk_comp *c, uint32_t token);
    es_status (*on_set)(esk_comp *c, const char *input, const esk_json *value);
    void (*observe)(esk_comp *c, esk_json *outputs);
    const esk_adev_ops *adev;                /* electrical device, or NULL */
    int joins_nets;                          /* a passive two-terminal device: its pins' nets share an island */
} esk_model_ops;

struct esk_comp {
    esk_engine *e;
    char *id;                /* instance id, scoped: "B1.R1" for a board's component */
    char *kind;              /* component description id, e.g. "emblink/resistor" */
    const esk_model_ops *ops;
    void *state;
    esk_pin *pins;
    uint32_t npins;
    esk_json *params;        /* object: resolved parameter values (numbers as doubles) */
    esk_json *inputs;        /* object: current interactive input values */
    const esk_json *desc;    /* the component description (borrowed from the catalog) */
    esk_adev *adev;          /* the analog device instance, or NULL */
    esk_event_handle wake[8];
    int functional_only;     /* no electrical model: under electrical policy stays digital */
    /* where it sits on the bench (layout.bench, SPEC-007 §2): x, y mm and its
     * turn in degrees, clockwise seen from above; for the world's rays */
    double pose[3];
    int posed;
    /* the current its supply pins draw now (ADR-036), amps: a part's model
     * sets it (esk_comp_set_supply); its power pins' bridges sink it */
    double supply_a;
};

typedef void (*esk_net_change_fn)(void *ctx, esk_net *net);

struct esk_engine {
    esk_alloc *a;
    esk_sched sched;
    esk_diag *diag;
    esk_policy policy;
    ESK_VEC(esk_comp *) comps;
    ESK_VEC(esk_net *) nets;
    ESK_VEC(esk_island *) islands;
    esk_map comp_by_id;
    esk_map net_by_name;
    uint32_t max_nets, max_comps;
    es_time_t crossing_resolution_ps;
    double default_supply_v;
    esk_net_change_fn on_net_change; /* probes and clients */
    void *on_net_change_ctx;
    /* an MCU's core enters an exception or interrupt, at its own time (a
     * recording's observations, SPEC-012 §1) */
    void (*on_irq)(void *ctx, esk_comp *mcu, int core, int cores, int n, es_time_t at);
    void *on_irq_ctx;
    int partitioned;
    int in_reset;            /* floating-input diagnostics are suppressed while models initialise */
    uint64_t signal_events;
    /* the scripted-model host (core/ext), created by the loader at the first
     * scripted component and freed after the components */
    void *ext;
    void (*ext_free)(void *ext);
    /* the room the circuit sits in (ADR-026) */
    esk_world world;
    /* the 2.4 GHz medium its radios share (air.h) */
    esk_air air;
    /* the internet beyond its routers (inet.h) */
    esk_inet inet;
};

/* the world's value for a component's input bound to it (its description's
 * `inputs[].world`), when the input was not set and the world has it: 1 and
 * *out in the input's unit, else 0 */
int esk_comp_world_input(const esk_comp *c, const char *input, double *out);
/* the world changed: `field` (an esk_world_field, or ESK_WF_DISTANCE for the
 * bodies and the parts' places); every component reading it through an
 * input it did not set hears it as that input's change */
void esk_engine_world_changed(esk_engine *e, int field);

/* 1 when no analog island is integrating: every one sits at its steady
 * state, asleep until a touch or its sources' next breakpoint */
int esk_engine_analog_quiet(esk_engine *e);

/* lifecycle */
es_status esk_engine_init(esk_engine *e, esk_alloc *a, esk_diag *diag, esk_policy policy);
void esk_engine_free(esk_engine *e);
es_status esk_engine_add_net(esk_engine *e, const char *name, esk_net **out);
es_status esk_engine_net_alias(esk_engine *e, esk_net *net, const char *alias);
/* create a component: pins from `pin_names`, models from `pin_models`; params and desc are copied/borrowed */
es_status esk_engine_add_comp(esk_engine *e, const char *id, const char *kind, const esk_model_ops *ops,
                              const char *const *pin_names, const esk_pin_model *pin_models, uint32_t npins,
                              const esk_json *params, const esk_json *desc, esk_comp **out);
es_status esk_engine_connect(esk_engine *e, esk_pin *pin, esk_net *net);
esk_comp *esk_engine_comp(const esk_engine *e, const char *id);
esk_net *esk_engine_net(const esk_engine *e, const char *name);
esk_pin *esk_comp_pin(esk_comp *c, const char *name);
/* after wiring: partition islands and insert bridges (SPEC-003 §4); create analog islands */
es_status esk_engine_partition(esk_engine *e);
/* reset everything to time 0: models, nets, islands, scheduler */
es_status esk_engine_reset(esk_engine *e);
/* advance virtual time (runs the scheduler) */
es_status esk_engine_advance(esk_engine *e, es_time_t until);
es_time_t esk_engine_now(const esk_engine *e);
/* an interactive input (SPEC-005 §7) applied through the command path at `now` */
es_status esk_engine_set_input(esk_engine *e, esk_comp *c, const char *input, const esk_json *value);

/* the model API (SPEC-003 §5) */
void esk_pin_drive(esk_pin *p, esk_logic value, esk_strength strength, es_time_t delay_ps);
void esk_pin_release(esk_pin *p);
esk_logic esk_pin_read(esk_pin *p);           /* samples the net with floating/unknown diagnostics */
esk_logic esk_net_value(const esk_net *n);
/* Current opposing known digital drives. Analog islands compare active digital
 * output intents; digital nets compare known values at the strongest strength.
 * X by itself is not evidence of contention. */
int esk_net_has_contention(const esk_net *n);
/* resolve a digital net ignoring the drivers of `excluded` (transmission-gate models) */
void esk_net_resolve_excluding(const esk_net *n, const esk_comp *excluded, esk_logic *v, esk_strength *s);
void esk_pin_set_direction(esk_pin *p, int output);
/* a digital_io pin an output driving `value` now: the direction and the
 * level change together, the net never seeing the level the pin held as
 * an input (a pulled-low input that becomes an output driving 0 makes no
 * edge); on an output, esk_pin_drive with no delay */
void esk_pin_output(esk_pin *p, esk_logic value, esk_strength strength);
/* the level the pin's net has, as the pin sees it, without diagnostics: the
 * resolved value of a digital net (ESK_LZ when nothing drives it), the
 * comparator's state on an analog one */
esk_logic esk_pin_level(const esk_pin *p);
/* the pin's model (pull, drive mode) changed: resolve its net again, or
 * retarget its bridge */
void esk_pin_refresh(esk_pin *p);
void esk_pin_watch(esk_pin *p, int watch);
es_status esk_comp_wake_at(esk_comp *c, es_time_t t, uint32_t token);
/* the part draws `amps` from its supply pins from `at` on (ADR-036): under
 * the electrical policy, split between its power pins that a supply of the
 * circuit's feeds, from each to the part's ground pin; nothing otherwise */
void esk_comp_set_supply(esk_comp *c, double amps, es_time_t at);
void esk_comp_wake_cancel(esk_comp *c, uint32_t token);
int esk_comp_param_double(const esk_comp *c, const char *name, double *out);
int esk_comp_param_int(const esk_comp *c, const char *name, int64_t *out);
int esk_comp_param_bool(const esk_comp *c, const char *name, int *out);
const char *esk_comp_param_str(const esk_comp *c, const char *name, const char *def);
int esk_comp_input_bool(const esk_comp *c, const char *name, int def);
double esk_comp_input_double(const esk_comp *c, const char *name, double def);
void esk_comp_diag(esk_comp *c, es_severity_t sev, const char *code, const char *fmt, ...);
es_time_t esk_comp_now(const esk_comp *c);

/* used by the analog island to deliver a comparator-bridge crossing (SPEC-004 §8) */
void esk_engine_bridge_crossed(esk_engine *e, esk_pin *p, esk_logic value, es_time_t at);
/* used by the analog island to deliver a crossing of a pin's watch_v */
void esk_engine_watch_crossed(esk_engine *e, esk_pin *p, esk_logic value, es_time_t at);
/* a model's comparator on an input on an analog net: on_input comes when
 * the node crosses `v` (from its side now); 0 when the net is not analog.
 * esk_pin_compare_off takes it off. */
int esk_pin_compare(esk_pin *p, double v);
void esk_pin_compare_off(esk_pin *p);
/* the Thevenin bridge's current target for an output pin on an analog net */
void esk_pin_bridge_target(const esk_pin *p, double *v, double *r, int *open);

/* a model that has only an electrical tier (no digital behaviour) */
ES_INLINE int esk_model_is_electrical_only(const esk_model_ops *ops) { return ops->adev != NULL && ops->reset == NULL && ops->on_input == NULL; }

const char *esk_logic_name(esk_logic v);
esk_logic esk_logic_from_name(const char *s);
const char *esk_pin_kind_name(esk_pin_kind k);
/* parse a pin model object (SPEC-003 §2) with a logic family's defaults; returns 0 or -1 */
int esk_pin_model_parse(const esk_json *obj, const char *family, esk_pin_model *out);
void esk_pin_model_family(const char *family, esk_pin_model *m); /* fill defaults */

#endif
