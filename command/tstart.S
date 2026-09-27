/*
 * tstart.S - entry of COMMAND's transient part: tran_main(R, reason), called
 * by the resident part on the transient's stack every time the transient
 * is (re)entered; the transient's .bss is zeroed each time.  Also a small
 * setjmp/longjmp (newlib's would pull in the unwinder).
 */
        .syntax unified
        .arm
        .section .text._start, "ax", %progbits
        .global _start
        .type   _start, %function
_start:
        mov     r4, r0
        mov     r5, r1
        ldr     r0, =__bss_start__
        ldr     r1, =__bss_end__
        mov     r2, #0
1:      cmp     r0, r1
        strlo   r2, [r0], #4
        blo     1b
        mov     r0, r4
        mov     r1, r5
        mov     fp, #0
        bl      tran_main
2:      b       2b
        .size   _start, . - _start

/* int cmd_setjmp(uint32_t b[10]) / void cmd_longjmp(uint32_t b[10], int v) */
        .section .text.cmd_setjmp, "ax", %progbits
        .global cmd_setjmp
        .type   cmd_setjmp, %function
cmd_setjmp:
        stmia   r0, {r4-r11}
        str     sp, [r0, #32]
        str     lr, [r0, #36]
        mov     r0, #0
        bx      lr
        .size   cmd_setjmp, . - cmd_setjmp

        .section .text.cmd_longjmp, "ax", %progbits
        .global cmd_longjmp
        .type   cmd_longjmp, %function
cmd_longjmp:
        ldmia   r0, {r4-r11}
        ldr     sp, [r0, #32]
        ldr     lr, [r0, #36]
        movs    r0, r1
        moveq   r0, #1
        bx      lr
        .size   cmd_longjmp, . - cmd_longjmp
