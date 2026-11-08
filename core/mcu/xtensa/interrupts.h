/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#ifndef ESK_XTENSA_INTERRUPTS_H
#define ESK_XTENSA_INTERRUPTS_H
#include "embsim/types.h"
struct esk_xtensa_windows;
/* ESP32 LX6 CPU input profile. No SoC interrupt matrix, external peripheral
 * sources, scheduler latency or electrical pin model. CPU counter/compare
 * behavior uses explicitly supplied ticks, never guessed instruction cycles. All transitions occur
 * at explicit instruction boundaries. Initial values are test configuration,
 * not hardware reset qualification. */
typedef struct esk_xtensa_interrupts {
    uint32_t enabled, intenable, lines, latched, waiting, last_level, last_pending;
    uint32_t epc[8], eps[8], excsave[8], vectors[8];
    uint32_t ccount, compare[3];
} esk_xtensa_interrupts;
/* Configuration resets controller/context state; requires exception profile.
 * high_vectors[0..5] are level 2..7 vectors. Level 1 uses kernel/user exception
 * vectors. Do not reconfigure a live interrupt context. */
es_status esk_xtensa_irq_configure(struct esk_xtensa_windows *w,const uint32_t high_vectors[6]);
/* Drive a CPU hardware/internal input. Software inputs 7/29 use INTSET only.
 * Edge/NMI inputs latch rising edges; deassert/reassert to generate another. */
es_status esk_xtensa_irq_line(struct esk_xtensa_windows *w,unsigned input,int asserted);
uint32_t esk_xtensa_irq_pending(const struct esk_xtensa_windows *w);
/* Poll without fetching. event 11..17: accepted level 1..7; zero: none. No
 * maskable source is acknowledged automatically. NMI edge is consumed on entry. */
es_status esk_xtensa_irq_poll(struct esk_xtensa_windows *w,unsigned *event);
es_status esk_xtensa_irq_return(struct esk_xtensa_windows *w,unsigned level);
/* Advance explicitly supplied processor-clock ticks, including WAITI. No ticks
 * are guessed from instruction retirement. Bulk advance latches compare hits;
 * callers split at deadlines when exact event scheduling matters. */
es_status esk_xtensa_clock_advance(struct esk_xtensa_windows *w,uint64_t ticks);
/* Earliest unlatched compare, 1..2^32 ticks away; zero when none/profile off. */
int esk_xtensa_clock_deadline(const struct esk_xtensa_windows *w,uint64_t *ticks);
/* Internal special-register interface for INTERRUPT/INTSET/INTCLEAR/INTENABLE,
 * EPC2..7, EPS2..7, EXCSAVE1..7, CCOUNT and CCOMPARE0..2.
 * INTCLEAR has no defined read value. */
int esk_xtensa_irq_special(unsigned reg);
es_status esk_xtensa_irq_read(const struct esk_xtensa_windows *w,unsigned reg,uint32_t *value);
es_status esk_xtensa_irq_write(struct esk_xtensa_windows *w,unsigned reg,uint32_t value);
#endif
