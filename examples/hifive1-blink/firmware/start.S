# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# start.S -- the HiFive1's user-code entry, at 0x20010000 where the
# bootloader jumps: the global and stack pointers, .data copied from flash,
# .bss zeroed, the trap vector, then main.
        .section .text.init, "ax", @progbits
        .globl  _start
_start:
        .option push
        .option norelax
        la      gp, __global_pointer$
        .option pop
        la      sp, __stack_top
        la      t0, __data_load
        la      t1, __data_start
        la      t2, __data_end
1:      bgeu    t1, t2, 2f
        lw      t3, 0(t0)
        sw      t3, 0(t1)
        addi    t0, t0, 4
        addi    t1, t1, 4
        j       1b
2:      la      t1, __bss_start
        la      t2, __bss_end
3:      bgeu    t1, t2, 4f
        sw      zero, 0(t1)
        addi    t1, t1, 4
        j       3b
4:      la      t0, trap_entry
        csrw    mtvec, t0
        call    main
5:      wfi
        j       5b
