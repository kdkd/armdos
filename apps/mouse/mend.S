/* mend.S - the end of MOUSE.COM's resident part (linked after mres.o) */
        .section .text.unlikely.mouse_zend, "ax", %progbits
        .global mouse_res_end
        .balign 16
mouse_res_end:
        .word   0
