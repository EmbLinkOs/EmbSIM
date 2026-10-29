/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* rtos.h -- an RTOS's tasks as the firmware's memory holds them (issue #96):
 * FreeRTOS, through the image's symbols and DWARF (rtos.c). */
#ifndef ESK_MCU_RTOS_H
#define ESK_MCU_RTOS_H

#include "mcu/mach.h"
#include "util/json.h"

/* the FreeRTOS kernel's tasks now: {kernel, tick, started, priorities,
 * layout_from_dwarf, tasks: [{name, tcb, priority, base_priority?, state
 * (running, ready, blocked, suspended, deleted), waits? {kind (delay,
 * notification, queue_receive, queue_send, event_list), index?, queue?,
 * queue_name?, until_tick?, in_ticks?, forever?}, stack_base,
 * stack_free_min?, stack_free_now?, notifications? [{index, state (none,
 * waiting, pending), value}]}]} */
typedef struct esk_rtos esk_rtos;

/* the image's kernel, its symbols and structure layouts read once; NULL
 * with the reason in `why` when the image has no kernel this reads */
esk_rtos *esk_rtos_open(esk_alloc *a, const u8 *elf, size_t len, char *why, size_t cap);
void esk_rtos_close(esk_rtos *r);
/* its tasks in the machine's memory now (the image must stay loaded) */
esk_json *esk_rtos_tasks(esk_rtos *r, struct esk_mach *m);

#endif
