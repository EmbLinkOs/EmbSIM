/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* analog.h -- the analog engine's interface to the engine (SPEC-004): an
 * island of nodes solved by MNA, devices that stamp it, bridges, and the
 * stepping the scheduler drives. Implemented in the analog directory. */
#ifndef ESK_ANALOG_H
#define ESK_ANALOG_H

#include "engine/engine.h"

typedef struct esk_amatrix esk_amatrix;  /* opaque: the Jacobian and residual being assembled */
typedef struct esk_integ esk_integ;      /* integration state passed to load_tr */

/* Stamping helpers available to devices during load_dc/load_tr. Node 0 is ground
 * (stamps to it are ignored). Branch unknowns are allocated in setup. */
void esk_am_stamp_g(esk_amatrix *m, int n1, int n2, double g);              /* conductance between nodes */
void esk_am_stamp_i(esk_amatrix *m, int n1, int n2, double i);              /* current source n1 -> n2 */
void esk_am_stamp_j(esk_amatrix *m, int row, int col, double v);            /* raw Jacobian entry */
void esk_am_stamp_f(esk_amatrix *m, int row, double v);                     /* raw residual entry (adds to f) */
double esk_am_x(const esk_amatrix *m, int idx);                             /* current iterate (node voltage or branch current) */
double esk_am_v(const esk_amatrix *m, int n1, int n2);                      /* x[n1] - x[n2] with ground = 0 */
/* companion-model support for reactive elements: the integrator returns the
 * equivalent conductance and current for a charge q(v) with dq/dv = c at this step */
void esk_integ_companion(const esk_integ *in, double q_now, double q_prev, double i_prev, double c, double *geq, double *ieq);
double esk_integ_h(const esk_integ *in);                                    /* current step in seconds */

struct esk_adev_ops {
    const char *name;
    size_t state_size;
    /* allocate branch rows, validate parameters; pins' nodes are in d->node[] */
    es_status (*setup)(esk_adev *d);
    void (*load_dc)(esk_adev *d, esk_amatrix *m);
    void (*load_tr)(esk_adev *d, esk_amatrix *m, const esk_integ *in);
    void (*accept)(esk_adev *d, es_time_t t);        /* a step was accepted: commit history */
    void (*reject)(esk_adev *d);                      /* a step was rejected: discard trial state */
    int (*next_breakpoint)(esk_adev *d, es_time_t after, es_time_t *at);
    double (*lte)(esk_adev *d, const esk_integ *in);  /* local truncation error estimate (volts), 0 if none */
    void (*observe)(esk_adev *d, esk_json *out);      /* currents, powers, model outputs */
    /* a digital value changed on a bridge pin (bridge devices only) */
    void (*set_logic)(esk_adev *d, esk_logic v, es_time_t at);
};

struct esk_adev {
    esk_island *isl;
    esk_comp *comp;           /* owning component (NULL for bridges owned by a pin) */
    esk_pin *pin;             /* for bridges: the digital pin */
    const esk_adev_ops *ops;
    void *state;
    int node[8];              /* nodes of the device's analog pins in pin order (0 = ground) */
    int nnodes;
    int branch[4];            /* branch unknown indices allocated in setup */
    int nbranch;
    double i_last[8];         /* current into each pin at the last accepted step (observe) */
};

typedef struct esk_island_params {
    double reltol, vntol, abstol, gmin, lte_tol;
    int max_iter, max_step_rejections;
    es_time_t h_min_ps, h_max_ps;
} esk_island_params;

struct esk_island {
    esk_engine *e;
    esk_alloc *a;
    uint32_t id;
    ESK_VEC(esk_net *) nets;      /* nets[i] has node i; nets[0] is the ground net (or NULL for an implicit ground) */
    ESK_VEC(esk_adev *) devs;
    esk_island_params p;
    int nunknowns;                /* nodes - 1 + branches */
    double *x;                    /* accepted solution */
    es_time_t t_accepted;         /* time of the accepted solution */
    es_time_t h_next_ps;          /* proposed next step */
    esk_event_handle step_event;
    uint64_t steps, rejections, newton_iters;
    uint64_t quiet_steps;         /* steps a steady island took without a solve */
    int failed;                   /* frozen after a fatal diagnostic */
    void *priv;                   /* solver-private */
};

/* engine -> analog */
esk_island *esk_island_create(esk_engine *e, uint32_t id);
void esk_island_destroy(esk_island *isl);
/* add a net; returns its node index (the first ground net added becomes node 0) */
int esk_island_add_net(esk_island *isl, esk_net *net, int is_ground);
/* add a device for a component whose analog pins are on this island; pin order = description order */
/* the pin names a primitive's nodes follow (NULL when it has no table) */
const char *const *esk_adev_pin_names(const esk_adev_ops *ops, int *n);
es_status esk_island_add_device(esk_island *isl, esk_comp *c, const esk_adev_ops *ops, esk_pin **pins, int npins, esk_adev **out);
/* add a bridge device for a digital pin on this island */
es_status esk_island_add_bridge(esk_island *isl, esk_pin *pin, esk_adev **out);
/* after all devices: allocate matrices, compute the operating point at t=0, schedule stepping */
es_status esk_island_start(esk_island *isl);
/* reset to the operating point and reschedule */
es_status esk_island_reset(esk_island *isl);
/* step from t_accepted to at most `until` (clipped by the caller to the next event);
 * stops early at a bridge crossing; sets *reached; schedules nothing itself */
es_status esk_island_step(esk_island *isl, es_time_t until, es_time_t *reached);
/* a source changed (a bridge pin toggled): the island must re-evaluate from now */
void esk_island_touch(esk_island *isl, es_time_t now);
double esk_island_node_v(const esk_island *isl, int node);
/* JSON of node voltages and device observations */
esk_json *esk_island_observe(esk_island *isl);
/* the registry of analog primitives (devices.c) */
const esk_adev_ops *esk_adev_find(const char *name);
/* bridge devices (bridge.c) */
extern const esk_adev_ops esk_bridge_out_ops;
extern const esk_adev_ops esk_bridge_in_ops;
/* the built-in device table, for listing */
const esk_adev_ops *const *esk_adev_table(size_t *n);

/* ---- added by analog (implementation notes and the few accessors the devices
 * need beyond the contract above) ------------------------------------------ */
/* Index space of the stamping helpers: 0 is ground (ignored), 1..nodes-1 are
 * node voltages, and nodes+k is extra unknown k (a branch current or a
 * device-internal node). A device asks for extra unknowns by setting
 * d->nbranch in setup(); the island fills d->branch[] with their indices before
 * the first load. setup() is called again (on zeroed state) at every reset so
 * parameters are re-read. esk_integ_companion returns *geq = dI/dV at this
 * step and *ieq = the device current I(q_now) itself (not the Norton offset). */
double esk_am_source_scale(const esk_amatrix *m);  /* 0..1 during source stepping, else 1 */
es_time_t esk_integ_t(const esk_integ *in);        /* time of the trial point (ps) */
double esk_integ_h_prev(const esk_integ *in);      /* previous accepted step (s), 0 if none */
int esk_integ_first_order(const esk_integ *in);    /* 1 when this step is backward Euler */
/* 1 when this step is negligible next to the previous one (a step forced by an
 * event at the same time, or a crossing bisection): reactive devices then
 * replace their newest history point instead of shifting history, so the LTE
 * divided differences keep a usable spacing (the island accumulates h_prev) */
int esk_integ_negligible(const esk_integ *in);
/* a device whose voltage limiting (pnjlim) altered its operating point this
 * iteration calls this so Newton does not declare convergence on the
 * linearised residual (SPICE's "noncon") */
void esk_am_noncon(esk_amatrix *m);
/* esk_island_step may integrate past `until` when every pending event before
 * its own step proposal is another island's ANALOG step (those cannot change
 * its sources); it never passes a SIGNAL, PROBE or other event. A catch-up from
 * esk_island_touch stops exactly at `now`. */

#endif
