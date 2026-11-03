/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* ide.c -- `embsim ide PROJECT`: VS Code's launch configurations for the
 * project's boards (ADR-032; docs/engineering/debug-probes.md, step 5).
 *
 * For each board with firmware:
 *   - a board whose description names its debug probe (`debug.probe`, the
 *     NUCLEO's ST-LINK) gets a Cortex-Debug configuration that starts
 *     OpenOCD with the board's `debug.openocd` files and the probe's
 *     backend command, after a task that runs the session in real time with
 *     the probe shared (`embsim serve PROJECT --stlink --run`): as a board
 *     on the bench is debugged through its probe;
 *   - every board gets one on EmbSIM's own GDB server (`embsim gdb`), a
 *     Cortex-M's through Cortex-Debug, another core's through the C/C++
 *     extension's GDB (cppdbg).
 * They are written to .vscode/launch.json and .vscode/tasks.json next to
 * the project (refused when they exist, unless --force). */
#include <stdio.h>
#include <string.h>

#include "ide.h"
#include "host/host.h"
#include "mcu/mach.h"
#include "mcu/mcu.h"
#include "session/session_priv.h"

/* the core's interface name, "cortex-m", "avr"... ("" without firmware) */
static const char *core_of(esk_board_inst *bi)
{
    struct esk_mach *m = bi->mcu ? esk_mcu_machine(bi->mcu) : NULL;
    return m && m->cpu ? m->cpu->ops->name : "";
}

static esk_json *strings(esk_alloc *a, const char *const *v, int n)
{
    esk_json *arr = esk_json_new_array(a);
    int i;
    for (i = 0; arr && i < n; i++)
        esk_json_array_push(a, arr, esk_json_new_string(a, v[i]));
    return arr;
}

/* a background task, ready when its output says `ready` */
static esk_json *task(esk_alloc *a, const char *label, const char *embsim, const char *const *args, int nargs,
                      const char *ready)
{
    esk_json *t = esk_json_new_object(a), *pm = esk_json_new_object(a), *bg = esk_json_new_object(a),
             *pat = esk_json_new_object(a);
    if (!t || !pm || !bg || !pat)
        return NULL;
    esk_json_set_string(a, t, "label", label);
    esk_json_set_string(a, t, "type", "process");
    esk_json_set_string(a, t, "command", embsim);
    esk_json_object_set(a, t, "args", strings(a, args, nargs));
    esk_json_set_bool(a, t, "isBackground", 1);
    /* the diagnostics as problems; the task ready once the server listens */
    esk_json_set_string(a, pat, "regexp", "^embsim: (error|warning): (.*)$");
    esk_json_set_integer(a, pat, "severity", 1);
    esk_json_set_integer(a, pat, "message", 2);
    esk_json_object_set(a, pm, "pattern", pat);
    esk_json_set_string(a, pm, "owner", "embsim");
    esk_json_set_bool(a, bg, "activeOnStart", 1);
    esk_json_set_string(a, bg, "beginsPattern", "^embsim:");
    esk_json_set_string(a, bg, "endsPattern", ready);
    esk_json_object_set(a, pm, "background", bg);
    esk_json_object_set(a, t, "problemMatcher", pm);
    return t;
}

static int write_json(esk_alloc *a, const char *path, const esk_json *j)
{
    size_t len;
    char *text = esk_json_format(a, j, 2, &len);
    int ok = text && esk_host_write_file(path, text, len) == ES_OK;
    if (text)
        esk_free(a, text, len + 1);
    return ok;
}

int embsim_ide(es_session *s, const char *project_file, const char *embsim, int force)
{
    esk_alloc *a = s->a;
    esk_json *launch = esk_json_new_object(a), *configs = esk_json_new_array(a);
    esk_json *tasks = esk_json_new_object(a), *tlist = esk_json_new_array(a);
    const char *base = strrchr(project_file, '/');
    char proj[512], dir_buf[1024];
    char *vsdir = NULL, *lpath = NULL, *tpath = NULL;
    size_t i;
    int n = 0, rc = 2;
#ifdef _WIN32
    {
        const char *b2 = strrchr(project_file, '\\');
        if (b2 && (!base || b2 > base))
            base = b2;
    }
#endif
    base = base ? base + 1 : project_file;
    esk_snprintf(proj, sizeof proj, "${workspaceFolder}/%s", base);
    if (!launch || !configs || !tasks || !tlist)
        return 2;
    for (i = 0; i < s->boards.len; i++) {
        esk_board_inst *bi = s->boards.data[i];
        const char *image = bi->firmware ? esk_json_get_str(bi->firmware, "image", NULL) : NULL;
        const esk_json *dbg = bi->item ? esk_json_get(bi->item->desc, "debug") : NULL;
        const char *core = core_of(bi);
        int cm = !strcmp(core, "cortex-m");
        char exe[640], name[160], label[160];
        esk_json *c;
        if (!image)
            continue;
        esk_snprintf(exe, sizeof exe, "${workspaceFolder}/%s", image);
        if (dbg && cm && esk_json_get_str(dbg, "probe", NULL)) {
            /* through the board's probe: OpenOCD, after the session runs with it shared */
            const char *args[] = { "serve", proj, "--port", "0", "--stlink", "7184", "--run" };
            const esk_json *files = esk_json_get(dbg, "openocd");
            esk_json *cf = esk_json_new_array(a), *cmds = esk_json_new_array(a);
            size_t k;
            esk_snprintf(label, sizeof label, "EmbSIM: %s with its %s", bi->id,
                         esk_json_get_str(dbg, "name", "debug probe"));
            esk_json_array_push(a, tlist, task(a, label, embsim, args, 7, "stlink-server on"));
            c = esk_json_new_object(a);
            if (!c || !cf || !cmds)
                return 2;
            esk_snprintf(name, sizeof name, "%s through its %s (OpenOCD)", bi->id,
                         esk_json_get_str(dbg, "name", "debug probe"));
            esk_json_set_string(a, c, "name", name);
            esk_json_set_string(a, c, "type", "cortex-debug");
            esk_json_set_string(a, c, "request", "launch");
            esk_json_set_string(a, c, "servertype", "openocd");
            esk_json_set_string(a, c, "cwd", "${workspaceFolder}");
            esk_json_set_string(a, c, "executable", exe);
            for (k = 0; k < esk_json_len(files); k++)
                esk_json_array_push(a, cf, esk_json_new_string(a, esk_json_str(esk_json_at(files, k))));
            esk_json_object_set(a, c, "configFiles", cf);
            esk_json_array_push(a, cmds, esk_json_new_string(a, esk_json_get_str(dbg, "openocd_command",
                                                                                 "hla_stlink_backend tcp 7184")));
            esk_json_object_set(a, c, "openOCDLaunchCommands", cmds);
            esk_json_set_string(a, c, "runToEntryPoint", "main");
            esk_json_set_string(a, c, "preLaunchTask", label);
            esk_json_array_push(a, configs, c);
            n++;
        }
        {
            /* EmbSIM's own GDB server, for any board */
            const char *args[] = { "gdb", proj, "--board", bi->id, "--port", "3333" };
            esk_snprintf(label, sizeof label, "EmbSIM: %s's GDB server", bi->id);
            esk_json_array_push(a, tlist, task(a, label, embsim, args, 6, "GDB server on"));
            c = esk_json_new_object(a);
            if (!c)
                return 2;
            esk_snprintf(name, sizeof name, "%s on EmbSIM's GDB server", bi->id);
            esk_json_set_string(a, c, "name", name);
            if (cm) {
                esk_json_set_string(a, c, "type", "cortex-debug");
                esk_json_set_string(a, c, "request", "attach");
                esk_json_set_string(a, c, "servertype", "external");
                esk_json_set_string(a, c, "gdbTarget", "localhost:3333");
                esk_json_set_string(a, c, "executable", exe);
                esk_json_set_string(a, c, "cwd", "${workspaceFolder}");
            } else {
                esk_json_set_string(a, c, "type", "cppdbg");
                esk_json_set_string(a, c, "request", "launch");
                esk_json_set_string(a, c, "program", exe);
                esk_json_set_string(a, c, "cwd", "${workspaceFolder}");
                esk_json_set_string(a, c, "MIMode", "gdb");
                esk_json_set_string(a, c, "miDebuggerPath", "gdb-multiarch");
                esk_json_set_string(a, c, "miDebuggerServerAddress", "localhost:3333");
            }
            esk_json_set_string(a, c, "preLaunchTask", label);
            esk_json_array_push(a, configs, c);
            n++;
        }
    }
    if (!n) {
        fprintf(stderr, "embsim: ide: no board of the project has firmware to debug\n");
        goto out;
    }
    esk_json_set_string(a, launch, "version", "0.2.0");
    esk_json_object_set(a, launch, "configurations", configs);
    configs = NULL;
    esk_json_set_string(a, tasks, "version", "2.0.0");
    esk_json_object_set(a, tasks, "tasks", tlist);
    tlist = NULL;
    esk_snprintf(dir_buf, sizeof dir_buf, "%s", s->base_dir ? s->base_dir : ".");
    vsdir = esk_host_path_join(a, dir_buf, ".vscode");
    lpath = vsdir ? esk_host_path_join(a, vsdir, "launch.json") : NULL;
    tpath = vsdir ? esk_host_path_join(a, vsdir, "tasks.json") : NULL;
    if (!lpath || !tpath || esk_host_mkdir_p(vsdir) != ES_OK) {
        fprintf(stderr, "embsim: ide: cannot make %s\n", vsdir ? vsdir : ".vscode");
        goto out;
    }
    if (!force && (esk_host_file_exists(lpath) || esk_host_file_exists(tpath))) {
        fprintf(stderr, "embsim: ide: %s or %s exists (--force replaces them)\n", lpath, tpath);
        goto out;
    }
    if (!write_json(a, lpath, launch) || !write_json(a, tpath, tasks)) {
        fprintf(stderr, "embsim: ide: cannot write %s\n", lpath);
        goto out;
    }
    printf("embsim: %d launch configuration(s) in %s, their tasks in %s\n", n, lpath, tpath);
    rc = 0;
out:
    esk_json_free(a, launch);
    esk_json_free(a, tasks);
    esk_json_free(a, configs);
    esk_json_free(a, tlist);
    esk_strfree(a, vsdir);
    esk_strfree(a, lpath);
    esk_strfree(a, tpath);
    return rc;
}
