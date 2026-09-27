/*
 * gemasm.S - the GEM AES dispatcher and trap entry for ARM-DOS.
 *
 * Replaces GEMASM.A86 (savestat/switchto/dsptch/psetup/gotopgm) and the
 * "gemcod" part of GEMDOSIF.A86 of the GEM/3 AES (Copyright 1987 Digital
 * Research, GNU GPL v2 via Caldera 1999).  The x86 code saved all registers
 * of an interrupted context; the AES only ever switches processes in
 * dsptch(), which C calls like a function, so here only the AAPCS
 * callee-saved registers are kept: a coroutine switch.
 *
 * UDA (struct.h, PTR32): +0 u_insuper, +4 u_spsuper, +8 u_spuser, +12 u_ctx
 * PD: +8 p_uda
 */
        .equ    U_INSUPER, 0
        .equ    U_SPSUPER, 4
        .equ    U_SPUSER, 8
        .equ    U_CTX, 12
        .equ    P_UDA, 8

        .text
        .arm

/* VOID dsptch(VOID): let the dispatcher pick the next process */
        .global dsptch
dsptch:
        ldr     r0, =indisp
        ldrb    r1, [r0]
        cmp     r1, #0
        bxne    lr
        stmfd   sp!, {r4-r11, lr}
        ldr     r2, =rlr
        ldr     r2, [r2]
        ldr     r2, [r2, #P_UDA]
        str     sp, [r2, #U_CTX]
        mov     r1, #1
        strb    r1, [r0]
        ldr     sp, =dspstk_top
        bl      disp                    /* ends with switchto() */
1:      b       1b

/* VOID savestat(UDA *): the state was saved by dsptch */
        .global savestat
savestat:
        bx      lr

/* VOID switchto(UDA *u): resume the process whose context u holds */
        .global switchto
switchto:
        ldr     sp, [r0, #U_CTX]
        ldr     r1, =indisp
        mov     r2, #0
        strb    r2, [r1]
        ldmfd   sp!, {r4-r11, lr}
        bx      lr

/* VOID psetup(PD *p, code): a new process starts at code on its
 * supervisor stack when it is first switched to */
        .global psetup
psetup:
        ldr     r2, [r0, #P_UDA]
        ldr     r3, [r2, #U_SPSUPER]
        bic     r3, r3, #7
        mov     r12, #0
        str     r12, [r3, #-4]!         /* pad (8-byte alignment) */
        str     r1, [r3, #-4]!          /* the code, for start_thunk */
        ldr     r12, =start_thunk
        str     r12, [r3, #-4]!         /* lr */
        mov     r12, #0
        mov     r0, #8
2:      str     r12, [r3, #-4]!         /* r4-r11 = 0 */
        subs    r0, r0, #1
        bne     2b
        str     r3, [r2, #U_CTX]
        bx      lr

start_thunk:
        ldr     r1, [sp], #8
        ldr     lr, =proc_exit
        bx      r1

proc_exit:
        bl      dsptch
        b       proc_exit

/*
 * aes_tramp: an AES call (svc #0xEF, CX = 200) continues here in the
 * caller's mode: the INT EFh handler (gemarm.c) pushed the return address
 * on the caller's stack.  r1 (BX) = the AES parameter block.  Switch to
 * the current process's supervisor stack, run super(), switch back; every
 * register is preserved, as by the x86 gemcod/supret.
 */
        .global aes_tramp
aes_tramp:
        stmfd   sp!, {r0-r12, lr}
        ldr     r4, =rlr
        ldr     r4, [r4]
        ldr     r4, [r4, #P_UDA]
        mov     r5, #1
        str     r5, [r4, #U_INSUPER]
        str     sp, [r4, #U_SPUSER]
        ldr     sp, [r4, #U_SPSUPER]
        bic     sp, sp, #7
        mov     r0, r1
        bl      super
        ldr     r4, =rlr
        ldr     r4, [r4]
        ldr     r4, [r4, #P_UDA]
        mov     r5, #0
        str     r5, [r4, #U_INSUPER]
        ldr     sp, [r4, #U_SPUSER]
        ldmfd   sp!, {r0-r12, lr}
        ldmfd   sp!, {pc}

/* LONG get_sp(VOID) */
        .global get_sp
get_sp:
        mov     r0, sp
        bx      lr

/* VOID enter_prog(entry, psp, base, end, sp): start a desk accessory
 * with the ARM-DOS program entry registers (ARCH.md 8) */
        .global enter_prog
enter_prog:
        ldr     r12, [sp]               /* 5th argument: the stack */
        mov     r4, r0
        mov     r0, r1
        mov     r1, r2
        mov     r2, r3
        mov     sp, r12
        mov     lr, #0
        bx      r4

        .bss
        .align  3
        .space  6144
dspstk_top:
