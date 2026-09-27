/*
 * tramp.S - where an INT EFh (svc #0xEF) VDI call continues (ARM-DOS GEM).
 *
 * The INT EFh handler (gdos.c) pushed the caller's return address on the
 * caller's stack and let the SVC return here instead, in the caller's mode.
 * Every register is preserved, like a return from INT on the PC; r3 (DX) is
 * the parameter block.
 */
        .text
        .arm
        .global vdi_tramp
vdi_tramp:
        stmfd   sp!, {r0-r12, lr}
        mov     r4, sp
        bic     sp, sp, #7              /* AAPCS: 8-byte aligned stack */
        mov     r0, r3
        bl      vdi_entry
        mov     sp, r4
        ldmfd   sp!, {r0-r12, lr}
        ldmfd   sp!, {pc}               /* the pushed return address */
