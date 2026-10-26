/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
.section .text.start,"ax",@progbits
.global _start
_start:
    la sp, __stack_top
    call main
1:  wfi
    j 1b
