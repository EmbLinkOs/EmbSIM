/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* toolchain.c -- toolchain profiles (SPEC-010): detection and builds.
 *
 * Detection runs a profile's `detect.command` and reads its version with
 * `detect.version_regex`, of the form "PREFIX([0-9.]+)SUFFIX" (the profiles'
 * only shape; PREFIX may escape a parenthesis as "\\)"). A GNU cross
 * toolchain is installed under more than one program prefix (xPack's
 * riscv-none-elf-, SiFive's and the distributions' riscv64-unknown-elf-):
 * with `detect.prefix` (the command's program begins with it) and
 * `detect.alternatives`, the first prefix whose program runs is the
 * toolchain's, and a build puts it in place of `detect.prefix` at the head
 * of every command it runs. An alternative may be {prefix, link_flags}:
 * that installation's link flags take the place of the target's (its C
 * library differs: xPack's newlib, the distributions' picolibc).
 *
 * A build expands the profile's step templates. Variables come from the
 * profile's target (cc, link_flags), the board's settings for the profile
 * (SPEC-006 §3: target, flags, link, linker_script) and the project's
 * firmware entry (sources, flags, defines, include, linker_script, image):
 * an argument that is exactly "${name}" of a list splices the list; any
 * other "${name}" is replaced by a single value. Each source compiles to
 * build/BOARD/NAME.o under the project; the link writes the image. Steps
 * run through the host layer, never through a shell; the log keeps each
 * command and its output. Nothing runs unless asked (toolchain.build,
 * `embsim build`). */
#include <stdio.h>
#include <string.h>

#include "host/host.h"
#include "mcu/mcu.h"
#include "session/session_priv.h"
#include "util/str.h"

#define TOOL_OUTPUT_CAP (256u * 1024u)
#define MAX_ARGS 512

/* ---- versions ------------------------------------------------------------------- */

/* "PREFIX([0-9.]+)..." against the output: the version, or "" */
static void version_of(const char *regex, const char *text, char *out, size_t cap)
{
    char prefix[64];
    size_t n = 0;
    const char *r = regex, *at, *hit = NULL;
    out[0] = 0;
    while (*r && strncmp(r, "([0-9.]+)", 9) != 0 && n + 1 < sizeof prefix) {
        if (*r == '\\' && r[1])
            r++;
        prefix[n++] = *r++;
    }
    prefix[n] = 0;
    if (strncmp(r, "([0-9.]+)", 9) != 0)
        return;
    for (at = text; (at = strstr(at, prefix)) != NULL; at++) {
        const char *v = at + n;
        if (*v >= '0' && *v <= '9') {
            hit = v;
            break;
        }
        if (!n)
            break;
    }
    if (!hit && !n) {
        for (at = text; *at && !(*at >= '0' && *at <= '9'); at++)
            ;
        hit = *at ? at : NULL;
    }
    for (n = 0; hit && ((hit[n] >= '0' && hit[n] <= '9') || hit[n] == '.') && n + 1 < cap; n++)
        out[n] = hit[n];
    out[n] = 0;
}

/* the detect command's program under `prefix` instead of `detect.prefix`
 * (NULL: as written), into buf */
static const char *program_as(const esk_json *detect, const char *prefix, char *buf, size_t cap)
{
    const char *prog = esk_json_str(esk_json_at(esk_json_get(detect, "command"), 0));
    const char *own = esk_json_get_str(detect, "prefix", "");
    size_t n = strlen(own);
    if (!prog || !prefix || !n || strncmp(prog, own, n) != 0)
        return prog;
    esk_snprintf(buf, cap, "%s%s", prefix, prog + n);
    return buf;
}

/* runs the detect command with its program under `prefix`: ES_OK with the
 * exit code and the output, or ES_ENOTFOUND */
static es_status probe(es_session *s, const esk_json *detect, const char *prefix, int *code, char **outp,
                       size_t *len)
{
    const esk_json *cmd = esk_json_get(detect, "command");
    const char *argv[16];
    char buf[128];
    size_t i, n = esk_json_len(cmd);
    if (!n || n >= ES_COUNTOF(argv))
        return ES_ENOTFOUND;
    for (i = 0; i < n; i++)
        argv[i] = esk_json_str(esk_json_at(cmd, i));
    argv[0] = program_as(detect, prefix, buf, sizeof buf);
    argv[n] = NULL;
    return esk_host_run(s->a, argv, NULL, 16384, code, outp, len);
}

static const char *alt_prefix(const esk_json *alt)
{
    return esk_json_str(alt) ? esk_json_str(alt) : esk_json_get_str(alt, "prefix", NULL);
}

/* the alternative the toolchain is installed under: NULL for the detect
 * command's own prefix (also when none runs), else the first alternative
 * that does */
static const esk_json *installed_alt(es_session *s, const esk_json *detect)
{
    const esk_json *alts = esk_json_get(detect, "alternatives");
    size_t i, len = 0;
    char *outp = NULL;
    int code = -1;
    es_status st = probe(s, detect, NULL, &code, &outp, &len);
    if (outp)
        esk_free(s->a, outp, len + 1);
    if (st == ES_OK && code == 0)
        return NULL;
    for (i = 0; esk_json_get(detect, "prefix") && i < esk_json_len(alts); i++) {
        const esk_json *alt = esk_json_at(alts, i);
        outp = NULL;
        code = -1;
        st = alt_prefix(alt) ? probe(s, detect, alt_prefix(alt), &code, &outp, &len) : ES_ENOTFOUND;
        if (outp)
            esk_free(s->a, outp, len + 1);
        if (st == ES_OK && code == 0)
            return alt;
    }
    return NULL;
}

static esk_json *detect_one(es_session *s, const esk_catalog_item *it)
{
    const esk_json *detect = esk_json_get(it->desc, "detect");
    const char *prefix = alt_prefix(installed_alt(s, detect)), *prog;
    size_t len = 0;
    char *outp = NULL, version[32], buf[128];
    int code = -1;
    es_status st;
    esk_json *o = esk_json_new_object(s->a);
    if (!o)
        return NULL;
    esk_json_set_string(s->a, o, "name", it->id);
    esk_json_set_string(s->a, o, "title", esk_json_get_str(it->desc, "name", it->id));
    st = probe(s, detect, prefix, &code, &outp, &len);
    esk_json_set_bool(s->a, o, "found", st == ES_OK && code == 0);
    prog = program_as(detect, prefix, buf, sizeof buf);
    if (st == ES_OK && code == 0 && prog)
        esk_json_set_string(s->a, o, "program", prog);
    if (st == ES_OK) {
        version_of(esk_json_get_str(detect, "version_regex", ""), outp, version, sizeof version);
        if (version[0])
            esk_json_set_string(s->a, o, "version", version);
        esk_free(s->a, outp, len + 1);
    }
    return o;
}

es_status esk_toolchain_detect(es_session *s, esk_json **out)
{
    esk_json *arr = esk_json_new_array(s->a);
    size_t i;
    if (!arr)
        return ES_ENOMEM;
    for (i = 0; s->catalog && i < s->catalog->items.len; i++) {
        const esk_catalog_item *it = s->catalog->items.data[i];
        if (esk_str_eq(it->kind, "toolchain"))
            esk_json_array_push(s->a, arr, detect_one(s, it));
    }
    *out = arr;
    return ES_OK;
}

/* ---- builds ---------------------------------------------------------------------- */

typedef struct build {
    es_session *s;
    const esk_json *profile, *ptarget, *board_tc, *fw;
    const char *prefix_from, *prefix_to; /* the program prefix the toolchain is installed under */
    const esk_json *alt_link_flags;      /* that installation's link flags, for the target's */
    char *dir;          /* the project's directory */
    char *image;        /* the image's path */
    char *map;
    char *hex;          /* the image as Intel HEX: its path with .hex for its extension */
    char *linker_script;
    char *build_dir;
    const char *source, *object;
    ESK_VEC(char *) objects;
    esk_strbuf log;
    ESK_VEC(char *) owned; /* strings the argument vector points into */
} build;

static char *own(build *b, char *p)
{
    if (p && esk_vec_push(b->s->a, &b->owned, p)) {
        esk_strfree(b->s->a, p);
        return NULL;
    }
    return p;
}

/* "-I" + each of the project's include directories, "-D" + each define */
static int push_prefixed(build *b, const char **argv, size_t *n, const esk_json *list, const char *flag, int path)
{
    size_t i;
    for (i = 0; i < esk_json_len(list) && *n + 1 < MAX_ARGS; i++) {
        const char *v = esk_json_str(esk_json_at(list, i));
        char *p, *full = NULL;
        size_t l;
        if (!v)
            continue;
        if (path)
            full = esk_host_path_join(b->s->a, b->dir, v);
        l = strlen(flag) + strlen(full ? full : v) + 1;
        p = (char *)esk_malloc(b->s->a, l);
        if (!p) {
            esk_strfree(b->s->a, full);
            return -1;
        }
        esk_snprintf(p, l, "%s%s", flag, full ? full : v);
        esk_strfree(b->s->a, full);
        /* sized exactly as esk_strdup does, so esk_strfree releases it */
        {
            char *q = esk_strdup(b->s->a, p);
            esk_free(b->s->a, p, l);
            p = q;
        }
        if (!own(b, p))
            return -1;
        argv[(*n)++] = p;
    }
    return 0;
}

static int push_list(const char **argv, size_t *n, const esk_json *list)
{
    size_t i;
    for (i = 0; i < esk_json_len(list) && *n + 1 < MAX_ARGS; i++) {
        const char *v = esk_json_str(esk_json_at(list, i));
        if (v)
            argv[(*n)++] = v;
    }
    return 0;
}

/* the single value of a variable, or NULL */
static const char *scalar(build *b, const char *name)
{
    if (!strcmp(name, "source")) return b->source;
    if (!strcmp(name, "object")) return b->object;
    if (!strcmp(name, "image")) return b->image;
    if (!strcmp(name, "map")) return b->map;
    if (!strcmp(name, "hex")) return b->hex;
    if (!strcmp(name, "linker_script")) return b->linker_script;
    if (!strcmp(name, "target")) return esk_json_get_str(b->board_tc, "target", NULL);
    if (!strcmp(name, "cpu")) return esk_json_get_str(b->board_tc, "cpu", NULL);
    return NULL;
}

/* one argument: a "${name}" inside it replaced by the variable's single
 * value; an absent linker script drops the argument */
static es_status push_one(build *b, const char *t, const char **argv, size_t *n)
{
    const char *open, *close;
    if ((open = strstr(t, "${")) != NULL && (close = strchr(open, '}')) != NULL) {
        char name[48], *p, *q;
        const char *v;
        size_t nl = (size_t)(close - open - 2), l;
        if (nl >= sizeof name)
            return ES_EINVAL;
        memcpy(name, open + 2, nl);
        name[nl] = 0;
        v = scalar(b, name);
        if (!v) {
            if (!strcmp(name, "linker_script"))
                return ES_OK; /* none given: the toolchain's default */
            esk_strbuf_printf(&b->log, "no value for ${%s} in this build\n", name);
            return ES_EINVAL;
        }
        l = (size_t)(open - t) + strlen(v) + strlen(close + 1) + 1;
        p = (char *)esk_malloc(b->s->a, l);
        if (!p)
            return ES_ENOMEM;
        esk_snprintf(p, l, "%.*s%s%s", (int)(open - t), t, v, close + 1);
        q = esk_strdup(b->s->a, p);
        esk_free(b->s->a, p, l);
        if (!own(b, q))
            return ES_ENOMEM;
        argv[(*n)++] = q;
        return ES_OK;
    }
    argv[(*n)++] = t;
    return ES_OK;
}

/* one template into an argument vector */
static es_status expand(build *b, const esk_json *tmpl, const char **argv, size_t *n)
{
    size_t i;
    *n = 0;
    for (i = 0; i < esk_json_len(tmpl) && *n + 1 < MAX_ARGS; i++) {
        const char *t = esk_json_str(esk_json_at(tmpl, i));
        es_status st;
        if (!t)
            continue;
        if (!strcmp(t, "${cc}")) {
            push_list(argv, n, esk_json_get(b->ptarget, "cc"));
        } else if (!strcmp(t, "${objcopy}")) {
            push_list(argv, n, esk_json_get(b->ptarget, "objcopy"));
        } else if (!strcmp(t, "${cflags}") || !strcmp(t, "${flags}")) {
            push_list(argv, n, esk_json_get(b->board_tc, "flags"));
            push_list(argv, n, esk_json_get(b->fw, "flags"));
        } else if (!strcmp(t, "${link_flags}")) {
            /* the profile's, then the project's (they may name the build's
             * variables: ${map}) */
            const esk_json *lists[2];
            size_t k, l;
            lists[0] = b->alt_link_flags ? b->alt_link_flags : esk_json_get(b->ptarget, "link_flags");
            lists[1] = esk_json_get(b->fw, "link_flags");
            for (l = 0; l < 2; l++)
                for (k = 0; k < esk_json_len(lists[l]) && *n + 1 < MAX_ARGS; k++) {
                    const char *f = esk_json_str(esk_json_at(lists[l], k));
                    if (f && (st = push_one(b, f, argv, n)) != ES_OK)
                        return st;
                }
        } else if (!strcmp(t, "${link}")) {
            push_list(argv, n, esk_json_get(b->board_tc, "link"));
        } else if (!strcmp(t, "${defines}")) {
            /* the target's (written as flags: -DF_CPU=16000000UL), then the project's */
            push_list(argv, n, esk_json_get(b->ptarget, "defines"));
            if (push_prefixed(b, argv, n, esk_json_get(b->fw, "defines"), "-D", 0))
                return ES_ENOMEM;
        } else if (!strcmp(t, "${include}")) {
            if (push_prefixed(b, argv, n, esk_json_get(b->fw, "include"), "-I", 1))
                return ES_ENOMEM;
        } else if (!strcmp(t, "${objects}")) {
            size_t k;
            for (k = 0; k < b->objects.len && *n + 1 < MAX_ARGS; k++)
                argv[(*n)++] = b->objects.data[k];
        } else if ((st = push_one(b, t, argv, n)) != ES_OK) {
            return st;
        }
    }
    argv[*n] = NULL;
    return *n ? ES_OK : ES_EINVAL;
}

/* run one step: 0 when it succeeded */
static int run_step(build *b, const esk_json *tmpl, esk_json *steps)
{
    const char *argv[MAX_ARGS + 1];
    size_t n = 0, i, len = 0;
    char *outp = NULL;
    int code = -1;
    es_status st = expand(b, tmpl, argv, &n);
    esk_json *rec = esk_json_new_object(b->s->a), *av = esk_json_new_array(b->s->a);
    if (st == ES_OK && n && b->prefix_to && argv[0] &&
        strncmp(argv[0], b->prefix_from, strlen(b->prefix_from)) == 0) {
        size_t l = strlen(b->prefix_to) + strlen(argv[0]) + 1;
        char *p = (char *)esk_malloc(b->s->a, l);
        if (p) {
            esk_snprintf(p, l, "%s%s", b->prefix_to, argv[0] + strlen(b->prefix_from));
            argv[0] = own(b, p) ? p : argv[0];
        }
    }
    if (!rec || !av) {
        esk_json_free(b->s->a, rec);
        esk_json_free(b->s->a, av);
        return -1;
    }
    for (i = 0; i < n; i++)
        esk_json_array_push(b->s->a, av, esk_json_new_string(b->s->a, argv[i]));
    esk_json_object_set(b->s->a, rec, "argv", av);
    esk_strbuf_puts(&b->log, "$");
    for (i = 0; i < n; i++) {
        esk_strbuf_putc(&b->log, ' ');
        esk_strbuf_puts(&b->log, argv[i]);
    }
    esk_strbuf_putc(&b->log, '\n');
    if (st == ES_OK)
        st = esk_host_run(b->s->a, argv, b->dir, TOOL_OUTPUT_CAP, &code, &outp, &len);
    if (st == ES_ENOTFOUND)
        esk_strbuf_printf(&b->log, "%s: not found (is the toolchain installed and on PATH?)\n", argv[0] ? argv[0] : "?");
    if (outp) {
        esk_strbuf_append(&b->log, outp, len);
        esk_json_set_string(b->s->a, rec, "output", outp);
        esk_free(b->s->a, outp, len + 1);
    }
    esk_json_set_integer(b->s->a, rec, "exit", code);
    esk_json_array_push(b->s->a, steps, rec);
    return st == ES_OK && code == 0 ? 0 : -1;
}

static char *object_for(build *b, const char *source)
{
    const char *base = source, *p;
    char name[256];
    size_t k;
    for (p = source; *p; p++)
        if (*p == '/' || *p == '\\')
            base = p + 1;
    esk_snprintf(name, sizeof name, "%s.o", base);
    for (k = 0; name[k]; k++)
        if (name[k] == ' ')
            name[k] = '_';
    return esk_host_path_join(b->s->a, b->build_dir, name);
}

es_status esk_toolchain_build(es_session *s, const char *board_id, const char *profile_id, esk_json **out)
{
    build b;
    esk_board_inst *bi = NULL;
    const esk_catalog_item *tc;
    const esk_json *sources, *steps;
    esk_json *report, *steplog;
    size_t i;
    int ok = 1;
    es_status st = ES_OK;
    char rel[160];
    memset(&b, 0, sizeof b);
    b.s = s;
    *out = NULL;
    if (!s->loaded)
        return ES_ESTATE;
    for (i = 0; i < s->boards.len; i++)
        if (board_id ? esk_str_eq(s->boards.data[i]->id, board_id) : s->boards.data[i]->firmware != NULL) {
            bi = s->boards.data[i];
            break;
        }
    if (!bi || !bi->firmware)
        return ES_ENOTFOUND;
    b.fw = bi->firmware;
    if (!profile_id)
        profile_id = esk_json_get_str(b.fw, "toolchain", NULL);
    tc = profile_id ? esk_catalog_find(s->catalog, "toolchain", profile_id) : NULL;
    if (!tc)
        return ES_ENOTFOUND;
    b.profile = tc->desc;
    if (esk_json_get(esk_json_get(b.profile, "detect"), "alternatives")) {
        b.prefix_from = esk_json_get_str(esk_json_get(b.profile, "detect"), "prefix", NULL);
        const esk_json *alt = b.prefix_from ? installed_alt(s, esk_json_get(b.profile, "detect")) : NULL;
        b.prefix_to = alt_prefix(alt);
        b.alt_link_flags = alt ? esk_json_get(alt, "link_flags") : NULL;
    }
    b.board_tc = esk_json_get(esk_json_get(bi->item->desc, "toolchains"), profile_id);
    /* the board names the profile's target: by triple (clang, EmbCC) or by
     * CPU (the GNU and XC profiles key their targets so) */
    b.ptarget = esk_json_get(esk_json_get(b.profile, "targets"),
                             esk_json_get_str(b.board_tc, "target", esk_json_get_str(b.board_tc, "cpu", "")));
    if (!b.ptarget && esk_json_len(esk_json_get(b.profile, "targets")) == 1)
        b.ptarget = esk_json_at(esk_json_get(b.profile, "targets"), 0);
    report = esk_json_new_object(s->a);
    steplog = esk_json_new_array(s->a);
    esk_strbuf_init(&b.log, s->a);
    b.dir = esk_strdup(s->a, s->base_dir ? s->base_dir : ".");
    esk_snprintf(rel, sizeof rel, "build/%s", bi->id);
    b.build_dir = b.dir ? esk_host_path_join(s->a, b.dir, rel) : NULL;
    b.image = b.dir ? esk_host_path_join(s->a, b.dir, esk_json_get_str(b.fw, "image", "firmware.elf")) : NULL;
    if (b.image) {
        size_t l = strlen(b.image) + 5;
        char *m = (char *)esk_malloc(s->a, l);
        if (m) {
            esk_snprintf(m, l, "%s.map", b.image);
            b.map = esk_strdup(s->a, m);
            esk_free(s->a, m, l);
        }
        {
            const char *dot = strrchr(b.image, '.'), *sep = strrchr(b.image, '/'), *bs = strrchr(b.image, '\\');
            if (bs && (!sep || bs > sep))
                sep = bs;
            size_t stem = dot && (!sep || dot > sep) ? (size_t)(dot - b.image) : strlen(b.image);
            m = (char *)esk_malloc(s->a, stem + 5);
            if (m) {
                memcpy(m, b.image, stem);
                memcpy(m + stem, ".hex", 5);
                b.hex = esk_strdup(s->a, m);
                esk_free(s->a, m, stem + 5);
            }
        }
    }
    {
        const char *ls = esk_json_get_str(b.fw, "linker_script", NULL);
        if (ls)
            b.linker_script = esk_host_path_join(s->a, b.dir, ls);
        else if ((ls = esk_json_get_str(b.board_tc, "linker_script", NULL)) != NULL)
            b.linker_script = esk_host_path_join(s->a, bi->item->dir, ls);
    }
    if (!report || !steplog || !b.dir || !b.build_dir || !b.image || !b.map || !b.hex) {
        st = ES_ENOMEM;
        goto done;
    }
    if (!b.ptarget) {
        esk_strbuf_printf(&b.log, "the %s profile has no target for board %s\n", profile_id, bi->board_id);
        ok = 0;
    }
    if (ok && esk_host_mkdir_p(b.build_dir) != ES_OK) {
        esk_strbuf_printf(&b.log, "cannot create %s\n", b.build_dir);
        ok = 0;
    }
    steps = esk_json_get(b.profile, "steps");
    sources = esk_json_get(b.fw, "sources");
    for (i = 0; ok && i < esk_json_len(sources); i++) {
        const char *src = esk_json_str(esk_json_at(sources, i));
        char *full = src ? esk_host_path_join(s->a, b.dir, src) : NULL, *obj = src ? object_for(&b, src) : NULL;
        if (!full || !obj || esk_vec_push(s->a, &b.objects, obj)) {
            esk_strfree(s->a, full);
            esk_strfree(s->a, obj);
            st = ES_ENOMEM;
            goto done;
        }
        own(&b, full);
        b.source = full;
        b.object = obj;
        if (run_step(&b, esk_json_get(steps, "compile"), steplog) != 0)
            ok = 0;
    }
    if (ok && !esk_json_len(sources)) {
        esk_strbuf_puts(&b.log, "the firmware entry names no sources\n");
        ok = 0;
    }
    if (ok && run_step(&b, esk_json_get(steps, "link"), steplog) != 0)
        ok = 0;
    if (ok && esk_json_get(steps, "convert") && esk_json_get(steps, "convert")->type == ESK_JSON_ARRAY)
        ok = run_step(&b, esk_json_get(steps, "convert"), steplog) == 0;
    /* the new image on the running MCU */
    if (ok && bi->mcu && esk_mcu_machine(bi->mcu)) {
        char *img = NULL;
        size_t len = 0;
        if (esk_host_read_file(s->a, b.image, &img, &len) == ES_OK) {
            char why[160];
            if (esk_mcu_set_image(bi->mcu, (const unsigned char *)img, len, why, sizeof why) != ES_OK) {
                esk_strbuf_printf(&b.log, "%s: %s\n", b.image, why);
                ok = 0;
            }
            esk_free(s->a, img, len + 1);
        }
    }
    esk_json_set_bool(s->a, report, "ok", ok);
    esk_json_set_string(s->a, report, "board", bi->id);
    esk_json_set_string(s->a, report, "profile", profile_id);
    esk_json_set_string(s->a, report, "image", b.image);
    {
        size_t ll = 0;
        char *log = esk_strbuf_detach(&b.log, &ll);
        if (log) {
            esk_json_set_string(s->a, report, "log", log);
            esk_free(s->a, log, ll + 1);
        }
    }
    esk_json_object_set(s->a, report, "steps", steplog);
    steplog = NULL;
    *out = report;
    report = NULL;
done:
    esk_json_free(s->a, report);
    esk_json_free(s->a, steplog);
    esk_strbuf_free(&b.log);
    for (i = 0; i < b.objects.len; i++)
        esk_strfree(s->a, b.objects.data[i]);
    esk_vec_free(s->a, &b.objects);
    for (i = 0; i < b.owned.len; i++)
        esk_strfree(s->a, b.owned.data[i]);
    esk_vec_free(s->a, &b.owned);
    esk_strfree(s->a, b.dir);
    esk_strfree(s->a, b.build_dir);
    esk_strfree(s->a, b.image);
    esk_strfree(s->a, b.map);
    esk_strfree(s->a, b.hex);
    esk_strfree(s->a, b.linker_script);
    return st;
}
