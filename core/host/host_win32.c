/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* host_win32.c -- host.h on Windows: C stdio and FindFirstFile for files,
 * GetTickCount64 for the clock, Winsock2 with select, ShellExecute for the
 * browser launcher, SetConsoleCtrlHandler for Ctrl-C. */
#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601 /* Windows 7+: inet_pton, GetTickCount64 */
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <fcntl.h>
#include <io.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <direct.h>
#include <errno.h>
#include <string.h>
#include "host/host.h"

#if defined(_MSC_VER)
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "shell32.lib")
#endif

/* ---- files --------------------------------------------------------------- */

es_status esk_host_read_file(esk_alloc *a, const char *path, char **data, size_t *len)
{
    FILE *f;
    char *buf = NULL;
    size_t cap = 0, n = 0;
    if (!a || !path || !data || !len)
        return ES_EINVAL;
    f = fopen(path, "rb");
    if (!f)
        return GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES ? ES_ENOTFOUND : ES_EIO;
    for (;;) {
        size_t got;
        if (n + 4096 + 1 > cap) {
            size_t ncap = cap ? cap * 2 : 8192;
            char *nb = (char *)esk_realloc(a, buf, cap, ncap);
            if (!nb) {
                esk_free(a, buf, cap);
                fclose(f);
                return ES_ENOMEM;
            }
            buf = nb;
            cap = ncap;
        }
        got = fread(buf + n, 1, cap - n - 1, f);
        n += got;
        if (got == 0) {
            if (ferror(f)) {
                esk_free(a, buf, cap);
                fclose(f);
                return ES_EIO;
            }
            break;
        }
    }
    fclose(f);
    buf[n] = 0;
    if (cap != n + 1) {
        char *nb = (char *)esk_realloc(a, buf, cap, n + 1);
        if (nb)
            buf = nb;
        else
            a->live_bytes -= cap - (n + 1);
    }
    *data = buf;
    *len = n;
    return ES_OK;
}

es_status esk_host_write_file(const char *path, const void *data, size_t len)
{
    FILE *f;
    if (!path || (!data && len))
        return ES_EINVAL;
    f = fopen(path, "wb");
    if (!f)
        return ES_EIO;
    if (len && fwrite(data, 1, len, f) != len) {
        fclose(f);
        return ES_EIO;
    }
    if (fclose(f) != 0)
        return ES_EIO;
    return ES_OK;
}

int esk_host_file_exists(const char *path)
{
    return path && GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

int esk_host_is_dir(const char *path)
{
    DWORD attr;
    if (!path)
        return 0;
    attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static int cmp_json_string(const void *x, const void *y)
{
    const esk_json *const *p = (const esk_json *const *)x, *const *q = (const esk_json *const *)y;
    return strcmp((*p)->u.string.data, (*q)->u.string.data);
}

es_status esk_host_list_dir(esk_alloc *a, const char *path, esk_json **out)
{
    WIN32_FIND_DATAA fd;
    HANDLE h;
    esk_json *arr;
    char *pattern;
    if (!a || !path || !out)
        return ES_EINVAL;
    if (!esk_host_is_dir(path))
        return ES_ENOTFOUND;
    pattern = esk_host_path_join(a, path, "*");
    if (!pattern)
        return ES_ENOMEM;
    h = FindFirstFileA(pattern, &fd);
    esk_strfree(a, pattern);
    arr = esk_json_new_array(a);
    if (!arr) {
        if (h != INVALID_HANDLE_VALUE)
            FindClose(h);
        return ES_ENOMEM;
    }
    if (h != INVALID_HANDLE_VALUE) {
        do {
            esk_json *s;
            if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, ".."))
                continue;
            s = esk_json_new_string(a, fd.cFileName);
            if (!s || esk_json_array_push(a, arr, s)) {
                esk_json_free(a, s);
                esk_json_free(a, arr);
                FindClose(h);
                return ES_ENOMEM;
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    if (arr->u.array.len > 1)
        qsort(arr->u.array.items, arr->u.array.len, sizeof *arr->u.array.items, cmp_json_string);
    *out = arr;
    return ES_OK;
}

char *esk_host_user_data_dir(esk_alloc *a)
{
    const char *base = getenv("APPDATA");
    if (!base || !*base)
        return NULL;
    return esk_host_path_join(a, base, "EmbSIM");
}

char *esk_host_temp_dir(esk_alloc *a)
{
    char buf[MAX_PATH + 1];
    DWORD n = GetTempPathA((DWORD)sizeof buf, buf);
    if (n == 0 || n >= sizeof buf)
        return esk_strdup(a, ".");
    while (n > 1 && (buf[n - 1] == '\\' || buf[n - 1] == '/'))
        buf[--n] = 0;
    return esk_strdup(a, buf);
}

/* ---- time ---------------------------------------------------------------- */

uint64_t esk_host_monotonic_ms(void) { return (uint64_t)GetTickCount64(); }
void esk_host_sleep_ms(uint32_t ms) { Sleep((DWORD)ms); }

/* ---- sockets ------------------------------------------------------------- */

static int g_wsa_refs = 0;

es_status esk_host_sock_init(void)
{
    WSADATA wd;
    if (g_wsa_refs == 0 && WSAStartup(MAKEWORD(2, 2), &wd) != 0)
        return ES_EIO;
    g_wsa_refs++;
    return ES_OK;
}

void esk_host_sock_shutdown(void)
{
    if (g_wsa_refs > 0 && --g_wsa_refs == 0)
        WSACleanup();
}

static int parse_host(const char *host, struct sockaddr_in *sa)
{
    memset(sa, 0, sizeof *sa);
    sa->sin_family = AF_INET;
    if (!host || !*host || !strcmp(host, "localhost"))
        host = "127.0.0.1";
    return inet_pton(AF_INET, host, &sa->sin_addr) == 1 ? 0 : -1;
}

es_status esk_host_sock_listen(const char *host, int port, esk_host_sock *out)
{
    struct sockaddr_in sa;
    SOCKET fd;
    BOOL one = TRUE;
    if (!out || port < 0 || port > 65535 || parse_host(host, &sa))
        return ES_EINVAL;
    sa.sin_port = htons((u_short)port);
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCKET)
        return ES_EIO;
    setsockopt(fd, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&one, (int)sizeof one);
    if (bind(fd, (struct sockaddr *)&sa, (int)sizeof sa) != 0 || listen(fd, 16) != 0) {
        closesocket(fd);
        return ES_EIO;
    }
    *out = (esk_host_sock)fd;
    return ES_OK;
}

es_status esk_host_sock_connect(const char *host, int port, esk_host_sock *out)
{
    struct sockaddr_in sa;
    SOCKET fd;
    BOOL one = TRUE;
    if (!out || port <= 0 || port > 65535 || parse_host(host, &sa))
        return ES_EINVAL;
    sa.sin_port = htons((u_short)port);
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCKET)
        return ES_EIO;
    if (connect(fd, (struct sockaddr *)&sa, (int)sizeof sa) != 0) {
        closesocket(fd);
        return ES_EIO;
    }
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, (int)sizeof one);
    *out = (esk_host_sock)fd;
    return ES_OK;
}

/* select() on one socket; `write` selects writability. */
static int wait_sock(SOCKET fd, int write, int timeout_ms)
{
    fd_set set;
    struct timeval tv;
    int r;
    FD_ZERO(&set);
    FD_SET(fd, &set);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    r = select(0, write ? NULL : &set, write ? &set : NULL, NULL, timeout_ms < 0 ? NULL : &tv);
    if (r == SOCKET_ERROR)
        return -1;
    return r > 0 ? 1 : 0;
}

int esk_host_sock_wait(esk_host_sock s, int timeout_ms)
{
    if (s == ESK_HOST_SOCK_INVALID)
        return -1;
    return wait_sock((SOCKET)s, 0, timeout_ms);
}

es_status esk_host_sock_accept(esk_host_sock listener, esk_host_sock *client)
{
    SOCKET fd;
    BOOL one = TRUE;
    if (listener == ESK_HOST_SOCK_INVALID || !client)
        return ES_EINVAL;
    fd = accept((SOCKET)listener, NULL, NULL);
    if (fd == INVALID_SOCKET)
        return ES_EIO;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, (int)sizeof one);
    *client = (esk_host_sock)fd;
    return ES_OK;
}

long esk_host_sock_recv(esk_host_sock s, void *buf, size_t cap, int timeout_ms)
{
    int n, w;
    if (s == ESK_HOST_SOCK_INVALID || !buf || cap == 0)
        return -1;
    w = wait_sock((SOCKET)s, 0, timeout_ms);
    if (w <= 0)
        return w;
    n = recv((SOCKET)s, (char *)buf, cap > (size_t)INT_MAX ? INT_MAX : (int)cap, 0);
    if (n <= 0)
        return -1;
    return (long)n;
}

long esk_host_sock_send(esk_host_sock s, const void *buf, size_t len, int timeout_ms)
{
    const char *p = (const char *)buf;
    size_t done = 0;
    uint64_t deadline = timeout_ms >= 0 ? esk_host_monotonic_ms() + (uint64_t)timeout_ms : 0;
    if (s == ESK_HOST_SOCK_INVALID || (!buf && len))
        return -1;
    while (done < len) {
        int n, w, remaining = -1;
        size_t chunk = len - done;
        if (timeout_ms >= 0) {
            uint64_t now = esk_host_monotonic_ms();
            if (now >= deadline)
                return -1;
            remaining = (int)(deadline - now);
        }
        w = wait_sock((SOCKET)s, 1, remaining);
        if (w < 0)
            return -1;
        if (w == 0)
            continue;
        n = send((SOCKET)s, p + done, chunk > (size_t)INT_MAX ? INT_MAX : (int)chunk, 0);
        if (n == SOCKET_ERROR) {
            if (WSAGetLastError() == WSAEWOULDBLOCK)
                continue;
            return -1;
        }
        done += (size_t)n;
    }
    return (long)done;
}

void esk_host_sock_close(esk_host_sock s)
{
    if (s != ESK_HOST_SOCK_INVALID)
        closesocket((SOCKET)s);
}

es_status esk_host_sock_local_port(esk_host_sock s, int *port)
{
    struct sockaddr_in sa;
    int len = (int)sizeof sa;
    if (s == ESK_HOST_SOCK_INVALID || !port)
        return ES_EINVAL;
    if (getsockname((SOCKET)s, (struct sockaddr *)&sa, &len) != 0)
        return ES_EIO;
    *port = (int)ntohs(sa.sin_port);
    return ES_OK;
}

/* ---- processes ----------------------------------------------------------- */

es_status esk_host_open_browser(const char *url)
{
    INT_PTR r;
    if (!url || !*url)
        return ES_EINVAL;
    r = (INT_PTR)ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
    return r > 32 ? ES_OK : ES_EIO;
}

/* one argument as CommandLineToArgv reads it back: quoted when it holds a
 * blank or a quote, backslashes doubled before a quote */
static void quote_arg(esk_strbuf *b, const char *arg)
{
    const char *p;
    if (*arg && !strpbrk(arg, " \t\"")) {
        esk_strbuf_puts(b, arg);
        return;
    }
    esk_strbuf_putc(b, '"');
    for (p = arg;; p++) {
        size_t bs = 0;
        while (*p == '\\') {
            p++;
            bs++;
        }
        if (!*p) {
            while (bs--) {
                esk_strbuf_putc(b, '\\');
                esk_strbuf_putc(b, '\\');
            }
            break;
        }
        if (*p == '"') {
            while (bs--) {
                esk_strbuf_putc(b, '\\');
                esk_strbuf_putc(b, '\\');
            }
            esk_strbuf_putc(b, '\\');
            esk_strbuf_putc(b, '"');
            continue;
        }
        while (bs--)
            esk_strbuf_putc(b, '\\');
        esk_strbuf_putc(b, *p);
    }
    esk_strbuf_putc(b, '"');
}

es_status esk_host_run(esk_alloc *a, const char *const *argv, const char *cwd, size_t cap, int *exit_code,
                       char **output, size_t *len)
{
    SECURITY_ATTRIBUTES sa;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    HANDLE rd = NULL, wr = NULL;
    esk_strbuf cmd, out;
    char buf[4096];
    DWORD got = 0, code = 0;
    size_t i;
    BOOL ok;
    *exit_code = -1;
    *output = NULL;
    *len = 0;
    if (!argv || !argv[0])
        return ES_EINVAL;
    memset(&sa, 0, sizeof sa);
    sa.nLength = sizeof sa;
    sa.bInheritHandle = TRUE;
    if (!CreatePipe(&rd, &wr, &sa, 0))
        return ES_EIO;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    esk_strbuf_init(&cmd, a);
    for (i = 0; argv[i]; i++) {
        if (i)
            esk_strbuf_putc(&cmd, ' ');
        quote_arg(&cmd, argv[i]);
    }
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = wr;
    si.hStdError = wr;
    memset(&pi, 0, sizeof pi);
    ok = !cmd.oom && CreateProcessA(NULL, cmd.data, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, cwd, &si, &pi);
    esk_strbuf_free(&cmd);
    CloseHandle(wr);
    if (!ok) {
        CloseHandle(rd);
        return ES_ENOTFOUND;
    }
    esk_strbuf_init(&out, a);
    while (ReadFile(rd, buf, (DWORD)sizeof buf, &got, NULL) && got > 0) {
        if (out.len < cap)
            esk_strbuf_append(&out, buf, (size_t)got < cap - out.len ? (size_t)got : cap - out.len);
    }
    CloseHandle(rd);
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    *exit_code = (int)code;
    *output = esk_strbuf_detach(&out, len);
    return *output ? ES_OK : ES_ENOMEM;
}

/* ---- standard streams ---------------------------------------------------- */

static struct {
    char buf[8192];
    size_t len, pos;
    int eof;
} g_stdin;

void esk_host_stdio_init(void)
{
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
}

/* Readiness of the stdin handle: a pipe is peeked, a console waited on, a
 * file is always ready. */
static int stdin_handle_ready(int timeout_ms)
{
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    DWORD type;
    uint64_t deadline;
    if (h == INVALID_HANDLE_VALUE || h == NULL)
        return -1;
    type = GetFileType(h);
    if (type == FILE_TYPE_CHAR) {
        DWORD r = WaitForSingleObject(h, timeout_ms < 0 ? INFINITE : (DWORD)timeout_ms);
        return r == WAIT_OBJECT_0 ? 1 : (r == WAIT_TIMEOUT ? 0 : -1);
    }
    if (type != FILE_TYPE_PIPE)
        return 1;
    deadline = esk_host_monotonic_ms() + (timeout_ms < 0 ? 0 : (uint64_t)timeout_ms);
    for (;;) {
        DWORD avail = 0;
        if (!PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL))
            return 1; /* broken pipe: a read reports the end of input */
        if (avail > 0)
            return 1;
        if (timeout_ms >= 0 && esk_host_monotonic_ms() >= deadline)
            return 0;
        Sleep(1);
    }
}

static int stdin_fill(void)
{
    int n;
    if (g_stdin.eof)
        return 0;
    if (g_stdin.pos > 0 && g_stdin.pos == g_stdin.len)
        g_stdin.pos = g_stdin.len = 0;
    if (g_stdin.len == sizeof g_stdin.buf)
        return 1;
    n = _read(_fileno(stdin), g_stdin.buf + g_stdin.len, (unsigned)(sizeof g_stdin.buf - g_stdin.len));
    if (n < 0)
        return -1;
    if (n == 0) {
        g_stdin.eof = 1;
        return 1;
    }
    g_stdin.len += (size_t)n;
    return 1;
}

int esk_host_stdin_ready(int timeout_ms)
{
    if (g_stdin.pos < g_stdin.len || g_stdin.eof)
        return 1;
    return stdin_handle_ready(timeout_ms);
}

int esk_host_stdin_readline(esk_strbuf *line)
{
    if (!line)
        return -1;
    esk_strbuf_clear(line);
    for (;;) {
        while (g_stdin.pos < g_stdin.len) {
            char c = g_stdin.buf[g_stdin.pos++];
            if (c == '\n') {
                if (line->len && line->data[line->len - 1] == '\r')
                    line->data[--line->len] = 0;
                return line->oom ? -1 : 1;
            }
            if (esk_strbuf_putc(line, c))
                return -1;
        }
        if (g_stdin.eof)
            return line->len ? 1 : 0;
        if (stdin_fill() < 0)
            return -1;
    }
}

es_status esk_host_stdout_write(const char *data, size_t len)
{
    if (!data && len)
        return ES_EINVAL;
    if (len && fwrite(data, 1, len, stdout) != len)
        return ES_EIO;
    if (fflush(stdout) != 0)
        return ES_EIO;
    return ES_OK;
}

/* ---- interruption -------------------------------------------------------- */

static volatile LONG g_interrupted = 0;
static int g_interrupt_installed = 0;

static BOOL WINAPI on_ctrl(DWORD type)
{
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) {
        InterlockedExchange(&g_interrupted, 1);
        return TRUE;
    }
    return FALSE;
}

void esk_host_interrupt_install(void)
{
    if (g_interrupt_installed)
        return;
    g_interrupt_installed = 1;
    SetConsoleCtrlHandler(on_ctrl, TRUE);
}

int esk_host_interrupted(void) { return g_interrupted != 0; }
void esk_host_interrupt_clear(void) { InterlockedExchange(&g_interrupted, 0); }

es_status esk_host_remove(const char *path)
{
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES)
        return ES_ENOTFOUND;
    if (attr & FILE_ATTRIBUTE_DIRECTORY)
        return RemoveDirectoryA(path) ? ES_OK : ES_EIO;
    if (attr & FILE_ATTRIBUTE_READONLY)
        (void)SetFileAttributesA(path, attr & ~(DWORD)FILE_ATTRIBUTE_READONLY);
    return DeleteFileA(path) ? ES_OK : ES_EIO;
}

es_status esk_host_mkdir_p(const char *path)
{
    char buf[1024];
    size_t i, n;
    if (!path || !*path)
        return ES_EINVAL;
    n = strlen(path);
    if (n >= sizeof buf)
        return ES_EINVAL;
    memcpy(buf, path, n + 1);
    for (i = 1; i <= n; i++) {
        if (buf[i] == '\\' || buf[i] == '/' || buf[i] == 0) {
            char c = buf[i];
            buf[i] = 0;
            if (buf[i - 1] != ':' && _mkdir(buf) != 0 && errno != EEXIST)
                return ES_EIO;
            buf[i] = c;
        }
    }
    return ES_OK;
}

#else
typedef int esk_host_win32_not_built; /* ISO C forbids an empty translation unit */
#endif
