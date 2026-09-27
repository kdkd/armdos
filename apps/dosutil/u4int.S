/*
 * u4int.S - "INT n" for the few interrupts the DOS 4 utilities raise besides
 * INT 21h (the SDK's _armdos_intr would add a 2 KB table).  Same convention
 * as the SDK's _armdos_intNN: load r0-r8 from *r, SVC #n, store r0-r8 and
 * the CPSR back, return CF.
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
        SVCFN   u4_int17, 0x17
        SVCFN   u4_int25, 0x25
        SVCFN   u4_int26, 0x26
        SVCFN   u4_int28, 0x28
        SVCFN   u4_int2f, 0x2F
        SVCFN   u4_int10, 0x10
        SVCFN   u4_int16, 0x16
