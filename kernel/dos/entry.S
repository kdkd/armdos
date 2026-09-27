/*
 * entry.S - ARMDOS.SYS low-level helpers.
 *
 * ARMDOS.SYS is an AR1 image (ARCH.md 8, 14.3): linked at 0, relocated by
 * SYSINIT, entered at dos_entry (init.c).  Here: setjmp/longjmp for the
 * INT 21h dispatcher (a terminate or ^C abort deep inside the kernel unwinds
 * back to the INT 21h entry, as DOS resets its stack) and the XMS-style
 * "far call" of an interrupt handler with a register frame.
 */
        .syntax unified
        .arm

        .section .text.k_setjmp, "ax"
        .global k_setjmp
        .type   k_setjmp, %function
k_setjmp:
        stmia   r0, {r4-r11, sp, lr}
        mov     r0, #0
        bx      lr

        .section .text.k_longjmp, "ax"
        .global k_longjmp
        .type   k_longjmp, %function
k_longjmp:
        ldmia   r0, {r4-r11, sp, lr}
        movs    r0, r1
        moveq   r0, #1
        bx      lr

/* void call_user_sys(uint32_t pc, uint32_t sp) - not used yet */
