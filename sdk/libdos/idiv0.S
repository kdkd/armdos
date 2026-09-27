/*
 * idiv0.S - integer division by zero raises INT 00h, as the x86 DIV does
 * (ARM has no divide instruction; libgcc's division routines call these).
 * DOS's INT 00h handler prints "Divide overflow" and ends the program; if
 * a program's own handler returns, the division yields 0.
 */
        .syntax unified
        .arm
        .section .text.__aeabi_idiv0, "ax", %progbits
        .global __aeabi_idiv0
        .global __aeabi_ldiv0
        .type   __aeabi_idiv0, %function
        .type   __aeabi_ldiv0, %function
__aeabi_idiv0:
__aeabi_ldiv0:
        push    {r4-r11, lr}
        svc     #0x00
        mov     r0, #0
        mov     r1, #0
        pop     {r4-r11, lr}
        bx      lr
