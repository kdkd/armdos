/* the character generators: VGA 8x16 and CGA 8x8, both code page 437 (see emu/fonts/README.md) */
        .section .rodata
        .balign 16
        .global font8x16
font8x16:
        .incbin "emu/fonts/vga8x16.bin"
        .global font8x8
font8x8:
        .incbin "emu/fonts/cga8x8.bin"
