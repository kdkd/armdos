/*
 * crashasm.S - the instructions CRASH.EXE uses to upset the ARM926.
 *
 * Copyright (C) 1989 Europa Micro Systems (ARM-DOS project).
 */
        .syntax unified
        .arm

        .macro FUNC name
        .section .text.\name, "ax", %progbits
        .global \name
        .type   \name, %function
        .align  2
\name:
        .endm

/* E7F198F8h: cond 1110, bits 27-20 = 0111 1111, bits 7-4 = 1111 - the ARM
 * ARM's "architecturally undefined" space (A3.16.5), which no ARM will ever
 * implement (later assemblers spell it UDF #1988h). The CPU takes the
 * undefined instruction exception: INT 06h. (A VFP instruction no longer
 * does: the VFP9-S is fitted and the BIOS enables it, ARCH.md 2.) */
FUNC do_undefined
        mov     r0, #0x06
        ldr     r1, =0x1988
        .word   0xE7F198F8              @ udf #0x1988 (permanently undefined)
        bx      lr

/* Load from 20000000h: nothing is decoded there (RAM ends at 16 MB, the ISA
 * I/O window is at 10000000h) -> data abort, INT 0Dh. */
FUNC do_data_abort
        mov     r0, #0x0D
        mov     r1, #0x20000000
        ldr     r2, [r1]
        bx      lr

/* Jump to 30000000h: the instruction fetch finds no memory -> prefetch
 * abort, INT 0Eh. LR shows where we came from. */
FUNC do_prefetch_abort
        mov     r0, #0x0E
        mov     r12, #0x30000000
        blx     r12
        bx      lr

/* uint32_t do_unaligned(const void *p) - a word load from an address that is
 * not a multiple of 4. ARMv5 has no alignment fault here: it loads the aligned
 * word and rotates it so the addressed byte ends up in bits 0-7. */
FUNC do_unaligned
        ldr     r0, [r0]
        bx      lr

/* uint32_t get_sp(void) */
FUNC get_sp
        mov     r0, sp
        bx      lr

/* Stray write to the interrupt vector table: INT 21h's vector becomes a wild
 * address, then call DOS. The BIOS's SVC dispatcher jumps there in SVC mode ->
 * prefetch abort inside "the operating system" -> the BIOS crash screen. */
FUNC do_vector_smash
        mov     r1, #0x84               @ &IVT[21h]
        ldr     r2, =0xDEADBEE0
        str     r2, [r1]
        mov     r0, #0x0900             @ AH=09h, print string (never gets there)
        svc     #0x21
        bx      lr
        .ltorg
