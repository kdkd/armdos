/* blob.S - the transient part's AR1 image, carried at the end of COMMAND.COM */
        .section .blob, "a", %progbits
        .balign 16
        .global tran_blob
tran_blob:
        .incbin TRANSIENT_AR1
        .balign 16
