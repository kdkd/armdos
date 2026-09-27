/*
 * t22.S - X86.EXE's INT 22h (terminate address) trampoline.
 *
 * X86.EXE hooks INT 08h/09h/1Bh/1Ch in the ARM IVT.  However it ends -
 * normally, Ctrl-C, a critical-error Abort, a crash the kernel catches - the
 * kernel continues at the terminate address in our PSP (ARCH.md 16: a
 * changed INT 22h wins), with the parent's registers.  We point it here,
 * put the vectors and the PIC/PIT back, and continue at the real one.
 */
        .syntax unified
        .arm
        .text
        .global x86_t22
        .type   x86_t22, %function
        .balign 4
x86_t22:
        sub     sp, sp, #8              @ room for the target (8-byte aligned)
        stmfd   sp!, {r0-r12, lr}
        mrs     r4, cpsr
        bl      x86_emergency_cleanup
        msr     cpsr_f, r4
        ldr     r0, =x86_orig22
        ldr     r0, [r0]
        str     r0, [sp, #56]
        ldmfd   sp!, {r0-r12, lr}
        ldr     pc, [sp], #8            @ (interworking: bit 0 = Thumb, ARMv5)
        .size   x86_t22, . - x86_t22

        .data
        .global x86_orig22
        .balign 4
x86_orig22:
        .word   0
