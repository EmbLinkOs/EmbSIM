; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 Junior Deogracias
; startup.S -- the ATmega328P's reset and vector table, in the AVR assembly
; both clang's and EmbCC's assemblers take. What a reset handler has to do
; on this part has no C spelling: r1 must be zero (compiled code reads it as
; zero), the stack pointer must be set before the first call, and .data
; (with .rodata, which C reads with `ld` from the data space) is copied
; from flash with `lpm`. A vector the program does not define is weak and
; resolves to 0: an unexpected interrupt restarts the program.

        .section .vectors, "ax", @progbits
        .globl  __vectors
__vectors:
        .weak   __vector_1
        .weak   __vector_2
        .weak   __vector_3
        .weak   __vector_4
        .weak   __vector_5
        .weak   __vector_6
        .weak   __vector_7
        .weak   __vector_8
        .weak   __vector_9
        .weak   __vector_10
        .weak   __vector_11
        .weak   __vector_12
        .weak   __vector_13
        .weak   __vector_14
        .weak   __vector_15
        .weak   __vector_16
        .weak   __vector_17
        .weak   __vector_18
        .weak   __vector_19
        .weak   __vector_20
        .weak   __vector_21
        .weak   __vector_22
        .weak   __vector_23
        .weak   __vector_24
        .weak   __vector_25
        jmp     __reset
        jmp     __vector_1
        jmp     __vector_2
        jmp     __vector_3
        jmp     __vector_4
        jmp     __vector_5
        jmp     __vector_6
        jmp     __vector_7
        jmp     __vector_8
        jmp     __vector_9
        jmp     __vector_10
        jmp     __vector_11
        jmp     __vector_12
        jmp     __vector_13
        jmp     __vector_14
        jmp     __vector_15
        jmp     __vector_16
        jmp     __vector_17
        jmp     __vector_18
        jmp     __vector_19
        jmp     __vector_20
        jmp     __vector_21
        jmp     __vector_22
        jmp     __vector_23
        jmp     __vector_24
        jmp     __vector_25

        .text
        .globl  __reset
__reset:
        clr     r1
        out     0x3f, r1                ; SREG
        ldi     r28, 0xff               ; SP = RAMEND, 0x08ff
        ldi     r29, 0x08
        out     0x3e, r29               ; SPH
        out     0x3d, r28               ; SPL
        ldi     r26, lo8(__data_start)  ; copy .data from flash
        ldi     r27, hi8(__data_start)
        ldi     r30, lo8(__data_load)
        ldi     r31, hi8(__data_load)
        ldi     r24, lo8(__data_end)
        ldi     r25, hi8(__data_end)
1:      cp      r26, r24
        cpc     r27, r25
        breq    2f
        lpm     r0, Z+
        st      X+, r0
        rjmp    1b
2:      ldi     r26, lo8(__bss_start)   ; zero .bss
        ldi     r27, hi8(__bss_start)
        ldi     r24, lo8(__bss_end)
        ldi     r25, hi8(__bss_end)
3:      cp      r26, r24
        cpc     r27, r25
        breq    4f
        st      X+, r1
        rjmp    3b
4:      call    main
5:      rjmp    5b
