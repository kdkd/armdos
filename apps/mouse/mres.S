/*
 * mres.S - MOUSE.COM resident helpers: "INT n" and the call of the user's
 * event handler (INT 33h AX=000Ch). In a .text.unlikely.* section so that
 * they sit in the resident part (see mouse.h).
 */
        .syntax unified
        .arm
        .section .text.unlikely.mouse_asm, "ax", %progbits

.macro  SVCFN   name, num
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

        SVCFN   m_int10, 0x10
        SVCFN   m_int15, 0x15

/* void m_callhandler(uint32_t fn, struct armregs *r)
   The event handler is a "far call" entry (ARCH.md 15): AX=r0 condition
   mask, BX=r1 buttons, CX=r2 x, DX=r3 y, SI=r4 / DI=r5 mickey counts; it may
   clobber anything but sp and r7-r11 and returns with bx lr. */
        .global m_callhandler
        .type   m_callhandler, %function
        .balign 4
m_callhandler:
        push    {r4-r11, lr}
        mov     r12, r0
        ldmia   r1, {r0-r6}
        blx     r12
        pop     {r4-r11, lr}
        bx      lr
        .size   m_callhandler, . - m_callhandler

/* uint32_t m_irqoff(void) / void m_irqrestore(uint32_t cpsr) */
        .global m_irqoff
        .type   m_irqoff, %function
        .balign 4
m_irqoff:
        mrs     r0, cpsr
        orr     r1, r0, #0x80
        msr     cpsr_c, r1
        bx      lr
        .size   m_irqoff, . - m_irqoff

        .global m_irqrestore
        .type   m_irqrestore, %function
        .balign 4
m_irqrestore:
        msr     cpsr_c, r0
        bx      lr
        .size   m_irqrestore, . - m_irqrestore
