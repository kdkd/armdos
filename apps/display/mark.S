/* mark.S - the end of DISPLAY.SYS's resident code (linked after ints.o and
   display.o, before init.o); the prepared fonts follow it */
        .section .text.display_res_end, "ax", %progbits
        .global display_res_end
        .balign 16
display_res_end:
        .word   0
