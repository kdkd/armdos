/*
 * karm.S - the few things C cannot say (and Thumb cannot either): CPSR
 * access, wait-for-interrupt, CP15 fault address, INT 21h from SYS mode.
 * ARM code; callable from ARM or Thumb (interworking returns).
 */
        .syntax unified
        .arm

        .macro FUNC name
        .section .text.\name, "ax"
        .global \name
        .type   \name, %function
        .balign 4
\name:
        .endm

FUNC irq_save
        mrs     r0, cpsr
        orr     r1, r0, #0x80
        msr     cpsr_c, r1
        bx      lr

FUNC irq_restore
        msr     cpsr_c, r0
        bx      lr

FUNC irq_on
        mrs     r0, cpsr
        bic     r0, r0, #0x80
        msr     cpsr_c, r0
        bx      lr

FUNC irq_off
        mrs     r0, cpsr
        orr     r0, r0, #0x80
        msr     cpsr_c, r0
        bx      lr

FUNC cpu_wfi
        mov     r0, #0
        mcr     p15, 0, r0, c7, c0, 4
        bx      lr

FUNC get_cpsr
        mrs     r0, cpsr
        bx      lr

FUNC read_far
        mrc     p15, 0, r0, c6, c0, 0
        bx      lr

/* int svc21(struct armregs *r): INT 21h through SVC (caller in SYS mode);
   r0-r8 in and out, returns CF */
FUNC svc21
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

/* division by zero inside the kernel: 0 (programs get INT 00h from the SDK) */
FUNC __aeabi_idiv0
        .global __aeabi_ldiv0
__aeabi_ldiv0:
        mov     r0, #0
        mov     r1, #0
        bx      lr
