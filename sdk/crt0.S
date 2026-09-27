/*
 * crt0.S - ARM-DOS program entry (ARCH.md section 8).
 *
 * Entry state: SYS mode, IRQs on,
 *   r0 = PSP (flat), r1 = load base (= PSP+0x100), r2 = end of the memory
 *   block (exclusive), sp = top of the stack area, lr = PSP.
 *
 * The loader zeroes .bss, but we do it again (cheap, and a raw .COM has no
 * loader-known bss). Then hand over to __armdos_start (libdos/startup.c),
 * which never returns.
 */
        .syntax unified
        .arm
        .section .text._start, "ax", %progbits
        .global _start
        .type   _start, %function
_start:
        mov     r4, r0                  @ PSP
        mov     r5, r1                  @ load base
        mov     r6, r2                  @ block end
        bic     sp, sp, #7              @ AAPCS: 8-byte aligned stack
        mov     r7, sp

        /* Zero .bss. The literals are absolute (ABS32) so the loader has
         * already relocated them. */
        ldr     r0, =__bss_start__
        ldr     r1, =__bss_end__
        mov     r2, #0
1:      cmp     r0, r1
        strlo   r2, [r0], #4
        blo     1b

        mov     fp, #0
        mov     lr, #0
        mov     r0, r4
        mov     r1, r5
        mov     r2, r6
        mov     r3, r7
        bl      __armdos_start          @ (psp, base, blockend, stacktop)
2:      b       2b                      @ not reached
        .size   _start, . - _start
        .ltorg

/* _init/_fini are called by newlib's __libc_init_array/__libc_fini_array.
 * ARM EABI uses .init_array/.fini_array, so these are empty. */
        .section .text._init, "ax", %progbits
        .global _init
        .type   _init, %function
_init:  bx      lr
        .size   _init, . - _init

        .section .text._fini, "ax", %progbits
        .global _fini
        .type   _fini, %function
_fini:  bx      lr
        .size   _fini, . - _fini
