/*
 * comstub.S - the self-relocating prologue elf2exe --com --selfreloc puts in
 * front of a .COM image that has absolute relocations (i.e. ordinary C).
 *
 * File layout:  [this stub][image, linked at 0][u32 reloc offsets...]
 * DOS enters at the stub (PSP+0x100) with r0 = PSP, r2 = block end, lr = PSP.
 * The stub adds the image base to every listed word, sets sp to the image's
 * stack top (image + bss + stack), and enters the image with r1 = image base,
 * exactly the EXE entry state. elf2exe.mjs embeds the assembled bytes and
 * patches the five trailing words; tests/run-tests.mjs checks they match.
 */
        .syntax unified
        .arm
        .text
stub:
        adr     r12, stub
        ldr     r4, stub_size
        add     r4, r12, r4             @ r4 = image base
        ldr     r5, image_size
        add     r5, r4, r5              @ r5 = relocation table
        ldr     r6, reloc_count
1:      subs    r6, r6, #1
        bmi     2f
        ldr     r7, [r5], #4
        ldr     r8, [r4, r7]
        add     r8, r8, r4
        str     r8, [r4, r7]
        b       1b
2:      ldr     r7, stack_top
        add     r7, r4, r7
        cmp     r7, r2                  @ does image+bss+stack fit the block?
        bhi     3f
        mov     sp, r7
        mov     r1, r4
        ldr     r7, entry
        add     r7, r4, r7
        bx      r7
3:      adr     r3, msg                 @ DS:DX
        mov     r0, #0x0900
        svc     #0x21
        mov     r0, #0x4C00
        orr     r0, r0, #0x08
        svc     #0x21
msg:    .ascii  "Not enough memory\r\n$"
        .balign 4
stub_size:   .word 0
image_size:  .word 0
reloc_count: .word 0
stack_top:   .word 0
entry:       .word 0
