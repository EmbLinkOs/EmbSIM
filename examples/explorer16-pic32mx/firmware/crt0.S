/* SPDX-License-Identifier: Apache-2.0 */
/* crt0.S -- the PIC32MX's start, from the reset vector in boot flash
 * (0xBFC00000): the stack, EBASE at the program's vectors (.vectors, in
 * program flash) with 32-byte vector spacing, Cause.IV for the vectored
 * interrupts, Status out of its boot state (BEV and ERL cleared), .data
 * copied from flash, .bss cleared, then main.
 *
 * Each vector (EBASE + 0x200 + 32 x n) jumps to one handler with its
 * number in k0: it saves the registers C code may change, calls
 * isr(vector), restores them and returns with eret. Interrupts do not
 * nest (Status.EXL stays set), and none runs at priority 7, which uses the
 * shadow register set (DEVCFG3.FSRSSEL). An exception that is not an
 * interrupt stops at sdbbp. */
        .set    noreorder
        .set    noat

        .section .reset, "ax", @progbits
        .globl  _reset
        .ent    _reset
_reset:
        la      $sp, __stack_top
        move    $gp, $zero
        la      $t0, __ebase
        mtc0    $t0, $15, 1             /* EBASE */
        li      $t0, 0x20
        mtc0    $t0, $12, 1             /* IntCtl.VS: 32 bytes */
        lui     $t0, 0x0080
        mtc0    $t0, $13, 0             /* Cause.IV */
        mtc0    $zero, $12, 0           /* Status: BEV, ERL, IE clear */
        ehb
        /* .data from flash */
        la      $t0, __data_load
        la      $t1, __data_start
        la      $t2, __data_end
1:      beq     $t1, $t2, 2f
        nop
        lw      $t3, 0($t0)
        sw      $t3, 0($t1)
        addiu   $t0, $t0, 4
        b       1b
        addiu   $t1, $t1, 4
2:      la      $t1, __bss_start
        la      $t2, __bss_end
3:      beq     $t1, $t2, 4f
        nop
        sw      $zero, 0($t1)
        b       3b
        addiu   $t1, $t1, 4
4:      la      $t0, main
        jalr    $t0
        nop
5:      b       5b
        nop
        .end    _reset

        /* EBASE + 0x180: an exception, not an interrupt */
        .section .vector_general, "ax", @progbits
        sdbbp
        nop

        /* EBASE + 0x200: the vectors this example uses */
        .section .vectors, "ax", @progbits
        .macro  VECTOR n
        .org    \n * 32
        j       isr_entry
        li      $k0, \n
        .endm
        VECTOR  4
        VECTOR  26
        VECTOR  32

        .text
        .ent    isr_entry
isr_entry:
        addiu   $sp, $sp, -88
        sw      $at, 0($sp)
        sw      $v0, 4($sp)
        sw      $v1, 8($sp)
        sw      $a0, 12($sp)
        sw      $a1, 16($sp)
        sw      $a2, 20($sp)
        sw      $a3, 24($sp)
        sw      $t0, 28($sp)
        sw      $t1, 32($sp)
        sw      $t2, 36($sp)
        sw      $t3, 40($sp)
        sw      $t4, 44($sp)
        sw      $t5, 48($sp)
        sw      $t6, 52($sp)
        sw      $t7, 56($sp)
        sw      $t8, 60($sp)
        sw      $t9, 64($sp)
        sw      $ra, 68($sp)
        mfhi    $t0
        sw      $t0, 72($sp)
        mflo    $t0
        sw      $t0, 76($sp)
        la      $t0, isr
        jalr    $t0
        move    $a0, $k0                /* the vector, in the delay slot */
        lw      $t0, 76($sp)
        mtlo    $t0
        lw      $t0, 72($sp)
        mthi    $t0
        lw      $ra, 68($sp)
        lw      $t9, 64($sp)
        lw      $t8, 60($sp)
        lw      $t7, 56($sp)
        lw      $t6, 52($sp)
        lw      $t5, 48($sp)
        lw      $t4, 44($sp)
        lw      $t3, 40($sp)
        lw      $t2, 36($sp)
        lw      $t1, 32($sp)
        lw      $t0, 28($sp)
        lw      $a3, 24($sp)
        lw      $a2, 20($sp)
        lw      $a1, 16($sp)
        lw      $a0, 12($sp)
        lw      $v1, 8($sp)
        lw      $v0, 4($sp)
        lw      $at, 0($sp)
        addiu   $sp, $sp, 88
        eret
        nop
        .end    isr_entry
