/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* mna.c -- the per-island analog solver (SPEC-004 §2 to §5, §9): modified
 * nodal analysis with Newton-Raphson, dense LU with partial pivoting (ties
 * broken by the lowest index so the pivot order is a function of the
 * topology), operating point by direct Newton then gmin stepping then source
 * stepping, and a transient of trapezoidal steps with backward-Euler restarts,
 * LTE step control, exact breakpoints and bisected comparator crossings. */
#include "analog/analog.h"
#include "util/num.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define MNA_MAX_SUBSTEPS  1000000 /* substeps of one esk_island_step call (CS-17) */
#define MNA_MAX_CATCHUP   100000  /* step calls one touch may issue */
#define MNA_MAX_BISECT    128     /* bisections for one crossing */
#define MNA_TOUCH_STEP_PS ES_PS_PER_NS
#define MNA_START_STEP_PS ES_PS_PER_NS
#define MNA_PS_TO_S       1e-12

enum { INTEG_TRAP = 0, INTEG_BE = 1 };

struct esk_integ {
    double h;       /* trial step, seconds */
    double h_prev;  /* previous accepted step, seconds (0 if none) */
    es_time_t t_ps; /* time of the trial point */
    int method;
    int negligible;
};

struct esk_amatrix {
    double *J; /* n*n, row-major */
    double *f; /* n */
    double *x; /* the iterate the stamps are evaluated at */
    int n;
    double source_scale;
    int dc;
    int noncon; /* a device limited its voltage this iteration */
};

typedef struct mna {
    esk_amatrix m;
    esk_integ in;
    int nnodes; /* including ground */
    int nextra; /* branch currents and internal nodes */
    int n;
    double *J, *f, *x_iter, *x_old, *dx;
    int *piv;
    int allocated;
    int started, running, in_step;
    int exact_until; /* a catch-up: never step past `until` */
    int restart;     /* next step is backward Euler and small */
    int touched;     /* a touch arrived during a step */
    /* two steps in a row changed no unknown, neither ended at a breakpoint
     * and none is pending: the island sits at its steady state (a device's
     * accept that changed its state was solved by the second step), and
     * until a touch or a breakpoint its steps move its time without a solve */
    int quiet;
    int still; /* the steps in a row that changed no unknown */
    double gmin_eff;
    double h_prev_s;
    es_time_t bracket_hi; /* a crossing lies before this time (0: none) */
    es_time_t h_before_bracket;
    es_time_t h_prop_ps; /* the last error-control proposal, before clipping to events */
    uint32_t consecutive_rej;
    ESK_VEC(esk_net *) extra_grounds;
    int diag_implicit_ground;
} mna;

/* ---- diagnostics --------------------------------------------------------- */

static void island_subject(const esk_island *isl, char *buf, size_t cap)
{
    esk_snprintf(buf, cap, "island %u", (unsigned)isl->id);
}

static void raise_isl(esk_island *isl, es_severity_t sev, const char *code, es_time_t t,
                      const char *fmt, ...)
{
    char subject[32], msg[240];
    va_list ap;
    if (!isl->e || !isl->e->diag)
        return;
    island_subject(isl, subject, sizeof subject);
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    esk_diag_raise(isl->e->diag, sev, code, t, "island", subject, NULL, 0, "%s", msg);
}

/* the name of unknown row r (0-based) */
static void row_name(const esk_island *isl, int r, char *buf, size_t cap)
{
    const mna *p = (const mna *)isl->priv;
    if (r + 1 < p->nnodes) {
        esk_net *net = isl->nets.data[(size_t)r + 1];
        esk_snprintf(buf, cap, "%s", net && net->name ? net->name : "?");
    } else {
        esk_snprintf(buf, cap, "branch %d", r + 1 - p->nnodes);
    }
}

static const char *dev_name(const esk_adev *d)
{
    if (d->comp && d->comp->id)
        return d->comp->id;
    if (d->pin && d->pin->name)
        return d->pin->name;
    return d->ops->name;
}

/* which devices touch unknown index idx (node or branch) */
static void devices_on(const esk_island *isl, int idx, char *buf, size_t cap)
{
    size_t i;
    size_t len = 0;
    int k, count = 0;
    buf[0] = 0;
    for (i = 0; i < isl->devs.len && count < 4; i++) {
        const esk_adev *d = isl->devs.data[i];
        int hit = 0;
        for (k = 0; k < d->nnodes && !hit; k++)
            hit = d->node[k] == idx;
        for (k = 0; k < d->nbranch && !hit; k++)
            hit = d->branch[k] == idx;
        if (hit) {
            len += esk_snprintf(buf + len, len < cap ? cap - len : 0, "%s%s", count ? "," : "",
                                dev_name(d));
            count++;
        }
    }
}

static void report_no_convergence(esk_island *isl, es_time_t t, const char *stage)
{
    mna *p = (mna *)isl->priv;
    int worst[3] = {-1, -1, -1};
    int i, k;
    char msg[240], name[48], devs[96];
    size_t len = 0;
    for (i = 0; i < p->n; i++) {
        double r = fabs(p->f[i]);
        for (k = 0; k < 3; k++) {
            if (worst[k] < 0 || r > fabs(p->f[worst[k]])) {
                int m2;
                for (m2 = 2; m2 > k; m2--)
                    worst[m2] = worst[m2 - 1];
                worst[k] = i;
                break;
            }
        }
    }
    len = esk_snprintf(msg, sizeof msg, "%s did not converge; worst residuals:", stage);
    for (k = 0; k < 3 && worst[k] >= 0; k++) {
        row_name(isl, worst[k], name, sizeof name);
        devices_on(isl, worst[k] + 1, devs, sizeof devs);
        len += esk_snprintf(msg + len, len < sizeof msg ? sizeof msg - len : 0,
                            " %s (%.3g, via %s)", name, fabs(p->f[worst[k]]), devs);
    }
    raise_isl(isl, ES_SEV_ERROR, "analog.no_convergence", t, "%s", msg);
}

/* ---- stamping helpers (index 0 = ground) --------------------------------- */

void esk_am_stamp_j(esk_amatrix *m, int row, int col, double v)
{
    if (row <= 0 || col <= 0 || row > m->n || col > m->n)
        return;
    m->J[(size_t)(row - 1) * (size_t)m->n + (size_t)(col - 1)] += v;
}

void esk_am_stamp_f(esk_amatrix *m, int row, double v)
{
    if (row <= 0 || row > m->n)
        return;
    m->f[row - 1] += v;
}

double esk_am_x(const esk_amatrix *m, int idx)
{
    if (idx <= 0 || idx > m->n)
        return 0.0;
    return m->x[idx - 1];
}

double esk_am_v(const esk_amatrix *m, int n1, int n2)
{
    return esk_am_x(m, n1) - esk_am_x(m, n2);
}

void esk_am_stamp_g(esk_amatrix *m, int n1, int n2, double g)
{
    double i = g * esk_am_v(m, n1, n2);
    esk_am_stamp_j(m, n1, n1, g);
    esk_am_stamp_j(m, n1, n2, -g);
    esk_am_stamp_j(m, n2, n1, -g);
    esk_am_stamp_j(m, n2, n2, g);
    esk_am_stamp_f(m, n1, i);
    esk_am_stamp_f(m, n2, -i);
}

void esk_am_stamp_i(esk_amatrix *m, int n1, int n2, double i)
{
    esk_am_stamp_f(m, n1, i);
    esk_am_stamp_f(m, n2, -i);
}

double esk_am_source_scale(const esk_amatrix *m)
{
    return m->source_scale;
}
void esk_am_noncon(esk_amatrix *m)
{
    m->noncon++;
}

/* ---- integration ----------------------------------------------------------- */

void esk_integ_companion(const esk_integ *in, double q_now, double q_prev, double i_prev, double c,
                         double *geq, double *ieq)
{
    double h = in->h > 0 ? in->h : 1e-15;
    if (in->method == INTEG_BE) {
        *geq = c / h;
        *ieq = (q_now - q_prev) / h;
    } else {
        *geq = 2.0 * c / h;
        *ieq = 2.0 * (q_now - q_prev) / h - i_prev;
    }
}

double esk_integ_h(const esk_integ *in)
{
    return in->h;
}
double esk_integ_h_prev(const esk_integ *in)
{
    return in->h_prev;
}
es_time_t esk_integ_t(const esk_integ *in)
{
    return in->t_ps;
}
int esk_integ_first_order(const esk_integ *in)
{
    return in->method == INTEG_BE;
}
int esk_integ_negligible(const esk_integ *in)
{
    return in->negligible;
}

/* ---- linear solver ----------------------------------------------------------- */

/* LU in place with partial pivoting; the pivot is the max-magnitude entry, the
 * lowest row on ties. Returns the row of a zero pivot, or -1. */
static int lu_factor(double *A, int *piv, int n)
{
    int i, j, k;
    for (k = 0; k < n; k++) {
        int best = k;
        double bmag = fabs(A[(size_t)k * (size_t)n + (size_t)k]);
        for (i = k + 1; i < n; i++) {
            double mag = fabs(A[(size_t)i * (size_t)n + (size_t)k]);
            if (mag > bmag) {
                bmag = mag;
                best = i;
            }
        }
        piv[k] = best;
        if (!(bmag > 0.0) || bmag != bmag)
            return k;
        if (best != k) {
            for (j = 0; j < n; j++) {
                double t = A[(size_t)k * (size_t)n + (size_t)j];
                A[(size_t)k * (size_t)n + (size_t)j] = A[(size_t)best * (size_t)n + (size_t)j];
                A[(size_t)best * (size_t)n + (size_t)j] = t;
            }
        }
        for (i = k + 1; i < n; i++) {
            double *ri = A + (size_t)i * (size_t)n;
            const double *rk = A + (size_t)k * (size_t)n;
            double l = ri[k] / rk[k];
            if (l == 0.0)
                continue;
            ri[k] = l;
            for (j = k + 1; j < n; j++)
                ri[j] -= l * rk[j];
        }
    }
    return -1;
}

static void lu_solve(const double *A, const int *piv, int n, double *b)
{
    int i, j;
    for (i = 0; i < n; i++) {
        if (piv[i] != i) {
            double t = b[i];
            b[i] = b[piv[i]];
            b[piv[i]] = t;
        }
    }
    for (i = 0; i < n; i++) {
        const double *ri = A + (size_t)i * (size_t)n;
        double s = b[i];
        for (j = 0; j < i; j++)
            s -= ri[j] * b[j];
        b[i] = s;
    }
    for (i = n - 1; i >= 0; i--) {
        const double *ri = A + (size_t)i * (size_t)n;
        double s = b[i];
        for (j = i + 1; j < n; j++)
            s -= ri[j] * b[j];
        b[i] = s / ri[i];
    }
}

/* ---- assembly and Newton ------------------------------------------------------ */

static void assemble(esk_island *isl, int dc)
{
    mna *p = (mna *)isl->priv;
    size_t i;
    int k;
    memset(p->J, 0, (size_t)p->n * (size_t)p->n * sizeof *p->J);
    memset(p->f, 0, (size_t)p->n * sizeof *p->f);
    p->m.x = p->x_iter;
    p->m.dc = dc;
    p->m.noncon = 0;
    for (i = 0; i < isl->devs.len; i++) {
        esk_adev *d = isl->devs.data[i];
        if (dc || !d->ops->load_tr) {
            if (d->ops->load_dc)
                d->ops->load_dc(d, &p->m);
        } else {
            d->ops->load_tr(d, &p->m, &p->in);
        }
    }
    /* gmin from every unknown to ground: a floating node solves to 0 V */
    for (k = 0; k < p->n; k++) {
        p->J[(size_t)k * (size_t)p->n + (size_t)k] += p->gmin_eff;
        p->f[k] += p->gmin_eff * p->x_iter[k];
    }
}

/* Newton-Raphson from p->x_iter; on success p->x_iter is the solution and the
 * devices' trial state matches it. Returns 0, 1 (no convergence) or 2 (singular;
 * *bad_row names the pivot row). */
static int newton(esk_island *isl, int dc, int *bad_row)
{
    mna *p = (mna *)isl->priv;
    int iter, i;
    int dx_small = 0;
    *bad_row = -1;
    for (iter = 0; iter <= isl->p.max_iter; iter++) {
        int res_ok = 1;
        assemble(isl, dc);
        isl->newton_iters++;
        if (dx_small && !p->m.noncon) {
            for (i = 0; i < p->n && res_ok; i++) {
                double scale = 0.0;
                int j;
                const double *row = p->J + (size_t)i * (size_t)p->n;
                for (j = 0; j < p->n; j++)
                    scale += fabs(row[j] * p->x_iter[j]);
                if (fabs(p->f[i]) > 1e-3 * scale + 1e-9 && fabs(p->f[i]) > isl->p.abstol)
                    res_ok = 0;
            }
            if (res_ok)
                return 0;
        }
        if (iter == isl->p.max_iter)
            break;
        {
            int bad = lu_factor(p->J, p->piv, p->n);
            if (bad >= 0) {
                *bad_row = bad;
                return 2;
            }
        }
        for (i = 0; i < p->n; i++)
            p->dx[i] = -p->f[i];
        lu_solve(p->J, p->piv, p->n, p->dx);
        dx_small = 1;
        for (i = 0; i < p->n; i++) {
            double tol;
            double nx;
            if (p->dx[i] != p->dx[i])
                return 1; /* NaN */
            p->x_old[i] = p->x_iter[i];
            nx = p->x_iter[i] + p->dx[i];
            tol = isl->p.reltol * fmax(fabs(nx), fabs(p->x_old[i])) +
                  (i + 1 < p->nnodes ? isl->p.vntol : isl->p.abstol);
            if (fabs(p->dx[i]) > tol)
                dx_small = 0;
            p->x_iter[i] = nx;
        }
    }
    return 1;
}

static void report_singular(esk_island *isl, int row, es_time_t t)
{
    char name[48], devs[96];
    row_name(isl, row, name, sizeof name);
    devices_on(isl, row + 1, devs, sizeof devs);
    raise_isl(isl, ES_SEV_ERROR, "analog.singular_topology", t,
              "singular matrix at %s (devices: %s)", name, devs);
}

/* ---- accepted solutions -------------------------------------------------------- */

static void publish(esk_island *isl, es_time_t t)
{
    mna *p = (mna *)isl->priv;
    size_t i;
    memcpy(isl->x, p->x_iter, (size_t)p->n * sizeof *isl->x);
    isl->t_accepted = t;
    for (i = 1; i < isl->nets.len; i++) {
        esk_net *net = isl->nets.data[i];
        if (net)
            net->voltage = isl->x[i - 1];
    }
    if (isl->nets.len && isl->nets.data[0])
        isl->nets.data[0]->voltage = 0.0;
    for (i = 0; i < p->extra_grounds.len; i++)
        p->extra_grounds.data[i]->voltage = 0.0;
    for (i = 0; i < isl->devs.len; i++) {
        esk_adev *d = isl->devs.data[i];
        if (d->ops->accept)
            d->ops->accept(d, t);
    }
}

static void reject_all(esk_island *isl)
{
    size_t i;
    for (i = 0; i < isl->devs.len; i++) {
        esk_adev *d = isl->devs.data[i];
        if (d->ops->reject)
            d->ops->reject(d);
    }
}

/* ---- comparator bridges ---------------------------------------------------------- */

static int bridge_input_role(const esk_adev *d)
{
    const esk_pin *pin = d->pin;
    if (!pin)
        return 0;
    if (d->ops != &esk_bridge_in_ops && d->ops != &esk_bridge_out_ops)
        return 0;
    return pin->model.kind == ESK_PIN_DIGITAL_IN ||
           (pin->model.kind == ESK_PIN_DIGITAL_IO && !pin->is_output);
}

static void thresholds(const esk_pin *pin, double *up, double *down)
{
    double vih = pin->model.vih, vil = pin->model.vil;
    if (vih < vil) {
        double t = vih;
        vih = vil;
        vil = t;
    }
    if (vih == vil && pin->model.hyst > 0) {
        vih += pin->model.hyst / 2;
        vil -= pin->model.hyst / 2;
    }
    *up = vih;
    *down = vil;
}

/* does the trial voltage of `d`'s node cross its pin's threshold? */
static int crossing(const esk_adev *d, const double *x, esk_logic *newv)
{
    double up, down, v;
    int node = d->node[0];
    esk_logic state = d->pin->sampled == ESK_L1 ? ESK_L1 : ESK_L0;
    thresholds(d->pin, &up, &down);
    v = node > 0 ? x[node - 1] : 0.0;
    if (state == ESK_L0 && v > up) {
        *newv = ESK_L1;
        return 1;
    }
    if (state == ESK_L1 && v < down) {
        *newv = ESK_L0;
        return 1;
    }
    return 0;
}

static int any_crossing(const esk_island *isl, const double *x)
{
    size_t i;
    esk_logic nv;
    for (i = 0; i < isl->devs.len; i++) {
        const esk_adev *d = isl->devs.data[i];
        if (bridge_input_role(d) && crossing(d, x, &nv))
            return 1;
    }
    return 0;
}

static void deliver_crossings(esk_island *isl, es_time_t t)
{
    size_t i;
    for (i = 0; i < isl->devs.len; i++) {
        esk_adev *d = isl->devs.data[i];
        esk_logic nv;
        if (bridge_input_role(d) && crossing(d, isl->x, &nv))
            esk_engine_bridge_crossed(isl->e, d->pin, nv, t);
    }
}

/* at the operating point every comparator samples its first value (no hysteresis state yet) */
static void deliver_initial(esk_island *isl)
{
    size_t i;
    for (i = 0; i < isl->devs.len; i++) {
        esk_adev *d = isl->devs.data[i];
        if (bridge_input_role(d)) {
            double up, down, v;
            int node = d->node[0];
            thresholds(d->pin, &up, &down);
            v = node > 0 ? isl->x[node - 1] : 0.0;
            esk_engine_bridge_crossed(isl->e, d->pin, v > up ? ESK_L1 : ESK_L0, isl->t_accepted);
        }
    }
}

/* ---- scheduling (mirrors the engine's island_step_cb) ------------------------------ */

static void local_step_cb(void *ctx, void *arg, es_time_t now);

static void schedule_local(esk_island *isl, es_time_t at)
{
    esk_engine *e = isl->e;
    if (isl->step_event) {
        esk_sched_cancel(&e->sched, isl->step_event);
        isl->step_event = 0;
    }
    if (at < e->sched.now)
        at = e->sched.now;
    esk_sched_at(&e->sched, at, ESK_EV_ANALOG, local_step_cb, e, isl, &isl->step_event);
}

static void local_step_cb(void *ctx, void *arg, es_time_t now)
{
    esk_engine *e = (esk_engine *)ctx;
    esk_island *isl = (esk_island *)arg;
    es_time_t next = esk_sched_next_time(&e->sched), until, reached = now;
    isl->step_event = 0;
    if (isl->failed)
        return;
    if (esk_time_add(now, isl->h_next_ps ? isl->h_next_ps : isl->p.h_max_ps, &until))
        until = ES_TIME_MAX - 1;
    if (next < until)
        until = next;
    if (until <= now)
        until = now + 1;
    if (esk_island_step(isl, until, &reached) != ES_OK) {
        isl->failed = 1;
        return;
    }
    if (reached < now)
        reached = now;
    schedule_local(isl, reached);
}

/* ---- lifecycle ----------------------------------------------------------------------- */

esk_island *esk_island_create(esk_engine *e, uint32_t id)
{
    esk_island *isl = (esk_island *)esk_calloc(e->a, 1, sizeof *isl);
    mna *p;
    if (!isl)
        return NULL;
    p = (mna *)esk_calloc(e->a, 1, sizeof *p);
    if (!p) {
        esk_free(e->a, isl, sizeof *isl);
        return NULL;
    }
    isl->e = e;
    isl->a = e->a;
    isl->id = id;
    isl->priv = p;
    isl->p.reltol = 1e-3;
    isl->p.vntol = 1e-6;
    isl->p.abstol = 1e-12;
    isl->p.gmin = 1e-12;
    isl->p.lte_tol = 1e-3;
    isl->p.max_iter = 100;
    isl->p.max_step_rejections = 50;
    isl->p.h_min_ps = 1;
    isl->p.h_max_ps = ES_PS_PER_MS;
    isl->h_next_ps = MNA_START_STEP_PS;
    p->nnodes = 0;
    p->gmin_eff = isl->p.gmin;
    p->m.source_scale = 1.0;
    return isl;
}

static void free_matrices(esk_island *isl)
{
    mna *p = (mna *)isl->priv;
    size_t n = (size_t)p->n;
    if (!p->allocated)
        return;
    esk_free(isl->a, p->J, n * n * sizeof *p->J);
    esk_free(isl->a, p->f, n * sizeof *p->f);
    esk_free(isl->a, p->x_iter, n * sizeof *p->x_iter);
    esk_free(isl->a, p->x_old, n * sizeof *p->x_old);
    esk_free(isl->a, p->dx, n * sizeof *p->dx);
    esk_free(isl->a, p->piv, n * sizeof *p->piv);
    esk_free(isl->a, isl->x, n * sizeof *isl->x);
    p->J = p->f = p->x_iter = p->x_old = p->dx = NULL;
    p->piv = NULL;
    isl->x = NULL;
    p->allocated = 0;
}

static void dev_free(esk_island *isl, esk_adev *d)
{
    if (d->ops && d->ops->state_size)
        esk_free(isl->a, d->state, d->ops->state_size);
    esk_free(isl->a, d, sizeof *d);
}

void esk_island_destroy(esk_island *isl)
{
    mna *p;
    size_t i;
    if (!isl)
        return;
    p = (mna *)isl->priv;
    if (isl->step_event) {
        esk_sched_cancel(&isl->e->sched, isl->step_event);
        isl->step_event = 0;
    }
    free_matrices(isl);
    for (i = 0; i < isl->devs.len; i++) {
        esk_adev *d = isl->devs.data[i];
        if (d->comp && d->comp->adev == d)
            d->comp->adev = NULL;
        if (d->pin && d->pin->bridge == d)
            d->pin->bridge = NULL;
        dev_free(isl, d);
    }
    esk_vec_free(isl->a, &isl->devs);
    esk_vec_free(isl->a, &isl->nets);
    esk_vec_free(isl->a, &p->extra_grounds);
    esk_free(isl->a, p, sizeof *p);
    esk_free(isl->a, isl, sizeof *isl);
}

int esk_island_add_net(esk_island *isl, esk_net *net, int is_ground)
{
    mna *p = (mna *)isl->priv;
    if (p->started)
        return -1;
    if (is_ground) {
        if (isl->nets.len == 0) {
            if (esk_vec_push(isl->a, &isl->nets, net))
                return -1;
            return 0;
        }
        if (isl->nets.data[0] == NULL) {
            isl->nets.data[0] = net;
            return 0;
        }
        /* another ground net: tied to node 0 */
        if (esk_vec_push(isl->a, &p->extra_grounds, net))
            return -1;
        return 0;
    }
    if (isl->nets.len == 0) {
        /* reserve node 0 for an implicit ground */
        if (esk_vec_push(isl->a, &isl->nets, (esk_net *)NULL))
            return -1;
    }
    if (esk_vec_push(isl->a, &isl->nets, net))
        return -1;
    return (int)isl->nets.len - 1;
}

static esk_adev *dev_new(esk_island *isl, const esk_adev_ops *ops)
{
    esk_adev *d = (esk_adev *)esk_calloc(isl->a, 1, sizeof *d);
    if (!d)
        return NULL;
    d->isl = isl;
    d->ops = ops;
    if (ops->state_size) {
        d->state = esk_calloc(isl->a, 1, ops->state_size);
        if (!d->state) {
            esk_free(isl->a, d, sizeof *d);
            return NULL;
        }
    }
    return d;
}

static int node_of_pin(const esk_island *isl, const esk_pin *pin)
{
    size_t i;
    const mna *p = (const mna *)isl->priv;
    if (!pin->net)
        return 0;
    if (pin->net->is_ground)
        return 0;
    for (i = 0; i < isl->nets.len; i++)
        if (isl->nets.data[i] == pin->net)
            return (int)i;
    for (i = 0; i < p->extra_grounds.len; i++)
        if (p->extra_grounds.data[i] == pin->net)
            return 0;
    return 0;
}

es_status esk_island_add_device(esk_island *isl, esk_comp *c, const esk_adev_ops *ops,
                                esk_pin **pins, int npins, esk_adev **out)
{
    mna *p = (mna *)isl->priv;
    esk_adev *d;
    int k;
    if (!ops || npins < 0 || npins > 8 || p->started)
        return ES_EINVAL;
    d = dev_new(isl, ops);
    if (!d)
        return ES_ENOMEM;
    d->comp = c;
    for (k = 0; k < npins; k++)
        d->node[k] = pins[k] ? node_of_pin(isl, pins[k]) : 0;
    d->nnodes = npins;
    if (esk_vec_push(isl->a, &isl->devs, d)) {
        dev_free(isl, d);
        return ES_ENOMEM;
    }
    if (out)
        *out = d;
    return ES_OK;
}

es_status esk_island_add_bridge(esk_island *isl, esk_pin *pin, esk_adev **out)
{
    mna *p = (mna *)isl->priv;
    esk_adev *d;
    const esk_adev_ops *ops;
    if (!pin || p->started)
        return ES_EINVAL;
    ops = pin->model.kind == ESK_PIN_DIGITAL_IN ? &esk_bridge_in_ops : &esk_bridge_out_ops;
    d = dev_new(isl, ops);
    if (!d)
        return ES_ENOMEM;
    d->pin = pin;
    d->node[0] = node_of_pin(isl, pin);
    d->nnodes = 1;
    if (esk_vec_push(isl->a, &isl->devs, d)) {
        dev_free(isl, d);
        return ES_ENOMEM;
    }
    if (out)
        *out = d;
    return ES_OK;
}

/* zero the device states, run setup, hand out the extra unknowns */
static es_status setup_devices(esk_island *isl)
{
    mna *p = (mna *)isl->priv;
    size_t i;
    int next = p->nnodes;
    es_status st = ES_OK;
    for (i = 0; i < isl->devs.len; i++) {
        esk_adev *d = isl->devs.data[i];
        int k;
        if (d->state)
            memset(d->state, 0, d->ops->state_size);
        d->nbranch = 0;
        for (k = 0; k < 8; k++)
            d->i_last[k] = 0.0;
        if (d->ops->setup) {
            es_status s2 = d->ops->setup(d);
            if (s2 != ES_OK && st == ES_OK)
                st = s2;
        }
        if (d->nbranch < 0 || d->nbranch > 4)
            return ES_EINVAL;
        for (k = 0; k < d->nbranch; k++)
            d->branch[k] = next++;
    }
    p->nextra = next - p->nnodes;
    return st;
}

static es_status allocate(esk_island *isl)
{
    mna *p = (mna *)isl->priv;
    size_t n;
    free_matrices(isl);
    p->n = p->nnodes - 1 + p->nextra;
    n = (size_t)(p->n > 0 ? p->n : 1);
    p->J = (double *)esk_calloc(isl->a, n * n, sizeof *p->J);
    p->f = (double *)esk_calloc(isl->a, n, sizeof *p->f);
    p->x_iter = (double *)esk_calloc(isl->a, n, sizeof *p->x_iter);
    p->x_old = (double *)esk_calloc(isl->a, n, sizeof *p->x_old);
    p->dx = (double *)esk_calloc(isl->a, n, sizeof *p->dx);
    p->piv = (int *)esk_calloc(isl->a, n, sizeof *p->piv);
    isl->x = (double *)esk_calloc(isl->a, n, sizeof *isl->x);
    p->allocated = 1;
    if (!p->J || !p->f || !p->x_iter || !p->x_old || !p->dx || !p->piv || !isl->x) {
        /* sizes were computed from n: free with the same n */
        size_t nn = n;
        esk_free(isl->a, p->J, nn * nn * sizeof *p->J);
        esk_free(isl->a, p->f, nn * sizeof *p->f);
        esk_free(isl->a, p->x_iter, nn * sizeof *p->x_iter);
        esk_free(isl->a, p->x_old, nn * sizeof *p->x_old);
        esk_free(isl->a, p->dx, nn * sizeof *p->dx);
        esk_free(isl->a, p->piv, nn * sizeof *p->piv);
        esk_free(isl->a, isl->x, nn * sizeof *isl->x);
        p->J = p->f = p->x_iter = p->x_old = p->dx = NULL;
        p->piv = NULL;
        isl->x = NULL;
        p->allocated = 0;
        return ES_ENOMEM;
    }
    p->m.J = p->J;
    p->m.f = p->f;
    p->m.x = p->x_iter;
    p->m.n = p->n;
    isl->nunknowns = p->n;
    return ES_OK;
}

/* ---- operating point ---------------------------------------------------------- */

static int try_newton_dc(esk_island *isl, int *singular)
{
    int bad;
    int r = newton(isl, 1, &bad);
    if (r == 2) {
        *singular = bad;
        return 0;
    }
    return r == 0;
}

static es_status operating_point(esk_island *isl, es_time_t t)
{
    mna *p = (mna *)isl->priv;
    int singular = -1;
    int ok;
    double g;
    p->in.h = 0.0;
    p->in.h_prev = 0.0;
    p->in.t_ps = t;
    p->in.method = INTEG_TRAP;
    p->in.negligible = 0;
    p->gmin_eff = isl->p.gmin;
    p->m.source_scale = 1.0;
    memset(p->x_iter, 0, (size_t)p->n * sizeof *p->x_iter);
    ok = try_newton_dc(isl, &singular);
    if (!ok && singular < 0) {
        /* gmin stepping: converge each decade from the previous one */
        int ok2 = 1;
        memset(p->x_iter, 0, (size_t)p->n * sizeof *p->x_iter);
        for (g = 1e-2; ok2 && g > isl->p.gmin * 1.0001; g /= 10.0) {
            p->gmin_eff = g;
            ok2 = try_newton_dc(isl, &singular);
        }
        p->gmin_eff = isl->p.gmin;
        if (ok2)
            ok2 = try_newton_dc(isl, &singular);
        ok = ok2;
    }
    if (!ok && singular < 0) {
        /* source stepping: scale the independent sources from 0 to 1 */
        double scale = 0.0, step = 0.1;
        int guard = 0;
        memset(p->x_iter, 0, (size_t)p->n * sizeof *p->x_iter);
        ok = 1;
        while (scale < 1.0 && guard++ < 200) {
            double next = scale + step;
            int ok2;
            if (next > 1.0)
                next = 1.0;
            p->m.source_scale = next;
            memcpy(p->x_old, p->x_iter, (size_t)p->n * sizeof *p->x_iter);
            ok2 = try_newton_dc(isl, &singular);
            if (ok2) {
                scale = next;
                if (step < 0.5)
                    step *= 2.0;
            } else {
                memcpy(p->x_iter, p->x_old, (size_t)p->n * sizeof *p->x_iter);
                step /= 2.0;
                if (step < 1e-4 || singular >= 0) {
                    ok = 0;
                    break;
                }
            }
        }
        if (guard >= 200)
            ok = 0;
        p->m.source_scale = 1.0;
    }
    p->m.source_scale = 1.0;
    p->gmin_eff = isl->p.gmin;
    if (singular >= 0) {
        report_singular(isl, singular, t);
        return ES_ECONVERGE;
    }
    if (!ok) {
        report_no_convergence(isl, t, "operating point");
        return ES_ECONVERGE;
    }
    return ES_OK;
}

static es_status start_or_reset(esk_island *isl)
{
    mna *p = (mna *)isl->priv;
    es_status st;
    size_t i;
    if (isl->nets.len == 0) {
        if (esk_vec_push(isl->a, &isl->nets, (esk_net *)NULL))
            return ES_ENOMEM;
    }
    p->nnodes = (int)isl->nets.len;
    if (isl->nets.data[0] == NULL && !p->diag_implicit_ground) {
        p->diag_implicit_ground = 1;
        raise_isl(isl, ES_SEV_INFO, "analog.implicit_ground", 0,
                  "no ground net; an implicit ground node was added");
    }
    isl->failed = 0;
    isl->steps = isl->rejections = isl->newton_iters = isl->quiet_steps = 0;
    isl->t_accepted = 0;
    isl->h_next_ps = MNA_START_STEP_PS;
    p->running = 0;
    p->in_step = 0;
    p->restart = 1;
    p->touched = 0;
    p->quiet = 0;
    p->still = 0;
    p->bracket_hi = 0;
    p->h_before_bracket = 0;
    p->h_prop_ps = MNA_START_STEP_PS;
    p->consecutive_rej = 0;
    p->h_prev_s = 0.0;
    st = setup_devices(isl);
    if (st != ES_OK) {
        isl->failed = 1;
        return st;
    }
    st = allocate(isl);
    if (st != ES_OK) {
        isl->failed = 1;
        return st;
    }
    p->started = 1;
    for (i = 1; i < isl->nets.len; i++)
        if (isl->nets.data[i])
            isl->nets.data[i]->voltage = 0.0;
    st = operating_point(isl, 0);
    if (st != ES_OK) {
        isl->failed = 1;
        return st;
    }
    publish(isl, 0);
    p->running = 1;
    deliver_initial(isl);
    return ES_OK;
}

es_status esk_island_start(esk_island *isl)
{
    es_status st = start_or_reset(isl);
    if (st == ES_OK)
        schedule_local(isl, 0);
    return st;
}

es_status esk_island_reset(esk_island *isl)
{
    return start_or_reset(isl);
}

/* ---- transient ------------------------------------------------------------------- */

static es_time_t next_breakpoint(esk_island *isl, es_time_t after)
{
    size_t i;
    es_time_t best = ES_TIME_MAX;
    for (i = 0; i < isl->devs.len; i++) {
        esk_adev *d = isl->devs.data[i];
        es_time_t at = ES_TIME_MAX;
        if (d->ops->next_breakpoint && d->ops->next_breakpoint(d, after, &at) && at > after &&
            at < best)
            best = at;
    }
    return best;
}

static double max_lte(esk_island *isl)
{
    mna *p = (mna *)isl->priv;
    size_t i;
    double worst = 0.0;
    for (i = 0; i < isl->devs.len; i++) {
        esk_adev *d = isl->devs.data[i];
        if (d->ops->lte) {
            double l = d->ops->lte(d, &p->in);
            if (l == l && l > worst)
                worst = l;
        }
    }
    return worst;
}

static double lte_tolerance(const esk_island *isl)
{
    const mna *p = (const mna *)isl->priv;
    double vmax = 1.0;
    int i;
    for (i = 0; i + 1 < p->nnodes; i++)
        if (fabs(p->x_iter[i]) > vmax)
            vmax = fabs(p->x_iter[i]);
    return isl->p.lte_tol * vmax;
}

static es_time_t clamp_h(const esk_island *isl, double h_s)
{
    double ps = h_s * 1e12;
    es_time_t h;
    if (!(ps > 0.0))
        return isl->p.h_min_ps;
    if (ps > (double)isl->p.h_max_ps)
        return isl->p.h_max_ps;
    h = (es_time_t)ps; /* truncation of a bounded positive value */
    if (h < isl->p.h_min_ps)
        h = isl->p.h_min_ps;
    return h;
}

static es_status fail_step(esk_island *isl, es_time_t t, const char *code, es_severity_t sev,
                           const char *msg)
{
    raise_isl(isl, sev, code, t, "%s", msg);
    isl->failed = 1;
    return ES_ECONVERGE;
}

/* The earliest pending event that is not another island's step, after `after`.
 * The engine clips an island's step to the next event of any class, so islands
 * scheduled at the same time would otherwise clip each other to 1 ps steps for
 * ever; their ANALOG events cannot change this island's sources (a crossing
 * reaches it through a SIGNAL event and esk_island_touch), so they are ignored. */
static es_time_t next_foreign_event(const esk_island *isl)
{
    const esk_sched *s = &isl->e->sched;
    es_time_t best = ES_TIME_MAX;
    size_t i;
    for (i = 0; i < s->len; i++) {
        const esk_event *ev = &s->heap[i];
        if (ev->cancelled || ev->cls == ESK_EV_ANALOG)
            continue;
        if (ev->time < best)
            best = ev->time;
    }
    return best;
}

/* how far this call may integrate: `until` as the caller clipped it, extended
 * past other islands' step events up to the island's own proposal */
static es_time_t step_horizon(const esk_island *isl, es_time_t until)
{
    const mna *p = (const mna *)isl->priv;
    es_time_t t = isl->t_accepted, want, foreign;
    es_time_t h = isl->h_next_ps ? isl->h_next_ps : isl->p.h_max_ps;
    if (p->exact_until || p->restart || p->touched)
        return until;
    if (esk_time_add(t, h, &want))
        want = ES_TIME_MAX - 1;
    if (want <= until)
        return until;
    /* a pending foreign event at or before the accepted time (one at `now` of a
     * later class, such as the advance() marker) means the caller's clip stands */
    foreign = next_foreign_event(isl);
    if (foreign <= t)
        return until;
    if (foreign < want)
        want = foreign;
    return want > until ? want : until;
}

/* the step to x_iter changed no unknown beyond rounding */
static int unchanged(const esk_island *isl)
{
    const mna *p = (const mna *)isl->priv;
    int i;
    for (i = 0; i < p->n; i++) {
        double a = isl->x[i], b = p->x_iter[i];
        if (fabs(a - b) > 1e-14 * (1.0 + fabs(a)))
            return 0;
    }
    return 1;
}

/* a trial step of the longest length from the accepted solution, discarded:
 * 1 when it changes nothing, so the island sits at its steady state (a step
 * as short as an event's clip can leave a slow transient looking still) */
static int steady(esk_island *isl)
{
    mna *p = (mna *)isl->priv;
    es_time_t tn;
    int bad = -1, ok;
    if (esk_time_add(isl->t_accepted, isl->p.h_max_ps, &tn))
        return 0;
    p->in.h = (double)isl->p.h_max_ps * MNA_PS_TO_S;
    p->in.h_prev = p->h_prev_s;
    p->in.t_ps = tn;
    p->in.method = INTEG_TRAP;
    p->in.negligible = 0;
    memcpy(p->x_iter, isl->x, (size_t)p->n * sizeof *p->x_iter);
    ok = newton(isl, 0, &bad) == 0 && unchanged(isl);
    reject_all(isl);
    return ok;
}

es_status esk_island_step(esk_island *isl, es_time_t until, es_time_t *reached)
{
    mna *p = (mna *)isl->priv;
    es_time_t t = isl->t_accepted;
    uint32_t substeps = 0;
    uint32_t bisections = 0;
    if (reached)
        *reached = t;
    if (!p->running || isl->failed)
        return ES_ECONVERGE;
    if (until <= t)
        return ES_OK;
    until = step_horizon(isl, until);
    if (p->in_step)
        return ES_ESTATE;
    p->in_step = 1;
    while (t < until && substeps++ < MNA_MAX_SUBSTEPS) {
        es_time_t h = isl->h_next_ps ? isl->h_next_ps : isl->p.h_max_ps;
        es_time_t tn, bp;
        int method = INTEG_TRAP;
        int bad = -1, r, i, at_bp;
        double lte, tol;
        if (p->touched) {
            p->touched = 0;
            p->restart = 1;
            p->quiet = 0;
            p->still = 0;
        }
        if (p->quiet && !p->restart && !p->bracket_hi && next_breakpoint(isl, t) > until) {
            /* steady: the solution at `until` is the one it has, and the next
             * step may be the longest */
            isl->t_accepted = until;
            isl->h_next_ps = p->h_prop_ps = isl->p.h_max_ps;
            isl->quiet_steps++;
            t = until;
            if (reached)
                *reached = t;
            break;
        }
        p->quiet = 0;
        if (p->restart) {
            method = INTEG_BE;
            if (h > MNA_TOUCH_STEP_PS)
                h = MNA_TOUCH_STEP_PS;
        }
        if (h < isl->p.h_min_ps)
            h = isl->p.h_min_ps;
        if (h > isl->p.h_max_ps)
            h = isl->p.h_max_ps;
        if (esk_time_add(t, h, &tn) || tn > until)
            tn = until;
        bp = next_breakpoint(isl, t);
        if (bp <= tn)
            tn = bp;
        at_bp = tn == bp;
        if (p->bracket_hi > t && p->bracket_hi < tn)
            tn = p->bracket_hi;
        h = tn - t;
        /* trial solve from the accepted solution */
        p->in.h = (double)h * MNA_PS_TO_S;
        p->in.h_prev = p->h_prev_s;
        p->in.t_ps = tn;
        p->in.method = method;
        p->in.negligible =
            method == INTEG_TRAP && p->h_prev_s > 0.0 && p->in.h < 1e-3 * p->h_prev_s;
        memcpy(p->x_iter, isl->x, (size_t)p->n * sizeof *p->x_iter);
        r = newton(isl, 0, &bad);
        if (r == 2) {
            reject_all(isl);
            report_singular(isl, bad, tn);
            isl->failed = 1;
            p->in_step = 0;
            return ES_ECONVERGE;
        }
        if (r == 0) {
            lte = max_lte(isl);
            tol = lte_tolerance(isl);
        } else {
            lte = 0.0;
            tol = 1.0;
        }
        if (r != 0 || lte > tol) {
            /* reject: halve (or follow the LTE proposal if it asks for less) */
            double h_new = (double)h / 2.0;
            reject_all(isl);
            isl->rejections++;
            p->consecutive_rej++;
            if (r == 0 && lte > 0.0) {
                double prop = (double)h * pow(tol / lte, 1.0 / 3.0);
                if (prop < h_new)
                    h_new = prop;
                if (h_new < (double)h / 4.0)
                    h_new = (double)h / 4.0;
            }
            if (p->consecutive_rej > (uint32_t)isl->p.max_step_rejections) {
                p->in_step = 0;
                if (r != 0)
                    report_no_convergence(isl, tn, "transient step");
                return fail_step(isl, tn, "analog.step_failure", ES_SEV_FATAL,
                                 "too many consecutive step rejections");
            }
            if (h <= isl->p.h_min_ps || h_new < (double)isl->p.h_min_ps) {
                char msg[200];
                size_t wi = 0;
                double worst = -1.0;
                for (i = 0; i < (int)isl->devs.len; i++) {
                    esk_adev *d = isl->devs.data[(size_t)i];
                    double l = d->ops->lte ? d->ops->lte(d, &p->in) : 0.0;
                    if (l > worst) {
                        worst = l;
                        wi = (size_t)i;
                    }
                }
                esk_snprintf(msg, sizeof msg, "step below h_min at %s (largest error: %s)",
                             r != 0 ? "a non-converging step" : "the error control",
                             isl->devs.len ? dev_name(isl->devs.data[wi]) : "none");
                p->in_step = 0;
                return fail_step(isl, tn, "analog.timestep_too_small", ES_SEV_ERROR, msg);
            }
            isl->h_next_ps = clamp_h(isl, h_new * MNA_PS_TO_S);
            p->h_prop_ps = isl->h_next_ps;
            p->restart = method == INTEG_BE;
            continue;
        }
        /* a comparator crossing inside the step: bisect unless already resolved */
        if (any_crossing(isl, p->x_iter)) {
            es_time_t res = isl->e->crossing_resolution_ps ? isl->e->crossing_resolution_ps : 100;
            if (h > res && bisections++ < MNA_MAX_BISECT) {
                es_time_t h2 = h / 2;
                reject_all(isl);
                if (!p->bracket_hi)
                    p->h_before_bracket = isl->h_next_ps;
                p->bracket_hi = tn;
                if (h2 < res)
                    h2 = res;
                if (h2 < isl->p.h_min_ps)
                    h2 = isl->p.h_min_ps;
                isl->h_next_ps = h2;
                p->restart = method == INTEG_BE;
                continue;
            }
            /* accept this step and deliver the crossing at its end */
            p->still = 0;
            publish(isl, tn);
            isl->steps++;
            p->consecutive_rej = 0;
            p->h_prev_s = p->in.negligible ? p->h_prev_s + p->in.h : p->in.h;
            p->restart = 0;
            isl->h_next_ps = p->bracket_hi && p->h_before_bracket ? p->h_before_bracket
                                                                  : clamp_h(isl, 2.0 * p->in.h);
            p->bracket_hi = 0;
            p->h_before_bracket = 0;
            deliver_crossings(isl, tn);
            t = tn;
            if (reached)
                *reached = t;
            p->in_step = 0;
            return ES_OK;
        }
        /* accept: unchanged trapezoidal steps away from breakpoints leave
         * the island quiet, once a second one has solved what the first's
         * accept changed, no breakpoint is pending after it, and a trial step
         * of the longest length changes nothing either */
        p->still = method == INTEG_TRAP && !p->bracket_hi && !at_bp && unchanged(isl) ? p->still + 1 : 0;
        publish(isl, tn);
        if (p->still >= 2 && next_breakpoint(isl, tn) == ES_TIME_MAX) {
            p->quiet = steady(isl);
            if (!p->quiet)
                p->still = 0;
        }
        isl->steps++;
        p->consecutive_rej = 0;
        p->h_prev_s = p->in.negligible ? p->h_prev_s + p->in.h : p->in.h;
        p->restart = 0;
        {
            /* the proposal grows from the larger of this step and the last unclipped
             * proposal, so a step cut short by an event does not throttle the next */
            double h_s = p->in.h, h_new;
            double h_base = (double)p->h_prop_ps * MNA_PS_TO_S;
            if (h_base < h_s)
                h_base = h_s;
            if (lte > 0.0)
                h_new = h_s * pow(tol / lte, 1.0 / 3.0);
            else
                h_new = 2.0 * h_base;
            if (h_new > 2.0 * h_base)
                h_new = 2.0 * h_base;
            if (h_new < h_s / 4.0)
                h_new = h_s / 4.0;
            if (method == INTEG_BE && h_new < h_s)
                h_new = h_s; /* the first-order estimate is pessimistic; trapezoidal follows */
            p->h_prop_ps = clamp_h(isl, h_new);
            isl->h_next_ps = p->h_prop_ps;
        }
        if (p->bracket_hi) {
            if (tn >= p->bracket_hi) {
                /* the bracket closed without a crossing: resume the larger steps */
                isl->h_next_ps = p->h_before_bracket ? p->h_before_bracket : isl->h_next_ps;
                p->bracket_hi = 0;
                p->h_before_bracket = 0;
            } else {
                /* bisection continues over the remaining bracket */
                isl->h_next_ps = p->bracket_hi - tn;
            }
        }
        t = tn;
        if (reached)
            *reached = t;
    }
    p->in_step = 0;
    if (substeps >= MNA_MAX_SUBSTEPS)
        return fail_step(isl, t, "analog.step_failure", ES_SEV_FATAL, "substep bound exceeded");
    return ES_OK;
}

void esk_island_touch(esk_island *isl, es_time_t now)
{
    mna *p = (mna *)isl->priv;
    es_time_t reached = 0;
    uint32_t guard = 0;
    if (!p->running || isl->failed)
        return;
    if (p->in_step) {
        p->touched = 1;
        return;
    }
    /* integrate up to `now` with the old sources before they change */
    p->exact_until = 1;
    while (isl->t_accepted < now && guard++ < MNA_MAX_CATCHUP) {
        if (esk_island_step(isl, now, &reached) != ES_OK) {
            isl->failed = 1;
            p->exact_until = 0;
            return;
        }
    }
    p->exact_until = 0;
    p->restart = 1;
    p->quiet = 0;
    p->still = 0;
    if (isl->h_next_ps > MNA_TOUCH_STEP_PS)
        isl->h_next_ps = MNA_TOUCH_STEP_PS;
    p->h_prop_ps = isl->h_next_ps;
    p->bracket_hi = 0;
    p->h_before_bracket = 0;
    schedule_local(isl, isl->t_accepted > now ? isl->t_accepted : now);
}

double esk_island_node_v(const esk_island *isl, int node)
{
    const mna *p = (const mna *)isl->priv;
    if (!isl->x || node <= 0 || node > p->n)
        return 0.0;
    return isl->x[node - 1];
}

esk_json *esk_island_observe(esk_island *isl)
{
    esk_alloc *a = isl->a;
    esk_json *o = esk_json_new_object(a), *nodes, *devs;
    size_t i;
    if (!o)
        return NULL;
    nodes = esk_json_new_object(a);
    devs = esk_json_new_object(a);
    if (!nodes || !devs) {
        esk_json_free(a, nodes);
        esk_json_free(a, devs);
        esk_json_free(a, o);
        return NULL;
    }
    esk_json_set_u64_string(a, o, "t_ps", isl->t_accepted);
    esk_json_set_integer(a, o, "island", (int64_t)isl->id);
    esk_json_set_integer(a, o, "steps", (int64_t)isl->steps);
    esk_json_set_integer(a, o, "rejections", (int64_t)isl->rejections);
    for (i = 0; i < isl->nets.len; i++) {
        esk_net *net = isl->nets.data[i];
        if (net && net->name)
            esk_json_set_number(a, nodes, net->name, i ? esk_island_node_v(isl, (int)i) : 0.0);
    }
    for (i = 0; i < isl->devs.len; i++) {
        esk_adev *d = isl->devs.data[i];
        esk_json *od;
        if (!d->ops->observe)
            continue;
        od = esk_json_new_object(a);
        if (!od)
            continue;
        d->ops->observe(d, od);
        if (d->comp && d->comp->id) {
            esk_json_object_set(a, devs, d->comp->id, od);
        } else if (d->pin && d->pin->comp && d->pin->comp->id) {
            char key[160];
            esk_snprintf(key, sizeof key, "%s.%s", d->pin->comp->id,
                         d->pin->name ? d->pin->name : "?");
            esk_json_object_set(a, devs, key, od);
        } else {
            esk_json_free(a, od);
        }
    }
    esk_json_object_set(a, o, "nodes", nodes);
    esk_json_object_set(a, o, "devices", devs);
    return o;
}
