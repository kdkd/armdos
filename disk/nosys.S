/*
 * nosys.S - the default boot-sector code mkimage.mjs puts at offset 0x40 of a
 * volume boot sector when no --boot/"boot" file is given: DOS 4's message for
 * a disk without a system, then wait for a key and INT 19h (bootstrap again).
 *
 * Position independent (the ROM enters the sector at 0x7C00 in SYS mode, the
 * branch at offset 0 jumps here). Assembled into mkimage.mjs as hex; `node
 * disk/mkimage.mjs selftest` checks the hex matches this source.
 */
        .syntax unified
        .arm
        .text
boot:
        adr     r4, msg
1:      ldrb    r0, [r4], #1
        cmp     r0, #0
        beq     2f
        orr     r0, r0, #0x0E00         @ INT 10h AH=0Eh teletype, AL = char
        mov     r1, #7                  @ BH = page 0, BL = colour
        svc     #0x10
        b       1b
2:      mov     r0, #0                  @ INT 16h AH=00h: wait for a key
        svc     #0x16
        svc     #0x19                   @ INT 19h: bootstrap
3:      b       3b
msg:    .asciz  "\r\nNon-System disk or disk error\r\nReplace and press any key when ready\r\n"
        .balign 4
