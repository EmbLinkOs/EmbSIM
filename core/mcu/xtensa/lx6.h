/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* lx6.h -- the Xtensa LX6 cores of the ESP32 (lx6.c, lx6fp.c), behind
 * the machine's one core interface: the configuration Espressif's
 * core-isa.h gives (esp32_v3_49_prod, LX6.0.3), two of them as the chip's
 * PRO_CPU and APP_CPU. */
#ifndef ESK_MCU_XTENSA_LX6_H
#define ESK_MCU_XTENSA_LX6_H

#include "mcu/mach.h"

/* the cores of model "esp32-lx6" (`cores` 1 or 2): core 0 runs from
 * reset, core 1 is held in reset until the part releases it */
es_status esk_lx6_create(struct esk_mach *m, const char *model, int cores, struct esk_cpu **out);
int esk_lx6_is(const struct esk_cpu *c);
/* the number of cores, and the one whose instruction is in progress (a
 * debugger's: the one that runs next) */
int esk_lx6_cores(struct esk_mach *m);
int esk_lx6_core(struct esk_mach *m);
/* interrupt input `line` (0-31) of core `core` is at `level`: a level
 * input is pending while it is high, an edge input latches a rise
 * (core-isa.h's XCHAL_INTTYPE_MASK_EXTERN_EDGE) */
void esk_lx6_irq(struct esk_mach *m, int core, int line, int level);
/* the core is held in reset (1) or released (0): released, it starts
 * at its reset vector */
void esk_lx6_core_reset(struct esk_mach *m, int core, int held);
/* the core's clock is gated or stalled (1: it stops where it is) for
 * reason `why` (0-7: the part's sources, any of which stops it), or runs */
void esk_lx6_core_stall(struct esk_mach *m, int core, int why, int stalled);
/* the core alone is reset (a software or watchdog CPU reset): it starts
 * again at its reset vector, the devices as they are */
void esk_lx6_core_restart(struct esk_mach *m, int core);
/* the core sees `size` bytes of read-only memory at `base` from `mem`
 * rather than the bus (its own ROM): 0, or -1 (two views at most) */
int esk_lx6_view(struct esk_mach *m, int core, u32 base, u32 size, const u8 *mem);
/* the core's reset vector: 0x40000400 (1) or 0x50000000 (0) */
void esk_lx6_reset_vector(struct esk_mach *m, int core, int sel);
/* the core's program counter, and its stack pointer (a1) */
u32 esk_lx6_pc(struct esk_mach *m, int core);
u32 esk_lx6_sp(struct esk_mach *m, int core);

/* ---- inside: the core's state, for lx6fp.c ------------------------------ */

struct lx6;
/* FP instruction `w` (op0 0 with op1 8, 10 or 11; op0 3): 0 when done,
 * or -1 when not an FP instruction */
int esk_lx6_fp(struct lx6 *c, u32 w);
u32 esk_lx6_fp_rd(struct lx6 *c, int n);
void esk_lx6_fp_wr(struct lx6 *c, int n, u32 v);
u32 *esk_lx6_fregs(struct lx6 *c);
u32 *esk_lx6_fcr(struct lx6 *c);
u32 *esk_lx6_fsr(struct lx6 *c);
u32 esk_lx6_ar(struct lx6 *c, int n);
void esk_lx6_set_ar(struct lx6 *c, int n, u32 v);
u32 esk_lx6_br(struct lx6 *c);
void esk_lx6_set_br(struct lx6 *c, u32 v);
/* a load or store of the core's: 0, or -1 when it took an exception */
int esk_lx6_load(struct lx6 *c, u32 a, int n, u32 *v);
/* the window overflow check of registers up to `hi`: 1 when it was taken */
int esk_lx6_win(struct lx6 *c, u32 hi);
int esk_lx6_store(struct lx6 *c, u32 a, int n, u32 v);
void esk_lx6_illegal(struct lx6 *c);
#endif
