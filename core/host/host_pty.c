/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* host_pty.c -- host.h's pseudo-terminals: the port a board's USB-serial
 * bridge presents to the host (ADR-031). POSIX posix_openpt on Linux and
 * macOS; Windows has none (Q16: com0com on request, later). */
#ifndef _WIN32

#define _XOPEN_SOURCE 600
#define _DARWIN_C_SOURCE 1
#include "host/host.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

struct esk_host_pty {
    esk_alloc *a;
    int fd; /* the master side */
    char path[128];
};

static const struct {
    speed_t code;
    uint32_t baud;
} SPEEDS[] = {
    { B50, 50 },       { B75, 75 },       { B110, 110 },     { B134, 134 },     { B150, 150 },
    { B200, 200 },     { B300, 300 },     { B600, 600 },     { B1200, 1200 },   { B1800, 1800 },
    { B2400, 2400 },   { B4800, 4800 },   { B9600, 9600 },   { B19200, 19200 }, { B38400, 38400 },
#ifdef B57600
    { B57600, 57600 },
#endif
#ifdef B115200
    { B115200, 115200 },
#endif
#ifdef B230400
    { B230400, 230400 },
#endif
#ifdef B460800
    { B460800, 460800 },
#endif
#ifdef B500000
    { B500000, 500000 },
#endif
#ifdef B921600
    { B921600, 921600 },
#endif
#ifdef B1000000
    { B1000000, 1000000 },
#endif
#ifdef B2000000
    { B2000000, 2000000 },
#endif
};

static uint32_t baud_of(speed_t code)
{
    size_t i;
    for (i = 0; i < sizeof SPEEDS / sizeof SPEEDS[0]; i++)
        if (SPEEDS[i].code == code)
            return SPEEDS[i].baud;
    /* macOS: speed_t is the rate itself */
    return code > 38400 ? (uint32_t)code : 0;
}

es_status esk_host_pty_open(esk_alloc *a, esk_host_pty **out)
{
    esk_host_pty *p;
    const char *name;
    struct termios t;
    int fd, s;
    *out = NULL;
    fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (fd < 0)
        return ES_EIO;
    if (grantpt(fd) || unlockpt(fd) || !(name = ptsname(fd)) || strlen(name) >= sizeof p->path) {
        close(fd);
        return ES_EIO;
    }
    (void)fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    (void)fcntl(fd, F_SETFD, FD_CLOEXEC);
    /* the line as a USB CDC port starts on Linux (cdc-acm's 9600 8N1); opened
     * and closed once, so that the master reports a hang-up until a program
     * opens the port */
    s = open(name, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (s < 0) {
        close(fd);
        return ES_EIO;
    }
    if (tcgetattr(s, &t) == 0) {
        t.c_cflag = (t.c_cflag & ~(tcflag_t)(CSIZE | PARENB | CSTOPB)) | CS8 | CREAD | HUPCL | CLOCAL;
        (void)cfsetispeed(&t, B9600);
        (void)cfsetospeed(&t, B9600);
        (void)tcsetattr(s, TCSANOW, &t);
    }
    close(s);
    p = (esk_host_pty *)esk_calloc(a, 1, sizeof *p);
    if (!p) {
        close(fd);
        return ES_ENOMEM;
    }
    p->a = a;
    p->fd = fd;
    strcpy(p->path, name);
    *out = p;
    return ES_OK;
}

const char *esk_host_pty_path(const esk_host_pty *p) { return p ? p->path : ""; }

int esk_host_pty_peer(esk_host_pty *p)
{
    struct pollfd pf;
    if (!p)
        return 0;
    pf.fd = p->fd;
    pf.events = POLLIN;
    pf.revents = 0;
    if (poll(&pf, 1, 0) < 0)
        return 0;
    return !(pf.revents & (POLLHUP | POLLERR | POLLNVAL));
}

uint32_t esk_host_pty_baud(esk_host_pty *p)
{
    struct termios t;
    uint32_t baud = 0;
    int s;
    if (!esk_host_pty_peer(p))
        return 0;
    /* the program's own settings are the slave side's: read them there */
    s = open(p->path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (s < 0)
        return 0;
    if (tcgetattr(s, &t) == 0)
        baud = baud_of(cfgetospeed(&t));
    close(s);
    return baud;
}

long esk_host_pty_read(esk_host_pty *p, void *buf, size_t cap)
{
    ssize_t n;
    if (!p || !cap)
        return 0;
    n = read(p->fd, buf, cap);
    if (n >= 0)
        return (long)n;
    /* EIO: no program has the port open */
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR || errno == EIO ? 0 : -1;
}

long esk_host_pty_write(esk_host_pty *p, const void *buf, size_t len)
{
    ssize_t n;
    if (!p || !len)
        return 0;
    n = write(p->fd, buf, len);
    if (n >= 0)
        return (long)n;
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR || errno == EIO ? 0 : -1;
}

void esk_host_pty_flush(esk_host_pty *p)
{
    if (p)
        (void)tcflush(p->fd, TCIOFLUSH);
}

void esk_host_pty_close(esk_host_pty *p)
{
    if (!p)
        return;
    close(p->fd);
    esk_free(p->a, p, sizeof *p);
}

#else /* _WIN32 */

#include "host/host.h"

/* Windows has no pseudo-terminals a serial tool opens; a com0com pair is
 * Q16's answer, installed on request. */
es_status esk_host_pty_open(esk_alloc *a, esk_host_pty **out)
{
    ES_UNUSED(a);
    *out = NULL;
    return ES_EUNSUPPORTED;
}
const char *esk_host_pty_path(const esk_host_pty *p)
{
    ES_UNUSED(p);
    return "";
}
int esk_host_pty_peer(esk_host_pty *p)
{
    ES_UNUSED(p);
    return 0;
}
uint32_t esk_host_pty_baud(esk_host_pty *p)
{
    ES_UNUSED(p);
    return 0;
}
long esk_host_pty_read(esk_host_pty *p, void *buf, size_t cap)
{
    ES_UNUSED(p);
    ES_UNUSED(buf);
    ES_UNUSED(cap);
    return -1;
}
long esk_host_pty_write(esk_host_pty *p, const void *buf, size_t len)
{
    ES_UNUSED(p);
    ES_UNUSED(buf);
    ES_UNUSED(len);
    return -1;
}
void esk_host_pty_flush(esk_host_pty *p) { ES_UNUSED(p); }
void esk_host_pty_close(esk_host_pty *p) { ES_UNUSED(p); }

#endif
