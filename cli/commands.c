/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* commands.c -- the embsim subcommands (05 §2, SPEC-008 §8-§9): run,
 * validate, serve, sidecar, gdb, build, toolchains, catalog, board, models,
 * version. The CLI is a
 * client of the session protocol like every other interface. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200112L
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "embsim/embsim.h"
#include "host/host.h"
#include "protocol/protocol.h"
#include "session/session_priv.h"
#include "sdk.h"
#include "util/num.h"
#include "util/str.h"

#ifndef EMBSIM_VERSION_STRING
#define EMBSIM_VERSION_STRING "0.0.0"
#endif

static int opt_json = 0;

static void usage(void)
{
    fputs("usage: embsim <command> [options]\n"
          "  run PROJECT [--until T] [--scenario FILE] [--policy P] [--probe TARGET]... [--record FILE] [--json]\n"
          "  replay RECORDING [--allow-drift] [--json]   run a recording again and compare what it observed\n"
          "  record to-scenario RECORDING [-o FILE] [--assert probes,serial|none]\n"
          "  validate PATH...            validate projects (descriptions: tools/validate.py)\n"
          "  serve PROJECT [--port N] [--host H] [--ui DIR] [--open] [--allow-origin ORIGIN]...\n"
          "  sidecar [PROJECT]           speak the session protocol on stdin/stdout\n"
          "  gdb PROJECT [--board B] [--port N] [--host H]   a GDB server for a board's MCU\n"
          "  build PROJECT [--board B] [--toolchain T]   build a board's firmware\n"
          "  toolchains                  the toolchain profiles found on this host\n"
          "  catalog list|search QUERY|info ID|libraries [--library DIR]\n"
          "  catalog validate [LIBRARY_DIR] [--library DEPENDENCY] [--json]\n"
          "  catalog new component|board NAME [--library DIR] [--lua]\n"
          "  catalog install LIBRARY_DIR [--to LIBRARIES_DIR] [--force]\n"
          "  board list|info ID\n"
          "  models                      list the model primitives\n"
          "  version\n"
          "Times: 10ms, 1.5us, 12345ps. Exit codes: 0 ok, 1 assertion failed, 2 usage/load, 3 fatal diagnostic, 4 replay diverged.\n",
          stderr);
}

static void print_diag(void *ctx, const es_diagnostic_t *d)
{
    ES_UNUSED(ctx);
    if (opt_json)
        return;
    if (d->severity == ES_SEV_INFO)
        return;
    fprintf(stderr, "embsim: %s: %s%s%s%s\n",
            d->severity == ES_SEV_WARNING ? "warning" : d->severity == ES_SEV_ERROR ? "error" : "fatal",
            d->code, d->subject ? " (" : "", d->subject ? d->subject : "", d->subject ? ")" : "");
    fprintf(stderr, "        %s\n", d->message);
}

static es_session *open_session(const char *policy, const char *argv0, const char *library)
{
    es_session_config_t cfg;
    es_session *s = NULL;
    char *dir;
    esk_alloc a;
    memset(&cfg, 0, sizeof cfg);
    cfg.policy = policy;
    esk_alloc_init(&a, NULL);
    /* find the bundled catalog relative to the executable and tell the session through the environment */
    dir = esk_catalog_bundled_dir(&a, argv0);
    if (dir) {
#ifdef _WIN32
        {
            char buf[1024];
            esk_snprintf(buf, sizeof buf, "EMBSIM_CATALOG=%s", dir);
            _putenv(buf);
        }
#else
        setenv("EMBSIM_CATALOG", dir, 0);
#endif
        esk_strfree(&a, dir);
    }
    if (es_session_create(&cfg, &s) != ES_OK) {
        fputs("embsim: cannot create a session\n", stderr);
        return NULL;
    }
    es_session_set_diagnostic_handler(s, print_diag, NULL);
    if (library)
        es_catalog_add_library(s->catalog, library);
    return s;
}

static void print_json(esk_json *j, esk_alloc *a)
{
    size_t len;
    char *text = esk_json_format(a, j, 2, &len);
    if (text) {
        fwrite(text, 1, len, stdout);
        fputc('\n', stdout);
        esk_free(a, text, len + 1);
    }
}

static void print_human_report(es_session *s, const esk_json *report)
{
    const esk_json *snap = esk_json_get(report, "snapshot");
    const esk_json *nets = esk_json_get(snap, "nets");
    const esk_json *comps = esk_json_get(snap, "components");
    const esk_json *as = esk_json_get(report, "assertions");
    size_t i;
    char tbuf[48];
    esk_format_time(tbuf, sizeof tbuf, s->eng.sched.now);
    printf("%s  policy %s  ended at %s (%s)\n", s->name, esk_json_get_str(report, "policy", ""), tbuf,
           esk_json_get_str(esk_json_get(report, "ended"), "reason", ""));
    printf("nets:\n");
    for (i = 0; i < esk_json_len(nets); i++) {
        const esk_json *n = esk_json_at(nets, i);
        if (esk_str_eq(esk_json_get_str(n, "kind", ""), "analog"))
            printf("  %-24s %8.4f V\n", n->key, esk_json_get_num(n, "voltage", 0));
        else
            printf("  %-24s %s (%s)\n", n->key, esk_json_get_str(n, "logic", "?"), esk_json_get_str(n, "strength", ""));
    }
    printf("components:\n");
    for (i = 0; i < esk_json_len(comps); i++) {
        const esk_json *c = esk_json_at(comps, i);
        const esk_json *out = esk_json_get(c, "outputs");
        size_t k;
        printf("  %-16s %-28s", c->key, esk_json_get_str(c, "kind", ""));
        for (k = 0; k < esk_json_len(out); k++) {
            const esk_json *v = esk_json_at(out, k);
            if (v->type == ESK_JSON_NUMBER) printf(" %s=%.4g", v->key, v->u.number.value);
            else if (v->type == ESK_JSON_BOOL) printf(" %s=%s", v->key, v->u.boolean ? "true" : "false");
            else if (v->type == ESK_JSON_STRING && v->u.string.len < 40) printf(" %s=%s", v->key, v->u.string.data);
        }
        printf("\n");
    }
    if (esk_json_len(as)) {
        printf("assertions:\n");
        for (i = 0; i < esk_json_len(as); i++) {
            const esk_json *a = esk_json_at(as, i);
            printf("  %-5s %s%s%s\n", esk_json_get_bool(a, "ok", 0) ? "PASS" : "FAIL", esk_json_get_str(a, "id", ""),
                   esk_json_get_bool(a, "ok", 0) ? "" : ": ", esk_json_get_bool(a, "ok", 0) ? "" : esk_json_get_str(a, "reason", ""));
        }
    }
    {
        const esk_json *st = esk_json_get(report, "statistics");
        printf("statistics: %lld events, %lld analog steps, %.3f s wall\n", (long long)esk_json_get_int(st, "events", 0),
               (long long)esk_json_get_int(st, "analog_steps", 0), esk_json_get_num(st, "wall_time_s", 0));
    }
}

static int cmd_run(int argc, char **argv, const char *argv0)
{
    const char *project = NULL, *scenario = NULL, *policy = NULL, *record = NULL, *library = NULL;
    const char *probes[32];
    int nprobes = 0, i, rc = 0;
    es_time_t until = 50 * ES_PS_PER_MS;
    int until_given = 0;
    es_session *s;
    esk_json *report = NULL;
    for (i = 0; i < argc; i++) {
        const char *a = argv[i];
        int more = i + 1 < argc;
        if (!strcmp(a, "--until") && more) {
            if (esk_parse_time(argv[++i], &until)) { fprintf(stderr, "embsim: bad time %s\n", argv[i]); return 2; }
            until_given = 1;
        } else if (!strcmp(a, "--scenario") && more) scenario = argv[++i];
        else if (!strcmp(a, "--policy") && more) policy = argv[++i];
        else if (!strcmp(a, "--record") && more) record = argv[++i];
        else if (!strcmp(a, "--library") && more) library = argv[++i];
        else if (!strcmp(a, "--probe") && more) { if (nprobes < 32) probes[nprobes++] = argv[++i]; }
        else if (!strcmp(a, "--json")) opt_json = 1;
        else if (a[0] == '-') { fprintf(stderr, "embsim: unknown option %s\n", a); return 2; }
        else if (!project) project = a;
        else { fprintf(stderr, "embsim: one project at a time\n"); return 2; }
    }
    if (!project) { usage(); return 2; }
    s = open_session(policy, argv0, library);
    if (!s) return 2;
    if (es_session_load_file(s, project) != ES_OK) {
        fprintf(stderr, "embsim: cannot load %s\n", project);
        es_session_destroy(s);
        return 2;
    }
    for (i = 0; i < nprobes; i++) {
        if (esk_session_probe_add(s, NULL, probes[i], NULL, 0, NULL) != ES_OK)
            fprintf(stderr, "embsim: warning: probe %s not created\n", probes[i]);
    }
    if (record)
        esk_record_start(s);
    if (scenario) {
        char *text;
        size_t len;
        int line;
        const char *err;
        esk_json *sc;
        char *dir;
        if (esk_host_read_file(s->a, scenario, &text, &len) != ES_OK) {
            fprintf(stderr, "embsim: cannot read %s\n", scenario);
            es_session_destroy(s);
            return 2;
        }
        sc = esk_json_parse(s->a, text, len, &line, &err);
        esk_free(s->a, text, len + 1);
        if (!sc) {
            fprintf(stderr, "embsim: %s:%d: %s\n", scenario, line, err);
            es_session_destroy(s);
            return 2;
        }
        dir = esk_host_dirname(s->a, scenario);
        report = esk_scenario_run(s, sc, dir, until_given ? until : 0);
        esk_strfree(s->a, dir);
        esk_json_free(s->a, sc);
        if (report && !esk_json_get_bool(report, "ok", 0))
            rc = s->diag.fatal ? 3 : 1;
    } else {
        uint64_t t0 = esk_host_monotonic_ms();
        es_status st;
        if (record)
            (void)esk_session_do_reset(s); /* from reset, as a scenario and a replay run */
        st = esk_session_do_advance(s, until);
        s->wall_time_s = (double)(esk_host_monotonic_ms() - t0) / 1000.0;
        report = esk_session_report(s, s->diag.fatal ? s->diag.fatal_code : st == ES_OK ? "until" : es_strerror(st), NULL);
        if (st != ES_OK || s->diag.fatal)
            rc = 3;
    }
    if (!report) {
        fputs("embsim: out of memory\n", stderr);
        es_session_destroy(s);
        return 2;
    }
    if (record) {
        esk_json *extra = esk_json_new_array(s->a), *rec;
        char *text;
        size_t len;
        for (i = 0; extra && i < nprobes; i++)
            esk_json_array_push(s->a, extra, esk_json_new_string(s->a, probes[i]));
        rec = esk_record_document(s, project, scenario, report, extra);
        esk_json_free(s->a, extra);
        text = rec ? esk_json_format(s->a, rec, 1, &len) : NULL;
        if (!text || esk_host_write_file(record, text, len) != ES_OK)
            fprintf(stderr, "embsim: cannot write the recording %s\n", record);
        if (text)
            esk_free(s->a, text, len + 1);
        esk_json_free(s->a, rec);
    }
    if (opt_json)
        print_json(report, s->a);
    else
        print_human_report(s, report);
    esk_json_free(s->a, report);
    es_session_destroy(s);
    return rc;
}

static esk_json *read_json_file(esk_alloc *a, const char *path)
{
    char *text;
    size_t len;
    int line;
    const char *err;
    esk_json *j;
    if (esk_host_read_file(a, path, &text, &len) != ES_OK) {
        fprintf(stderr, "embsim: cannot read %s\n", path);
        return NULL;
    }
    j = esk_json_parse(a, text, len, &line, &err);
    esk_free(a, text, len + 1);
    if (!j)
        fprintf(stderr, "embsim: %s:%d: %s\n", path, line, err);
    return j;
}

/* the recording's project: as recorded, else beside the recording */
static char *recorded_project(esk_alloc *a, const char *rec_path, const char *path)
{
    char *dir, *full;
    if (esk_host_file_exists(path))
        return esk_strdup(a, path);
    dir = esk_host_dirname(a, rec_path);
    full = esk_host_path_join(a, dir ? dir : ".", path);
    esk_strfree(a, dir);
    return full;
}

static void print_obs(const char *label, const esk_json *o, esk_alloc *a)
{
    size_t len = 0;
    char *t = o ? esk_json_format(a, o, 0, &len) : NULL;
    printf("  %s %s\n", label, t ? t : "(none)");
    if (t)
        esk_free(a, t, len + 1);
}

/* replay REC: the recording's project, its inputs at their times, and its
 * observations compared in order (SPEC-012 §2) */
static int cmd_replay(int argc, char **argv, const char *argv0)
{
    const char *rec_path = NULL, *library = NULL;
    int allow_drift = 0, i, rc = 0;
    esk_alloc a;
    esk_json *rec, *drift = NULL, *cmp = NULL, *out;
    char *project;
    es_session *s;
    es_time_t end = 0;
    es_status st;
    for (i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--allow-drift")) allow_drift = 1;
        else if (!strcmp(argv[i], "--json")) opt_json = 1;
        else if (!strcmp(argv[i], "--library") && i + 1 < argc) library = argv[++i];
        else if (argv[i][0] == '-') { fprintf(stderr, "embsim: unknown option %s\n", argv[i]); return 2; }
        else if (!rec_path) rec_path = argv[i];
        else { fprintf(stderr, "embsim: one recording at a time\n"); return 2; }
    }
    if (!rec_path) { usage(); return 2; }
    esk_alloc_init(&a, NULL);
    rec = read_json_file(&a, rec_path);
    if (!rec)
        return 2;
    if (!esk_str_eq(esk_json_get_str(rec, "kind", ""), "recording") ||
        esk_json_to_time(esk_json_get(esk_json_get(rec, "end"), "at_ps"), &end)) {
        fprintf(stderr, "embsim: %s is not a recording\n", rec_path);
        esk_json_free(&a, rec);
        return 2;
    }
    project = recorded_project(&a, rec_path, esk_json_get_str(esk_json_get(rec, "project"), "path", ""));
    s = project ? open_session(esk_json_get_str(rec, "policy", NULL), argv0, library) : NULL;
    if (!s || es_session_load_file(s, project) != ES_OK) {
        fprintf(stderr, "embsim: cannot load the recording's project %s\n", project ? project : "?");
        if (s) es_session_destroy(s);
        esk_strfree(&a, project);
        esk_json_free(&a, rec);
        return 2;
    }
    st = esk_record_replay(s, rec, allow_drift, &cmp);
    drift = cmp ? esk_json_clone(s->a, esk_json_get(cmp, "drift")) : NULL;
    if (st == ES_ESTATE) {
        for (i = 0; i < (int)esk_json_len(drift); i++) {
            const esk_json *d = esk_json_at(drift, (size_t)i);
            fprintf(stderr, "embsim: %s changed since the recording (%s, now %s)\n", esk_json_get_str(d, "what", "?"),
                    esk_json_get_str(d, "recorded", ""), esk_json_get_str(d, "now", ""));
        }
        fputs("embsim: replay refuses a changed project or image; --allow-drift replays it anyway\n", stderr);
        esk_json_free(s->a, cmp);
        esk_json_free(s->a, drift);
        es_session_destroy(s);
        esk_strfree(&a, project);
        esk_json_free(&a, rec);
        return 2;
    }
    if (!cmp) {
        fputs("embsim: out of memory\n", stderr);
        rc = 2;
    } else if (!esk_json_get_bool(cmp, "ok", 0)) {
        rc = 4;
    }
    if (cmp && opt_json) {
        out = esk_json_clone(s->a, cmp);
        if (out) {
            esk_json_object_set(s->a, out, "drift", drift ? esk_json_clone(s->a, drift) : esk_json_new_array(s->a));
            print_json(out, s->a);
            esk_json_free(s->a, out);
        }
    } else if (cmp) {
        for (i = 0; i < (int)esk_json_len(drift); i++)
            printf("drift: %s changed since the recording\n", esk_json_get_str(esk_json_at(drift, (size_t)i), "what", "?"));
        if (rc == 0) {
            printf("replay: %d observations match (%s)\n", (int)esk_json_get_int(cmp, "compared", 0), rec_path);
        } else {
            const esk_json *d = esk_json_get(cmp, "divergence");
            printf("replay: diverged at observation %d of %d\n", (int)esk_json_get_int(d, "index", 0) + 1,
                   (int)esk_json_len(esk_json_get(rec, "observations")));
            print_obs("expected", esk_json_get(d, "expected"), s->a);
            print_obs("observed", esk_json_get(d, "observed"), s->a);
        }
    }
    esk_json_free(s->a, cmp);
    esk_json_free(s->a, drift);
    es_session_destroy(s);
    esk_strfree(&a, project);
    esk_json_free(&a, rec);
    return rc;
}

/* record to-scenario REC [-o FILE] [--assert probes,serial|none] */
static int cmd_record(int argc, char **argv)
{
    const char *rec_path = NULL, *outp = NULL, *assert_what = "probes,serial", *name = NULL;
    esk_alloc a;
    esk_json *rec, *sc;
    char *text;
    size_t len;
    int i, rc = 0;
    if (argc < 1 || strcmp(argv[0], "to-scenario")) { usage(); return 2; }
    for (i = 1; i < argc; i++) {
        if ((!strcmp(argv[i], "-o") || !strcmp(argv[i], "--output")) && i + 1 < argc) outp = argv[++i];
        else if (!strcmp(argv[i], "--assert") && i + 1 < argc) assert_what = argv[++i];
        else if (!strcmp(argv[i], "--name") && i + 1 < argc) name = argv[++i];
        else if (argv[i][0] == '-') { fprintf(stderr, "embsim: unknown option %s\n", argv[i]); return 2; }
        else if (!rec_path) rec_path = argv[i];
        else { fprintf(stderr, "embsim: one recording at a time\n"); return 2; }
    }
    if (!rec_path) { usage(); return 2; }
    esk_alloc_init(&a, NULL);
    rec = read_json_file(&a, rec_path);
    if (!rec)
        return 2;
    sc = esk_record_to_scenario(&a, rec, name, strstr(assert_what, "probes") != NULL, strstr(assert_what, "serial") != NULL);
    text = sc ? esk_json_format(&a, sc, 2, &len) : NULL;
    if (!text) {
        fputs("embsim: out of memory\n", stderr);
        rc = 2;
    } else if (outp) {
        if (esk_host_write_file(outp, text, len) != ES_OK) {
            fprintf(stderr, "embsim: cannot write %s\n", outp);
            rc = 2;
        }
    } else {
        fwrite(text, 1, len, stdout);
        fputc('\n', stdout);
    }
    if (text)
        esk_free(&a, text, len + 1);
    esk_json_free(&a, sc);
    esk_json_free(&a, rec);
    return rc;
}

static int cmd_validate(int argc, char **argv, const char *argv0)
{
    int i, rc = 0;
    const char *library = NULL;
    for (i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--json")) opt_json = 1;
        else if (!strcmp(argv[i], "--library") && i + 1 < argc) library = argv[++i];
    }
    for (i = 0; i < argc; i++) {
        const char *path = argv[i];
        es_session *s;
        es_status st;
        if (path[0] == '-' || (i && !strcmp(argv[i - 1], "--library")))
            continue;
        s = open_session(NULL, argv0, library);
        if (!s) return 2;
        st = es_session_load_file(s, path);
        {
            size_t errors = esk_diag_count(&s->diag, ES_SEV_ERROR) + esk_diag_count(&s->diag, ES_SEV_FATAL);
            if (opt_json) {
                esk_json *o = esk_json_new_object(s->a);
                if (o) {
                    esk_json_set_string(s->a, o, "path", path);
                    esk_json_set_bool(s->a, o, "ok", st == ES_OK && !errors);
                    esk_json_object_set(s->a, o, "diagnostics", esk_diag_to_json(&s->diag));
                    print_json(o, s->a);
                    esk_json_free(s->a, o);
                }
            } else {
                printf("%s: %s (%u error(s), %u warning(s))\n", path, st == ES_OK && !errors ? "ok" : "invalid",
                       (unsigned)errors, (unsigned)esk_diag_count(&s->diag, ES_SEV_WARNING));
            }
            if (st != ES_OK || errors)
                rc = 2;
        }
        es_session_destroy(s);
    }
    return rc;
}

static void serve_idle(void *ctx)
{
    es_session *s = (es_session *)ctx;
    esk_session_run_slice(s);
}

static int cmd_serve(int argc, char **argv, const char *argv0, int sidecar)
{
    const char *project = NULL, *host = "127.0.0.1", *ui = NULL, *policy = NULL, *library = NULL;
    const char *origins[8];
    int port = 8765, open = 0, i, n_origins = 0;
    es_session *s;
    es_status st;
    for (i = 0; i < argc; i++) {
        const char *a = argv[i];
        int more = i + 1 < argc;
        if (!strcmp(a, "--port") && more) port = atoi(argv[++i]);
        else if (!strcmp(a, "--allow-origin") && more) {
            if (n_origins == (int)ES_COUNTOF(origins)) { fprintf(stderr, "embsim: at most 8 --allow-origin\n"); return 2; }
            origins[n_origins++] = argv[++i];
        }
        else if (!strcmp(a, "--host") && more) host = argv[++i];
        else if (!strcmp(a, "--ui") && more) ui = argv[++i];
        else if (!strcmp(a, "--policy") && more) policy = argv[++i];
        else if (!strcmp(a, "--library") && more) library = argv[++i];
        else if (!strcmp(a, "--open")) open = 1;
        else if (a[0] == '-') { fprintf(stderr, "embsim: unknown option %s\n", a); return 2; }
        else project = a;
    }
    s = open_session(policy, argv0, library);
    if (!s) return 2;
    if (project && es_session_load_file(s, project) != ES_OK) {
        fprintf(stderr, "embsim: cannot load %s\n", project);
        es_session_destroy(s);
        return 2;
    }
    for (i = 0; i < n_origins; i++)
        es_serve_allow_origin(s, origins[i]);
    esk_protocol_set_idle(serve_idle, s);
    if (sidecar) {
        opt_json = 1; /* keep stderr quiet of human diagnostics; they go over the protocol */
        st = es_serve_stdio(s);
    } else {
        char *uidir = NULL;
        if (!ui) {
            /* the built workbench beside the catalog: <root>/ui/apps/workbench/dist */
            char *cat = esk_catalog_bundled_dir(s->a, argv0);
            if (cat) {
                char *root = esk_host_dirname(s->a, cat);
                if (root) {
                    uidir = esk_host_path_join(s->a, root, "ui/apps/workbench/dist");
                    if (uidir && !esk_host_is_dir(uidir)) { esk_strfree(s->a, uidir); uidir = NULL; }
                    esk_strfree(s->a, root);
                }
                esk_strfree(s->a, cat);
            }
        }
        st = es_serve_websocket(s, host, port, ui ? ui : uidir, open);
        esk_strfree(s->a, uidir);
    }
    es_session_destroy(s);
    if (st != ES_OK) {
        fprintf(stderr, "embsim: server stopped: %s\n", es_strerror(st));
        return 2;
    }
    return 0;
}

static void gdb_listening(void *ctx, int port)
{
    ES_UNUSED(ctx);
    printf("embsim: GDB server on 127.0.0.1:%d (target extended-remote :%d)\n", port, port);
    fflush(stdout);
}

static int cmd_gdb(int argc, char **argv, const char *argv0)
{
    const char *project = NULL, *host = "127.0.0.1", *board = NULL, *library = NULL;
    int port = 3333, i;
    es_session *s;
    es_status st;
    for (i = 0; i < argc; i++) {
        const char *a = argv[i];
        int more = i + 1 < argc;
        if (!strcmp(a, "--port") && more) port = atoi(argv[++i]);
        else if (!strcmp(a, "--host") && more) host = argv[++i];
        else if (!strcmp(a, "--board") && more) board = argv[++i];
        else if (!strcmp(a, "--library") && more) library = argv[++i];
        else if (a[0] == '-') { fprintf(stderr, "embsim: unknown option %s\n", a); return 2; }
        else project = a;
    }
    if (!project) { usage(); return 2; }
    s = open_session(NULL, argv0, library);
    if (!s) return 2;
    if (es_session_load_file(s, project) != ES_OK) {
        fprintf(stderr, "embsim: cannot load %s\n", project);
        es_session_destroy(s);
        return 2;
    }
    st = es_serve_gdb(s, board, host, port, gdb_listening, NULL);
    es_session_destroy(s);
    if (st != ES_OK) {
        fprintf(stderr, "embsim: gdb server: %s\n", st == ES_ENOTFOUND ? "no board with firmware" : es_strerror(st));
        return 2;
    }
    return 0;
}

static int cmd_build(int argc, char **argv, const char *argv0)
{
    const char *project = NULL, *board = NULL, *profile = NULL, *library = NULL;
    int i, ok;
    es_session *s;
    esk_json *rep = NULL;
    es_status st;
    for (i = 0; i < argc; i++) {
        const char *a = argv[i];
        int more = i + 1 < argc;
        if (!strcmp(a, "--board") && more) board = argv[++i];
        else if (!strcmp(a, "--toolchain") && more) profile = argv[++i];
        else if (!strcmp(a, "--library") && more) library = argv[++i];
        else if (!strcmp(a, "--json")) opt_json = 1;
        else if (a[0] == '-') { fprintf(stderr, "embsim: unknown option %s\n", a); return 2; }
        else project = a;
    }
    if (!project) { usage(); return 2; }
    s = open_session(NULL, argv0, library);
    if (!s) return 2;
    if (es_session_load_file(s, project) != ES_OK) {
        fprintf(stderr, "embsim: cannot load %s\n", project);
        es_session_destroy(s);
        return 2;
    }
    st = esk_toolchain_build(s, board, profile, &rep);
    if (st != ES_OK || !rep) {
        fprintf(stderr, "embsim: build: %s\n", st == ES_ENOTFOUND ? "no board with firmware, or no such toolchain profile" : es_strerror(st));
        es_session_destroy(s);
        return 2;
    }
    ok = esk_json_get_bool(rep, "ok", 0);
    if (opt_json) {
        char *t = esk_json_format(s->a, rep, 2, NULL);
        if (t) { printf("%s\n", t); esk_strfree(s->a, t); }
    } else {
        fputs(esk_json_get_str(rep, "log", ""), stdout);
        printf("%s: %s (%s)\n", ok ? "built" : "failed", esk_json_get_str(rep, "image", "?"), esk_json_get_str(rep, "profile", "?"));
    }
    esk_json_free(s->a, rep);
    es_session_destroy(s);
    return ok ? 0 : 1;
}

static int cmd_toolchains(const char *argv0)
{
    es_session *s = open_session(NULL, argv0, NULL);
    esk_json *arr = NULL;
    size_t k;
    if (!s) return 2;
    if (esk_toolchain_detect(s, &arr) == ES_OK) {
        for (k = 0; k < esk_json_len(arr); k++) {
            const esk_json *p = esk_json_at(arr, k);
            printf("%-10s %-9s %-10s %s\n", esk_json_get_str(p, "name", ""), esk_json_get_bool(p, "found", 0) ? "found" : "not found",
                   esk_json_get_str(p, "version", ""), esk_json_get_str(p, "title", ""));
        }
    }
    esk_json_free(s->a, arr);
    es_session_destroy(s);
    return 0;
}

/* the human form of an es_catalog_validate report */
static void print_validation(const char *report, esk_alloc *a)
{
    int line;
    const char *err;
    esk_json *r = esk_json_parse(a, report, strlen(report), &line, &err);
    const esk_json *items = esk_json_get(r, "items"), *sum = esk_json_get(r, "summary");
    size_t i, k, f;
    for (i = 0; i < esk_json_len(items); i++) {
        const esk_json *it = esk_json_at(items, i);
        const char *status = esk_json_get_str(it, "status", "?");
        const esk_json *fx = esk_json_get(it, "fixtures"), *probs = esk_json_get(it, "problems");
        printf("%-8s %-36s %u fixture(s)\n", esk_str_eq(status, "ok") ? "ok" : esk_str_eq(status, "broken") ? "BROKEN" : "INVALID",
               esk_json_get_str(it, "id", ""), (unsigned)esk_json_len(fx));
        for (k = 0; k < esk_json_len(probs); k++)
            printf("         %s\n", esk_json_str(esk_json_at(probs, k)));
        for (k = 0; k < esk_json_len(fx); k++) {
            const esk_json *x = esk_json_at(fx, k), *fl = esk_json_get(x, "failures");
            if (esk_json_get_bool(x, "ok", 0))
                continue;
            for (f = 0; f < esk_json_len(fl); f++)
                printf("         tests/%s: %s\n", esk_json_get_str(x, "file", ""), esk_json_str(esk_json_at(fl, f)));
        }
    }
    printf("%lld ok, %lld broken, %lld invalid; %lld fixture(s) run\n", (long long)esk_json_get_int(sum, "ok", 0),
           (long long)esk_json_get_int(sum, "broken", 0), (long long)esk_json_get_int(sum, "invalid", 0),
           (long long)esk_json_get_int(sum, "fixtures", 0));
    esk_json_free(a, r);
}

static int cmd_catalog(int argc, char **argv, const char *argv0)
{
    const char *sub = argc ? argv[0] : "list", *library = NULL, *arg = NULL;
    es_session *s;
    int i;
    if (!strcmp(sub, "new"))
        return embsim_catalog_sdk(sub, argc - 1, argv + 1, NULL);
    if (!strcmp(sub, "install")) {
        int rc;
        s = open_session(NULL, argv0, NULL);
        if (!s) return 2;
        rc = embsim_catalog_sdk(sub, argc - 1, argv + 1, s->catalog);
        es_session_destroy(s);
        return rc;
    }
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--json")) opt_json = 1;
        else if (!strcmp(argv[i], "--library") && i + 1 < argc) library = argv[++i];
        else arg = argv[i];
    }
    s = open_session(NULL, argv0, library);
    if (!s) return 2;
    if (!strcmp(sub, "list") || !strcmp(sub, "search")) {
        esk_json *items = esk_catalog_list(s->catalog, !strcmp(sub, "list") && arg ? arg : "component", NULL, !strcmp(sub, "search") ? arg : NULL);
        size_t k;
        if (opt_json) print_json(items, s->catalog->a);
        else {
            for (k = 0; k < esk_json_len(items); k++) {
                const esk_json *it = esk_json_at(items, k);
                const esk_json *tiers = esk_json_get(it, "fidelity_tiers");
                size_t t;
                printf("%-32s %-10s %-24s", esk_json_get_str(it, "id", ""), esk_json_get_str(it, "category", ""), esk_json_get_str(it, "name", ""));
                for (t = 0; t < esk_json_len(tiers); t++)
                    printf(" %s", esk_json_str(esk_json_at(tiers, t)));
                printf("\n");
            }
            printf("%u item(s)\n", (unsigned)esk_json_len(items));
        }
        esk_json_free(s->catalog->a, items);
    } else if (!strcmp(sub, "info")) {
        const esk_catalog_item *it = arg ? esk_catalog_find(s->catalog, "component", arg) : NULL;
        if (!it && arg) it = esk_catalog_find(s->catalog, "board", arg);
        if (!it && arg) it = esk_catalog_find(s->catalog, "mcu", arg);
        if (!it) {
            fprintf(stderr, "embsim: %s not found\n", arg ? arg : "(no id)");
            es_session_destroy(s);
            return 2;
        }
        print_json(it->desc, s->catalog->a);
    } else if (!strcmp(sub, "libraries")) {
        print_json(s->catalog->libraries, s->catalog->a);
    } else if (!strcmp(sub, "validate")) {
        char *report = NULL;
        char *dir = arg ? NULL : esk_catalog_bundled_dir(s->a, argv0);
        es_status st = es_catalog_validate(s->catalog, arg ? arg : dir, &report);
        int rc = st == ES_OK ? 0 : 1;
        if (!report) {
            fprintf(stderr, "embsim: cannot validate %s: %s\n", arg ? arg : dir ? dir : "(no catalog)", es_strerror(st));
            rc = 2;
        } else if (opt_json) {
            fputs(report, stdout);
            fputc('\n', stdout);
        } else {
            print_validation(report, s->a);
        }
        es_free(report);
        esk_strfree(s->a, dir);
        es_session_destroy(s);
        return rc;
    } else {
        usage();
        es_session_destroy(s);
        return 2;
    }
    es_session_destroy(s);
    return 0;
}

static int cmd_board(int argc, char **argv, const char *argv0)
{
    const char *sub = argc ? argv[0] : "list";
    es_session *s = open_session(NULL, argv0, NULL);
    if (!s) return 2;
    if (!strcmp(sub, "list")) {
        esk_json *items = esk_catalog_list(s->catalog, "board", NULL, NULL);
        size_t k;
        for (k = 0; k < esk_json_len(items); k++)
            printf("%-28s %s\n", esk_json_get_str(esk_json_at(items, k), "id", ""), esk_json_get_str(esk_json_at(items, k), "name", ""));
        esk_json_free(s->catalog->a, items);
    } else if (!strcmp(sub, "info") && argc > 1) {
        const esk_catalog_item *it = esk_catalog_find(s->catalog, "board", argv[1]);
        if (!it) { fprintf(stderr, "embsim: board %s not found\n", argv[1]); es_session_destroy(s); return 2; }
        print_json(it->desc, s->catalog->a);
    } else {
        usage();
        es_session_destroy(s);
        return 2;
    }
    es_session_destroy(s);
    return 0;
}

static int cmd_models(const char *argv0)
{
    es_session *s = open_session(NULL, argv0, NULL);
    esk_json *list;
    size_t k;
    if (!s) return 2;
    list = esk_registry_list(&s->reg);
    for (k = 0; k < esk_json_len(list); k++)
        printf("%-24s %s\n", esk_json_get_str(esk_json_at(list, k), "name", ""), esk_json_get_str(esk_json_at(list, k), "kind", ""));
    esk_json_free(s->a, list);
    es_session_destroy(s);
    return 0;
}

int embsim_cli_main(int argc, char **argv);
int embsim_cli_main(int argc, char **argv)
{
    const char *cmd;
    esk_host_stdio_init();
    if (argc < 2) { usage(); return 2; }
    cmd = argv[1];
    if (!strcmp(cmd, "run")) return cmd_run(argc - 2, argv + 2, argv[0]);
    if (!strcmp(cmd, "validate")) return cmd_validate(argc - 2, argv + 2, argv[0]);
    if (!strcmp(cmd, "replay")) return cmd_replay(argc - 2, argv + 2, argv[0]);
    if (!strcmp(cmd, "record")) return cmd_record(argc - 2, argv + 2);
    if (!strcmp(cmd, "serve")) return cmd_serve(argc - 2, argv + 2, argv[0], 0);
    if (!strcmp(cmd, "sidecar")) return cmd_serve(argc - 2, argv + 2, argv[0], 1);
    if (!strcmp(cmd, "gdb")) return cmd_gdb(argc - 2, argv + 2, argv[0]);
    if (!strcmp(cmd, "build")) return cmd_build(argc - 2, argv + 2, argv[0]);
    if (!strcmp(cmd, "toolchains")) return cmd_toolchains(argv[0]);
    if (!strcmp(cmd, "catalog")) return cmd_catalog(argc - 2, argv + 2, argv[0]);
    if (!strcmp(cmd, "board")) return cmd_board(argc - 2, argv + 2, argv[0]);
    if (!strcmp(cmd, "models")) return cmd_models(argv[0]);
    if (!strcmp(cmd, "version") || !strcmp(cmd, "--version")) { printf("embsim %s (protocol 1.0, abi %d)\n", EMBSIM_VERSION_STRING, es_abi_version()); return 0; }
    if (!strcmp(cmd, "help") || !strcmp(cmd, "--help") || !strcmp(cmd, "-h")) { usage(); return 0; }
    fprintf(stderr, "embsim: unknown command %s\n", cmd);
    usage();
    return 2;
}
