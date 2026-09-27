/*
 * popasm.S - POPUP's stack switch, as every TSR did: the pop-up must not run
 * on whatever stack the interrupted code (or DOS) happened to be using.
 *
 *   void call_on_stack(void (*fn)(void), void *sp)
 */
        .syntax unified
        .arm
        .section .text.call_on_stack, "ax", %progbits
        .global call_on_stack
        .type   call_on_stack, %function
call_on_stack:
        push    {r4, lr}
        mov     r4, sp
        mov     sp, r1
        blx     r0
        mov     sp, r4
        pop     {r4, pc}
        .size   call_on_stack, . - call_on_stack
