/*
 * armasm.S - ARMINFO's hand-written ARM code: CPU identification, feature
 * probes that actually execute the instructions, and the inner loops of the
 * "ARM features" benchmarks (barrel shifter, conditional execution, LDM/STM).
 *
 * Copyright (C) 1989 Europa Micro Systems (ARM-DOS project).
 */
        .syntax unified
        .arm
        .text

        .macro FUNC name
        .section .text.\name, "ax", %progbits
        .global \name
        .type   \name, %function
        .align  2
\name:
        .endm

/* ------------------------------------------------------ identification */

/* uint32_t cp15_id(void) - main ID register (c0, op2 0) */
FUNC cp15_id
        mrc     p15, 0, r0, c0, c0, 0
        bx      lr

/* uint32_t cp15_cachetype(void) - cache type register (c0, op2 1) */
FUNC cp15_cachetype
        mrc     p15, 0, r0, c0, c0, 1
        bx      lr

/* uint32_t cp15_control(void) - control register (c1) */
FUNC cp15_control
        mrc     p15, 0, r0, c1, c0, 0
        bx      lr

/* uint32_t read_cpsr(void) */
FUNC read_cpsr
        mrs     r0, cpsr
        bx      lr

/* uint32_t read_sp(void) */
FUNC read_sp
        mov     r0, sp
        bx      lr

/* ------------------------------------------------------ feature probes */

/* int probe_thumb(int x) - interworking call into Thumb code; returns x*3+1
   computed by 16-bit Thumb instructions */
FUNC probe_thumb
        push    {r4, lr}
        adr     r12, thumb_part + 1
        blx     r12
        pop     {r4, pc}
        .thumb
        .thumb_func
thumb_part:
        lsls    r1, r0, #1
        adds    r0, r0, r1
        adds    r0, r0, #1
        bx      lr
        .arm

/* uint32_t probe_qadd(uint32_t a, uint32_t b, uint32_t *qflag) - saturating
   add; *qflag = the sticky Q bit afterwards (v5TE DSP extension) */
FUNC probe_qadd
        mrs     r12, cpsr
        bic     r12, r12, #0x08000000   @ clear Q
        msr     cpsr_f, r12
        qadd    r0, r0, r1
        mrs     r12, cpsr
        mov     r12, r12, lsr #27
        and     r12, r12, #1
        str     r12, [r2]
        bx      lr

/* uint32_t probe_clz(uint32_t x) */
FUNC probe_clz
        clz     r0, r0
        bx      lr

/* int32_t probe_smulbb(int32_t a, int32_t b) - 16x16 signed multiply */
FUNC probe_smulbb
        smulbb  r0, r0, r1
        bx      lr

/* int32_t probe_smlawb(int32_t a, int32_t b, int32_t acc) - 32x16 MAC, top 32 */
FUNC probe_smlawb
        smlawb  r0, r0, r1, r2
        bx      lr

/* uint64_t probe_ldrd(const uint64_t *p) - doubleword load */
FUNC probe_ldrd
        ldrd    r2, r3, [r0]
        mov     r0, r2
        mov     r1, r3
        bx      lr

/* uint32_t probe_umull_hi(uint32_t a, uint32_t b) - long multiply, high word */
FUNC probe_umull_hi
        umull   r2, r0, r0, r1
        bx      lr

/* uint32_t probe_swp(uint32_t *p, uint32_t v) - atomic swap, returns old */
FUNC probe_swp
        swp     r2, r1, [r0]
        mov     r0, r2
        bx      lr

/* ------------------------------------------------ barrel shifter test */
/*
 * y[i] = x[i] + x[i]*4 + x[i]/8    (x*5 + x>>3)
 *
 * bs_separate: the way an 8086 has to do it - every shift is an instruction
 *              of its own (SHL/SAR, then ADD).
 * bs_barrel:   the ARM way - the shift rides along inside the ADD, for free.
 * Both: void f(const int32_t *x, int32_t *y, unsigned n), n a multiple of 4.
 */
FUNC bs_separate
        push    {r4-r6, lr}
1:      ldr     r3, [r0], #4
        mov     r12, r3, lsl #2         @ shl
        add     r12, r12, r3            @ add
        mov     r4, r3, asr #3          @ sar
        add     r12, r12, r4            @ add
        str     r12, [r1], #4
        subs    r2, r2, #1
        bne     1b
        pop     {r4-r6, pc}

FUNC bs_barrel
        push    {r4-r6, lr}
1:      ldr     r3, [r0], #4
        add     r12, r3, r3, lsl #2     @ x + x*4 in one instruction
        add     r12, r12, r3, asr #3    @ + x/8 in one more
        str     r12, [r1], #4
        subs    r2, r2, #1
        bne     1b
        pop     {r4-r6, pc}

/* ------------------------------------------ conditional execution test */
/*
 * Euclid's GCD by subtraction, the textbook ARM example.
 *   uint32_t gcd_branchy(uint32_t a, uint32_t b)  - compare and jump, as on an 8086
 *   uint32_t gcd_cond(uint32_t a, uint32_t b)     - no jumps but the loop:
 *        loop: CMP a,b / SUBGT a,a,b / SUBLT b,b,a / BNE loop
 */
FUNC gcd_branchy
1:      cmp     r0, r1
        beq     3f
        blt     2f
        sub     r0, r0, r1
        b       1b
2:      sub     r1, r1, r0
        b       1b
3:      bx      lr

FUNC gcd_cond
1:      cmp     r0, r1
        subgt   r0, r0, r1
        sublt   r1, r1, r0
        bne     1b
        bx      lr

/* --------------------------------------------------- block move test */
/*
 * void move_bytes(void *dst, const void *src, unsigned n)   LDRB/STRB, like REP MOVSB
 * void move_ldmstm(void *dst, const void *src, unsigned n)  LDMIA/STMIA 8 words (32 bytes)
 *                                                           per instruction pair; n % 32 == 0
 */
FUNC move_bytes
1:      ldrb    r3, [r1], #1
        strb    r3, [r0], #1
        subs    r2, r2, #1
        bne     1b
        bx      lr

FUNC move_ldmstm
        push    {r4-r10, lr}
1:      ldmia   r1!, {r3-r10}
        stmia   r0!, {r3-r10}
        subs    r2, r2, #32
        bne     1b
        pop     {r4-r10, pc}
