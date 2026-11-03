/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* st_link_priv.h -- the ST-LINK/V2-1 part's state, shared by st_link.c (its
 * commands, SWO) and st_link_usb.c (its USB device and drive). */
#ifndef ESK_MODEL_ST_LINK_PRIV_H
#define ESK_MODEL_ST_LINK_PRIV_H

#include "engine/engine.h"
#include "engine/usb.h"
#include "mcu/mcu.h"
#include "model/registry.h"

#define LOG_CAP 4096
#define TRACE_CAP 4096u /* the probe's trace buffer: OpenOCD's STLINK_TRACE_SIZE */

struct sl_usb;

typedef struct sl_state {
    esk_comp *c;
    int mode;
    int swd;          /* SWD entered: the debug port powered up */
    int nrst_low;     /* the probe pulls NRST low */
    uint8_t rw_status;
    uint32_t rw_addr; /* the address of the last transfer's fault */
    uint32_t dp_select, ap_csw, ap_tar;
    unsigned divisor; /* SWD_SET_FREQ's */
    uint64_t commands;
    /* SWO trace: START_TRACE_RX's rate, the bytes received and not read */
    int trace_on;
    uint32_t trace_hz;
    uint8_t trace[TRACE_CAP];
    uint32_t trace_n;
    uint64_t trace_bytes, trace_lost, trace_garbled;
    struct sl_usb *usb; /* its USB device (st_link_usb.c) */
    char log[LOG_CAP];
    size_t log_len;
} sl_state;

/* the MCU on the SWDIO net, or NULL */
esk_comp *sl_target(sl_state *s);
/* its supply as T_VCC reads it is up */
int sl_powered(sl_state *s);
/* a memory access through the target's MEM-AP: 0, or the probe's error */
int sl_mem(sl_state *s, uint32_t addr, int bytes, int write, uint32_t *v);
/* a line in the probe's log */
void sl_say(sl_state *s, const char *fmt, const char *a);

es_status sl_usb_create(sl_state *s);
void sl_usb_destroy(sl_state *s);
void sl_usb_reset(sl_state *s);
void sl_usb_on_time(sl_state *s, uint32_t token);
void sl_usb_observe(sl_state *s, esk_json *o);

#endif
