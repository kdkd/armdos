/*
 * start.S - IO.SYS entry.  The boot sector loaded the whole file at 0x700
 * and jumped here in SYS mode with r3 = boot drive, r4 = BPB.
 *
 * Like SYSINIT on a PC, the initialisation code does not stay where it was
 * loaded: it is linked to run at the top of conventional memory (0x90100)
 * and copied there first, so that ARMDOS.SYS can be placed immediately after
 * IO.SYS's resident drivers.
 */
        .syntax unified
        .arm
        .section .text.start, "ax"
        .global _start
_start:
        mov     r9, r3
        mov     r10, r4
        ldr     sp, =0x9EF00            /* below the boot sector copy (0x9F000) */

        ldr     r0, =__init_load        /* SYSINIT -> 0x90100 */
        ldr     r1, =__init_start
        ldr     r2, =__init_end
1:      cmp     r1, r2
        ldrlo   r3, [r0], #4
        strlo   r3, [r1], #4
        blo     1b

        mov     r3, #0
        ldr     r1, =__initbss_start
        ldr     r2, =__initbss_end
2:      cmp     r1, r2
        strlo   r3, [r1], #4
        blo     2b
        ldr     r1, =__bss_start        /* resident bss (overlaps where SYSINIT was) */
        ldr     r2, =__bss_end
3:      cmp     r1, r2
        strlo   r3, [r1], #4
        blo     3b

        mov     r0, r9
        mov     r1, r10
        bl      sysinit_main
4:      b       4b

        .ltorg
