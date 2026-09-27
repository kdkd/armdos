/*
 * ints.S - INT 21h for ARMCD.SYS's INIT (a freestanding driver: no libdos).
 *   int int21(struct armregs *r)   load r0-r8, svc #0x21, store r0-r8 + CPSR, return CF
 */
        .syntax unified
        .arm
        .section .text.zinit_int21, "ax", %progbits
        .global int21
        .type   int21, %function
        .balign 4
int21:
        push    {r4-r11, lr}
        push    {r0}
        ldmia   r0, {r0-r8}
        svc     #0x21
        pop     {r12}
        stmia   r12, {r0-r8}
        mrs     r0, cpsr
        str     r0, [r12, #64]
        lsr     r0, r0, #29
        and     r0, r0, #1
        pop     {r4-r11, lr}
        bx      lr
        .size   int21, . - int21
