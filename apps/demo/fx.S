/*
 * fx.S - the demo's inner loops, hand-written ARM assembly.
 *
 * What each one shows off:
 *   fx_copy       LDM/STM: 64,000 bytes to video memory, 32 bytes per pair
 *   fx_fill       STM: 8 registers of colour per store
 *   fx_plasma     LDRB with post-increment, 5 instructions a pixel
 *   fx_roto       one ADD moves u AND v (two 16-bit fields in one register),
 *                 the barrel shifter builds the texel address: 5 a pixel
 *   fx_tunnel     a shifted register offset in LDRB does the 16-bit wrap
 *                 of the texture coordinates for free: 5 a pixel
 *   fx_scroll_col conditional execution: MOVS shifts the font bit into the
 *                 carry, STRCSB stores only where the bit was set - no jumps
 *   fx_smulbb     the DSP multiply (SMULBB, 16x16 signed) for the effects'
 *                 per-frame set-up (see demo.c)
 *
 * Copyright (C) 1990 Europa Micro Systems (ARM-DOS project).
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

/* void fx_copy(void *dst, const void *src, unsigned bytes)  bytes % 64 == 0 */
FUNC fx_copy
        push    {r4-r11, lr}
1:      ldmia   r1!, {r3-r10}
        stmia   r0!, {r3-r10}
        ldmia   r1!, {r3-r10}
        stmia   r0!, {r3-r10}
        subs    r2, r2, #64
        bne     1b
        pop     {r4-r11, pc}

/* void fx_fill(void *dst, uint32_t colour4, unsigned bytes)  bytes % 32 == 0 */
FUNC fx_fill
        push    {r4-r11, lr}
        mov     r3, r1
        mov     r4, r1
        mov     r5, r1
        mov     r6, r1
        mov     r7, r1
        mov     r8, r1
        mov     r9, r1
        mov     r10, r1
1:      stmia   r0!, {r3-r10}
        subs    r2, r2, #32
        bne     1b
        pop     {r4-r11, pc}

/* void fx_plasma(uint8_t *dst, const uint8_t *s1, const uint8_t *s2, uint32_t row)
 * 320 pixels: dst[x] = s1[x] + s2[x] + row */
FUNC fx_plasma
        push    {r4-r7, lr}
        mov     r12, #320
1:      ldrb    r4, [r1], #1
        ldrb    r5, [r2], #1
        ldrb    r6, [r1], #1
        ldrb    r7, [r2], #1
        add     r4, r4, r5
        add     r4, r4, r3
        add     r6, r6, r7
        add     r6, r6, r3
        strb    r4, [r0], #1
        strb    r6, [r0], #1
        subs    r12, r12, #2
        bne     1b
        pop     {r4-r7, pc}

/* void fx_roto(uint8_t *dst, const uint8_t *tex, uint32_t uv, uint32_t duv)
 * 320 pixels of a 256x256 texture. uv holds u in bits 31-16 (8.8) and v in
 * bits 15-0 (8.8): ONE add steps both (a v overflow nudges u by 1/256 of a
 * texel - invisible). Texel = tex[(v & 0xFF00) | (u >> 24)]. */
FUNC fx_roto
        push    {r4-r5, lr}
        mov     r12, #320
1:      add     r2, r2, r3
        and     r4, r2, #0xFF00
        orr     r4, r4, r2, lsr #24
        ldrb    r5, [r1, r4]
        strb    r5, [r0], #1
        add     r2, r2, r3
        and     r4, r2, #0xFF00
        orr     r4, r4, r2, lsr #24
        ldrb    r5, [r1, r4]
        strb    r5, [r0], #1
        subs    r12, r12, #2
        bne     1b
        pop     {r4-r5, pc}

/* void fx_tunnel(uint8_t *dst, const uint32_t *tab, const uint8_t *tex,
 *                uint32_t off, unsigned pixels)
 * tab[i] = shade << 24 | distance << 8 | angle, off = (dz << 8 | da) << 16.
 * The sum's top 16 bits are the texture index, wrapped for free by the
 * 32-bit add; LDRB takes it with "LSR #16" in its address. */
FUNC fx_tunnel
        push    {r4-r7, lr}
        ldr     r12, [sp, #20]
1:      ldr     r4, [r1], #4
        add     r5, r3, r4, lsl #16
        ldrb    r6, [r2, r5, lsr #16]
        add     r6, r6, r4, lsr #24
        strb    r6, [r0], #1
        subs    r12, r12, #1
        bne     1b
        pop     {r4-r7, pc}

/* void fx_scroll_col(uint8_t *dst, uint32_t bits, uint32_t colour)
 * One column of the scroller: 16 font rows (bit 31 = top), each two pixels
 * high; colour counts up per row (a copper gradient in the palette).
 * MOVS puts the next font bit in C; STRCSB stores only if it was set. */
FUNC fx_scroll_col
        .rept   16
        movs    r1, r1, lsl #1
        strbcs  r2, [r0]
        strbcs  r2, [r0, #320]
        add     r0, r0, #640
        add     r2, r2, #1
        .endr
        bx      lr

/* int32_t fx_smulbb(int32_t a, int32_t b)  (a & 0xFFFF) * (b & 0xFFFF), signed */
FUNC fx_smulbb
        smulbb  r0, r0, r1
        bx      lr

/* int32_t fx_smlabb(int32_t a, int32_t b, int32_t acc) */
FUNC fx_smlabb
        smlabb  r0, r0, r1, r2
        bx      lr
