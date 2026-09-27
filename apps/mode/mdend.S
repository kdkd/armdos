/* mdend.S - the end of MODE.COM's resident portion (linked after mdres.o) */
        .section .text.unlikely.mode_zend, "ax", %progbits
        .global mode_res_end
        .balign 16
mode_res_end:
        .word   0
