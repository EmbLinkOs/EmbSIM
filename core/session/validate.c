/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* validate.c -- es_catalog_validate (SPEC-014 §3): every component and board
 * of a library is instantiated alone under each policy its fidelity declares
 * (a load error makes it `invalid`), then its fixtures (SPEC-005 §9) run as a
 * project and a scenario in a fresh session (a failed expectation makes it
 * `broken`). Schema validation is tools/validate.py's; this is the engine's
 * half of the check.
 *
 * The library under test is loaded first into a catalog of its own, so its
 * items win over same-named items of the libraries it depends on, which are
 * the libraries of the catalog passed in.
 *
 * Report: {"library": PATH, "items": [{"id", "kind", "status": "ok" |
 * "broken" | "invalid", "problems": [...], "fixtures": [{"file", "ok",
 * "failures": [...]}]}], "summary": {"ok", "broken", "invalid", "fixtures"}} */
#include "session/session_priv.h"

#include <stdlib.h>
#include <string.h>

#include "host/host.h"
#include "util/num.h"

typedef struct collect {
    esk_alloc *a;
    esk_json *problems; /* error diagnostics as "code: message" */
} collect;

static void collect_diag(void *ctx, const es_diagnostic_t *d)
{
    collect *c = (collect *)ctx;
    char buf[400];
    if (d->severity < ES_SEV_ERROR || !c->problems)
        return;
    esk_snprintf(buf, sizeof buf, "%s: %s", d->code, d->message);
    esk_json_array_push(c->a, c->problems, esk_json_new_string(c->a, buf));
}

static es_session *new_session(es_catalog *cat, const char *policy, collect *col)
{
    es_session_config_t cfg;
    es_session *s = NULL;
    memset(&cfg, 0, sizeof cfg);
    cfg.catalog = cat;
    cfg.policy = policy;
    if (es_session_create(&cfg, &s) != ES_OK)
        return NULL;
    es_session_set_diagnostic_handler(s, collect_diag, col);
    return s;
}

static es_status load_json_doc(es_session *s, const esk_json *doc, const char *base_dir)
{
    size_t len;
    char *text = esk_json_format(s->a, doc, 0, &len);
    es_status st;
    if (!text)
        return ES_ENOMEM;
    st = es_session_load_json(s, text, len, base_dir);
    esk_free(s->a, text, len + 1);
    return st;
}

/* the item alone in a project, under `policy` */
static void check_instantiates(es_catalog *cat, const esk_catalog_item *it, const char *policy, collect *col)
{
    esk_alloc *a = col->a;
    esk_json *proj = esk_json_new_object(a), *list = esk_json_new_array(a), *inst = esk_json_new_object(a);
    es_session *s;
    size_t before = esk_json_len(col->problems);
    int board = esk_str_eq(it->kind, "board");
    if (!proj || !list || !inst) {
        esk_json_free(a, proj);
        esk_json_free(a, list);
        esk_json_free(a, inst);
        return;
    }
    esk_json_set_integer(a, proj, "schema_version", 1);
    esk_json_set_string(a, proj, "kind", "project");
    esk_json_set_string(a, proj, "name", it->id);
    esk_json_set_string(a, proj, "policy", policy);
    esk_json_set_string(a, inst, "id", board ? "B1" : "DUT");
    esk_json_set_string(a, inst, board ? "board" : "component", it->id);
    esk_json_array_push(a, list, inst);
    esk_json_object_set(a, proj, board ? "boards" : "components", list);
    if (board)
        esk_json_object_set(a, proj, "components", esk_json_new_array(a));
    esk_json_object_set(a, proj, "nets", esk_json_new_object(a));
    s = new_session(cat, NULL, col);
    if (s) {
        if (load_json_doc(s, proj, it->dir) != ES_OK && esk_json_len(col->problems) == before) {
            char buf[160];
            esk_snprintf(buf, sizeof buf, "load: %s does not load under the %s policy", it->id, policy);
            esk_json_array_push(a, col->problems, esk_json_new_string(a, buf));
        }
        es_session_destroy(s);
    }
    /* say which policy each problem came from */
    {
        size_t i;
        for (i = before; i < esk_json_len(col->problems); i++) {
            esk_json *p = esk_json_at_mut(col->problems, i);
            char buf[480];
            esk_snprintf(buf, sizeof buf, "[%s] %s", policy, esk_json_str(p));
            esk_json_free(a, p);
            p = esk_json_new_string(a, buf);
            if (p)
                col->problems->u.array.items[i] = p;
        }
    }
    esk_json_free(a, proj);
}

static int is_time_key(const char *k)
{
    return esk_str_eq(k, "at") || esk_str_eq(k, "between") || esk_str_eq(k, "by");
}

/* a fixture as a project and a scenario: one probe and one assertion per
 * expectation */
static int fixture_to_run(esk_alloc *a, const esk_json *fx, esk_json **proj_out, esk_json **scen_out)
{
    const esk_json *circuit = esk_json_get(fx, "circuit"), *expect = esk_json_get(fx, "expect");
    esk_json *proj = esk_json_new_object(a), *scen = esk_json_new_object(a);
    esk_json *probes = esk_json_new_array(a), *asserts = esk_json_new_array(a);
    es_time_t until_ps = 0;
    size_t i, k;
    if (esk_parse_time(esk_json_get_str(fx, "until", "0"), &until_ps) != 0)
        until_ps = 0;
    if (!proj || !scen || !probes || !asserts) {
        esk_json_free(a, proj);
        esk_json_free(a, scen);
        esk_json_free(a, probes);
        esk_json_free(a, asserts);
        return -1;
    }
    esk_json_set_integer(a, proj, "schema_version", 1);
    esk_json_set_string(a, proj, "kind", "project");
    esk_json_set_string(a, proj, "name", esk_json_get_str(fx, "name", "fixture"));
    esk_json_set_string(a, proj, "policy", esk_json_get_str(fx, "policy", "electrical"));
    esk_json_object_set(a, proj, "components", esk_json_clone(a, esk_json_get(circuit, "components")));
    esk_json_object_set(a, proj, "nets", esk_json_clone(a, esk_json_get(circuit, "nets")));
    if (esk_json_get(circuit, "boards"))
        esk_json_object_set(a, proj, "boards", esk_json_clone(a, esk_json_get(circuit, "boards")));
    esk_json_set_integer(a, scen, "schema_version", 1);
    esk_json_set_string(a, scen, "kind", "scenario");
    esk_json_set_string(a, scen, "name", esk_json_get_str(fx, "name", "fixture"));
    if (esk_json_get(fx, "until"))
        esk_json_object_set(a, scen, "until", esk_json_clone(a, esk_json_get(fx, "until")));
    if (esk_json_get(fx, "inputs"))
        esk_json_object_set(a, scen, "inputs", esk_json_clone(a, esk_json_get(fx, "inputs")));
    for (i = 0; i < esk_json_len(expect); i++) {
        const esk_json *e = esk_json_at(expect, i);
        esk_json *probe = esk_json_new_object(a), *as = esk_json_new_object(a), *ex = esk_json_new_object(a);
        char id[16];
        int analog = esk_json_get(e, "min_v") || esk_json_get(e, "max_v");
        int digital = esk_json_get(e, "logic") || esk_json_get(e, "edges");
        if (!probe || !as || !ex) {
            esk_json_free(a, probe);
            esk_json_free(a, as);
            esk_json_free(a, ex);
            continue;
        }
        esk_snprintf(id, sizeof id, "e%u", (unsigned)i);
        esk_json_set_string(a, probe, "id", id);
        esk_json_set_string(a, probe, "target", e->key);
        if ((esk_str_has_prefix(e->key, "net:") || esk_str_has_prefix(e->key, "pin:")) && (analog || digital))
            esk_json_set_string(a, probe, "kind", analog ? "analog" : "digital");
        /* a sampled value over a long fixture (a fuse heating for half an
         * hour, a rail collapsing after it trips) is sampled at most 20000
         * times, not at the default 10 kHz: each sample is a solve */
        if (!digital && !esk_str_has_prefix(e->key, "serial:") && until_ps > 2 * ES_PS_PER_S)
            esk_json_set_number(a, probe, "rate_hz", 20000.0 / ((double)until_ps / (double)ES_PS_PER_S));
        esk_json_array_push(a, probes, probe);
        esk_json_set_string(a, as, "id", e->key);
        esk_json_set_string(a, as, "probe", id);
        for (k = 0; k < esk_json_len(e); k++) {
            const esk_json *m = esk_json_at(e, k);
            esk_json_object_set(a, is_time_key(m->key) ? as : ex, m->key, esk_json_clone(a, m));
        }
        esk_json_object_set(a, as, "expect", ex);
        esk_json_array_push(a, asserts, as);
    }
    esk_json_object_set(a, proj, "probes", probes);
    esk_json_object_set(a, scen, "assertions", asserts);
    *proj_out = proj;
    *scen_out = scen;
    return 0;
}

static esk_json *run_fixture(es_catalog *cat, esk_alloc *a, const char *path, const char *name)
{
    esk_json *res = esk_json_new_object(a), *failures = esk_json_new_array(a), *fx = NULL, *proj = NULL, *scen = NULL;
    collect col;
    char *text = NULL, *dir = NULL;
    size_t len = 0, i;
    int ok = 0;
    es_session *s = NULL;
    if (!res || !failures) {
        esk_json_free(a, res);
        esk_json_free(a, failures);
        return NULL;
    }
    col.a = a;
    col.problems = failures;
    esk_json_set_string(a, res, "file", name);
    if (esk_host_read_file(a, path, &text, &len) == ES_OK) {
        int line;
        const char *err;
        fx = esk_json_parse(a, text, len, &line, &err);
        esk_free(a, text, len + 1);
        if (!fx)
            esk_json_array_push(a, failures, esk_json_new_string(a, err));
    } else {
        esk_json_array_push(a, failures, esk_json_new_string(a, "cannot read the fixture"));
    }
    if (fx && !esk_str_eq(esk_json_get_str(fx, "kind", ""), "component_test")) {
        esk_json_free(a, fx); /* not a fixture: skip it */
        esk_json_free(a, res);
        esk_json_free(a, failures);
        return NULL;
    }
    dir = esk_host_dirname(a, path);
    if (fx && dir && fixture_to_run(a, fx, &proj, &scen) == 0 && (s = new_session(cat, NULL, &col)) != NULL) {
        if (load_json_doc(s, proj, dir) == ES_OK) {
            esk_json *report = esk_scenario_run(s, scen, dir, 0);
            const esk_json *as = esk_json_get(report, "assertions");
            const char *reason = esk_json_get_str(esk_json_get(report, "ended"), "reason", "");
            ok = report != NULL;
            if (report && !esk_str_eq(reason, "until")) {
                char buf[200];
                esk_snprintf(buf, sizeof buf, "the run ended early: %s", reason);
                esk_json_array_push(a, failures, esk_json_new_string(a, buf));
                ok = 0;
            }
            for (i = 0; i < esk_json_len(as); i++) {
                const esk_json *r = esk_json_at(as, i);
                if (!esk_json_get_bool(r, "ok", 0)) {
                    char buf[400];
                    esk_snprintf(buf, sizeof buf, "%s: %s", esk_json_get_str(r, "id", "?"), esk_json_get_str(r, "reason", "failed"));
                    esk_json_array_push(a, failures, esk_json_new_string(a, buf));
                    ok = 0;
                }
            }
            esk_json_free(s->a, report);
        } else if (esk_json_len(failures) == 0) {
            esk_json_array_push(a, failures, esk_json_new_string(a, "the fixture's circuit does not load"));
        }
        es_session_destroy(s);
    }
    if (esk_json_len(failures))
        ok = 0;
    esk_json_set_bool(a, res, "ok", ok);
    esk_json_object_set(a, res, "failures", failures);
    esk_json_free(a, fx);
    esk_json_free(a, proj);
    esk_json_free(a, scen);
    esk_strfree(a, dir);
    return res;
}

/* the policies whose fidelity tier the description declares (functional
 * when it declares none) */
static void declared_policies(const esk_json *desc, const char **out, int *n)
{
    static const char *const tiers[] = { "functional", "timed", "electrical" };
    const esk_json *fid = esk_json_get(desc, "fidelity");
    int i;
    *n = 0;
    for (i = 0; i < 3; i++) {
        const esk_json *t = esk_json_get(fid, tiers[i]);
        if (t && t->type == ESK_JSON_OBJECT)
            out[(*n)++] = tiers[i];
    }
    if (!*n)
        out[(*n)++] = "functional";
}

static esk_json *validate_item(es_catalog *cat, const esk_catalog_item *it, int *fixtures)
{
    esk_alloc *a = cat->a;
    esk_json *o = esk_json_new_object(a), *problems = esk_json_new_array(a), *fx = esk_json_new_array(a), *names = NULL;
    collect col;
    const char *pols[3];
    int npol, i, broken = 0;
    char *tdir;
    size_t k;
    if (!o || !problems || !fx) {
        esk_json_free(a, o);
        esk_json_free(a, problems);
        esk_json_free(a, fx);
        return NULL;
    }
    col.a = a;
    col.problems = problems;
    esk_json_set_string(a, o, "id", it->id);
    esk_json_set_string(a, o, "kind", it->kind);
    esk_json_set_string(a, o, "path", it->path);
    /* an importer's placeholder is unfinished work (SPEC-006 §4) */
    {
        const esk_json *cl = esk_json_get(it->desc, "components");
        size_t c;
        for (c = 0; c < esk_json_len(cl); c++) {
            const esk_json *ci = esk_json_at(cl, c);
            if (esk_json_get_bool(ci, "todo", 0)) {
                char buf[200];
                esk_snprintf(buf, sizeof buf, "catalog.todo: %s is an unfinished importer placeholder", esk_json_get_str(ci, "id", "?"));
                esk_json_array_push(a, problems, esk_json_new_string(a, buf));
            }
        }
    }
    declared_policies(it->desc, pols, &npol);
    if (esk_str_eq(it->kind, "board"))
        npol = 1; /* a board is a circuit; one load checks it */
    for (i = 0; i < npol; i++)
        check_instantiates(cat, it, pols[i], &col);
    tdir = esk_host_path_join(a, it->dir, "tests");
    if (tdir && esk_host_is_dir(tdir) && esk_host_list_dir(a, tdir, &names) == ES_OK) {
        for (k = 0; k < esk_json_len(names); k++) {
            const char *n = esk_json_str(esk_json_at(names, k));
            size_t nl = n ? strlen(n) : 0;
            char *p;
            esk_json *r;
            if (!n || nl < 6 || strcmp(n + nl - 5, ".json") != 0)
                continue;
            p = esk_host_path_join(a, tdir, n);
            r = p ? run_fixture(cat, a, p, n) : NULL;
            esk_strfree(a, p);
            if (!r)
                continue;
            (*fixtures)++;
            if (!esk_json_get_bool(r, "ok", 0))
                broken = 1;
            esk_json_array_push(a, fx, r);
        }
        esk_json_free(a, names);
    }
    esk_strfree(a, tdir);
    esk_json_set_string(a, o, "status", esk_json_len(problems) ? "invalid" : broken ? "broken" : "ok");
    esk_json_object_set(a, o, "problems", problems);
    esk_json_object_set(a, o, "fixtures", fx);
    return o;
}

es_status es_catalog_validate(es_catalog *c, const char *path, char **report_json)
{
    es_catalog *cat = NULL;
    esk_json *report, *items, *summary;
    es_status st;
    size_t i;
    int ok = 0, broken = 0, invalid = 0, fixtures = 0;
    if (report_json)
        *report_json = NULL;
    if (!path || !report_json)
        return ES_EINVAL;
    st = es_catalog_create(&cat);
    if (st != ES_OK)
        return st;
    /* the library under test first: its items win */
    st = esk_catalog_load_library(cat, path, NULL);
    if (st != ES_OK) {
        es_catalog_destroy(cat);
        return st;
    }
    for (i = 0; c && i < esk_json_len(c->libraries); i++)
        esk_catalog_load_library(cat, esk_json_get_str(esk_json_at(c->libraries, i), "path", ""), NULL);
    report = esk_json_new_object(cat->a);
    items = esk_json_new_array(cat->a);
    summary = esk_json_new_object(cat->a);
    if (!report || !items || !summary) {
        esk_json_free(cat->a, report);
        esk_json_free(cat->a, items);
        esk_json_free(cat->a, summary);
        es_catalog_destroy(cat);
        return ES_ENOMEM;
    }
    esk_json_set_string(cat->a, report, "library", path);
    {
        /* the items the library under test contributed: they were loaded first */
        const char *lib = esk_json_get_str(esk_json_at(cat->libraries, 0), "name", "");
        int n = (int)esk_json_get_int(esk_json_at(cat->libraries, 0), "items", 0);
        for (i = 0; i < cat->items.len && (int)i < n; i++) {
            const esk_catalog_item *it = cat->items.data[i];
            esk_json *r;
            if (!esk_str_eq(it->library, lib) || !(esk_str_eq(it->kind, "component") || esk_str_eq(it->kind, "board")))
                continue;
            r = validate_item(cat, it, &fixtures);
            if (!r)
                continue;
            if (esk_str_eq(esk_json_get_str(r, "status", ""), "ok"))
                ok++;
            else if (esk_str_eq(esk_json_get_str(r, "status", ""), "broken"))
                broken++;
            else
                invalid++;
            esk_json_array_push(cat->a, items, r);
        }
    }
    esk_json_object_set(cat->a, report, "items", items);
    esk_json_set_integer(cat->a, summary, "ok", ok);
    esk_json_set_integer(cat->a, summary, "broken", broken);
    esk_json_set_integer(cat->a, summary, "invalid", invalid);
    esk_json_set_integer(cat->a, summary, "fixtures", fixtures);
    esk_json_object_set(cat->a, report, "summary", summary);
    {
        size_t len;
        char *text = esk_json_format(cat->a, report, 2, &len);
        if (text) {
            *report_json = (char *)malloc(len + 1);
            if (*report_json)
                memcpy(*report_json, text, len + 1);
            esk_free(cat->a, text, len + 1);
        }
    }
    esk_json_free(cat->a, report);
    es_catalog_destroy(cat);
    return *report_json ? (broken || invalid ? ES_ESCHEMA : ES_OK) : ES_ENOMEM;
}
