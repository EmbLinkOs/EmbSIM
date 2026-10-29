/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* export.c -- `embsim export svg` (issue #94): the schematic sheet as the
 * workbench lays it out, or what a display shows at a moment, as an SVG
 * file for a README, an issue or a CI artefact.
 *
 * The sheet: each part's catalog symbol at its layout (the workbench's
 * transform: translate to the origin, mirror and rotate about the centre),
 * each board as the block of its ports the workbench draws for it (the
 * first connector down the left, the second down the right, the rest along
 * the bottom), the project's wires and net labels, and a dashed straight
 * line between the pins of a net no wire draws. Symbols keep their own
 * markup, their ids prefixed by the instance; their colours come from
 * --symbol-stroke and --symbol-fill, set by the theme (light, or dark as the
 * workbench's default).
 *
 * A display: a part with a `pixels` output (an SVG path, one unit a pixel:
 * the SSD1306's) drawn on its glass, or one with line outputs (line1...,
 * an HD44780 module's) as the characters on its backlight.
 *
 * Omitted: live states on symbols (an LED's glow), buses' drawing, groups'
 * frames, the bench and the board artwork. */
#include "export.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "host/host.h"
#include "project/catalog.h"
#include "session/session_priv.h"
#include "util/str.h"

#define GRID 10.0
#define PITCH (2 * GRID)
#define HEADER (3 * GRID)

typedef struct sym {
    double w, h;
    const esk_json *pins; /* name -> {x, y}, or NULL for a board (bpins) */
    /* a board's generated block */
    char **bnames;
    double *bx, *by;
    int nb;
    size_t bcap; /* the arrays' length */
    char *svg; /* the markup to place, owned */
} sym;

typedef struct place {
    const char *id;
    double x, y;
    int rot, mirror;
    sym s;
} place;

typedef struct sheet {
    esk_alloc *a;
    esk_strbuf out;
    place *pl;
    int npl;
    double x0, y0, x1, y1;
} sheet;

static void grow_bounds(sheet *sh, double x, double y)
{
    if (x < sh->x0) sh->x0 = x;
    if (y < sh->y0) sh->y0 = y;
    if (x > sh->x1) sh->x1 = x;
    if (y > sh->y1) sh->y1 = y;
}

static void xml_text(esk_strbuf *b, const char *s)
{
    for (; s && *s; s++) {
        switch (*s) {
        case '<': esk_strbuf_puts(b, "&lt;"); break;
        case '>': esk_strbuf_puts(b, "&gt;"); break;
        case '&': esk_strbuf_puts(b, "&amp;"); break;
        case '"': esk_strbuf_puts(b, "&quot;"); break;
        default: esk_strbuf_putc(b, *s); break;
        }
    }
}

/* localToSheet: a symbol-local point on the sheet */
static void to_sheet(const place *p, double lx, double ly, double *x, double *y)
{
    double cx = p->s.w / 2, cy = p->s.h / 2, dx = lx - cx, dy = ly - cy, rx = dx, ry = dy;
    if (p->mirror)
        dx = -dx;
    rx = dx;
    ry = dy;
    if (p->rot == 90) { rx = -dy; ry = dx; }
    else if (p->rot == 180) { rx = -dx; ry = -dy; }
    else if (p->rot == 270) { rx = dy; ry = -dx; }
    *x = p->x + cx + rx;
    *y = p->y + cy + ry;
}

static int norm_rot(double r)
{
    int n = (int)floor(r / 90.0 + 0.5) * 90;
    n %= 360;
    return n < 0 ? n + 360 : n;
}

/* the symbol's markup with its ids (and their references) prefixed by the
 * instance's, so two instances of one part keep theirs apart */
static char *scoped(esk_alloc *a, const char *svg, const char *id)
{
    esk_strbuf b;
    const char *p = svg;
    esk_strbuf_init(&b, a);
    if (!strncmp(p, "<?xml", 5)) {
        const char *e = strstr(p, "?>");
        p = e ? e + 2 : p;
    }
    while (*p) {
        if (!strncmp(p, "id=\"", 4)) {
            esk_strbuf_printf(&b, "id=\"%s-", id);
            p += 4;
        } else if (!strncmp(p, "url(#", 5)) {
            esk_strbuf_printf(&b, "url(#%s-", id);
            p += 5;
        } else if (!strncmp(p, "href=\"#", 7)) {
            esk_strbuf_printf(&b, "href=\"#%s-", id);
            p += 7;
        } else {
            esk_strbuf_putc(&b, *p++);
        }
    }
    if (b.oom) {
        esk_strbuf_free(&b);
        return NULL;
    }
    return b.data ? b.data : esk_strdup(a, "");
}

/* a board's block of ports (boardSymbol in the workbench's scene) */
static void board_block(esk_alloc *a, const esk_json *desc, sym *s)
{
    const esk_json *ports = esk_json_get(desc, "ports");
    size_t n = esk_json_len(ports), i, k;
    char **conn;
    double *pin;
    size_t *order;
    const char *groups[2] = { NULL, NULL };
    size_t nl = 0, nr = 0, nbot = 0, rows;
    esk_strbuf b;
    double stub = GRID / 2;
    if (!n)
        return;
    conn = (char **)esk_calloc(a, n, sizeof *conn);
    pin = (double *)esk_calloc(a, n, sizeof *pin);
    order = (size_t *)esk_calloc(a, n, sizeof *order);
    s->bnames = (char **)esk_calloc(a, n, sizeof *s->bnames);
    s->bx = (double *)esk_calloc(a, n, sizeof *s->bx);
    s->by = (double *)esk_calloc(a, n, sizeof *s->by);
    s->bcap = n;
    if (!conn || !pin || !order || !s->bnames || !s->bx || !s->by)
        goto out;
    for (i = 0; i < n; i++) {
        const esk_json *p = esk_json_at(ports, i);
        conn[i] = (char *)esk_json_get_str(p, "connector", "");
        pin[i] = esk_json_get(p, "pin") ? esk_json_get_num(p, "pin", 0) : (double)i;
        order[i] = i;
    }
    /* a stable grouping: first-seen connector order */
    {
        size_t *g = (size_t *)esk_calloc(a, n, sizeof *g), gi = 0;
        const char *seen[64];
        size_t nseen = 0, c;
        if (!g)
            goto out;
        for (i = 0; i < n; i++) {
            for (c = 0; c < nseen && strcmp(seen[c], conn[i]); c++)
                ;
            if (c == nseen && nseen < 64)
                seen[nseen++] = conn[i];
        }
        for (c = 0; c < nseen; c++) {
            size_t first = gi;
            for (i = 0; i < n; i++)
                if (!strcmp(conn[i], seen[c]))
                    g[gi++] = i;
            for (i = first + 1; i < gi; i++) { /* by pin within the connector, stable */
                size_t v = g[i], j = i;
                while (j > first && pin[g[j - 1]] > pin[v]) {
                    g[j] = g[j - 1];
                    j--;
                }
                g[j] = v;
            }
            if (c < 2)
                groups[c] = seen[c];
        }
        memcpy(order, g, n * sizeof *order);
        esk_free(a, g, n * sizeof *g);
    }
    for (i = 0; i < n; i++) {
        const char *c = conn[order[i]];
        if (groups[0] && !strcmp(c, groups[0])) nl++;
        else if (groups[1] && !strcmp(c, groups[1])) nr++;
        else nbot++;
    }
    rows = nl > nr ? nl : nr;
    if (!rows)
        rows = 1;
    s->w = 16 * GRID > (double)(nbot + 1) * PITCH ? 16 * GRID : (double)(nbot + 1) * PITCH;
    s->h = HEADER + (double)rows * PITCH + (nbot ? 3 * GRID : GRID);
    esk_strbuf_init(&b, a);
    esk_strbuf_printf(&b, "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 %g %g\" width=\"%g\" height=\"%g\" overflow=\"visible\">",
                      s->w, s->h, s->w, s->h);
    esk_strbuf_printf(&b, "<rect x=\"%g\" y=\"0\" width=\"%g\" height=\"%g\" rx=\"4\" fill=\"var(--symbol-fill)\" stroke=\"var(--symbol-stroke)\" stroke-width=\"1.5\"/>",
                      stub, s->w - 2 * stub, s->h - (nbot ? stub : 0));
    esk_strbuf_printf(&b, "<text x=\"%g\" y=\"%g\" text-anchor=\"middle\" font-family=\"sans-serif\" font-size=\"9\" font-weight=\"600\" fill=\"var(--symbol-stroke)\">",
                      s->w / 2, GRID * 1.8);
    xml_text(&b, esk_json_get_str(desc, "name", ""));
    esk_strbuf_puts(&b, "</text>");
    {
        size_t li = 0, ri = 0, bi = 0;
        for (k = 0; k < n; k++) {
            size_t idx = order[k];
            const char *name = esk_json_at(ports, idx)->key;
            const char *c = conn[idx];
            double x, y;
            if (groups[0] && !strcmp(c, groups[0])) {
                x = 0;
                y = HEADER + (double)li++ * PITCH + GRID;
                esk_strbuf_printf(&b, "<line x1=\"0\" y1=\"%g\" x2=\"%g\" y2=\"%g\" stroke=\"var(--symbol-stroke)\" stroke-width=\"1.5\"/>"
                                      "<text x=\"%g\" y=\"%g\" font-family=\"sans-serif\" font-size=\"7\" fill=\"var(--symbol-stroke)\">",
                                  y, stub, y, stub + 3, y + 2.5);
            } else if (groups[1] && !strcmp(c, groups[1])) {
                x = s->w;
                y = HEADER + (double)ri++ * PITCH + GRID;
                esk_strbuf_printf(&b, "<line x1=\"%g\" y1=\"%g\" x2=\"%g\" y2=\"%g\" stroke=\"var(--symbol-stroke)\" stroke-width=\"1.5\"/>"
                                      "<text x=\"%g\" y=\"%g\" text-anchor=\"end\" font-family=\"sans-serif\" font-size=\"7\" fill=\"var(--symbol-stroke)\">",
                                  x, y, x - stub, y, x - stub - 3, y + 2.5);
            } else {
                x = (double)(++bi) * PITCH;
                y = s->h;
                esk_strbuf_printf(&b, "<line x1=\"%g\" y1=\"%g\" x2=\"%g\" y2=\"%g\" stroke=\"var(--symbol-stroke)\" stroke-width=\"1.5\"/>"
                                      "<text x=\"%g\" y=\"%g\" transform=\"rotate(-90 %g %g)\" font-family=\"sans-serif\" font-size=\"7\" fill=\"var(--symbol-stroke)\">",
                                  x, y, x, y - stub, x + 2.5, y - stub - 3, x + 2.5, y - stub - 3);
            }
            xml_text(&b, name);
            esk_strbuf_puts(&b, "</text>");
            s->bnames[s->nb] = (char *)name;
            s->bx[s->nb] = x;
            s->by[s->nb] = y;
            s->nb++;
        }
    }
    esk_strbuf_puts(&b, "</svg>");
    s->svg = b.oom ? NULL : b.data;
    if (b.oom)
        esk_strbuf_free(&b);
out:
    if (conn) esk_free(a, conn, n * sizeof *conn);
    if (pin) esk_free(a, pin, n * sizeof *pin);
    if (order) esk_free(a, order, n * sizeof *order);
}

static int pin_local(const place *p, const char *pin, double *x, double *y)
{
    int i;
    if (p->s.pins) {
        const esk_json *q = esk_json_get(p->s.pins, pin);
        if (!q)
            return 0;
        *x = esk_json_get_num(q, "x", 0);
        *y = esk_json_get_num(q, "y", 0);
        return 1;
    }
    for (i = 0; i < p->s.nb; i++)
        if (!strcmp(p->s.bnames[i], pin)) {
            *x = p->s.bx[i];
            *y = p->s.by[i];
            return 1;
        }
    return 0;
}

/* "R1.1" or "B1.D2" on the sheet */
static int pin_xy(const sheet *sh, const char *ref, double *x, double *y)
{
    const char *dot = strchr(ref, '.');
    int i;
    double lx, ly;
    if (!dot)
        return 0;
    for (i = 0; i < sh->npl; i++) {
        const place *p = &sh->pl[i];
        if (strlen(p->id) == (size_t)(dot - ref) && !strncmp(p->id, ref, (size_t)(dot - ref)) && pin_local(p, dot + 1, &lx, &ly)) {
            to_sheet(p, lx, ly, x, y);
            return 1;
        }
    }
    return 0;
}

static const char *const VALUE_KEYS[] = { "resistance", "capacitance", "inductance", "voltage", "frequency", "part", "color" };

static void value_text(const esk_json *inst, const esk_json *desc, char *out, size_t cap)
{
    size_t k;
    out[0] = 0;
    for (k = 0; k < sizeof VALUE_KEYS / sizeof VALUE_KEYS[0]; k++) {
        const esk_json *pd = esk_json_get(esk_json_get(desc, "params"), VALUE_KEYS[k]);
        const esk_json *v = esk_json_get(esk_json_get(inst, "params"), VALUE_KEYS[k]);
        const char *unit = esk_json_get_str(pd, "unit", "");
        if (!pd)
            continue;
        if (!v)
            v = esk_json_get(pd, "default");
        if (!v)
            continue;
        if (v->type == ESK_JSON_STRING)
            snprintf(out, cap, "%s", esk_json_str(v));
        else if (v->type == ESK_JSON_NUMBER)
            snprintf(out, cap, "%g%s%s", v->u.number.value, *unit ? " " : "", unit);
        return;
    }
}

static void add_place(sheet *sh, es_session *s, const esk_json *inst, const char *kind, const char *cat_id)
{
    const esk_catalog_item *it = esk_catalog_find(s->catalog, kind, cat_id);
    const esk_json *lay = esk_json_get(inst, "layout");
    place *p = &sh->pl[sh->npl];
    memset(p, 0, sizeof *p);
    p->id = esk_json_get_str(inst, "id", "?");
    p->x = esk_json_get_num(lay, "x", 0);
    p->y = esk_json_get_num(lay, "y", 0);
    p->rot = norm_rot(esk_json_get_num(lay, "rotation", 0));
    p->mirror = esk_json_get_bool(lay, "mirror", 0);
    if (!it)
        return;
    if (!strcmp(kind, "board")) {
        board_block(sh->a, it->desc, &p->s);
    } else {
        const esk_json *sy = esk_json_get(it->desc, "symbol");
        size_t len = 0;
        char *raw = sy ? esk_catalog_read_item_file(s->catalog, it, esk_json_get_str(sy, "svg", "symbol.svg"), &len) : NULL;
        p->s.w = esk_json_get_num(sy, "width", 40);
        p->s.h = esk_json_get_num(sy, "height", 20);
        p->s.pins = esk_json_get(sy, "pins");
        if (raw) {
            p->s.svg = scoped(sh->a, raw, p->id);
            esk_free(s->catalog->a, raw, len + 1);
        }
    }
    sh->npl++;
}

static void place_bounds(sheet *sh, const place *p)
{
    double x, y;
    to_sheet(p, 0, 0, &x, &y);
    grow_bounds(sh, x, y);
    to_sheet(p, p->s.w, 0, &x, &y);
    grow_bounds(sh, x, y);
    to_sheet(p, 0, p->s.h, &x, &y);
    grow_bounds(sh, x, y);
    to_sheet(p, p->s.w, p->s.h, &x, &y);
    grow_bounds(sh, x, y);
}

static const char *theme_vars(int dark)
{
    return dark ? "--symbol-stroke:#cdd6e4;--symbol-fill:#1b2230" : "--symbol-stroke:#243044;--symbol-fill:#ffffff";
}

static int write_out(const char *path, esk_strbuf *b)
{
    if (b->oom)
        return 0;
    if (!path || !strcmp(path, "-")) {
        fputs(b->data ? b->data : "", stdout);
        return 1;
    }
    if (esk_host_write_file(path, b->data ? b->data : "", b->len) != ES_OK) {
        fprintf(stderr, "embsim: cannot write %s\n", path);
        return 0;
    }
    return 1;
}

int embsim_export_sheet(es_session *s, const char *path, int dark)
{
    sheet sh;
    const esk_json *comps = esk_json_get(s->project, "components"), *boards = esk_json_get(s->project, "boards");
    const esk_json *lay = esk_json_get(s->project, "layout"), *wires = esk_json_get(lay, "wires");
    const esk_json *labels = esk_json_get(lay, "labels"), *notes = esk_json_get(lay, "notes"), *nets = esk_json_get(s->project, "nets");
    size_t i, k, nplace = esk_json_len(comps) + esk_json_len(boards);
    esk_strbuf body;
    int ok, j;
    memset(&sh, 0, sizeof sh);
    sh.a = s->a;
    sh.x0 = sh.y0 = 1e18;
    sh.x1 = sh.y1 = -1e18;
    sh.pl = (place *)esk_calloc(s->a, nplace ? nplace : 1, sizeof *sh.pl);
    if (!sh.pl)
        return 0;
    for (i = 0; i < esk_json_len(boards); i++)
        add_place(&sh, s, esk_json_at(boards, i), "board", esk_json_get_str(esk_json_at(boards, i), "board", ""));
    for (i = 0; i < esk_json_len(comps); i++)
        add_place(&sh, s, esk_json_at(comps, i), "component", esk_json_get_str(esk_json_at(comps, i), "component", ""));
    esk_strbuf_init(&body, s->a);
    /* the wires under the symbols, as the workbench layers them */
    for (i = 0; i < esk_json_len(wires); i++) {
        const esk_json *pts = esk_json_get(esk_json_at(wires, i), "points");
        esk_strbuf_puts(&body, "<polyline class=\"w\" points=\"");
        for (k = 0; k < esk_json_len(pts); k++) {
            const esk_json *px = esk_json_at(esk_json_at(pts, k), 0), *py = esk_json_at(esk_json_at(pts, k), 1);
            double x = px && px->type == ESK_JSON_NUMBER ? px->u.number.value : 0;
            double y = py && py->type == ESK_JSON_NUMBER ? py->u.number.value : 0;
            grow_bounds(&sh, x, y);
            esk_strbuf_printf(&body, "%s%g,%g", k ? " " : "", x, y);
        }
        esk_strbuf_puts(&body, "\"/>");
    }
    /* a net no wire draws: dashed from its first pin to the others */
    for (i = 0; i < esk_json_len(nets); i++) {
        const char *net = esk_json_at(nets, i)->key;
        const esk_json *pins = esk_json_at(nets, i);
        int drawn = 0;
        double fx = 0, fy = 0, x, y;
        int have = 0;
        for (k = 0; k < esk_json_len(wires) && !drawn; k++)
            drawn = esk_str_eq(esk_json_get_str(esk_json_at(wires, k), "net", ""), net);
        if (drawn)
            continue;
        for (k = 0; k < esk_json_len(pins); k++) {
            if (!pin_xy(&sh, esk_json_str(esk_json_at(pins, k)), &x, &y))
                continue;
            if (!have) {
                fx = x;
                fy = y;
                have = 1;
                continue;
            }
            esk_strbuf_printf(&body, "<line class=\"u\" x1=\"%g\" y1=\"%g\" x2=\"%g\" y2=\"%g\"/>", fx, fy, x, y);
        }
    }
    for (j = 0; j < sh.npl; j++) {
        const place *p = &sh.pl[j];
        double cx = p->s.w / 2, cy = p->s.h / 2, tx, ty;
        char val[64];
        const esk_json *inst = NULL;
        const esk_catalog_item *it = NULL;
        if (!p->s.svg)
            continue;
        place_bounds(&sh, p);
        esk_strbuf_printf(&body, "<g transform=\"translate(%g %g)", p->x, p->y);
        if (p->mirror)
            esk_strbuf_printf(&body, " translate(%g %g) scale(-1 1) translate(%g %g)", cx, cy, -cx, -cy);
        if (p->rot)
            esk_strbuf_printf(&body, " rotate(%d %g %g)", p->rot, cx, cy);
        esk_strbuf_puts(&body, "\">");
        esk_strbuf_puts(&body, p->s.svg);
        esk_strbuf_puts(&body, "</g>");
        /* its name above it, its value below */
        to_sheet(p, cx, cy, &tx, &ty);
        {
            double top = ty - ((p->rot == 90 || p->rot == 270) ? p->s.w : p->s.h) / 2;
            double bottom = ty + ((p->rot == 90 || p->rot == 270) ? p->s.w : p->s.h) / 2;
            esk_strbuf_printf(&body, "<text class=\"id\" x=\"%g\" y=\"%g\" text-anchor=\"middle\">", tx, top - 4);
            xml_text(&body, p->id);
            esk_strbuf_puts(&body, "</text>");
            for (i = 0; i < esk_json_len(comps) && !inst; i++)
                if (esk_str_eq(esk_json_get_str(esk_json_at(comps, i), "id", ""), p->id))
                    inst = esk_json_at(comps, i);
            if (inst)
                it = esk_catalog_find(s->catalog, "component", esk_json_get_str(inst, "component", ""));
            val[0] = 0;
            if (it)
                value_text(inst, it->desc, val, sizeof val);
            if (val[0]) {
                esk_strbuf_printf(&body, "<text class=\"v\" x=\"%g\" y=\"%g\" text-anchor=\"middle\">", tx, bottom + 10);
                xml_text(&body, val);
                esk_strbuf_puts(&body, "</text>");
                grow_bounds(&sh, tx, bottom + 12);
            }
            grow_bounds(&sh, tx, top - 12);
        }
    }
    for (i = 0; i < esk_json_len(labels); i++) {
        const esk_json *l = esk_json_at(labels, i);
        double x = esk_json_get_num(l, "x", 0), y = esk_json_get_num(l, "y", 0);
        grow_bounds(&sh, x, y);
        esk_strbuf_printf(&body, "<text class=\"n\" x=\"%g\" y=\"%g\">", x, y);
        xml_text(&body, esk_json_get_str(l, "net", ""));
        esk_strbuf_puts(&body, "</text>");
    }
    for (i = 0; i < esk_json_len(notes); i++) {
        const esk_json *n = esk_json_at(notes, i);
        double x = esk_json_get_num(n, "x", 0), y = esk_json_get_num(n, "y", 0);
        grow_bounds(&sh, x, y);
        esk_strbuf_printf(&body, "<text class=\"v\" x=\"%g\" y=\"%g\">", x, y);
        xml_text(&body, esk_json_get_str(n, "text", ""));
        esk_strbuf_puts(&body, "</text>");
    }
    if (sh.x0 > sh.x1) { /* nothing placed */
        sh.x0 = sh.y0 = 0;
        sh.x1 = sh.y1 = 100;
    }
    sh.x0 -= 40; /* room for labels' text, which the bounds count by their anchors */
    sh.y0 -= 20;
    sh.x1 += 40;
    sh.y1 += 20;
    esk_strbuf_init(&sh.out, s->a);
    esk_strbuf_printf(&sh.out,
                      "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"%g %g %g %g\" width=\"%g\" height=\"%g\">\n"
                      "<style>svg{%s}.w{stroke:%s;stroke-width:1.5;fill:none;stroke-linejoin:round}"
                      ".u{stroke:%s;stroke-width:1;stroke-dasharray:3 3}.id{font:600 8px sans-serif;fill:var(--symbol-stroke)}"
                      ".v{font:7px sans-serif;fill:%s}.n{font:7px sans-serif;fill:%s}</style>\n"
                      "<rect x=\"%g\" y=\"%g\" width=\"%g\" height=\"%g\" fill=\"%s\"/>\n",
                      sh.x0, sh.y0, sh.x1 - sh.x0, sh.y1 - sh.y0, sh.x1 - sh.x0, sh.y1 - sh.y0, theme_vars(dark),
                      dark ? "#5aa0ff" : "#2f6fdb", dark ? "#5aa0ff" : "#2f6fdb", dark ? "#8b98ad" : "#5b6b82",
                      dark ? "#5aa0ff" : "#2f6fdb", sh.x0, sh.y0, sh.x1 - sh.x0, sh.y1 - sh.y0, dark ? "#11161f" : "#ffffff");
    if (body.data)
        esk_strbuf_puts(&sh.out, body.data);
    esk_strbuf_puts(&sh.out, "\n</svg>\n");
    ok = !body.oom && write_out(path, &sh.out);
    esk_strbuf_free(&body);
    esk_strbuf_free(&sh.out);
    for (j = 0; j < sh.npl; j++) {
        place *p = &sh.pl[j];
        if (p->s.svg) esk_strfree(s->a, p->s.svg);
        if (p->s.bnames) esk_free(s->a, p->s.bnames, p->s.bcap * sizeof *p->s.bnames);
        if (p->s.bx) esk_free(s->a, p->s.bx, p->s.bcap * sizeof *p->s.bx);
        if (p->s.by) esk_free(s->a, p->s.by, p->s.bcap * sizeof *p->s.by);
    }
    esk_free(s->a, sh.pl, (nplace ? nplace : 1) * sizeof *sh.pl);
    return ok;
}

int embsim_export_display(es_session *s, const char *id, const char *path, int scale)
{
    esk_comp *c = esk_engine_comp(&s->eng, id);
    esk_json *o;
    esk_strbuf b;
    const char *pixels;
    int ok = 1;
    if (!c || !c->ops || !c->ops->observe) {
        fprintf(stderr, "embsim: export: no part %s with outputs\n", id);
        return 0;
    }
    o = esk_json_new_object(s->a);
    if (!o)
        return 0;
    c->ops->observe(c, o);
    esk_strbuf_init(&b, s->a);
    if (scale < 1)
        scale = 4;
    pixels = esk_json_get_str(o, "pixels", NULL);
    if (pixels) {
        /* a graphic display: its pixels on dark glass, as an SSD1306 lit in white-blue */
        int cols = (int)esk_json_get_num(c->params, "columns", esk_json_get_num(c->params, "width", 128));
        int rows = (int)esk_json_get_num(c->params, "rows", esk_json_get_num(c->params, "height", 64));
        int on = esk_json_get(o, "on") ? esk_json_get_bool(o, "on", 1) : 1;
        esk_strbuf_printf(&b,
                          "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"-2 -2 %d %d\" width=\"%d\" height=\"%d\" shape-rendering=\"crispEdges\">\n"
                          "<rect x=\"-2\" y=\"-2\" width=\"%d\" height=\"%d\" fill=\"#05070a\"/>\n",
                          cols + 4, rows + 4, (cols + 4) * scale, (rows + 4) * scale, cols + 4, rows + 4);
        if (on && *pixels)
            esk_strbuf_printf(&b, "<path d=\"%s\" fill=\"#cfe8ff\"/>\n", pixels);
        esk_strbuf_puts(&b, "</svg>\n");
    } else if (esk_json_get(o, "line1")) {
        /* a character module: its lines on the backlight, monospace cells */
        int cols = (int)esk_json_get_num(c->params, "columns", 16), rows = 0, r;
        char key[16];
        int lit = esk_json_get(o, "backlight") ? esk_json_get_bool(o, "backlight", 1) : 1;
        for (r = 1; r <= 8; r++) {
            snprintf(key, sizeof key, "line%d", r);
            if (esk_json_get(o, key))
                rows = r;
        }
        esk_strbuf_printf(&b,
                          "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 %d %d\" width=\"%d\" height=\"%d\">\n"
                          "<rect width=\"%d\" height=\"%d\" rx=\"4\" fill=\"%s\"/>\n",
                          cols * 12 + 16, rows * 20 + 12, (cols * 12 + 16) * scale / 2, (rows * 20 + 12) * scale / 2,
                          cols * 12 + 16, rows * 20 + 12, lit ? "#9bd43a" : "#5d7a2a");
        for (r = 1; r <= rows; r++) {
            snprintf(key, sizeof key, "line%d", r);
            esk_strbuf_printf(&b, "<text x=\"8\" y=\"%d\" font-family=\"monospace\" font-size=\"18\" textLength=\"%d\" "
                                  "lengthAdjust=\"spacing\" fill=\"#1d2a0c\" xml:space=\"preserve\">",
                              r * 20 + 2, cols * 12);
            xml_text(&b, esk_json_get_str(o, key, ""));
            esk_strbuf_puts(&b, "</text>\n");
        }
        esk_strbuf_puts(&b, "</svg>\n");
    } else {
        fprintf(stderr, "embsim: export: %s shows nothing this exports (a pixels output or line outputs)\n", id);
        ok = 0;
    }
    ok = ok && write_out(path, &b);
    esk_strbuf_free(&b);
    esk_json_free(s->a, o);
    return ok;
}
