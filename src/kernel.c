#include "embsim/kernel.h"
#include <stdlib.h>

#define NETS 256
#define DRIVERS 1024
#define EVENTS 8192

struct driver { int net, strength, value; };
struct event { uint64_t at; int driver, value; };
struct es_kernel {
    uint64_t now;
    int nets, drivers, events;
    struct driver driver[DRIVERS];
    struct event event[EVENTS];
};

es_kernel *es_create(void) { return calloc(1, sizeof(es_kernel)); }
void es_destroy(es_kernel *k) { free(k); }

int es_add_net(es_kernel *k)
{
    if (!k) return -1;
    if (k->nets == NETS) return -2;
    return k->nets++;
}

int es_add_driver(es_kernel *k, int net, int strength)
{
    int id;
    if (!k || net < 0 || net >= k->nets || strength < 0 || strength > 1)
        return -1;
    if (k->drivers == DRIVERS) return -2;
    id = k->drivers++;
    k->driver[id].net = net;
    k->driver[id].strength = strength;
    k->driver[id].value = 2;
    return id;
}

int es_schedule(es_kernel *k, uint64_t at, int driver, int value)
{
    int pos;
    if (!k || driver < 0 || driver >= k->drivers || value < 0 || value > 3)
        return -1;
    if (at < k->now) return -3;
    if (k->events == EVENTS) return -2;
    pos = k->events;
    /* Equal-time events retain insertion order. */
    while (pos && k->event[pos - 1].at > at) {
        k->event[pos] = k->event[pos - 1];
        --pos;
    }
    k->event[pos].at = at;
    k->event[pos].driver = driver;
    k->event[pos].value = value;
    ++k->events;
    return 0;
}

int es_advance(es_kernel *k, uint64_t until)
{
    int count = 0, i;
    if (!k) return -1;
    if (until < k->now) return -3;
    while (count < k->events && k->event[count].at <= until) {
        struct event *e = &k->event[count++];
        k->driver[e->driver].value = e->value;
    }
    for (i = count; i < k->events; ++i)
        k->event[i - count] = k->event[i];
    k->events -= count;
    k->now = until;
    return 0;
}

int es_read(const es_kernel *k, int net)
{
    int value = 2, strength = -1, i;
    if (!k || net < 0 || net >= k->nets) return -1;
    for (i = 0; i < k->drivers; ++i) {
        const struct driver *d = &k->driver[i];
        if (d->net != net || d->value == 2 || d->strength < strength) continue;
        if (d->strength > strength) {
            value = d->value;
            strength = d->strength;
        } else if (value != d->value) value = 3;
    }
    return value;
}

uint64_t es_now(const es_kernel *k) { return k ? k->now : 0; }

const char *es_error(int status)
{
    switch (status) {
    case -1: return "invalid argument or identifier";
    case -2: return "kernel capacity exceeded";
    case -3: return "time cannot move backwards";
    default: return "unknown kernel error";
    }
}
