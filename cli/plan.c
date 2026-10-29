/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* plan.c -- `embsim plan BOARD --needs ...` (issue #101): a pin assignment
 * for a project's peripherals on a board, as an engineer makes it with the
 * board's user manual and CubeMX's pin view: each peripheral on one
 * instance (SPI1's SCK, MISO and MOSI together), each signal on a pin that
 * carries it (the MCU description's io.alternate, its ADC channels from
 * io.analog), each pin reachable on a header, and clear of what the board
 * itself does with its pins.
 *
 * What the board does with a pin is read from the board's own nets: any
 * part on the pin's net but the MCU and the headers (LD2's resistor, the
 * LSE crystal, the ST-LINK's console), named by the board's leds, buttons
 * and console when they name it. Such a pin is used only when nothing
 * else carries the signal, and is flagged; the debugger's SWDIO and SWCLK
 * never are. Among equal pins the preferred headers' come first
 * (--prefer arduino: D0-D15 and A0-A5; or a connector's name, CN7).
 *
 * The choice is greedy, the scarcest needs first (peripherals, then timer
 * channels, analog inputs, plain GPIO), each instance tried and the one
 * with the fewest flagged pins kept; it does not search every combination.
 * An MCU whose description has no alternate-function table gets plain
 * GPIO only, and says so. */
#include "plan.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host/host.h"
#include "project/catalog.h"
#include "session/session_priv.h"
#include "util/str.h"

#define PL_PINS 256
#define PL_ROWS 128

typedef struct pl_pin {
    const char *name;   /* PA5 */
    const esk_json *af; /* its io.alternate entry: AF number -> signals */
    const char *adc;    /* io.analog: ADC12_IN5 */
    const char *ports[6];
    int nports;
    int preferred;      /* on a preferred header */
    char uses[128];     /* what the board does with it */
    int hard;           /* never (the debugger's) */
    int used;
} pl_pin;

typedef struct pl_row {
    char need[24], signal[32], pin[12], port[16], af[8], note[160];
} pl_row;

typedef struct planner {
    es_session *s;
    const esk_json *board, *mcu;
    const char *inst;
    pl_pin pins[PL_PINS];
    int npins;
    pl_row rows[PL_ROWS];
    int nrows;
    const char *prefer;
    int failed;
    char taken[16][12]; /* the instances given out: SPI2 */
    int ntaken;
    char cs[8][12]; /* the SPIs whose chip selects come later */
    int ncs;
} planner;

static int is_port_pin(const char *n)
{
    /* GPIO names as the descriptions write them: PA5, P0.13, GPIO8, RB3 */
    return n && ((n[0] == 'P' && ((n[1] >= 'A' && n[1] <= 'Z') || (n[1] >= '0' && n[1] <= '9'))) ||
                 !strncmp(n, "GPIO", 4) || (n[0] == 'R' && n[1] >= 'A' && n[1] <= 'K'));
}

static int preferred_port(const planner *p, const char *port, const char *connector)
{
    if (!p->prefer)
        return 0;
    if (!strcmp(p->prefer, "arduino"))
        return (port[0] == 'D' || port[0] == 'A') && port[1] >= '0' && port[1] <= '9';
    return connector && !strcmp(connector, p->prefer);
}

static pl_pin *pin_named(planner *p, const char *name)
{
    int i;
    for (i = 0; i < p->npins; i++)
        if (!strcmp(p->pins[i].name, name))
            return &p->pins[i];
    if (p->npins == PL_PINS)
        return NULL;
    memset(&p->pins[p->npins], 0, sizeof p->pins[0]);
    p->pins[p->npins].name = name;
    return &p->pins[p->npins++];
}

static void add_use(pl_pin *q, const char *what)
{
    size_t n = strlen(q->uses);
    if (strstr(q->uses, what))
        return;
    snprintf(q->uses + n, sizeof q->uses - n, "%s%s", n ? ", " : "", what);
}

/* the board's pins: reachable ones (on a header), their uses, their functions */
static void survey(planner *p)
{
    const esk_json *ports = esk_json_get(p->board, "ports"), *nets = esk_json_get(p->board, "nets");
    const esk_json *comps = esk_json_get(p->board, "components"), *io = esk_json_get(p->mcu, "io");
    const esk_json *alt = esk_json_get(io, "alternate"), *analog = esk_json_get(io, "analog");
    const esk_json *leds = esk_json_get(p->board, "leds"), *buttons = esk_json_get(p->board, "buttons");
    const esk_json *console = esk_json_get(p->board, "console");
    size_t i, k, j, n = strlen(p->inst);
    for (i = 0; i < esk_json_len(ports); i++) {
        const esk_json *pt = esk_json_at(ports, i);
        const esk_json *members = esk_json_get(nets, esk_json_get_str(pt, "net", ""));
        for (k = 0; k < esk_json_len(members); k++) {
            const char *m = esk_json_str(esk_json_at(members, k));
            pl_pin *q;
            if (!m || strncmp(m, p->inst, n) || m[n] != '.' || !is_port_pin(m + n + 1))
                continue;
            q = pin_named(p, m + n + 1);
            if (!q)
                continue;
            if (q->nports < 6)
                q->ports[q->nports++] = pt->key;
            if (preferred_port(p, pt->key, esk_json_get_str(pt, "connector", NULL)))
                q->preferred = 1;
            /* the others on its net: the board's own use of it */
            for (j = 0; j < esk_json_len(members); j++) {
                const char *o = esk_json_str(esk_json_at(members, j));
                const char *dot = o ? strchr(o, '.') : NULL;
                size_t c;
                const char *kind = NULL;
                char id[32];
                if (!dot || (size_t)(dot - o) >= sizeof id)
                    continue;
                memcpy(id, o, (size_t)(dot - o));
                id[dot - o] = 0;
                if (!strcmp(id, p->inst))
                    continue;
                for (c = 0; c < esk_json_len(comps); c++)
                    if (esk_str_eq(esk_json_get_str(esk_json_at(comps, c), "id", ""), id))
                        kind = esk_json_get_str(esk_json_at(comps, c), "component", "");
                if (kind && strcmp(kind, "emblink/header"))
                    add_use(q, id);
            }
        }
    }
    for (i = 0; i < (size_t)p->npins; i++) {
        pl_pin *q = &p->pins[i];
        q->af = esk_json_get(alt, q->name);
        q->adc = esk_json_get_str(analog, q->name, NULL);
        for (k = 0; k < esk_json_len(leds); k++)
            if (esk_str_eq(esk_json_get_str(esk_json_at(leds, k), "gpio", ""), q->name))
                add_use(q, esk_json_get_str(esk_json_at(leds, k), "component", "LED"));
        for (k = 0; k < esk_json_len(buttons); k++)
            if (esk_str_eq(esk_json_get_str(esk_json_at(buttons, k), "gpio", ""), q->name))
                add_use(q, esk_json_get_str(esk_json_at(buttons, k), "component", "button"));
        if (esk_str_eq(esk_json_get_str(console, "tx", ""), q->name) || esk_str_eq(esk_json_get_str(console, "rx", ""), q->name))
            add_use(q, "the console");
        for (k = 0; k < esk_json_len(q->af); k++) {
            const esk_json *sigs = esk_json_at(q->af, k);
            for (j = 0; j < esk_json_len(sigs); j++) {
                const char *sg = esk_json_str(esk_json_at(sigs, j));
                if (sg && (strstr(sg, "SWDIO") || strstr(sg, "SWCLK")))
                    q->hard = 1;
                else if (sg && strstr(sg, "SWO"))
                    add_use(q, "the debugger's SWO");
            }
        }
    }
}

/* the AF number carrying `signal` on the pin, or NULL */
static const char *af_of(const pl_pin *q, const char *signal)
{
    size_t k, j;
    for (k = 0; k < esk_json_len(q->af); k++) {
        const esk_json *sigs = esk_json_at(q->af, k);
        for (j = 0; j < esk_json_len(sigs); j++)
            if (esk_str_eq(esk_json_str(esk_json_at(sigs, j)), signal))
                return sigs->key;
    }
    return NULL;
}

static int cost(const pl_pin *q)
{
    return (q->uses[0] ? 10 : 0) + (q->preferred ? 0 : 1);
}

/* a header the board documents for the signal ("PB8, I2C1_SCL" on D15) */
static int documented(const planner *p, const pl_pin *q, const char *signal)
{
    int k;
    for (k = 0; k < q->nports; k++)
        if (strstr(esk_json_get_str(esk_json_get(esk_json_get(p->board, "ports"), q->ports[k]), "doc", ""), signal))
            return 1;
    return 0;
}

/* the best free pin carrying `signal`, or NULL */
static pl_pin *best_for(planner *p, const char *signal, const char **af)
{
    pl_pin *best = NULL;
    int i, bc = 0;
    for (i = 0; i < p->npins; i++) {
        pl_pin *q = &p->pins[i];
        const char *a;
        int c;
        if (q->used || q->hard || !(a = af_of(q, signal)))
            continue;
        c = 2 * cost(q) - documented(p, q, signal);
        if (!best || c < bc) {
            best = q;
            bc = c;
            *af = a;
        }
    }
    return best;
}

static void row(planner *p, const char *need, const char *signal, const pl_pin *q, const char *af)
{
    pl_row *r;
    if (p->nrows == PL_ROWS)
        return;
    r = &p->rows[p->nrows++];
    memset(r, 0, sizeof *r);
    snprintf(r->need, sizeof r->need, "%s", need);
    snprintf(r->signal, sizeof r->signal, "%s", signal);
    if (!q) {
        snprintf(r->note, sizeof r->note, "no free pin");
        p->failed = 1;
        return;
    }
    snprintf(r->pin, sizeof r->pin, "%s", q->name);
    snprintf(r->af, sizeof r->af, "%s", af ? af : "");
    {
        int k, pick = 0;
        for (k = 0; k < q->nports; k++)
            if (preferred_port(p, q->ports[k], NULL) || (p->prefer && strcmp(p->prefer, "arduino") &&
                                                          esk_str_eq(esk_json_get_str(esk_json_get(esk_json_get(p->board, "ports"), q->ports[k]), "connector", ""), p->prefer))) {
                pick = k;
                break;
            }
        snprintf(r->port, sizeof r->port, "%s", q->nports ? q->ports[pick] : "");
    }
    if (q->uses[0])
        snprintf(r->note, sizeof r->note, "shared with %s", q->uses);
}

/* one instance of a peripheral kind: the prefixes tried in order (SPI,
 * then I2C...) and the signals it needs */
static int peripheral(planner *p, const char *need, const char *const *prefixes, const char *const *sigs, int nsig)
{
    int inst, best_inst = -1, best_cost = 1 << 30, k, pi;
    const char *best_pre = NULL;
    char name[32];
    for (pi = 0; prefixes[pi]; pi++)
        for (inst = 1; inst <= 8; inst++) {
            int total = 0, ok = 1, t;
            int marks[8];
            char iname[12];
            snprintf(iname, sizeof iname, "%s%d", prefixes[pi], inst);
            for (t = 0; t < p->ntaken && strcmp(p->taken[t], iname); t++)
                ;
            if (t < p->ntaken)
                continue; /* given to an earlier need */
            for (k = 0; k < nsig && ok; k++) {
                const char *af;
                pl_pin *q;
                snprintf(name, sizeof name, "%s%d_%s", prefixes[pi], inst, sigs[k]);
                q = best_for(p, name, &af);
                if (!q) {
                    ok = 0;
                    break;
                }
                q->used = 1; /* two signals never on one pin */
                marks[k] = (int)(q - p->pins);
                total += cost(q);
            }
            while (k-- > 0)
                p->pins[marks[k]].used = 0;
            if (ok && total < best_cost) {
                best_cost = total;
                best_inst = inst;
                best_pre = prefixes[pi];
            }
        }
    if (best_inst < 0) {
        row(p, need, "(no instance free)", NULL, NULL);
        return 0;
    }
    if (p->ntaken < 16) {
        snprintf(p->taken[p->ntaken], sizeof p->taken[0], "%s%d", best_pre, best_inst);
        p->ntaken++;
    }
    for (k = 0; k < nsig; k++) {
        const char *af;
        pl_pin *q;
        snprintf(name, sizeof name, "%s%d_%s", best_pre, best_inst, sigs[k]);
        q = best_for(p, name, &af);
        if (q)
            q->used = 1;
        row(p, need, name, q, af);
    }
    return 1;
}

static void pwm(planner *p, int count)
{
    int t, ch, got = 0;
    char name[24];
    /* channels of as few timers as can give them: a timer's channels share its period */
    for (t = 1; t <= 14 && got < count; t++)
        for (ch = 1; ch <= 4 && got < count; ch++) {
            const char *af;
            pl_pin *q;
            snprintf(name, sizeof name, "TIM%d_CH%d", t, ch);
            q = best_for(p, name, &af);
            if (!q || q->uses[0])
                continue;
            q->used = 1;
            row(p, "pwm", name, q, af);
            got++;
        }
    while (got++ < count)
        row(p, "pwm", "TIMx_CHy", NULL, NULL);
}

static pl_pin *best_free(planner *p, int need_adc)
{
    pl_pin *best = NULL;
    int i;
    for (i = 0; i < p->npins; i++) {
        pl_pin *q = &p->pins[i];
        if (q->used || q->hard || (need_adc && !q->adc))
            continue;
        if (!best || cost(q) < cost(best) || (cost(q) == cost(best) && !need_adc && !q->adc && best->adc))
            best = q; /* plain GPIO leaves the ADC inputs for adc needs */
    }
    return best;
}

static void simple(planner *p, const char *need, int count, int adc)
{
    int k;
    for (k = 0; k < count; k++) {
        pl_pin *q = best_free(p, adc);
        char sig[24];
        if (q)
            q->used = 1;
        snprintf(sig, sizeof sig, "%s", adc && q ? q->adc : need);
        row(p, need, sig, q, NULL);
    }
}

int embsim_plan(es_session *s, const char *board_id, const char *needs, const char *prefer, const char *header, int json)
{
    static const char *const spi_pre[] = { "SPI", NULL }, *const spi_sig[] = { "SCK", "MISO", "MOSI" };
    static const char *const i2c_pre[] = { "I2C", NULL }, *const i2c_sig[] = { "SCL", "SDA" };
    static const char *const uart_pre[] = { "USART", "UART", NULL }, *const uart_sig[] = { "TX", "RX" };
    static const char *const can_pre[] = { "CAN", NULL }, *const can_sig[] = { "RX", "TX" };
    /* spi_cs: each SPI's chip select, a plain GPIO, once every peripheral has its pins */
    static const char *const order[] = { "spi", "i2c", "uart", "can", "spi_cs", "pwm", "adc", "gpio" };
    planner *p = (planner *)esk_calloc(s->a, 1, sizeof *p);
    const esk_catalog_item *b = esk_catalog_find(s->catalog, "board", board_id);
    const esk_catalog_item *m;
    const char *mref;
    int o, i, has_alt;
    if (!p)
        return 2;
    if (!b) {
        fprintf(stderr, "embsim: board %s not found\n", board_id);
        esk_free(s->a, p, sizeof *p);
        return 2;
    }
    mref = esk_json_get_str(esk_json_get(b->desc, "mcu"), "ref", "");
    m = esk_catalog_find(s->catalog, "mcu", mref);
    p->s = s;
    p->board = b->desc;
    p->mcu = m ? m->desc : NULL;
    p->inst = esk_json_get_str(esk_json_get(b->desc, "mcu"), "instance", "U1");
    p->prefer = prefer;
    survey(p);
    has_alt = esk_json_get(esk_json_get(p->mcu, "io"), "alternate") != NULL;
    /* the needs, scarcest first */
    for (o = 0; o < (int)(sizeof order / sizeof order[0]); o++) {
        const char *q = needs;
        if (!strcmp(order[o], "spi_cs"))
            for (i = 0; i < p->ncs; i++) { /* the chip selects: GPIOs the driver toggles */
                char cs[16];
                simple(p, "spi_cs", 1, 0);
                memcpy(cs, p->cs[i], sizeof p->cs[0]);
                cs[sizeof p->cs[0]] = 0;
                snprintf(p->rows[p->nrows - 1].signal, sizeof p->rows[0].signal, "%s_CS", cs);
            }
        while (*q) {
            size_t len = strcspn(q, ",");
            char item[32];
            char *colon;
            int count = 1;
            snprintf(item, sizeof item, "%.*s", (int)(len < sizeof item ? len : sizeof item - 1), q);
            colon = strchr(item, ':');
            if (colon) {
                *colon = 0;
                count = atoi(colon + 1);
            }
            if (!strcmp(item, order[o])) {
                if (!has_alt && strcmp(item, "gpio")) {
                    if (o == 0 || strcmp(item, order[o - 1]))
                        fprintf(stderr, "embsim: plan: %s's description has no alternate-function table (io.alternate): "
                                        "only gpio can be planned on it\n", mref);
                    p->failed = 1;
                } else if (!strcmp(item, "spi")) {
                    for (i = 0; i < count; i++) {
                        if (peripheral(p, "spi", spi_pre, spi_sig, 3) && p->ncs < 8)
                            memcpy(p->cs[p->ncs++], p->taken[p->ntaken - 1], sizeof p->cs[0]);
                    }
                } else if (!strcmp(item, "i2c")) {
                    for (i = 0; i < count; i++)
                        peripheral(p, "i2c", i2c_pre, i2c_sig, 2);
                } else if (!strcmp(item, "uart")) {
                    for (i = 0; i < count; i++)
                        peripheral(p, "uart", uart_pre, uart_sig, 2);
                } else if (!strcmp(item, "can")) {
                    for (i = 0; i < count; i++)
                        peripheral(p, "can", can_pre, can_sig, 2);
                } else if (!strcmp(item, "spi_cs")) {
                    /* not a need of its own: spi's */
                } else if (!strcmp(item, "pwm")) {
                    pwm(p, count);
                } else if (!strcmp(item, "adc")) {
                    simple(p, "adc", count, 1);
                } else {
                    simple(p, "gpio", count, 0);
                }
            }
            q += len;
            if (*q == ',')
                q++;
        }
    }
    {
        const char *q = needs; /* needs it does not know */
        while (*q) {
            size_t len = strcspn(q, ",:");
            int known = 0;
            for (o = 0; o < (int)(sizeof order / sizeof order[0]); o++)
                if (strlen(order[o]) == len && !strncmp(order[o], q, len))
                    known = 1;
            if (!known) {
                fprintf(stderr, "embsim: plan: no need %.*s (spi, i2c, uart, can, pwm:N, adc:N, gpio:N)\n", (int)len, q);
                p->failed = 1;
            }
            q += strcspn(q, ",");
            if (*q == ',')
                q++;
        }
    }
    if (json) {
        esk_json *out = esk_json_new_object(s->a), *arr = esk_json_new_array(s->a);
        char *t;
        esk_json_set_string(s->a, out, "board", board_id);
        esk_json_set_bool(s->a, out, "ok", !p->failed);
        for (i = 0; i < p->nrows; i++) {
            esk_json *r = esk_json_new_object(s->a);
            esk_json_set_string(s->a, r, "need", p->rows[i].need);
            esk_json_set_string(s->a, r, "signal", p->rows[i].signal);
            if (p->rows[i].pin[0]) {
                esk_json_set_string(s->a, r, "pin", p->rows[i].pin);
                esk_json_set_string(s->a, r, "port", p->rows[i].port);
                if (p->rows[i].af[0])
                    esk_json_set_integer(s->a, r, "af", atoi(p->rows[i].af));
            }
            if (p->rows[i].note[0])
                esk_json_set_string(s->a, r, "note", p->rows[i].note);
            esk_json_array_push(s->a, arr, r);
        }
        esk_json_object_set(s->a, out, "pins", arr);
        t = esk_json_format(s->a, out, 2, NULL);
        if (t) {
            printf("%s\n", t);
            esk_strfree(s->a, t);
        }
        esk_json_free(s->a, out);
    } else {
        printf("%s: a pin plan for %s%s%s\n", board_id, needs, prefer ? ", preferring " : "", prefer ? prefer : "");
        printf("  %-8s %-14s %-6s %-4s %-10s %s\n", "need", "signal", "pin", "AF", "header", "note");
        for (i = 0; i < p->nrows; i++)
            printf("  %-8s %-14s %-6s %-4s %-10s %s\n", p->rows[i].need, p->rows[i].signal, p->rows[i].pin[0] ? p->rows[i].pin : "-",
                   p->rows[i].af, p->rows[i].port, p->rows[i].note);
    }
    if (header) {
        esk_strbuf h;
        esk_strbuf_init(&h, s->a);
        esk_strbuf_printf(&h, "/* %s's pins for %s, from embsim plan */\n#ifndef PINS_H\n#define PINS_H\n\n", board_id, needs);
        for (i = 0; i < p->nrows; i++) {
            const pl_row *r = &p->rows[i];
            char macro[48];
            size_t k;
            if (!r->pin[0])
                continue;
            if (strchr(r->signal, '_')) /* a peripheral's signal: SPI1_SCK */
                snprintf(macro, sizeof macro, "PIN_%s", r->signal);
            else { /* the n-th of its kind: PIN_GPIO_3 */
                int nth = 0, j;
                for (j = 0; j < i; j++)
                    nth += !strcmp(p->rows[j].need, r->need);
                snprintf(macro, sizeof macro, "PIN_%s_%d", r->need, nth);
            }
            for (k = 0; macro[k]; k++)
                macro[k] = (char)(macro[k] >= 'a' && macro[k] <= 'z' ? macro[k] - 32 : (macro[k] >= 'A' && macro[k] <= 'Z') || (macro[k] >= '0' && macro[k] <= '9') ? macro[k] : '_');
            esk_strbuf_printf(&h, "#define %-28s \"%s\" /* %s%s%s%s */\n", macro, r->pin, r->port, r->af[0] ? ", AF" : "",
                              r->af, r->note[0] ? "; shared" : "");
        }
        esk_strbuf_puts(&h, "\n#endif\n");
        if (h.oom || esk_host_write_file(header, h.data ? h.data : "", h.len) != ES_OK) {
            fprintf(stderr, "embsim: cannot write %s\n", header);
            p->failed = 1;
        }
        esk_strbuf_free(&h);
    }
    i = p->failed ? 1 : 0;
    esk_free(s->a, p, sizeof *p);
    return i;
}
