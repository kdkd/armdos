/*
 * ARM/AT BIOS — reset, exception vectors and the interrupt dispatcher.
 *
 * This is where "INT n" becomes ARM.  An x86 PC raises software interrupt n
 * with the INT instruction and the CPU fetches the handler from the vector
 * table at 0000:n*4.  Here the program executes `SVC #n`, the ARM traps to the
 * SVC vector, and this code reads the 8-bit number out of the instruction,
 * fetches the handler from the SAME table at physical address n*4, and calls it
 * with a pointer to the caller's saved registers (struct armregs, ARCH.md §5).
 * Hardware interrupts take the same road: IRQ n becomes INT 08h+n (or 70h+n-8)
 * exactly as the 8259 is programmed on a PC.
 *
 * Frame layout (must match struct armregs):
 *   0..48  r0-r12   52 sp   56 lr   60 pc   64 cpsr   68 intno
 *   72     ksp  (the SVC stack pointer to restore — private)
 *   76     pad
 */

        .equ F_SP,    52
        .equ F_LR,    56
        .equ F_PC,    60
        .equ F_CPSR,  64
        .equ F_INT,   68
        .equ F_KSP,   72
        .equ F_SIZE,  80

        .equ MODE_USR, 0x10
        .equ MODE_FIQ, 0x11
        .equ MODE_IRQ, 0x12
        .equ MODE_SVC, 0x13
        .equ MODE_ABT, 0x17
        .equ MODE_UND, 0x1B
        .equ MODE_SYS, 0x1F
        .equ I_BIT,    0x80
        .equ F_BIT,    0x40
        .equ T_BIT,    0x20
        .equ C_FLAG,   0x20000000

        .equ IO_BASE,  0x10000000

/* ------------------------------------------------------------------ vectors */

        .section .vectors, "ax"
        .arm
        .global _vectors
_vectors:
        ldr     pc, v_reset
        ldr     pc, v_undef
        ldr     pc, v_svc
        ldr     pc, v_pabt
        ldr     pc, v_dabt
        ldr     pc, v_reset             /* reserved */
        ldr     pc, v_irq
        ldr     pc, v_fiq
v_reset: .word  reset_entry
v_undef: .word  undef_entry
v_svc:   .word  svc_entry
v_pabt:  .word  pabt_entry
v_dabt:  .word  dabt_entry
v_irq:   .word  irq_entry
v_fiq:   .word  fiq_entry

/* -------------------------------------------------------------------- reset */

        .text
        .arm
        .global reset_entry
reset_entry:
        msr     cpsr_c, #(MODE_ABT | I_BIT | F_BIT)
        ldr     sp, =__abt_stack_top
        msr     cpsr_c, #(MODE_UND | I_BIT | F_BIT)
        ldr     sp, =__und_stack_top
        msr     cpsr_c, #(MODE_IRQ | I_BIT | F_BIT)
        ldr     sp, =__irq_stack_top
        msr     cpsr_c, #(MODE_FIQ | I_BIT | F_BIT)
        ldr     sp, =__irq_stack_top
        msr     cpsr_c, #(MODE_SYS | I_BIT | F_BIT)
        ldr     sp, =0x7C00
        msr     cpsr_c, #(MODE_SVC | I_BIT | F_BIT)
        ldr     sp, =__svc_stack_top

        /* enable the VFP9-S floating-point coprocessor: FPEXC.EN (it is off at reset) */
        mov     r0, #0x40000000
        .word   0xEEE80A10              /* fmxr fpexc, r0 (vmsr), encoded so the soft-float build needs no .fpu */

        /* copy .data from ROM, zero .bss (both live in the HMA) */
        ldr     r0, =__data_load
        ldr     r1, =__data_start
        ldr     r2, =__data_end
1:      cmp     r1, r2
        ldrlo   r3, [r0], #4
        strlo   r3, [r1], #4
        blo     1b
        ldr     r1, =__bss_start
        ldr     r2, =__bss_end
        mov     r3, #0
2:      cmp     r1, r2
        strlo   r3, [r1], #4
        blo     2b

        bl      bios_main
3:      b       3b

/*
 * void bios_enter_sys(uint32_t pc, uint32_t sp, uint32_t r3, uint32_t irq_on)
 *   Leave the BIOS for good: reset the SVC stack, and continue at `pc` in SYS
 *   mode with the given stack and r3 (the boot drive in DL).  Used by INT 19h.
 */
        .global bios_enter_sys
bios_enter_sys:
        msr     cpsr_c, #(MODE_SVC | I_BIT | F_BIT)
        ldr     sp, =__svc_stack_top
        mov     r12, r0
        mov     r0, #MODE_SYS | F_BIT
        cmp     r3, #0
        orreq   r0, r0, #I_BIT
        msr     spsr_cxsf, r0
        mov     r3, r2
        msr     cpsr_c, #(MODE_SYS | I_BIT | F_BIT)
        mov     sp, r1
        msr     cpsr_c, #(MODE_SVC | I_BIT | F_BIT)
        mov     r0, #0
        mov     r1, #0
        mov     r2, #0
        mov     lr, r12
        movs    pc, lr

/* ------------------------------------------------------------- SVC = INT n */

svc_entry:
        stmfd   sp!, {r0-r3}            /* 16-byte scratch; the frame goes below */
        add     r0, sp, #16             /* r0 = the SVC sp at entry */
        sub     r3, sp, #F_SIZE
        bic     r3, r3, #7              /* 8-byte aligned frame for AAPCS */
        add     r1, r3, #16
        stmia   r1, {r4-r12}            /* frame r4..r12 */
        mov     r4, r3                  /* r4 = frame */
        str     r0, [r4, #F_KSP]
        ldmia   sp, {r0-r3}
        stmia   r4, {r0-r3}             /* frame r0..r3 */
        mov     sp, r4
        str     lr, [sp, #F_PC]
        mrs     r2, spsr
        str     r2, [sp, #F_CPSR]

        and     r3, r2, #0x1F
        cmp     r3, #MODE_SVC
        ldreq   r0, [sp, #F_KSP]
        streq   r0, [sp, #F_SP]
        streq   lr, [sp, #F_LR]         /* the caller's lr is gone; keep something sane */
        addne   r0, sp, #F_SP
        stmneia r0, {sp, lr}^           /* USR/SYS bank */
        nop

        /* which INT?  the low byte of the SVC's immediate */
        tst     r2, #T_BIT
        ldrneh  r0, [lr, #-2]
        ldreq   r0, [lr, #-4]
        and     r0, r0, #0xFF
        str     r0, [sp, #F_INT]

        /* software interrupts run with the caller's interrupt state (the
           handler would STI first thing on a PC; here it is done for it) */
        tst     r2, #I_BIT
        msreq   cpsr_c, #(MODE_SVC | F_BIT)

dispatch:
        mov     r1, r0, lsl #2
        ldr     r1, [r1]
        cmp     r1, #0
        beq     no_handler
        mov     r0, sp
        blx     r1
        b       exc_return

no_handler:
        ldr     r0, [sp, #F_CPSR]
        orr     r0, r0, #C_FLAG
        str     r0, [sp, #F_CPSR]
        /* fall through */

/*
 * Common return: put the (possibly modified) frame back and resume the caller.
 * A handler may change any field — EXEC and TERMINATE work by rewriting pc, sp
 * and cpsr — except that a caller that was itself in SVC mode keeps its sp.
 */
        .global exc_return
exc_return:
        msr     cpsr_c, #(MODE_SVC | I_BIT | F_BIT)
        ldr     r0, [sp, #F_CPSR]
        msr     spsr_cxsf, r0
        and     r1, r0, #0x1F
        cmp     r1, #MODE_SVC
        beq     ret_to_svc
        cmp     r1, #MODE_USR
        cmpne   r1, #MODE_SYS
        addeq   r0, sp, #F_SP
        ldmeqia r0, {sp, lr}^
        nop
        ldr     lr, [sp, #F_PC]
        ldmia   sp, {r0-r12}
        ldr     sp, [sp, #F_KSP]
        movs    pc, lr

ret_to_svc:
        /* the interrupted code was in SVC mode: its lr is live, so return
           through a word parked just below its stack pointer */
        ldr     r0, [sp, #F_KSP]
        ldr     r1, [sp, #F_PC]
        str     r1, [r0, #-4]
        ldr     lr, [sp, #F_LR]
        ldmia   sp, {r0-r12}
        ldr     sp, [sp, #F_KSP]
        sub     sp, sp, #4
        ldmia   sp!, {pc}^

/* ------------------------------------------------- IRQ / UNDEF / ABORTS */

/*
 * Every other exception is turned into an INT in SVC mode with a full frame:
 *   IRQ          -> INT 08h..0Fh / 70h..77h (the PIC says which line)
 *   undefined    -> INT 06h  (x86's invalid opcode)
 *   data abort   -> INT 0Dh  (x86's general protection fault)
 *   prefetch ab. -> INT 0Eh  (x86's page fault)
 * Entered with r12 = the INT number (0xFF for IRQ) and lr already adjusted
 * so that it is the address to resume at (or of the faulting instruction).
 */
        .macro  EXC_STUB name, adjust, intno
\name:
        sub     lr, lr, #\adjust
        stmfd   sp!, {r0-r3, r12, lr}
        mov     r12, #\intno
        b       exc_common
        .endm

        EXC_STUB irq_entry,   4, 0xFF
        EXC_STUB undef_entry, 4, 0x06
        EXC_STUB pabt_entry,  4, 0x0E
        EXC_STUB dabt_entry,  8, 0x0D

fiq_entry:
        subs    pc, lr, #4

exc_common:
        /* in IRQ/UND/ABT mode: its stack holds r0-r3, r12, resume pc */
        mrs     r0, spsr
        mov     r1, sp
        add     sp, sp, #24
        mov     r2, r12                 /* INT number */
        msr     cpsr_c, #(MODE_SVC | I_BIT | F_BIT)

        mov     r3, sp                  /* ksp */
        sub     r12, sp, #(F_SIZE + 16) /* 16 bytes of room for ret_to_svc */
        bic     r12, r12, #7
        str     r3, [r12, #F_KSP]
        str     r0, [r12, #F_CPSR]
        str     r2, [r12, #F_INT]
        add     r3, r12, #16
        stmia   r3, {r4-r11}
        mov     r4, r12                 /* frame */
        ldmia   r1, {r0-r3, r5, r6}     /* r0-r3, r12, pc */
        stmia   r4, {r0-r3}
        str     r5, [r4, #48]
        str     r6, [r4, #F_PC]
        mov     sp, r4

        ldr     r0, [sp, #F_CPSR]
        and     r1, r0, #0x1F
        cmp     r1, #MODE_SVC
        ldreq   r2, [sp, #F_KSP]
        streq   r2, [sp, #F_SP]
        streq   lr, [sp, #F_LR]
        addne   r2, sp, #F_SP
        stmneia r2, {sp, lr}^
        nop

        ldr     r0, [sp, #F_INT]
        cmp     r0, #0xFF
        bne     dispatch                /* faults dispatch with IRQs off */

        /* IRQ: acknowledge the PIC, which answers with the line number */
        ldr     r1, =IO_BASE
        ldrb    r0, [r1, #0x20]
        cmp     r0, #0xFF
        beq     exc_return              /* spurious */
        cmp     r0, #8
        addlo   r0, r0, #0x08
        addhs   r0, r0, #0x68           /* IRQ 8 -> INT 70h */
        str     r0, [sp, #F_INT]
        mov     r1, r0, lsl #2
        ldr     r1, [r1]
        cmp     r1, #0
        beq     irq_unhandled
        mov     r0, sp
        blx     r1
        b       exc_return

irq_unhandled:
        /* nobody owns the line: end the interrupt so the PIC is not wedged */
        ldr     r1, =IO_BASE
        mov     r2, #0x20
        ldr     r0, [sp, #F_INT]
        cmp     r0, #0x70
        strhsb  r2, [r1, #0xA0]
        strb    r2, [r1, #0x20]
        b       exc_return

/* ------------------------------------------------------- small helpers */

/* void wfi(void) — sleep until an interrupt is pending (ARM926 CP15 wait) */
        .global wfi
wfi:
        mov     r0, #0
        mcr     p15, 0, r0, c7, c0, 4
        bx      lr

/* uint32_t irq_save(void) / void irq_restore(uint32_t) / irq_enable / irq_disable */
        .global irq_save
irq_save:
        mrs     r0, cpsr
        orr     r1, r0, #I_BIT
        msr     cpsr_c, r1
        bx      lr
        .global irq_restore
irq_restore:
        msr     cpsr_c, r0
        bx      lr
        .global irq_enable
irq_enable:
        mrs     r0, cpsr
        bic     r0, r0, #I_BIT
        msr     cpsr_c, r0
        bx      lr
        .global irq_disable
irq_disable:
        mrs     r0, cpsr
        orr     r0, r0, #I_BIT
        msr     cpsr_c, r0
        bx      lr

/* uint32_t read_cpuid(void) */
        .global read_cpuid
read_cpuid:
        mrc     p15, 0, r0, c0, c0, 0
        bx      lr

/*
 * int bios_int(int n, struct armregs *r) — raise INT n from inside the BIOS
 * (so a hooked vector is honoured, as a PC BIOS calling INT 1Ch does).
 * Loads r0-r6 from the struct, does the SVC through a table, stores them back.
 */
        .global bios_int
bios_int:
        stmfd   sp!, {r4-r11, lr}
        mov     r11, r1
        adr     r12, svc_table
        add     r12, r12, r0, lsl #3
        ldmia   r11, {r0-r6}
        bx      r12                     /* lr cannot survive an SVC made in SVC mode */
bios_int_after:
        stmia   r11, {r0-r6}
        mrs     r0, cpsr
        str     r0, [r11, #F_CPSR]
        ldmfd   sp!, {r4-r11, lr}
        bx      lr

svc_table:
        .set    n, 0
        .rept   256
        svc     #n
        b       bios_int_after
        .set    n, n + 1
        .endr

        .ltorg
