/*
 * intr.S - raising software interrupts ("INT n" = SVC #n, ARCH.md section 5).
 *
 *   int _armdos_intNN(struct armregs *r);
 *   int _armdos_intr(int n, struct armregs *r);
 *
 * Load r0-r8 from *r, SVC #n, store r0-r8 and the CPSR back into *r, return
 * the carry flag. The SVC number is an immediate, so the generic form jumps
 * into a 256-entry table of "svc #n ; b done" pairs (8 bytes each, 2 KB, only
 * linked when _armdos_intr is used). Each routine lives in its own section so
 * --gc-sections drops what a program does not use.
 */
        .syntax unified
        .arm

        .equ    CPSR_OFF, 64

/* Common tail: *(sp) = struct pointer. Store results, return carry. */
.macro  SVC_EPILOGUE
        pop     {r12}                   @ struct armregs *
        stmia   r12, {r0-r8}
        mrs     r0, cpsr
        str     r0, [r12, #CPSR_OFF]
        lsr     r0, r0, #29
        and     r0, r0, #1
        pop     {r4-r11, lr}
        bx      lr
.endm

.macro  SVCFN   name, num
        .section .text.\name, "ax", %progbits
        .global \name
        .type   \name, %function
        .balign 4
\name:
        push    {r4-r11, lr}
        push    {r0}
        ldmia   r0, {r0-r8}
        svc     #\num
        SVC_EPILOGUE
        .size   \name, . - \name
.endm

        SVCFN   _armdos_int10, 0x10
        SVCFN   _armdos_int13, 0x13
        SVCFN   _armdos_int16, 0x16
        SVCFN   _armdos_int1a, 0x1A
        SVCFN   _armdos_int21, 0x21
        SVCFN   _armdos_int2f, 0x2F
        SVCFN   _armdos_int33, 0x33

/* int _armdos_intr(int n, struct armregs *r) */
        .section .text._armdos_intr, "ax", %progbits
        .global _armdos_intr
        .type   _armdos_intr, %function
        .balign 4
_armdos_intr:
        push    {r4-r11, lr}
        push    {r1}
        and     r0, r0, #0xFF
        adr     r12, 1f
        add     r12, r12, r0, lsl #3    @ &table[n]
        ldmia   r1, {r0-r8}
        mov     pc, r12
2:      SVC_EPILOGUE
        .balign 4
1:
        .irp    hi, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
        .irp    lo, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
        svc     #(\hi * 16 + \lo)
        b       2b
        .endr
        .endr
        .size   _armdos_intr, . - _armdos_intr

/* uint32_t _armdos_farcall(void *entry, struct armregs *r)
 * For driver entry points such as XMS: registers in, registers out.
 * The callee may clobber anything but sp; we preserve r4-r11 ourselves. */
        .section .text._armdos_farcall, "ax", %progbits
        .global _armdos_farcall
        .type   _armdos_farcall, %function
        .balign 4
_armdos_farcall:
        push    {r4-r11, lr}
        push    {r1}
        mov     r12, r0
        ldmia   r1, {r0-r6}
        blx     r12
        pop     {r12}
        stmia   r12, {r0-r6}
        pop     {r4-r11, lr}
        bx      lr
        .size   _armdos_farcall, . - _armdos_farcall
