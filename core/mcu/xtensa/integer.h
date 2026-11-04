/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#ifndef ESK_XTENSA_INTEGER_H
#define ESK_XTENSA_INTEGER_H
#include "embsim/types.h"
/* Experimental LX6 little-endian integer execution kernel. The caller supplies
 * the currently visible A0..A15 window. This is NOT a CPU backend: no fetch,
 * windows, interrupts/exceptions, memory, control flow, FPU, special
 * registers, SMP or timing. Unsupported encodings never change the registers.
 * Wide ADD/SUB/ADDX/SUBX, AND/OR/XOR, MOVI/ADDI/ADDMI, NEG/ABS, MULL,
 * immediate shifts and EXTUI.
 * Also density ADD.N/ADDI.N/MOV.N/MOVI.N. No timing is inferred.
 * Success consumes two or three bytes. */
es_status esk_xtensa_integer_execute(uint32_t a[16], const unsigned char *code,
                                     size_t available, size_t *consumed);
#endif
