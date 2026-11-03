/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* swd.h -- an Arm MCU's serial wire as a debug probe drives it, clock by
 * clock (ARM IHI 0031F, ADIv5.2, B4): its debug ports, DPv1 (an SWJ-DP
 * that starts in JTAG) or DPv2 with multi-drop (the RP2040's two cores and
 * its rescue DP on one wire), each with its MEM-AP. */
#ifndef ESK_MCU_SWD_H
#define ESK_MCU_SWD_H

#include "embsim/types.h"
#include "util/alloc.h"

#define ESK_SWD_PORTS 3

/* one debug port on the wire */
typedef struct esk_swd_port_cfg {
    uint32_t dpidr;
    uint32_t targetid; /* DPv2's TARGETID (TREVISION in its top 4 bits) */
    int version;       /* 1 or 2 */
    int jtag;          /* an SWJ-DP: it starts in JTAG until JTAG-to-SWD */
    int multidrop;     /* TARGETSEL selects it */
    int instance;      /* TINSTANCE, unless the target's instance() gives it */
    int core;          /* the core its MEM-AP reaches; -1: no AP (a rescue DP) */
    int rescue;        /* CDBGPWRUPREQ holds the part in its rescue reset */
    uint32_t ap_idr, ap_base;
} esk_swd_port_cfg;

/* what the ports reach on the part */
typedef struct esk_swd_target {
    void *ctx;
    /* the MEM-AP's access, 1, 2 or 4 bytes: 0 done, else the bus's error */
    int (*mem)(void *ctx, int core, uint32_t addr, int bytes, int write, uint32_t *v);
    /* port `port`'s TINSTANCE now (a part's register may set it), or -1 */
    int (*instance)(void *ctx, int port);
    /* a rescue DP's CDBGPWRUPREQ set (hold) or cleared */
    void (*rescue)(void *ctx, int hold);
    /* port `port` is powered and out of reset: else it does not drive SWDIO
     * and starts again from its power-on state */
    int (*present)(void *ctx, int port);
    /* an access port other than the MEM-AP (APSEL `apsel`, an nRF52's
     * CTRL-AP at 1): 1 when there is one and it took the access; NULL, none */
    int (*ap)(void *ctx, int port, uint32_t apsel, uint32_t reg, int write, uint32_t *v);
    /* the MEM-AP refuses accesses (CSW.DeviceEn reads 0: an nRF52's
     * APPROTECT); NULL, never */
    int (*locked)(void *ctx, int port);
} esk_swd_target;

typedef struct esk_swd esk_swd;

esk_swd *esk_swd_create(esk_alloc *a, const esk_swd_port_cfg *ports, int n, const esk_swd_target *t);
void esk_swd_destroy(esk_alloc *a, esk_swd *w);
/* one SWCLK cycle: the host drives SWDIO to `bit` (`drive`), or lets it go
 * and samples it; the level SWDIO has in the cycle (a pull-up's 1 when
 * nothing drives it) */
int esk_swd_clock(esk_swd *w, int drive, int bit);
/* the ports' power-on state; `keep_rescue`: a rescue DP keeps its own (a
 * rescue reset does not reset the DP that made it) */
void esk_swd_reset(esk_swd *w, int keep_rescue);
/* what a port saw, for the part's observations: packets answered, faults */
void esk_swd_counts(const esk_swd *w, uint64_t *packets, uint64_t *faults, int *selected);

#endif
