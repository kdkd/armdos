/* kbend.S - the end of KEYB.COM's resident code (linked after kbres.o); the
   tables KEYB builds from KEYBOARD.SYS are copied here */
        .section .text.unlikely.keyb_zend, "ax", %progbits
        .global keyb_res_end
        .balign 16
keyb_res_end:
        .word   0
