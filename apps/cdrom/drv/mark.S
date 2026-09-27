/* mark.S - the end of ARMCD.SYS's resident part (linked after armcd.o, before
   init.o and ints.o; INIT returns this address as the break address) */
        .section .text.armcd_res_end, "ax", %progbits
        .global armcd_res_end
        .balign 16
armcd_res_end:
        .word   0
