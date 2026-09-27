/*
 * bootsect.S - the ARM-DOS boot sector (build/bootsect.bin, kernel/boot/boot.S)
 * embedded at build time, as MS-DOS 4.00's FORMAT and SYS carry the boot
 * record (INC/BOOT.INC) inside themselves.  The BPB (0x0B-0x3D) is filled in
 * by the program before it is written.
 */
        .section .rodata.bootsect, "a"
        .balign 4
        .global bootsect_bin
bootsect_bin:
        .incbin BOOTSECT_BIN
        .size   bootsect_bin, 512
