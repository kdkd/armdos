/* mark.S - the end of ANSI.SYS's resident part (linked after ints.o and
   ansi.o, before init.o; INIT returns this address as the break address) */
        .section .text.ansi_res_end, "ax", %progbits
        .global ansi_res_end
        .balign 16
ansi_res_end:
        .word   0
