/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* rtos.c -- FreeRTOS task awareness (issue #96), as a debugger's RTOS
 * support reads it (OpenOCD's FreeRTOS.c, SEGGER's): through the image's
 * symbols (pxCurrentTCB, pxReadyTasksLists, xDelayedTaskList1/2,
 * xSuspendedTaskList, xPendingReadyList, xTasksWaitingTermination,
 * xTickCount, xQueueRegistry) and the target's memory, read without side
 * effects. The structures' layouts come from the image's DWARF (the
 * members of tskTaskControlBlock, xLIST, xLIST_ITEM, QueueDefinition),
 * so FreeRTOSConfig.h's choices move nothing; without .debug_info the
 * kernel's default layout for 32-bit ports stands, and the notification
 * state is left out.
 *
 * A task's state is the list its state item is in: a ready list (running
 * when it is pxCurrentTCB), a delayed list (blocked until a tick), the
 * suspended list (suspended, or blocked for ever when it also waits on an
 * event or a notification), the termination list (deleted). What it
 * waits on is its event item's list: a queue's (or semaphore's, or
 * mutex's) waiting-to-receive or waiting-to-send list, named by the queue
 * registry or a symbol; or a notification index in the waiting state. The
 * stack's high-water mark is the run of 0xA5 fill bytes (tskSTACK_FILL_BYTE)
 * above pxStack, which the kernel writes when stack checking, the trace
 * facility or uxTaskGetStackHighWaterMark is on.
 *
 * Omitted: SMP kernels (configNUMBER_OF_CORES > 1: one pxCurrentTCBs per
 * core), 16-bit ports (an AVR's 16-bit pointers and ticks), MPU ports'
 * xMPUSettings without DWARF, event groups' and stream buffers' names
 * (their waits show as an event list's address), stacks growing upward. */
#include "mcu/rtos.h"

#include <string.h>

#include "mcu/dwarf.h"
#include "util/str.h"

#define RTOS_MAX_TASKS 256
#define RTOS_MAX_STACK (64u * 1024u)

enum { T_TOP, T_STATE, T_EVENT, T_PRIO, T_STACK, T_NAME, T_BASE, T_NVAL, T_NSTATE, T_N };
static const char *const tcb_names[T_N] = { "pxTopOfStack", "xStateListItem", "xEventListItem", "uxPriority",
                                            "pxStack", "pcTaskName", "uxBasePriority", "ulNotifiedValue",
                                            "ucNotifyState" };
enum { I_VALUE, I_NEXT, I_PREV, I_OWNER, I_CONT, I_CONT_OLD, I_N };
static const char *const item_names[I_N] = { "xItemValue", "pxNext", "pxPrevious", "pvOwner", "pxContainer",
                                             "pvContainer" };
enum { L_N_ITEMS, L_INDEX, L_END, L_N };
static const char *const list_names[L_N] = { "uxNumberOfItems", "pxIndex", "xListEnd" };
enum { Q_SEND, Q_RECV, Q_N };
static const char *const queue_names[Q_N] = { "xTasksWaitingToSend", "xTasksWaitingToReceive" };

typedef struct rtos {
    struct esk_mach *m;
    const u8 *elf;
    size_t len;
    long tcb[T_N], item[I_N], list[L_N], queue[Q_N];
    int dwarf;
    u32 list_size;
    u32 ready, nready, delayed1, delayed2, suspended, pending, terminating, current;
    u32 current_at, tick_at; /* pxCurrentTCB's and xTickCount's addresses */
    u32 registry, nregistry;
} rtos;

static int rd32(rtos *r, u32 a, u32 *v)
{
    return esk_bus_debug_read(&r->m->bus, a, 4, v) == 0;
}

static int rd8(rtos *r, u32 a, u32 *v)
{
    return esk_bus_debug_read(&r->m->bus, a, 1, v) == 0;
}

static u32 sym(rtos *r, const char *name, u32 *size)
{
    u32 v = 0, sz = 0;
    if (!esk_elf_symbol_size(r->elf, r->len, name, &v, &sz))
        return 0;
    if (size)
        *size = sz;
    return v;
}

/* the kernel's default layout on a 32-bit port without the MPU and without
 * list integrity checks */
static void defaults(rtos *r)
{
    static const long tcb[T_N] = { 0, 4, 24, 44, 48, 52, -1, -1, -1 };
    static const long item[I_N] = { 0, 4, 8, 12, 16, -1 };
    static const long list[L_N] = { 0, 4, 8 };
    static const long queue[Q_N] = { 16, 36 };
    memcpy(r->tcb, tcb, sizeof tcb);
    memcpy(r->item, item, sizeof item);
    memcpy(r->list, list, sizeof list);
    memcpy(r->queue, queue, sizeof queue);
}

static void layouts(rtos *r, esk_alloc *a)
{
    long t[T_N], it[I_N], l[L_N], q[Q_N];
    int i;
    defaults(r);
    if (esk_dwarf_members(a, r->elf, r->len, "tskTaskControlBlock", tcb_names, T_N, t) > 0 && t[T_STATE] >= 0) {
        memcpy(r->tcb, t, sizeof t);
        r->dwarf = 1;
    }
    if (esk_dwarf_members(a, r->elf, r->len, "xLIST_ITEM", item_names, I_N, it) > 0 && it[I_NEXT] >= 0) {
        if (it[I_CONT] < 0)
            it[I_CONT] = it[I_CONT_OLD]; /* before V10.4: pvContainer */
        memcpy(r->item, it, sizeof it);
    }
    if (esk_dwarf_members(a, r->elf, r->len, "xLIST", list_names, L_N, l) > 0 && l[L_END] >= 0)
        memcpy(r->list, l, sizeof l);
    if (esk_dwarf_members(a, r->elf, r->len, "QueueDefinition", queue_names, Q_N, q) > 0 && q[Q_RECV] >= 0)
        memcpy(r->queue, q, sizeof q);
    for (i = 0; i < T_N; i++)
        if (!r->dwarf && (i == T_NVAL || i == T_NSTATE || i == T_BASE))
            r->tcb[i] = -1;
}

/* the queue whose waiting list `cont` is, by the registry's name or a
 * symbol; "" when neither names it */
static void queue_name(rtos *r, u32 qbase, char *out, size_t cap)
{
    u32 i, off = 0;
    out[0] = 0;
    for (i = 0; r->registry && i < r->nregistry; i++) {
        u32 name = 0, h = 0, k;
        if (!rd32(r, r->registry + i * 8u, &name) || !rd32(r, r->registry + i * 8u + 4u, &h) || h != qbase || !name)
            continue;
        for (k = 0; k + 1 < cap && k < 31; k++) {
            u32 c = 0;
            if (!rd8(r, name + k, &c) || !c)
                break;
            out[k] = (char)c;
        }
        out[k] = 0;
        return;
    }
    if (esk_elf_symbol_at(r->elf, r->len, qbase, out, cap, &off) && off)
        out[0] = 0; /* inside a symbol, not at a queue's start */
}

static void wait_on_list(rtos *r, u32 cont, esk_alloc *a, esk_json *w)
{
    int k;
    char name[48];
    for (k = 0; k < Q_N; k++) {
        u32 qbase = cont - (u32)r->queue[k], n = 0;
        /* a queue's list: its uxNumberOfItems is small and its end marker
         * points back into the list */
        if (r->queue[k] < 0 || !rd32(r, cont + (u32)r->list[L_N_ITEMS], &n) || n > RTOS_MAX_TASKS)
            continue;
        queue_name(r, qbase, name, sizeof name);
        if (name[0] || k == Q_RECV) {
            esk_json_set_string(a, w, "kind", k == Q_RECV ? "queue_receive" : "queue_send");
            esk_json_set_integer(a, w, "queue", (int64_t)qbase);
            if (name[0])
                esk_json_set_string(a, w, "queue_name", name);
            return;
        }
    }
    esk_json_set_string(a, w, "kind", "event_list");
    esk_json_set_integer(a, w, "list", (int64_t)cont);
}

static void task_json(rtos *r, esk_alloc *a, u32 tcb, u32 state_list, u32 tick, esk_json *arr)
{
    esk_json *t = esk_json_new_object(a), *w = NULL, *notes = NULL;
    char name[33];
    u32 v = 0, prio = 0, stack = 0, cont = 0, wake = 0, k, nidx = 0;
    const char *state = "ready";
    int waiting_note = -1;
    if (!t)
        return;
    for (k = 0; k < sizeof name - 1; k++) {
        if (r->tcb[T_NAME] < 0 || !rd8(r, tcb + (u32)r->tcb[T_NAME] + k, &v) || !v)
            break;
        name[k] = (char)v;
    }
    name[k] = 0;
    esk_json_set_string(a, t, "name", name);
    esk_json_set_integer(a, t, "tcb", (int64_t)tcb);
    if (rd32(r, tcb + (u32)r->tcb[T_PRIO], &prio))
        esk_json_set_integer(a, t, "priority", (int64_t)prio);
    if (r->tcb[T_BASE] >= 0 && rd32(r, tcb + (u32)r->tcb[T_BASE], &v) && v != prio)
        esk_json_set_integer(a, t, "base_priority", (int64_t)v);
    /* the notifications: as many as the array holds between its two members */
    if (r->tcb[T_NVAL] >= 0 && r->tcb[T_NSTATE] > r->tcb[T_NVAL])
        nidx = (u32)(r->tcb[T_NSTATE] - r->tcb[T_NVAL]) / 4u;
    if (nidx > 16)
        nidx = 16;
    for (k = 0; k < nidx; k++) {
        u32 st = 0, val = 0;
        esk_json *n;
        if (!rd8(r, tcb + (u32)r->tcb[T_NSTATE] + k, &st) || !rd32(r, tcb + (u32)r->tcb[T_NVAL] + 4u * k, &val))
            continue;
        if (!notes)
            notes = esk_json_new_array(a);
        n = esk_json_new_object(a);
        esk_json_set_integer(a, n, "index", (int64_t)k);
        esk_json_set_string(a, n, "state", st == 1 ? "waiting" : st == 2 ? "pending" : "none");
        esk_json_set_integer(a, n, "value", (int64_t)val);
        esk_json_array_push(a, notes, n);
        if (st == 1 && waiting_note < 0)
            waiting_note = (int)k;
    }
    (void)rd32(r, tcb + (u32)r->tcb[T_EVENT] + (u32)r->item[I_CONT], &cont);
    (void)rd32(r, tcb + (u32)r->tcb[T_STATE] + (u32)r->item[I_VALUE], &wake);
    if (tcb == r->current)
        state = "running";
    else if (state_list == r->delayed1 || state_list == r->delayed2)
        state = "blocked";
    else if (state_list == r->suspended)
        state = cont || waiting_note >= 0 ? "blocked" : "suspended";
    else if (state_list == r->terminating)
        state = "deleted";
    esk_json_set_string(a, t, "state", state);
    if (!strcmp(state, "blocked")) {
        w = esk_json_new_object(a);
        if (cont)
            wait_on_list(r, cont, a, w);
        else if (waiting_note >= 0) {
            esk_json_set_string(a, w, "kind", "notification");
            esk_json_set_integer(a, w, "index", waiting_note);
        } else
            esk_json_set_string(a, w, "kind", "delay");
        if (state_list != r->suspended) {
            esk_json_set_integer(a, w, "until_tick", (int64_t)wake);
            esk_json_set_integer(a, w, "in_ticks", (int64_t)(u32)(wake - tick));
        } else {
            esk_json_set_bool(a, w, "forever", 1);
        }
        esk_json_object_set(a, t, "waits", w);
    }
    if (rd32(r, tcb + (u32)r->tcb[T_STACK], &stack) && stack) {
        u32 free_b = 0, b = 0;
        esk_json_set_integer(a, t, "stack_base", (int64_t)stack);
        while (free_b < RTOS_MAX_STACK && rd8(r, stack + free_b, &b) && b == 0xA5u)
            free_b++;
        if (free_b)
            esk_json_set_integer(a, t, "stack_free_min", (int64_t)free_b); /* the high-water mark, bytes */
        if (rd32(r, tcb + (u32)r->tcb[T_TOP], &v) && v > stack)
            esk_json_set_integer(a, t, "stack_free_now", (int64_t)(v - stack));
    }
    if (notes)
        esk_json_object_set(a, t, "notifications", notes);
    esk_json_array_push(a, arr, t);
}

/* the TCBs a list holds, through its items' owners */
static int walk(rtos *r, esk_alloc *a, u32 list, u32 tick, u32 *seen, int *nseen, esk_json *arr)
{
    u32 end = list + (u32)r->list[L_END], it = 0;
    int guard = 0;
    if (!list || !rd32(r, end + (u32)r->item[I_NEXT], &it))
        return 0;
    while (it && it != end && guard++ < RTOS_MAX_TASKS) {
        u32 owner = 0;
        int k;
        if (!rd32(r, it + (u32)r->item[I_OWNER], &owner) || !owner)
            return -1;
        for (k = 0; k < *nseen && seen[k] != owner; k++)
            ;
        if (k == *nseen && *nseen < RTOS_MAX_TASKS) {
            seen[(*nseen)++] = owner;
            task_json(r, a, owner, list, tick, arr);
        }
        if (!rd32(r, it + (u32)r->item[I_NEXT], &it))
            return -1;
    }
    return 0;
}

struct esk_rtos {
    esk_alloc *a;
    rtos r;
};

esk_rtos *esk_rtos_open(esk_alloc *a, const u8 *elf, size_t len, char *why, size_t cap)
{
    esk_rtos *o;
    rtos *r;
    u32 size = 0;
    if (why && cap)
        why[0] = 0;
    if (!elf || len < 52 || memcmp(elf, "\177ELF", 4) != 0 || elf[4] != 1) {
        if (why) esk_snprintf(why, cap, "the image is not a 32-bit ELF");
        return NULL;
    }
    o = (esk_rtos *)esk_calloc(a, 1, sizeof *o);
    if (!o)
        return NULL;
    o->a = a;
    r = &o->r;
    r->elf = elf;
    r->len = len;
    r->current_at = r->current = sym(r, "pxCurrentTCB", NULL);
    r->ready = sym(r, "pxReadyTasksLists", &size);
    if (!r->current || !r->ready) {
        if (why) esk_snprintf(why, cap, "the image has no FreeRTOS kernel (pxCurrentTCB, pxReadyTasksLists)");
        esk_free(a, o, sizeof *o);
        return NULL;
    }
    layouts(r, a);
    {
        u32 lsz = 0; /* a List_t's size: xDelayedTaskList1 is one */
        (void)sym(r, "xDelayedTaskList1", &lsz);
        r->list_size = lsz ? lsz : (u32)r->list[L_END] + 3u * 4u; /* else the end marker as a mini item */
    }
    r->nready = size && r->list_size ? size / r->list_size : 0;
    if (!r->nready || r->nready > 64) {
        if (why) esk_snprintf(why, cap, "pxReadyTasksLists has no size (configMAX_PRIORITIES unknown)");
        esk_free(a, o, sizeof *o);
        return NULL;
    }
    r->delayed1 = sym(r, "xDelayedTaskList1", NULL);
    r->delayed2 = sym(r, "xDelayedTaskList2", NULL);
    r->suspended = sym(r, "xSuspendedTaskList", NULL);
    r->pending = sym(r, "xPendingReadyList", NULL);
    r->terminating = sym(r, "xTasksWaitingTermination", NULL);
    r->tick_at = sym(r, "xTickCount", NULL);
    r->registry = sym(r, "xQueueRegistry", &size);
    r->nregistry = r->registry ? size / 8u : 0;
    return o;
}

void esk_rtos_close(esk_rtos *o)
{
    if (o)
        esk_free(o->a, o, sizeof *o);
}

esk_json *esk_rtos_tasks(esk_rtos *o, struct esk_mach *m)
{
    rtos *r = &o->r;
    esk_alloc *a = o->a;
    esk_json *root, *tasks;
    u32 tick = 0, i, seen[RTOS_MAX_TASKS], cur = 0;
    int nseen = 0;
    r->m = m;
    (void)rd32(r, r->tick_at, &tick);
    if (!rd32(r, r->current_at, &cur))
        return NULL;
    r->current = cur;
    root = esk_json_new_object(a);
    tasks = esk_json_new_array(a);
    if (!root || !tasks) {
        esk_json_free(a, root);
        esk_json_free(a, tasks);
        return NULL;
    }
    esk_json_set_string(a, root, "kernel", "FreeRTOS");
    esk_json_set_integer(a, root, "tick", (int64_t)tick);
    esk_json_set_bool(a, root, "started", cur != 0);
    esk_json_set_integer(a, root, "priorities", (int64_t)r->nready);
    esk_json_set_bool(a, root, "layout_from_dwarf", r->dwarf);
    for (i = r->nready; i-- > 0;)
        (void)walk(r, a, r->ready + i * r->list_size, tick, seen, &nseen, tasks);
    (void)walk(r, a, r->pending, tick, seen, &nseen, tasks);
    (void)walk(r, a, r->delayed1, tick, seen, &nseen, tasks);
    (void)walk(r, a, r->delayed2, tick, seen, &nseen, tasks);
    (void)walk(r, a, r->suspended, tick, seen, &nseen, tasks);
    (void)walk(r, a, r->terminating, tick, seen, &nseen, tasks);
    esk_json_object_set(a, root, "tasks", tasks);
    return root;
}
