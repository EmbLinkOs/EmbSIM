/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* host.h -- the host layer (02 §4, CS-3, CS-5): the only module of the core
 * that calls the operating system. Files and paths, the monotonic clock,
 * sleeping, loopback sockets, the standard streams used by the stdio
 * transport (SPEC-008 §1), a best-effort browser launcher and the Ctrl-C
 * flag. One implementation per platform: host_posix.c and host_win32.c. */
#ifndef ESK_HOST_H
#define ESK_HOST_H

#include <stddef.h>
#include <stdint.h>
#include "embsim/types.h"
#include "util/alloc.h"
#include "util/json.h"
#include "util/str.h"

/* ---- files and paths ----------------------------------------------------- */

/* Read a whole file. *data gets len + 1 bytes (binary-safe, with a NUL after
 * the last byte); release it with esk_free(a, data, len + 1). */
es_status esk_host_read_file(esk_alloc *a, const char *path, char **data, size_t *len);
/* Write (create or truncate) a whole file. */
es_status esk_host_write_file(const char *path, const void *data, size_t len);
/* create a directory and its parents; existing is fine */
es_status esk_host_mkdir_p(const char *path);
/* 1 when `path` names an existing file or directory. */
int esk_host_file_exists(const char *path);
/* 1 when `path` names an existing directory. */
int esk_host_is_dir(const char *path);
/* The directory part of a path ("." when there is none). esk_strfree to release. */
char *esk_host_dirname(esk_alloc *a, const char *path);
/* base + separator + rel, normalised: "." components dropped, ".." resolved
 * against the preceding component when there is one, repeated separators
 * collapsed. An absolute `rel` ("/x", "C:\x", "C:/x", "\\server\share")
 * replaces `base`. esk_strfree to release. */
char *esk_host_path_join(esk_alloc *a, const char *base, const char *rel);
/* The names in a directory (no "." and ".."), sorted, as a JSON array of
 * strings. esk_json_free to release. */
es_status esk_host_list_dir(esk_alloc *a, const char *path, esk_json **out);
/* Remove a file, or an empty directory. */
es_status esk_host_remove(const char *path);
/* Remove a directory and everything under it (or a file). A link is removed,
 * never followed. A missing path is ES_OK. */
es_status esk_host_remove_tree(esk_alloc *a, const char *path);
/* The per-user data directory of SPEC-014 §1 (not created). esk_strfree to release. */
char *esk_host_user_data_dir(esk_alloc *a);
/* The host's temporary directory. esk_strfree to release. */
char *esk_host_temp_dir(esk_alloc *a);

/* ---- time ---------------------------------------------------------------- */

/* Milliseconds from an arbitrary origin; never decreases within a process. */
uint64_t esk_host_monotonic_ms(void);
void esk_host_sleep_ms(uint32_t ms);

/* ---- sockets ------------------------------------------------------------- */

typedef intptr_t esk_host_sock;
#define ESK_HOST_SOCK_INVALID ((esk_host_sock)-1)

/* Process-wide socket setup (WSAStartup on Windows); counted, so pairs nest. */
es_status esk_host_sock_init(void);
void esk_host_sock_shutdown(void);
/* Listen on host:port (port 0: ephemeral). `host` is a numeric IPv4 address
 * or "localhost"; NULL means 127.0.0.1. */
es_status esk_host_sock_listen(const char *host, int port, esk_host_sock *out);
/* Connect to host:port (same host rules). */
es_status esk_host_sock_connect(const char *host, int port, esk_host_sock *out);
/* Wait up to timeout_ms for the socket to become readable (data, a pending
 * connection, or a close). Returns 1 readable, 0 timeout, -1 error. */
int esk_host_sock_wait(esk_host_sock s, int timeout_ms);
/* Accept a pending connection (blocks; call after esk_host_sock_wait). */
es_status esk_host_sock_accept(esk_host_sock listener, esk_host_sock *client);
/* Receive up to cap bytes, waiting at most timeout_ms (-1: forever).
 * Returns the byte count (> 0), 0 on timeout, -1 on error or when the peer
 * closed the connection. */
long esk_host_sock_recv(esk_host_sock s, void *buf, size_t cap, int timeout_ms);
/* Send all `len` bytes, waiting at most timeout_ms in total (-1: forever).
 * Returns the byte count sent, or -1 on error or timeout. */
long esk_host_sock_send(esk_host_sock s, const void *buf, size_t len, int timeout_ms);
void esk_host_sock_close(esk_host_sock s);
/* The port a listening (or connected) socket is bound to. */
es_status esk_host_sock_local_port(esk_host_sock s, int *port);

/* ---- pseudo-terminals (a board's serial port on the host, ADR-031) ------ */

typedef struct esk_host_pty esk_host_pty;
/* Open a pseudo-terminal: the port a program on the host opens, at
 * esk_host_pty_path (/dev/pts/3, /dev/ttys004). Its line starts as a USB CDC
 * port's does on Linux: 9600 baud, 8N1, canonical with echo, until a program
 * sets its own. ES_EUNSUPPORTED where the host has none (Windows, Q16). */
es_status esk_host_pty_open(esk_alloc *a, esk_host_pty **out);
const char *esk_host_pty_path(const esk_host_pty *p);
/* 1 while a program has the port open, 0 when none has */
int esk_host_pty_peer(esk_host_pty *p);
/* The baud rate the program set on the line (0 when none has it open). */
uint32_t esk_host_pty_baud(esk_host_pty *p);
/* What the program wrote, without waiting: the byte count, 0 for none, -1
 * when the port failed. */
long esk_host_pty_read(esk_host_pty *p, void *buf, size_t cap);
/* Write to the program without waiting: the bytes taken (fewer when its
 * buffer is full), -1 when the port failed. */
long esk_host_pty_write(esk_host_pty *p, const void *buf, size_t len);
/* Drop what waits in the port in both directions, as a closed port does. */
void esk_host_pty_flush(esk_host_pty *p);
void esk_host_pty_close(esk_host_pty *p);

/* ---- processes ----------------------------------------------------------- */

/* Open `url` in the user's browser (xdg-open / open / ShellExecute). Best
 * effort: ES_OK when the launcher was started, ES_EUNSUPPORTED or ES_EIO
 * otherwise. The URL is passed as one argument, never through a shell. */
es_status esk_host_open_browser(const char *url);
/* Run a program (argv[0] searched on PATH; never through a shell) in `cwd`
 * (NULL: the current directory) and wait for it. Its stdout and stderr,
 * together, go into *output (len + 1 bytes, NUL-terminated; release with
 * esk_free(a, *output, *len + 1)), at most `cap` bytes kept; its exit status
 * into *exit_code. ES_ENOTFOUND when the program cannot be started. */
es_status esk_host_run(esk_alloc *a, const char *const *argv, const char *cwd, size_t cap, int *exit_code,
                       char **output, size_t *len);

/* ---- standard streams (the stdio transport) ------------------------------ */

/* Put stdin/stdout in binary mode where that matters (Windows). */
void esk_host_stdio_init(void);
/* Wait up to timeout_ms for stdin to have data (or end of input) ready.
 * Returns 1 ready, 0 timeout, -1 error or unknown (callers then read). */
int esk_host_stdin_ready(int timeout_ms);
/* Read one line from stdin into `line` (cleared first; the trailing "\n" or
 * "\r\n" is removed). Returns 1 for a line, 0 at end of input, -1 on error. */
int esk_host_stdin_readline(esk_strbuf *line);
/* Write `len` bytes to stdout and flush. */
es_status esk_host_stdout_write(const char *data, size_t len);

/* ---- interruption -------------------------------------------------------- */

/* Install a Ctrl-C (SIGINT / console control) handler that only sets a
 * flag; a second installation is a no-op. */
void esk_host_interrupt_install(void);
/* 1 once Ctrl-C was seen since esk_host_interrupt_install (or _clear). */
int esk_host_interrupted(void);
void esk_host_interrupt_clear(void);

#endif
