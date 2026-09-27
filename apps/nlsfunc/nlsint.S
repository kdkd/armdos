/*
 * nlsint.S - "INT n" for NLSFUNC's resident part (which may not call the C
 * library): nls_int21(struct armregs *r) and nls_int2f; the contract of the
 * SDK's _armdos_intNN. In .text.unlikely, with the resident code.
 */
        .syntax unified
        .arm

.macro  SVCFN   name, num
        .section .text.unlikely.\name, "ax", %progbits
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

        SVCFN   nls_int21, 0x21

        .section .text.unlikely.nls_zend, "ax", %progbits
        .global nls_res_end
        .balign 16
nls_res_end:
        .word   0
