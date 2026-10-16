/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* host_posix.c -- host.h on POSIX systems (Linux, macOS, the BSDs): stdio
 * and dirent for files, clock_gettime for the clock, BSD sockets with poll,
 * fork/exec for the browser launcher, sigaction for Ctrl-C. */
#ifndef _WIN32

#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1
#include "host/host.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

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
        return errno == ENOENT ? ES_ENOTFOUND : ES_EIO;
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
    /* shrink to the exact size so the caller frees len + 1 bytes */
    if (cap != n + 1) {
        char *nb = (char *)esk_realloc(a, buf, cap, n + 1);
        if (nb)
            buf = nb;
        else
            a->live_bytes -= cap - (n + 1); /* keep the block; account as n + 1 */
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
    struct stat st;
    return path && stat(path, &st) == 0;
}

int esk_host_is_dir(const char *path)
{
    struct stat st;
    return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int cmp_json_string(const void *x, const void *y)
{
    const esk_json *const *p = (const esk_json *const *)x, *const *q = (const esk_json *const *)y;
    return strcmp((*p)->u.string.data, (*q)->u.string.data);
}

es_status esk_host_list_dir(esk_alloc *a, const char *path, esk_json **out)
{
    DIR *d;
    struct dirent *e;
    esk_json *arr;
    if (!a || !path || !out)
        return ES_EINVAL;
    d = opendir(path);
    if (!d)
        return errno == ENOENT ? ES_ENOTFOUND : ES_EIO;
    arr = esk_json_new_array(a);
    if (!arr) {
        closedir(d);
        return ES_ENOMEM;
    }
    while ((e = readdir(d)) != NULL) {
        esk_json *s;
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        s = esk_json_new_string(a, e->d_name);
        if (!s || esk_json_array_push(a, arr, s)) {
            esk_json_free(a, s);
            esk_json_free(a, arr);
            closedir(d);
            return ES_ENOMEM;
        }
    }
    closedir(d);
    if (arr->u.array.len > 1)
        qsort(arr->u.array.items, arr->u.array.len, sizeof *arr->u.array.items, cmp_json_string);
    *out = arr;
    return ES_OK;
}

char *esk_host_user_data_dir(esk_alloc *a)
{
    const char *home = getenv("HOME");
#if defined(__APPLE__)
    if (!home || !*home)
        return NULL;
    return esk_host_path_join(a, home, "Library/Application Support/EmbSIM");
#else
    const char *xdg = getenv("XDG_DATA_HOME");
    if (xdg && *xdg)
        return esk_host_path_join(a, xdg, "embsim");
    if (!home || !*home)
        return NULL;
    return esk_host_path_join(a, home, ".local/share/embsim");
#endif
}

char *esk_host_temp_dir(esk_alloc *a)
{
    const char *t = getenv("TMPDIR");
    return esk_strdup(a, t && *t ? t : "/tmp");
}

/* ---- time ---------------------------------------------------------------- */

uint64_t esk_host_monotonic_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (uint64_t)ts.tv_sec * UINT64_C(1000) + (uint64_t)ts.tv_nsec / UINT64_C(1000000);
}

void esk_host_sleep_ms(uint32_t ms)
{
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR)
        ;
}

/* ---- sockets ------------------------------------------------------------- */

es_status esk_host_sock_init(void) { return ES_OK; }
void esk_host_sock_shutdown(void) {}

static int parse_host(const char *host, struct sockaddr_in *sa)
{
    memset(sa, 0, sizeof *sa);
    sa->sin_family = AF_INET;
    if (!host || !*host || !strcmp(host, "localhost"))
        host = "127.0.0.1";
    return inet_pton(AF_INET, host, &sa->sin_addr) == 1 ? 0 : -1;
}

static void no_sigpipe(int fd)
{
#ifdef SO_NOSIGPIPE
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#else
    ES_UNUSED(fd);
#endif
}

es_status esk_host_sock_listen(const char *host, int port, esk_host_sock *out)
{
    struct sockaddr_in sa;
    int fd, one = 1;
    if (!out || port < 0 || port > 65535 || parse_host(host, &sa))
        return ES_EINVAL;
    sa.sin_port = htons((uint16_t)port);
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return ES_EIO;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    no_sigpipe(fd);
    if (bind(fd, (struct sockaddr *)&sa, sizeof sa) != 0 || listen(fd, 16) != 0) {
        close(fd);
        return ES_EIO;
    }
    *out = (esk_host_sock)fd;
    return ES_OK;
}

es_status esk_host_sock_connect(const char *host, int port, esk_host_sock *out)
{
    struct sockaddr_in sa;
    int fd, one = 1;
    if (!out || port <= 0 || port > 65535 || parse_host(host, &sa))
        return ES_EINVAL;
    sa.sin_port = htons((uint16_t)port);
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return ES_EIO;
    no_sigpipe(fd);
    if (connect(fd, (struct sockaddr *)&sa, sizeof sa) != 0) {
        close(fd);
        return ES_EIO;
    }
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    *out = (esk_host_sock)fd;
    return ES_OK;
}

static int wait_fd(int fd, short events, int timeout_ms)
{
    struct pollfd p;
    int r;
    p.fd = fd;
    p.events = events;
    p.revents = 0;
    r = poll(&p, 1, timeout_ms);
    if (r < 0)
        return errno == EINTR ? 0 : -1;
    return r > 0 ? 1 : 0;
}

int esk_host_sock_wait(esk_host_sock s, int timeout_ms)
{
    if (s == ESK_HOST_SOCK_INVALID)
        return -1;
    return wait_fd((int)s, POLLIN, timeout_ms);
}

es_status esk_host_sock_accept(esk_host_sock listener, esk_host_sock *client)
{
    int fd, one = 1;
    if (listener == ESK_HOST_SOCK_INVALID || !client)
        return ES_EINVAL;
    do {
        fd = accept((int)listener, NULL, NULL);
    } while (fd < 0 && errno == EINTR && !esk_host_interrupted());
    if (fd < 0)
        return ES_EIO;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    no_sigpipe(fd);
    *client = (esk_host_sock)fd;
    return ES_OK;
}

long esk_host_sock_recv(esk_host_sock s, void *buf, size_t cap, int timeout_ms)
{
    ssize_t n;
    int w;
    if (s == ESK_HOST_SOCK_INVALID || !buf || cap == 0)
        return -1;
    w = wait_fd((int)s, POLLIN, timeout_ms);
    if (w <= 0)
        return w; /* 0 timeout, -1 error */
    do {
        n = recv((int)s, buf, cap, 0);
    } while (n < 0 && errno == EINTR);
    if (n <= 0)
        return -1; /* closed or failed */
    return (long)n;
}

long esk_host_sock_send(esk_host_sock s, const void *buf, size_t len, int timeout_ms)
{
    const unsigned char *p = (const unsigned char *)buf;
    size_t done = 0;
    uint64_t deadline = timeout_ms >= 0 ? esk_host_monotonic_ms() + (uint64_t)timeout_ms : 0;
    int flags = 0;
#ifdef MSG_NOSIGNAL
    flags |= MSG_NOSIGNAL;
#endif
    if (s == ESK_HOST_SOCK_INVALID || (!buf && len))
        return -1;
    while (done < len) {
        ssize_t n;
        int remaining = -1, w;
        if (timeout_ms >= 0) {
            uint64_t now = esk_host_monotonic_ms();
            if (now >= deadline)
                return -1;
            remaining = (int)(deadline - now); /* bounded by timeout_ms */
        }
        w = wait_fd((int)s, POLLOUT, remaining);
        if (w < 0)
            return -1;
        if (w == 0)
            continue;
        n = send((int)s, p + done, len - done, flags);
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN)
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
        close((int)s);
}

es_status esk_host_sock_local_port(esk_host_sock s, int *port)
{
    struct sockaddr_in sa;
    socklen_t len = sizeof sa;
    if (s == ESK_HOST_SOCK_INVALID || !port)
        return ES_EINVAL;
    if (getsockname((int)s, (struct sockaddr *)&sa, &len) != 0)
        return ES_EIO;
    *port = (int)ntohs(sa.sin_port);
    return ES_OK;
}

/* ---- processes ----------------------------------------------------------- */

es_status esk_host_open_browser(const char *url)
{
    pid_t pid;
    int status = 0;
#if defined(__APPLE__)
    const char *launcher = "open";
#else
    const char *launcher = "xdg-open";
#endif
    if (!url || !*url)
        return ES_EINVAL;
    /* double fork so the launcher is reparented and never a zombie of ours */
    pid = fork();
    if (pid < 0)
        return ES_EIO;
    if (pid == 0) {
        pid_t p2 = fork();
        if (p2 == 0) {
            int devnull = open("/dev/null", O_RDWR);
            if (devnull >= 0) {
                dup2(devnull, 0);
                dup2(devnull, 1);
                dup2(devnull, 2);
                if (devnull > 2)
                    close(devnull);
            }
            execlp(launcher, launcher, url, (char *)NULL);
            _exit(127);
        }
        _exit(p2 < 0 ? 1 : 0);
    }
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
        ;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return ES_EIO;
    return ES_OK;
}

es_status esk_host_run(esk_alloc *a, const char *const *argv, const char *cwd, size_t cap, int *exit_code,
                       char **output, size_t *len)
{
    int fds[2], status = 0, exec_err[2];
    pid_t pid;
    esk_strbuf out;
    char buf[4096];
    ssize_t n;
    *exit_code = -1;
    *output = NULL;
    *len = 0;
    if (!argv || !argv[0])
        return ES_EINVAL;
    if (pipe(fds) != 0)
        return ES_EIO;
    if (pipe(exec_err) != 0) {
        close(fds[0]);
        close(fds[1]);
        return ES_EIO;
    }
    (void)fcntl(exec_err[1], F_SETFD, FD_CLOEXEC);
    pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        close(exec_err[0]);
        close(exec_err[1]);
        return ES_EIO;
    }
    if (pid == 0) {
        int e;
        close(fds[0]);
        close(exec_err[0]);
        dup2(fds[1], 1);
        dup2(fds[1], 2);
        close(fds[1]);
        if (cwd && chdir(cwd) != 0)
            _exit(126);
        execvp(argv[0], (char *const *)argv);
        e = errno;
        if (write(exec_err[1], &e, sizeof e) < 0)
            _exit(127);
        _exit(127);
    }
    close(fds[1]);
    close(exec_err[1]);
    esk_strbuf_init(&out, a);
    while ((n = read(fds[0], buf, sizeof buf)) > 0 || (n < 0 && errno == EINTR)) {
        if (n > 0 && out.len < cap)
            esk_strbuf_append(&out, buf, (size_t)n < cap - out.len ? (size_t)n : cap - out.len);
    }
    close(fds[0]);
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
        ;
    {
        int e = 0;
        ssize_t got = read(exec_err[0], &e, sizeof e);
        close(exec_err[0]);
        if (got == (ssize_t)sizeof e) {
            esk_strbuf_free(&out);
            return ES_ENOTFOUND;
        }
    }
    *exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
    *output = esk_strbuf_detach(&out, len); /* NUL-terminated, len + 1 bytes */
    return *output ? ES_OK : ES_ENOMEM;
}

/* ---- standard streams ---------------------------------------------------- */

/* stdin is read through our own buffer (not FILE's) so readiness can be
 * asked with poll() without missing bytes already buffered. */
static struct {
    char buf[8192];
    size_t len, pos;
    int eof;
} g_stdin;

void esk_host_stdio_init(void) {}

static int stdin_fill(int timeout_ms)
{
    ssize_t n;
    int w;
    if (g_stdin.eof)
        return 0;
    if (g_stdin.pos > 0 && g_stdin.pos == g_stdin.len)
        g_stdin.pos = g_stdin.len = 0;
    if (g_stdin.len == sizeof g_stdin.buf)
        return 1; /* full: the caller must consume first */
    w = wait_fd(0, POLLIN, timeout_ms);
    if (w <= 0)
        return w;
    do {
        n = read(0, g_stdin.buf + g_stdin.len, sizeof g_stdin.buf - g_stdin.len);
    } while (n < 0 && errno == EINTR && !esk_host_interrupted());
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
    return wait_fd(0, POLLIN, timeout_ms);
}

int esk_host_stdin_readline(esk_strbuf *line)
{
    if (!line)
        return -1;
    esk_strbuf_clear(line);
    for (;;) {
        /* consume what is buffered up to a newline */
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
        if (stdin_fill(-1) < 0)
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

static volatile sig_atomic_t g_interrupted = 0;
static int g_interrupt_installed = 0;

static void on_sigint(int sig)
{
    ES_UNUSED(sig);
    g_interrupted = 1;
}

void esk_host_interrupt_install(void)
{
    struct sigaction sa;
    if (g_interrupt_installed)
        return;
    g_interrupt_installed = 1;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; /* no SA_RESTART: blocking calls return EINTR and see the flag */
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

int esk_host_interrupted(void) { return g_interrupted != 0; }
void esk_host_interrupt_clear(void) { g_interrupted = 0; }

es_status esk_host_remove(const char *path)
{
    struct stat st;
    if (lstat(path, &st) != 0)
        return errno == ENOENT ? ES_ENOTFOUND : ES_EIO;
    return (S_ISDIR(st.st_mode) ? rmdir(path) : unlink(path)) == 0 ? ES_OK : ES_EIO;
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
        if (buf[i] == '/' || buf[i] == 0) {
            char c = buf[i];
            buf[i] = 0;
            if (mkdir(buf, 0775) != 0 && errno != EEXIST)
                return ES_EIO;
            buf[i] = c;
        }
    }
    return ES_OK;
}

#else
typedef int esk_host_posix_not_built; /* ISO C forbids an empty translation unit */
#endif
