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
#include "mcu/analysis.h"
#include "mcu/mcu.h"
#include "protocol/protocol.h"
#include "session/session_priv.h"
#include "sdk.h"
#include "util/num.h"
#include "util/str.h"

#ifndef EMBSIM_VERSION_STRING
#define EMBSIM_VERSION_STRING "0.0.0"
#endif

static int opt_json = 0;
static int opt_quiet = 0; /* run: the verdict and the summary only */

/* who answers: another program called embsim may come first on PATH */
#define EMBSIM_ID "EmbSIM " EMBSIM_VERSION_STRING " (github.com/EmbLinkOs/EmbSIM)"

static void usage_to(FILE *out)
{
    fputs(EMBSIM_ID "\n"
          "usage: embsim <command> [options]\n"
          "  run PROJECT [--until T] [--scenario FILE] [--policy P] [--probe TARGET]... [--record FILE] [--json] [--quiet]\n"
          "  run --target BOARD --image FILE [options]   one board of the catalog running an image, no project,\n"
          "      plugged into a USB port (5 V on its VBUS) under the electrical policy\n"
          "      [--image [ID=]FILE] [--until-text TEXT] [--console] [--allow-no-image] [--build]\n"
          "      [--dump-regs PERIPH[,BOARD.PERIPH]...]   registers at the end, decoded\n"
          "      [--circuit-profile FILE|-]   islands, nets and parts by simulation cost (.json for JSON)\n"
          "      [--coverage FILE [--coverage-format text|lcov|json]]\n"
          "      [--profile FILE [--profile-format text|collapsed|collapsed-insns|json]]\n"
          "      [--stack FILE [--stack-format text|json]] [--faults FILE [--faults-format text|json]]\n"
          "      [--board ID]\n"
          "  replay RECORDING [--allow-drift] [--json]   run a recording again and compare what it observed\n"
          "  record to-scenario RECORDING [-o FILE] [--assert probes,serial|none]\n"
          "  validate PATH...            validate projects (descriptions: tools/validate.py)\n"
          "  serve PROJECT [--port N] [--host H] [--ui DIR] [--open] [--allow-origin ORIGIN]...\n"
          "        [--serial B1]...      a board's serial port on the host (a pseudo-terminal)\n"
          "  sidecar [PROJECT]           speak the session protocol on stdin/stdout\n"
          "  gdb PROJECT [--board B] [--port N] [--host H]   a GDB server for a board's MCU\n"
          "  build PROJECT [--board B] [--toolchain T]   build a board's firmware\n"
          "  toolchains                  the toolchain profiles found on this host\n"
          "  catalog list|search QUERY|info ID [--json]|libraries [--library DIR]\n"
          "  catalog validate [LIBRARY_DIR] [--library DEPENDENCY] [--json]\n"
          "  catalog new component|board NAME [--library DIR] [--lua]\n"
          "  catalog install LIBRARY_DIR|ARCHIVE.zip|URL [--to LIBRARIES_DIR] [--force]\n"
          "  board list|info ID [--pins]\n"
          "  mcu pins ID [PIN] [--json]    the alternate functions and ADC channels on each pin\n"
          "  mcu signal ID SIGNAL [--json] the pins and AF numbers that carry a signal (TIM2_CH3)\n"
          "  models                      list the model primitives\n"
          "  version [--json]\n"
          "  --help, --version\n"
          "Times: 10ms, 1.5us, 12345ps. Exit codes: 0 ok, 1 assertion failed, 2 usage/load, 3 fatal diagnostic, 4 replay diverged.\n",
          out);
}

static void usage(void) { usage_to(stderr); }

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

/* a JSON value on one line, for an assertion's expected and observed */
static void print_value(esk_alloc *a, const char *label, const esk_json *v)
{
    size_t len = 0;
    char *t = v ? esk_json_format(a, v, 0, &len) : NULL;
    printf("%s %s", label, t ? t : "(none)");
    if (t)
        esk_free(a, t, len + 1);
}

/* text mode: the verdict first, then (unless --quiet) the circuit's state,
 * then one line that answers "did it pass?" */
static void print_human_report(es_session *s, const esk_json *report)
{
    const esk_json *snap = esk_json_get(report, "snapshot");
    const esk_json *nets = esk_json_get(snap, "nets");
    const esk_json *comps = esk_json_get(snap, "components");
    const esk_json *as = esk_json_get(report, "assertions");
    const esk_json *st = esk_json_get(report, "statistics");
    size_t i, passed = 0;
    char tbuf[48];
    esk_format_time(tbuf, sizeof tbuf, s->eng.sched.now);
    printf("%s  policy %s  ended at %s (%s)\n", s->name, esk_json_get_str(report, "policy", ""), tbuf,
           esk_json_get_str(esk_json_get(report, "ended"), "reason", ""));
    if (esk_json_len(as)) {
        printf("assertions:\n");
        for (i = 0; i < esk_json_len(as); i++) {
            const esk_json *a = esk_json_at(as, i);
            int ok = esk_json_get_bool(a, "ok", 0);
            passed += ok ? 1u : 0u;
            printf("  %-5s %s", ok ? "PASS" : "FAIL", esk_json_get_str(a, "id", ""));
            if (!ok) {
                const char *reason = esk_json_get_str(a, "reason", "");
                const char *at = esk_json_get_str(a, "at_ps", NULL);
                if (reason[0])
                    printf(": %s", reason);
                if (esk_json_get(a, "expected") || esk_json_get(a, "observed")) {
                    printf("\n        ");
                    print_value(s->a, "expected", esk_json_get(a, "expected"));
                    print_value(s->a, ", observed", esk_json_get(a, "observed"));
                    if (at) {
                        char abuf[48];
                        esk_format_time(abuf, sizeof abuf, (es_time_t)strtoull(at, NULL, 10));
                        printf(" at %s", abuf);
                    }
                }
            }
            printf("\n");
        }
    }
    if (!opt_quiet) {
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
        printf("statistics: %lld events, %lld analog steps, %.3f s wall\n", (long long)esk_json_get_int(st, "events", 0),
               (long long)esk_json_get_int(st, "analog_steps", 0), esk_json_get_num(st, "wall_time_s", 0));
    }
    if (esk_json_len(as))
        printf("%s: %u/%u passed (%s simulated in %.1f s)\n", s->name, (unsigned)passed, (unsigned)esk_json_len(as), tbuf,
               esk_json_get_num(st, "wall_time_s", 0));
}

/* the boards' consoles followed during a run: what was printed of each, and
 * whether `until_text` has appeared */
#define FOLLOW_BOARDS 16

typedef struct follow {
    const char *until_text;
    FILE *out; /* where --console prints, or NULL */
    u64 printed[FOLLOW_BOARDS];
} follow;

static int follow_consoles(es_session *s, follow *f)
{
    size_t i, need = f->until_text ? strlen(f->until_text) : 0;
    int found = 0;
    for (i = 0; i < s->boards.len && i < FOLLOW_BOARDS; i++) {
        esk_board_inst *bi = s->boards.data[i];
        size_t len = 0, fresh, window;
        u64 total = 0;
        const char *text;
        if (!bi->mcu)
            continue;
        text = esk_mcu_console(bi->mcu, &len, &total);
        fresh = total - f->printed[i] > len ? len : (size_t)(total - f->printed[i]);
        if (fresh && f->out) {
            fwrite(text + len - fresh, 1, fresh, f->out);
            fflush(f->out);
        }
        /* the new text, and what came before it that a match may start in */
        window = fresh + need > len ? len : fresh + (need ? need - 1 : 0);
        if (need && fresh && strstr(text + len - window, f->until_text))
            found = 1;
        f->printed[i] = total;
    }
    return found;
}

/* the board's USB power port (its USB connector's VBUS), or NULL */
static const char *usb_power_port(es_session *s, const char *target)
{
    static const char *const names[] = { "USB_VBUS", "VBUS", "U5V" };
    const esk_catalog_item *it = esk_catalog_find(s->catalog, "board", target);
    const esk_json *ports = it ? esk_json_get(it->desc, "ports") : NULL;
    size_t i;
    for (i = 0; ports && i < sizeof names / sizeof names[0]; i++)
        if (esk_json_get(ports, names[i]) && esk_json_get(ports, "GND"))
            return names[i];
    return NULL;
}

/* a project of one catalog board running `image` (--target BOARD --image FILE),
 * plugged into the computer's USB port when it has one: 5 V on its VBUS. Under a
 * policy other than the electrical one the supplies are ideal and the port's source,
 * an electrical primitive, has no place: the board runs without it. */
static es_status load_target(es_session *s, const char *target, const char *image, const char *policy)
{
    esk_json *p = esk_json_new_object(s->a), *boards = esk_json_new_array(s->a), *b = esk_json_new_object(s->a);
    esk_json *fw = esk_json_new_object(s->a), *probes = esk_json_new_array(s->a), *pr = esk_json_new_object(s->a);
    const char *base = strrchr(image, '/'), *dot;
    char name[64], *text = NULL;
    size_t len = 0;
    es_status st = ES_ENOMEM;
    base = base ? base + 1 : image;
    esk_snprintf(name, sizeof name, "%s", base);
    dot = strrchr(name, '.');
    if (dot && dot != name)
        name[dot - name] = 0;
    if (p && boards && b && fw && probes && pr) {
        esk_json_set_integer(s->a, p, "schema_version", 1);
        esk_json_set_string(s->a, p, "kind", "project");
        esk_json_set_string(s->a, p, "name", name);
        esk_json_set_string(s->a, b, "id", "B1");
        esk_json_set_string(s->a, b, "board", target);
        esk_json_set_string(s->a, fw, "image", image);
        esk_json_object_set(s->a, b, "firmware", fw);
        fw = NULL;
        esk_json_array_push(s->a, boards, b);
        b = NULL;
        esk_json_object_set(s->a, p, "boards", boards);
        boards = NULL;
        esk_json_set_string(s->a, pr, "id", "console");
        esk_json_set_string(s->a, pr, "target", "serial:B1");
        esk_json_set_string(s->a, pr, "kind", "serial");
        esk_json_array_push(s->a, probes, pr);
        pr = NULL;
        esk_json_object_set(s->a, p, "probes", probes);
        probes = NULL;
        if (usb_power_port(s, target) && (!policy || !strcmp(policy, "electrical"))) {
            esk_json *comps = esk_json_new_array(s->a), *nets = esk_json_new_object(s->a);
            esk_json *host = esk_json_new_object(s->a), *gnd = esk_json_new_object(s->a);
            esk_json *params = esk_json_new_object(s->a), *vb = esk_json_new_array(s->a), *g = esk_json_new_array(s->a);
            char pin[48];
            if (comps && nets && host && gnd && params && vb && g) {
                esk_json_set_string(s->a, host, "id", "USB");
                esk_json_set_string(s->a, host, "component", "emblink/vsource");
                esk_json_set_string(s->a, params, "kind", "dc");
                esk_json_set_number(s->a, params, "dc", 5.0);
                esk_json_object_set(s->a, host, "params", params);
                params = NULL;
                esk_json_array_push(s->a, comps, host);
                host = NULL;
                esk_json_set_string(s->a, gnd, "id", "GND0");
                esk_json_set_string(s->a, gnd, "component", "emblink/gnd");
                esk_json_array_push(s->a, comps, gnd);
                gnd = NULL;
                esk_snprintf(pin, sizeof pin, "B1.%s", usb_power_port(s, target));
                esk_json_array_push(s->a, vb, esk_json_new_string(s->a, "USB.+"));
                esk_json_array_push(s->a, vb, esk_json_new_string(s->a, pin));
                esk_json_array_push(s->a, g, esk_json_new_string(s->a, "USB.-"));
                esk_json_array_push(s->a, g, esk_json_new_string(s->a, "GND0.GND"));
                esk_json_array_push(s->a, g, esk_json_new_string(s->a, "B1.GND"));
                esk_json_object_set(s->a, nets, "USB_5V", vb);
                vb = NULL;
                esk_json_object_set(s->a, nets, "USB_GND", g);
                g = NULL;
                esk_json_object_set(s->a, p, "components", comps);
                comps = NULL;
                esk_json_object_set(s->a, p, "nets", nets);
                nets = NULL;
            }
            esk_json_free(s->a, comps);
            esk_json_free(s->a, nets);
            esk_json_free(s->a, host);
            esk_json_free(s->a, gnd);
            esk_json_free(s->a, params);
            esk_json_free(s->a, vb);
            esk_json_free(s->a, g);
        }
        text = esk_json_format(s->a, p, 0, &len);
    }
    if (text) {
        st = es_session_load_json(s, text, len, ".");
        esk_free(s->a, text, len + 1);
    }
    esk_json_free(s->a, p);
    esk_json_free(s->a, boards);
    esk_json_free(s->a, b);
    esk_json_free(s->a, fw);
    esk_json_free(s->a, probes);
    esk_json_free(s->a, pr);
    return st;
}

/* --image [ID=]FILE over a project's board: its firmware replaced */
static int override_image(es_session *s, const char *arg)
{
    const char *eq = strchr(arg, '=');
    const char *path = eq ? eq + 1 : arg;
    char id[64], why[160], *data = NULL;
    size_t len = 0, i;
    esk_board_inst *target = NULL;
    int n = 0;
    id[0] = 0;
    if (eq)
        esk_snprintf(id, sizeof id, "%.*s", (int)(eq - arg), arg);
    for (i = 0; i < s->boards.len; i++) {
        esk_board_inst *bi = s->boards.data[i];
        if (!bi->mcu || (id[0] && !esk_str_eq(bi->id, id)))
            continue;
        target = bi;
        n++;
    }
    if (n != 1) {
        fprintf(stderr, "embsim: --image: %s\n", n ? "the project has several boards: give --image ID=FILE"
                                                     : "no such board with an MCU");
        return 0;
    }
    if (esk_host_read_file(s->a, path, &data, &len) != ES_OK) {
        fprintf(stderr, "embsim: cannot read %s\n", path);
        return 0;
    }
    if (esk_mcu_set_image(target->mcu, (const unsigned char *)data, len, why, sizeof why) != ES_OK) {
        fprintf(stderr, "embsim: %s: %s\n", path, why);
        esk_free(s->a, data, len + 1);
        return 0;
    }
    esk_free(s->a, data, len + 1);
    return 1;
}

/* a report format's name for analysis `which`: ESK_AN_*, -2 for JSON, -1
 * for none */
static int an_format(const char *name, unsigned which)
{
    if (!strcmp(name, "json")) return -2;
    if (!strcmp(name, "text")) return ESK_AN_TEXT;
    if (which == ESK_AN_COVERAGE) return !strcmp(name, "lcov") ? ESK_AN_LCOV : -1;
    if (which == ESK_AN_STACK || which == ESK_AN_FAULT) return -1;
    if (!strcmp(name, "collapsed")) return ESK_AN_COLLAPSED;
    if (!strcmp(name, "collapsed-insns")) return ESK_AN_COLLAPSED_INSNS;
    return -1;
}

/* the coverage, profile, stack or fault report into `path` ("-": stderr) */
static int write_analysis(es_session *s, const char *board, unsigned which, int fmt, const char *path)
{
    esk_strbuf b;
    char why[160];
    int ok = 1;
    esk_strbuf_init(&b, s->a);
    if (fmt == -2) {
        esk_json *j = esk_session_analysis_json(s, board, which, 1);
        ok = j != NULL;
        if (j) {
            (void)esk_json_write(&b, j, 1);
            (void)esk_strbuf_putc(&b, '\n');
        }
        esk_json_free(s->a, j);
        why[0] = 0;
    } else if (esk_session_analysis_text(s, board, which, fmt, &b, why, sizeof why) != ES_OK) {
        ok = 0;
    }
    if (!ok || b.oom) {
        fprintf(stderr, "embsim: %s: %s\n", esk_session_analysis_name(which), why[0] ? why : "out of memory");
        ok = 0;
    } else if (!strcmp(path, "-")) {
        fwrite(b.data, 1, b.len, stderr);
    } else if (esk_host_write_file(path, b.data, b.len) != ES_OK) {
        fprintf(stderr, "embsim: cannot write %s\n", path);
        ok = 0;
    }
    esk_strbuf_free(&b);
    return ok;
}

static void follow_scenario(es_session *s, void *ctx)
{
    (void)follow_consoles(s, (follow *)ctx);
}

/* a board whose firmware image cannot be read would run its whole time
 * doing nothing: say so and stop, unless the circuit is to run without it */
static int missing_images(es_session *s)
{
    size_t i, n = 0;
    for (i = 0; i < s->diag.len; i++) {
        const esk_diag_record *r = &s->diag.log[(s->diag.head + i) % s->diag.cap];
        if (!strcmp(r->code, "mcu.no_image")) {
            fprintf(stderr, "embsim: error: project.image_missing (%s)\n        %s\n", r->subject, r->message);
            n++;
        }
    }
    if (n)
        fputs("embsim: build the firmware (or run --build), fix boards[].firmware.image, or pass --allow-no-image to run the circuit without it\n", stderr);
    return n != 0;
}

/* run --build (issue #91): build each board whose image the load found
 * missing (mcu.no_image) or older than its sources (mcu.stale_image), with
 * the project's toolchain, as embsim build would. 1 when something was
 * built (the caller loads the project again), 0 when nothing needed it, -1
 * when a build failed. Never at load: building runs the host's compiler. */
static int build_stale(es_session *s)
{
    char ids[8][96];
    int n = 0, k, built = 0;
    size_t i;
    for (i = 0; i < s->diag.len && n < 8; i++) {
        const esk_diag_record *r = &s->diag.log[(s->diag.head + i) % s->diag.cap];
        if (!strcmp(r->code, "mcu.no_image") || !strcmp(r->code, "mcu.stale_image")) {
            for (k = 0; k < n && strcmp(ids[k], r->subject); k++)
                ;
            if (k == n) {
                snprintf(ids[n], sizeof ids[n], "%s", r->subject);
                n++;
            }
        }
    }
    for (k = 0; k < n; k++) {
        esk_json *rep = NULL;
        es_status st = esk_toolchain_build(s, ids[k], NULL, &rep);
        int ok = st == ES_OK && rep && esk_json_get_bool(rep, "ok", 0);
        if (rep)
            fputs(esk_json_get_str(rep, "log", ""), stderr);
        fprintf(stderr, EMBSIM_ID ": %s %s: %s\n", ok ? "built" : "build failed for", ids[k],
                rep ? esk_json_get_str(rep, "image", "?") : es_strerror(st));
        esk_json_free(s->a, rep);
        if (!ok)
            return -1;
        built = 1;
    }
    return built;
}

/* run --dump-regs LIST (issue #95): each named peripheral's registers as the
 * run leaves them, decoded; a name is PERIPH (every board that has one) or
 * BOARD.PERIPH. 0 when a name matched nothing */
static int dump_regs(es_session *s, const char *list, FILE *out)
{
    char name[96];
    const char *p = list;
    int ok = 1;
    while (*p) {
        size_t n = strcspn(p, ",");
        const char *dot;
        size_t i;
        int found = 0;
        esk_strbuf b;
        if (n && n < sizeof name) {
            memcpy(name, p, n);
            name[n] = 0;
            dot = strchr(name, '.');
            esk_strbuf_init(&b, s->a);
            for (i = 0; i < s->boards.len; i++) {
                esk_board_inst *bi = (esk_board_inst *)s->boards.data[i];
                if (!bi->mcu)
                    continue;
                if (dot && (strncmp(bi->id, name, (size_t)(dot - name)) || bi->id[dot - name]))
                    continue;
                found |= esk_mcu_dump_regs(bi->mcu, dot ? dot + 1 : name, &b);
            }
            if (b.data)
                fputs(b.data, out);
            esk_strbuf_free(&b);
            if (!found) {
                fprintf(stderr, EMBSIM_ID ": --dump-regs: no peripheral %s on the project's boards\n", name);
                ok = 0;
            }
        }
        p += n;
        if (*p == ',')
            p++;
    }
    return ok;
}

/* run --circuit-profile FILE (issue #87): where the circuit's time went,
 * as text, or JSON when FILE ends in .json; "-" for stderr */
static int write_circuit_profile(es_session *s, const char *path)
{
    esk_json *p = esk_session_circuit_profile(s);
    const esk_json *isl = esk_json_get(p, "islands"), *nets = esk_json_get(p, "nets"), *comps = esk_json_get(p, "components");
    size_t n = strlen(path), i;
    esk_strbuf b;
    int ok;
    if (!p)
        return 0;
    esk_strbuf_init(&b, s->a);
    if (n > 5 && !strcmp(path + n - 5, ".json")) {
        char *t = esk_json_format(s->a, p, 2, NULL);
        if (t) {
            esk_strbuf_puts(&b, t);
            esk_strbuf_puts(&b, "\n");
            esk_strfree(s->a, t);
        }
    } else {
        esk_strbuf_printf(&b, "circuit profile: %.6g s simulated in %.3g s; %lld analog steps, %.3g s solving, %lld events\n",
                          esk_json_get_num(p, "simulated_s", 0), esk_json_get_num(p, "wall_time_s", 0),
                          (long long)esk_json_get_int(p, "analog_steps", 0), esk_json_get_num(p, "solve_s", 0),
                          (long long)esk_json_get_int(p, "events", 0));
        esk_strbuf_printf(&b, "\nanalog islands, by solve time\n  %-24s %6s %6s %10s %8s %10s %8s %6s  %s\n", "island", "nodes",
                          "devs", "steps", "rejects", "newton", "solve s", "share", "busiest net");
        for (i = 0; i < esk_json_len(isl); i++) {
            const esk_json *o = esk_json_at(isl, i);
            esk_strbuf_printf(&b, "  %-24s %6lld %6lld %10lld %8lld %10lld %8.3f %5.1f%%  %s\n", esk_json_get_str(o, "name", ""),
                              (long long)esk_json_get_int(o, "nodes", 0), (long long)esk_json_get_int(o, "devices", 0),
                              (long long)esk_json_get_int(o, "steps", 0), (long long)esk_json_get_int(o, "rejections", 0),
                              (long long)esk_json_get_int(o, "newton_iterations", 0), esk_json_get_num(o, "solve_s", 0),
                              100.0 * esk_json_get_num(o, "solve_share", 0), esk_json_get_str(o, "busiest_net", "-"));
        }
        esk_strbuf_printf(&b, "\nnets, by edges\n  %-24s %10s %12s %8s\n", "net", "edges", "Hz", "island");
        for (i = 0; i < esk_json_len(nets); i++) {
            const esk_json *o = esk_json_at(nets, i);
            long long nodes = (long long)esk_json_get_int(o, "island_nodes", 0);
            char where[24];
            snprintf(where, sizeof where, nodes ? "%lld nodes" : "digital", nodes);
            esk_strbuf_printf(&b, "  %-24s %10lld %12.4g %8s\n", esk_json_get_str(o, "name", ""), (long long)esk_json_get_int(o, "edges", 0),
                              esk_json_get_num(o, "hz", 0), where);
        }
        esk_strbuf_printf(&b, "\nparts, by events\n  %-24s %-24s %10s %10s %10s\n", "part", "kind", "edges", "wakes", "inputs");
        for (i = 0; i < esk_json_len(comps); i++) {
            const esk_json *o = esk_json_at(comps, i);
            esk_strbuf_printf(&b, "  %-24s %-24s %10lld %10lld %10lld\n", esk_json_get_str(o, "id", ""), esk_json_get_str(o, "kind", ""),
                              (long long)esk_json_get_int(o, "edges", 0), (long long)esk_json_get_int(o, "wakes", 0),
                              (long long)esk_json_get_int(o, "inputs", 0));
        }
    }
    ok = !b.oom;
    if (ok && !strcmp(path, "-"))
        fputs(b.data ? b.data : "", stderr);
    else if (ok && esk_host_write_file(path, b.data ? b.data : "", b.len) != ES_OK) {
        fprintf(stderr, EMBSIM_ID ": cannot write the circuit profile %s\n", path);
        ok = 0;
    }
    esk_strbuf_free(&b);
    esk_json_free(s->a, p);
    return ok;
}

static int cmd_run(int argc, char **argv, const char *argv0)
{
    const char *project = NULL, *scenario = NULL, *policy = NULL, *record = NULL, *library = NULL;
    const char *coverage = NULL, *profile = NULL, *stack = NULL, *faults = NULL, *board = NULL, *target = NULL, *image = NULL, *until_text = NULL;
    int cov_fmt = ESK_AN_TEXT, prof_fmt = ESK_AN_TEXT, stack_fmt = ESK_AN_TEXT, fault_fmt = ESK_AN_TEXT, console = 0;
    int allow_no_image = 0, build = 0;
    const char *dump = NULL, *circuit = NULL;
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
        else if (!strcmp(a, "--coverage") && more) coverage = argv[++i];
        else if (!strcmp(a, "--profile") && more) profile = argv[++i];
        else if (!strcmp(a, "--stack") && more) stack = argv[++i];
        else if (!strcmp(a, "--faults") && more) faults = argv[++i];
        else if (!strcmp(a, "--board") && more) board = argv[++i];
        else if (!strcmp(a, "--target") && more) target = argv[++i];
        else if (!strcmp(a, "--image") && more) image = argv[++i];
        else if (!strcmp(a, "--until-text") && more) until_text = argv[++i];
        else if (!strcmp(a, "--console")) console = 1;
        else if (!strcmp(a, "--allow-no-image")) allow_no_image = 1;
        else if (!strcmp(a, "--build")) build = 1;
        else if (!strcmp(a, "--dump-regs") && more) dump = argv[++i];
        else if (!strcmp(a, "--circuit-profile") && more) circuit = argv[++i];
        else if ((!strcmp(a, "--coverage-format") || !strcmp(a, "--profile-format") || !strcmp(a, "--stack-format") ||
                  !strcmp(a, "--faults-format")) && more) {
            unsigned which = a[2] == 'c' ? ESK_AN_COVERAGE : a[2] == 's' ? ESK_AN_STACK : a[2] == 'f' ? ESK_AN_FAULT : ESK_AN_PROFILE;
            int f = an_format(argv[++i], which);
            if (f == -1) { fprintf(stderr, "embsim: %s: no format %s\n", a, argv[i]); return 2; }
            if (which == ESK_AN_COVERAGE) cov_fmt = f;
            else if (which == ESK_AN_STACK) stack_fmt = f;
            else if (which == ESK_AN_FAULT) fault_fmt = f;
            else prof_fmt = f;
        }
        else if (!strcmp(a, "--json")) opt_json = 1;
        else if (!strcmp(a, "--quiet") || !strcmp(a, "-q")) opt_quiet = 1;
        else if (a[0] == '-') { fprintf(stderr, EMBSIM_ID ": unknown option %s (embsim --help)\n", a); return 2; }
        else if (!project) project = a;
        else { fprintf(stderr, EMBSIM_ID ": one project at a time\n"); return 2; }
    }
    if (!project && !target) { usage(); return 2; }
    if (project && target) { fprintf(stderr, "embsim: a project or --target, not both\n"); return 2; }
    if (target && !image) { fprintf(stderr, "embsim: --target needs --image FILE\n"); return 2; }
    if (scenario && until_text) { fprintf(stderr, EMBSIM_ID ": --until-text runs without a scenario (a scenario has its own end)\n"); return 2; }
    s = open_session(policy, argv0, library);
    if (!s) return 2;
    if (target ? load_target(s, target, image, policy) != ES_OK : es_session_load_file(s, project) != ES_OK) {
        fprintf(stderr, "embsim: cannot load %s\n", target ? target : project);
        es_session_destroy(s);
        return 2;
    }
    if (build && !target && !image) {
        int b = build_stale(s);
        if (b < 0) {
            es_session_destroy(s);
            return 2;
        }
        if (b > 0) { /* the new images, from a fresh load */
            es_session_destroy(s);
            s = open_session(policy, argv0, library);
            if (!s) return 2;
            if (es_session_load_file(s, project) != ES_OK) {
                fprintf(stderr, "embsim: cannot load %s\n", project);
                es_session_destroy(s);
                return 2;
            }
        }
    }
    if (target)
        project = image; /* the run's name in its report and recording */
    else if (image && !override_image(s, image)) {
        es_session_destroy(s);
        return 2;
    }
    if (!image && !allow_no_image && missing_images(s)) {
        es_session_destroy(s);
        return 2;
    }
    for (i = 0; i < nprobes; i++) {
        if (esk_session_probe_add(s, NULL, probes[i], NULL, 0, NULL) != ES_OK)
            fprintf(stderr, "embsim: warning: probe %s not created\n", probes[i]);
    }
    if (coverage || profile || stack || faults) {
        char why[160];
        unsigned what = (coverage ? ESK_AN_COVERAGE : 0u) | (profile ? ESK_AN_PROFILE : 0u) | (stack ? ESK_AN_STACK : 0u) |
                        (faults ? ESK_AN_FAULT : 0u);
        if (esk_session_analyze(s, board, what, why, sizeof why) != ES_OK) {
            fprintf(stderr, "embsim: %s: %s\n", coverage ? "--coverage" : profile ? "--profile" : stack ? "--stack" : "--faults", why);
            es_session_destroy(s);
            return 2;
        }
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
        follow scen_follow;
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
        if (console) {
            /* the consoles as the scenario runs, on stderr under --json */
            memset(&scen_follow, 0, sizeof scen_follow);
            scen_follow.out = opt_json ? stderr : stdout;
            s->scenario_follow = follow_scenario;
            s->scenario_follow_ctx = &scen_follow;
        }
        report = esk_scenario_run(s, sc, dir, until_given ? until : 0);
        s->scenario_follow = NULL;
        esk_strfree(s->a, dir);
        esk_json_free(s->a, sc);
        if (report && !esk_json_get_bool(report, "ok", 0))
            rc = s->diag.fatal ? 3 : 1;
    } else {
        uint64_t t0 = esk_host_monotonic_ms();
        es_status st;
        int seen = 0;
        if (record)
            (void)esk_session_do_reset(s); /* from reset, as a scenario and a replay run */
        if (until_text || console) {
            /* a millisecond at a time, following the consoles */
            follow f;
            es_time_t at = s->eng.sched.now;
            memset(&f, 0, sizeof f);
            f.until_text = until_text && until_text[0] ? until_text : NULL;
            f.out = console ? (opt_json ? stderr : stdout) : NULL;
            st = ES_OK;
            while (st == ES_OK && !s->diag.fatal && at < until && !seen) {
                at = until - at > ES_PS_PER_MS ? at + ES_PS_PER_MS : until;
                st = esk_session_do_advance(s, at);
                seen = follow_consoles(s, &f);
            }
        } else {
            st = esk_session_do_advance(s, until);
        }
        s->wall_time_s = (double)(esk_host_monotonic_ms() - t0) / 1000.0;
        report = esk_session_report(s, s->diag.fatal ? s->diag.fatal_code : seen ? "until_text" : st == ES_OK ? "until" : es_strerror(st), NULL);
        if (st != ES_OK || s->diag.fatal)
            rc = 3;
        else if (until_text && !seen)
            rc = 1; /* the text the run waited for never came */
    }
    if (!report) {
        fputs("embsim: out of memory\n", stderr);
        es_session_destroy(s);
        return 2;
    }
    if (record) {
        esk_json *extra = esk_json_new_array(s->a), *rec;
        char *text;
        size_t len = 0;
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
    if (coverage && !write_analysis(s, board, ESK_AN_COVERAGE, cov_fmt, coverage) && !rc)
        rc = 2;
    if (profile && !write_analysis(s, board, ESK_AN_PROFILE, prof_fmt, profile) && !rc)
        rc = 2;
    if (stack && !write_analysis(s, board, ESK_AN_STACK, stack_fmt, stack) && !rc)
        rc = 2;
    if (faults && !write_analysis(s, board, ESK_AN_FAULT, fault_fmt, faults) && !rc)
        rc = 2;
    if (opt_json)
        print_json(report, s->a);
    else
        print_human_report(s, report);
    if (dump && !dump_regs(s, dump, opt_json ? stderr : stdout) && !rc)
        rc = 2;
    if (circuit && !write_circuit_profile(s, circuit) && !rc)
        rc = 2;
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
        else if (argv[i][0] == '-') { fprintf(stderr, EMBSIM_ID ": unknown option %s (embsim --help)\n", argv[i]); return 2; }
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
    size_t len = 0;
    int i, rc = 0;
    if (argc < 1 || strcmp(argv[0], "to-scenario")) { usage(); return 2; }
    for (i = 1; i < argc; i++) {
        if ((!strcmp(argv[i], "-o") || !strcmp(argv[i], "--output")) && i + 1 < argc) outp = argv[++i];
        else if (!strcmp(argv[i], "--assert") && i + 1 < argc) assert_what = argv[++i];
        else if (!strcmp(argv[i], "--name") && i + 1 < argc) name = argv[++i];
        else if (argv[i][0] == '-') { fprintf(stderr, EMBSIM_ID ": unknown option %s (embsim --help)\n", argv[i]); return 2; }
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
        /* the circuit settled at its first instant, firmware not run: what
         * the wiring itself gets wrong (contention, supplies, families) */
        if (st == ES_OK)
            st = esk_session_do_advance(s, 0);
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
    const char *origins[8], *serials[4];
    int port = 8765, open = 0, i, n_origins = 0, n_serials = 0;
    es_session *s;
    es_status st;
    for (i = 0; i < argc; i++) {
        const char *a = argv[i];
        int more = i + 1 < argc;
        if (!strcmp(a, "--port") && more) port = atoi(argv[++i]);
        else if (!strcmp(a, "--serial") && more) {
            if (n_serials == (int)ES_COUNTOF(serials)) { fprintf(stderr, "embsim: at most 4 --serial\n"); return 2; }
            serials[n_serials++] = argv[++i];
        }
        else if (!strcmp(a, "--allow-origin") && more) {
            if (n_origins == (int)ES_COUNTOF(origins)) { fprintf(stderr, "embsim: at most 8 --allow-origin\n"); return 2; }
            origins[n_origins++] = argv[++i];
        }
        else if (!strcmp(a, "--host") && more) host = argv[++i];
        else if (!strcmp(a, "--ui") && more) ui = argv[++i];
        else if (!strcmp(a, "--policy") && more) policy = argv[++i];
        else if (!strcmp(a, "--library") && more) library = argv[++i];
        else if (!strcmp(a, "--open")) open = 1;
        else if (a[0] == '-') { fprintf(stderr, EMBSIM_ID ": unknown option %s (embsim --help)\n", a); return 2; }
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
    /* a board's console (or a terminal part) as a serial port on the host (ADR-031) */
    for (i = 0; i < n_serials; i++) {
        const struct esk_host_port *hp;
        char why[160];
        esk_json *o;
        why[0] = 0;
        if (esk_session_port_open(s, serials[i], &hp, why, sizeof why) != ES_OK) {
            fprintf(stderr, "embsim: --serial %s: %s\n", serials[i], why[0] ? why : "cannot open");
            es_session_destroy(s);
            return 2;
        }
        o = esk_session_port_json(s, hp);
        fprintf(stderr, "embsim: %s's serial port is %s\n", serials[i], esk_json_get_str(o, "path", "?"));
        esk_json_free(s->a, o);
    }
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
        else if (a[0] == '-') { fprintf(stderr, EMBSIM_ID ": unknown option %s (embsim --help)\n", a); return 2; }
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
        else if (a[0] == '-') { fprintf(stderr, EMBSIM_ID ": unknown option %s (embsim --help)\n", a); return 2; }
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

/* a scalar for a table cell: a string, a number, a bool, or "" */
static void cell(char *buf, size_t cap, const esk_json *v)
{
    if (!v) buf[0] = 0;
    else if (v->type == ESK_JSON_STRING) esk_snprintf(buf, cap, "%s", v->u.string.data);
    else if (v->type == ESK_JSON_NUMBER) esk_snprintf(buf, cap, "%g", v->u.number.value);
    else if (v->type == ESK_JSON_BOOL) esk_snprintf(buf, cap, "%s", v->u.boolean ? "true" : "false");
    else if (v->type == ESK_JSON_NULL) esk_snprintf(buf, cap, "null");
    else buf[0] = 0;
}

/* catalog info: a part as one reads it to wire and test it, not its JSON */
static void print_component_summary(const esk_json *d)
{
    const esk_json *pins = esk_json_get(d, "pins"), *params = esk_json_get(d, "params");
    const esk_json *inputs = esk_json_get(d, "inputs"), *outputs = esk_json_get(d, "outputs");
    const esk_json *fid = esk_json_get(d, "fidelity");
    static const char *const tiers[] = {"functional", "timed", "electrical"};
    char a[64], b[64], c[64];
    size_t i, k;
    printf("%s  %s  %s\n", esk_json_get_str(d, "id", ""), esk_json_get_str(d, "version", ""), esk_json_get_str(d, "name", ""));
    if (esk_json_get_str(d, "category", NULL))
        printf("category: %s\n", esk_json_get_str(d, "category", ""));
    if (esk_json_get_str(d, "description", NULL))
        printf("%s\n", esk_json_get_str(d, "description", ""));
    if (esk_json_len(pins)) {
        printf("\npins:\n  %-4s %-12s %-12s %s\n", "#", "name", "kind", "");
        for (i = 0; i < esk_json_len(pins); i++) {
            const esk_json *p = esk_json_at(pins, i);
            cell(a, sizeof a, esk_json_get(p, "number"));
            printf("  %-4s %-12s %-12s %s\n", a, esk_json_get_str(p, "name", ""), esk_json_get_str(esk_json_get(p, "model"), "kind", ""),
                   esk_json_get_str(p, "doc", ""));
        }
    }
    if (esk_json_len(params)) {
        printf("\nparams:\n  %-18s %-8s %-14s %-16s %s\n", "name", "type", "default", "range", "");
        for (k = 0; k < esk_json_len(params); k++) {
            const esk_json *p = esk_json_at(params, k);
            char range[64];
            cell(a, sizeof a, esk_json_get(p, "default"));
            cell(b, sizeof b, esk_json_get(p, "min"));
            cell(c, sizeof c, esk_json_get(p, "max"));
            range[0] = 0;
            if (b[0] || c[0])
                esk_snprintf(range, sizeof range, "%s..%s", b, c);
            if (esk_json_get_str(p, "unit", NULL)) {
                size_t n = strlen(a);
                esk_snprintf(a + n, sizeof a - n, " %s", esk_json_get_str(p, "unit", ""));
            }
            printf("  %-18s %-8s %-14s %-16s %s\n", p->key, esk_json_get_str(p, "type", ""), a, range, esk_json_get_str(p, "doc", ""));
        }
    }
    if (esk_json_len(inputs)) {
        printf("\ninputs (scenario and workbench):\n");
        for (i = 0; i < esk_json_len(inputs); i++) {
            const esk_json *p = esk_json_at(inputs, i);
            cell(a, sizeof a, esk_json_get(p, "default"));
            printf("  %-18s %-8s %-10s %s\n", esk_json_get_str(p, "name", ""), esk_json_get_str(p, "type", ""), a,
                   esk_json_get_str(p, "doc", ""));
        }
    }
    if (esk_json_len(outputs)) {
        printf("\noutputs (probe as output:ID.NAME):\n");
        for (i = 0; i < esk_json_len(outputs); i++) {
            const esk_json *p = esk_json_at(outputs, i);
            printf("  %-18s %-8s %s\n", esk_json_get_str(p, "name", ""), esk_json_get_str(p, "type", ""), esk_json_get_str(p, "doc", ""));
        }
    }
    for (i = 0; i < 3; i++) {
        const esk_json *t = esk_json_get(fid, tiers[i]), *om;
        if (!t || t->type != ESK_JSON_OBJECT)
            continue;
        printf("\nfidelity, %s: %s\n", tiers[i], esk_json_get_str(t, "claims", ""));
        om = esk_json_get(t, "omissions");
        for (k = 0; k < esk_json_len(om); k++)
            printf("  not modelled: %s\n", esk_json_str(esk_json_at(om, k)) ? esk_json_str(esk_json_at(om, k)) : "");
    }
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
        if (!opt_json && esk_str_eq(it->kind, "component"))
            print_component_summary(it->desc);
        else
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

/* the MCU pin a board net reaches ("U5.PA5" in the net: "PA5"), or NULL */
static const char *net_mcu_pin(const esk_json *members, const char *mcu)
{
    size_t i, n = strlen(mcu);
    for (i = 0; i < esk_json_len(members); i++) {
        const char *m = esk_json_str(esk_json_at(members, i));
        if (m && !strncmp(m, mcu, n) && m[n] == '.')
            return m + n + 1;
    }
    return NULL;
}

/* board info --pins: each header pin, the MCU pin behind it, what the board
 * itself does with that pin, and the board's note (solder bridges) */
static void print_board_pins(const esk_json *d)
{
    const esk_json *ports = esk_json_get(d, "ports"), *nets = esk_json_get(d, "nets");
    const esk_json *leds = esk_json_get(d, "leds"), *buttons = esk_json_get(d, "buttons"), *console = esk_json_get(d, "console");
    const char *mcu = esk_json_get_str(esk_json_get(d, "mcu"), "instance", "U1");
    size_t i, k;
    printf("%s: %u header pins (MCU %s)\n", esk_json_get_str(d, "id", ""), (unsigned)esk_json_len(ports), mcu);
    printf("  %-10s %-9s %-7s %-34s %s\n", "pin", "header", "MCU", "on the board", "note");
    for (i = 0; i < esk_json_len(ports); i++) {
        const esk_json *pt = esk_json_at(ports, i);
        const char *net = esk_json_get_str(pt, "net", "");
        const esk_json *members = esk_json_get(nets, net);
        const char *pin = net_mcu_pin(members, mcu);
        char hdr[24], uses[160];
        size_t n = 0;
        esk_snprintf(hdr, sizeof hdr, "%s.%lld", esk_json_get_str(pt, "connector", "?"), (long long)esk_json_get_int(pt, "pin", 0));
        uses[0] = 0;
        for (k = 0; pin && k < esk_json_len(leds); k++)
            if (esk_str_eq(esk_json_get_str(esk_json_at(leds, k), "gpio", ""), pin))
                n += (size_t)esk_snprintf(uses + n, sizeof uses - n, "%s%s (%s LED)", n ? ", " : "",
                                          esk_json_get_str(esk_json_at(leds, k), "component", ""), esk_json_get_str(esk_json_at(leds, k), "color", ""));
        for (k = 0; pin && k < esk_json_len(buttons); k++)
            if (esk_str_eq(esk_json_get_str(esk_json_at(buttons, k), "gpio", ""), pin))
                n += (size_t)esk_snprintf(uses + n, sizeof uses - n, "%s%s (%s button)", n ? ", " : "",
                                          esk_json_get_str(esk_json_at(buttons, k), "component", ""), esk_json_get_str(esk_json_at(buttons, k), "name", ""));
        if (pin && esk_str_eq(esk_json_get_str(console, "tx", ""), pin))
            n += (size_t)esk_snprintf(uses + n, sizeof uses - n, "%sconsole %s TX", n ? ", " : "", esk_json_get_str(console, "peripheral", ""));
        if (pin && esk_str_eq(esk_json_get_str(console, "rx", ""), pin))
            n += (size_t)esk_snprintf(uses + n, sizeof uses - n, "%sconsole %s RX", n ? ", " : "", esk_json_get_str(console, "peripheral", ""));
        if (n >= sizeof uses)
            uses[sizeof uses - 1] = 0;
        printf("  %-10s %-9s %-7s %-34s %s\n", pt->key, hdr, pin ? pin : "-", uses, esk_json_get_str(pt, "doc", ""));
    }
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
        if (argc > 2 && !strcmp(argv[2], "--pins"))
            print_board_pins(it->desc);
        else
            print_json(it->desc, s->catalog->a);
    } else {
        usage();
        es_session_destroy(s);
        return 2;
    }
    es_session_destroy(s);
    return 0;
}

/* mcu pins ID [PIN] / mcu signal ID SIGNAL: the alternate-function table
 * (io.alternate) and the ADC channels (io.analog), as CubeMX's pin view */
static int cmd_mcu(int argc, char **argv, const char *argv0)
{
    const char *sub = argc > 0 ? argv[0] : "", *id = argc > 1 ? argv[1] : NULL, *what = NULL;
    const esk_catalog_item *it;
    const esk_json *alt, *analog;
    es_session *s;
    esk_json *out;
    size_t i, k, j, found = 0;
    int i_arg;
    for (i_arg = 2; i_arg < argc; i_arg++) {
        if (!strcmp(argv[i_arg], "--json")) opt_json = 1;
        else if (!what) what = argv[i_arg];
    }
    if (!id || (strcmp(sub, "pins") && strcmp(sub, "signal")) || (!strcmp(sub, "signal") && !what)) { usage(); return 2; }
    s = open_session(NULL, argv0, NULL);
    if (!s) return 2;
    it = esk_catalog_find(s->catalog, "mcu", id);
    if (!it) { fprintf(stderr, "embsim: mcu %s not found\n", id); es_session_destroy(s); return 2; }
    alt = esk_json_get(esk_json_get(it->desc, "io"), "alternate");
    analog = esk_json_get(esk_json_get(it->desc, "io"), "analog");
    if (!alt) {
        fprintf(stderr, "embsim: %s's description has no alternate-function table (io.alternate)\n", id);
        es_session_destroy(s);
        return 2;
    }
    out = esk_json_new_array(s->a);
    for (i = 0; out && i < esk_json_len(alt); i++) {
        const esk_json *pin = esk_json_at(alt, i);
        const char *adc = esk_json_get_str(analog, pin->key, NULL);
        if (!strcmp(sub, "pins") && what && !esk_str_eq(pin->key, what))
            continue;
        for (k = 0; k < esk_json_len(pin); k++) {
            const esk_json *af = esk_json_at(pin, k);
            for (j = 0; j < esk_json_len(af); j++) {
                const char *sig = esk_json_str(esk_json_at(af, j));
                esk_json *row;
                if (!sig || (!strcmp(sub, "signal") && !esk_str_eq(sig, what)))
                    continue;
                row = esk_json_new_object(s->a);
                if (!row) continue;
                esk_json_set_string(s->a, row, "pin", pin->key);
                esk_json_set_integer(s->a, row, "af", strtol(af->key, NULL, 10));
                esk_json_set_string(s->a, row, "signal", sig);
                esk_json_array_push(s->a, out, row);
                found++;
            }
        }
        if (adc && (!strcmp(sub, "pins") || esk_str_eq(adc, what))) {
            esk_json *row = esk_json_new_object(s->a);
            if (row) {
                esk_json_set_string(s->a, row, "pin", pin->key);
                esk_json_set_string(s->a, row, "analog", adc);
                esk_json_array_push(s->a, out, row);
                found++;
            }
        }
    }
    if (opt_json) {
        print_json(out, s->a);
    } else {
        const char *last = "";
        if (!found)
            printf("%s: no %s %s\n", id, !strcmp(sub, "pins") ? "pin" : "signal", what ? what : "");
        for (i = 0; i < esk_json_len(out); i++) {
            const esk_json *r = esk_json_at(out, i);
            const char *pin = esk_json_get_str(r, "pin", "");
            if (!strcmp(sub, "pins") && strcmp(pin, last))
                printf("%s\n", pin);
            last = pin;
            if (esk_json_get_str(r, "analog", NULL))
                printf("  %s%-6s %s\n", !strcmp(sub, "pins") ? "" : pin, !strcmp(sub, "pins") ? "analog" : "  ", esk_json_get_str(r, "analog", ""));
            else if (!strcmp(sub, "pins"))
                printf("  AF%-4lld %s\n", (long long)esk_json_get_int(r, "af", 0), esk_json_get_str(r, "signal", ""));
            else
                printf("  %-6s AF%lld\n", pin, (long long)esk_json_get_int(r, "af", 0));
        }
    }
    esk_json_free(s->a, out);
    es_session_destroy(s);
    return found ? 0 : 1;
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
    {
        int i;
        for (i = 2; i < argc; i++)
            if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) { usage_to(stdout); return 0; }
    }
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
    if (!strcmp(cmd, "mcu")) return cmd_mcu(argc - 2, argv + 2, argv[0]);
    if (!strcmp(cmd, "version") || !strcmp(cmd, "--version")) {
        if (argc > 2 && !strcmp(argv[2], "--json"))
            printf("{\"name\": \"EmbSIM\", \"version\": \"%s\", \"protocol\": \"1.0\", \"abi\": %d, \"url\": \"https://github.com/EmbLinkOs/EmbSIM\"}\n",
                   EMBSIM_VERSION_STRING, es_abi_version());
        else
            printf("embsim %s (protocol 1.0, abi %d)\n", EMBSIM_VERSION_STRING, es_abi_version());
        return 0;
    }
    if (!strcmp(cmd, "help") || !strcmp(cmd, "--help") || !strcmp(cmd, "-h")) { usage_to(stdout); return 0; }
    fprintf(stderr, EMBSIM_ID ": unknown command %s (embsim --help)\n", cmd);
    usage();
    return 2;
}
