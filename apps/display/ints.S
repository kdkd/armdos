/*
 * ints.S - "INT n" for DISPLAY.SYS (a freestanding driver: no libdos).
 *
 *   int int10(struct armregs *r)   and int21, int2f
 *
 * Load r0-r8 from *r, SVC #n, store r0-r8 and the CPSR back, return CF (the
 * contract of the SDK's _armdos_intNN, as apps/ansi/ints.S).
 */
        .syntax unified
        .arm

.macro  SVCFN   name, num
        .section .text.\name, "ax", %progbits
        .global \name
        .type   \name, %function
        .balign 4
\name:
        push    {r4-r11, lr}
        push    {r0}
        ldmia   r0, {r0-r8}
        svc     #\num
        pop     {r12}
        stmia   r12, {r0-r8}
        mrs     r0, cpsr
        str     r0, [r12, #64]
        lsr     r0, r0, #29
        and     r0, r0, #1
        pop     {r4-r11, lr}
        bx      lr
        .size   \name, . - \name
.endm

        SVCFN   int21, 0x21
        SVCFN   int2f, 0x2F
