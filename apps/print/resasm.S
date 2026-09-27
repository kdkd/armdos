/*
 * resasm.S - the resident part's two assembly helpers (in the resident
 * sections, see res.c):
 *   int  r_i21(struct armregs *r)        INT 21h: r0-r8 from *r, back, CF
 *   void r_callstk(void (*fn)(void), void *stacktop)
 *        run the spooler on PRINT's own stack (PRINT_R.ASM's ISTACK): the
 *        timer and idle hooks are entered on whatever stack was in use
 */
        .syntax unified
        .arm
        .section .text.unlikely.prresa, "ax", %progbits
        .balign 4
        .global r_i21
        .type   r_i21, %function
r_i21:
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
        .size   r_i21, . - r_i21

        .global r_callstk
        .type   r_callstk, %function
r_callstk:
        push    {r4, lr}
        mov     r4, sp
        bic     sp, r1, #7
        blx     r0
        mov     sp, r4
        pop     {r4, pc}
        .size   r_callstk, . - r_callstk
