/*
 * mbr.S - the default master boot record code for hard-disk images
 * (offsets 0x000-0x1B7; the partition table follows at 0x1BE).
 *
 * Does what the DOS 4 FDISK MBR does, in ARM: the ROM enters at 0x7C00 in SYS
 * mode with r3 = boot drive (DL) and sp = 0x7C00. Copy ourselves to 0x0600,
 * find the one active partition (0x80; every other entry must be 0x00), read
 * its first sector to 0x7C00 (INT 13h AH=42h LBA, falling back to AH=02h CHS
 * from the table), check 55AA, and jump to 0x7C00 in ARM state with
 * r3 = drive, r4 = flat pointer to the partition entry (DS:SI), sp = 0x7C00.
 * No active partition: INT 18h, like the original.
 */
        .syntax unified
        .arm
        .equ    RELOC, 0x0600
        .equ    BOOT,  0x7C00
        .equ    DAP,   RELOC + 0x200           @ disk address packet scratch
        .text
start:
        and     r5, r3, #0xFF                  @ boot drive
        mov     r0, #BOOT
        mov     r1, #RELOC
        mov     r2, #512
1:      ldr     r3, [r0], #4
        str     r3, [r1], #4
        subs    r2, r2, #4
        bne     1b
        mov     r0, #RELOC
        add     r0, r0, #(cont - start)
        bx      r0                             @ continue in the copy

cont:   ldr     r6, =RELOC + 0x1BE             @ partition table at 0x7BE
        mov     r7, #4
        mov     r8, #0
2:      ldrb    r0, [r6]
        cmp     r0, #0x80
        bne     6f
        cmp     r8, #0                         @ a second active entry?
        bne     invalid
        mov     r8, r6
        b       7f
6:      cmp     r0, #0                         @ anything but 00/80 is invalid
        bne     invalid
7:      add     r6, r6, #16
        subs    r7, r7, #1
        bne     2b
        cmp     r8, #0
        bne     load
        svc     #0x18                          @ no active partition: ROM BASIC
        b       halt

load:   mov     r9, #5                         @ tries
4:      mov     r0, #DAP                       @ INT 13h AH=42h: extended read
        ldr     r1, =0x00010010
        str     r1, [r0]                       @ size 16, reserved 0, count 1
        mov     r1, #BOOT
        str     r1, [r0, #4]                   @ buffer (flat)
        ldrh    r1, [r8, #8]                   @ LBA from the entry (halfword aligned)
        ldrh    r2, [r8, #10]
        orr     r1, r1, r2, lsl #16
        str     r1, [r0, #8]
        mov     r1, #0
        str     r1, [r0, #12]
        mov     r4, r0                         @ DS:SI = packet
        mov     r0, #0x4200
        mov     r3, r5                         @ DL = drive
        svc     #0x13
        bcc     5f
        mov     r0, #0x0200                    @ INT 13h AH=02h: CHS read
        orr     r0, r0, #1                     @ AL = 1 sector
        mov     r1, #BOOT                      @ ES:BX
        ldrb    r2, [r8, #2]                   @ CL = sector | cylinder bits 8-9
        ldrb    r3, [r8, #3]                   @ CH = cylinder bits 0-7
        orr     r2, r2, r3, lsl #8
        ldrb    r3, [r8, #1]                   @ DH = head
        orr     r3, r5, r3, lsl #8             @ DL = drive
        svc     #0x13
        bcc     5f
        mov     r0, #0                         @ reset and try again
        mov     r3, r5
        svc     #0x13
        subs    r9, r9, #1
        bne     4b
        adr     r4, msg_load
        b       print

5:      ldr     r0, =BOOT + 0x1FE              @ signature at 0x7DFE
        ldrh    r1, [r0]
        ldr     r2, =0xAA55
        cmp     r1, r2
        adrne   r4, msg_missing
        bne     print
        mov     r3, r5                         @ DL = drive
        mov     r4, r8                         @ SI = partition entry
        mov     sp, #BOOT
        mov     r0, #BOOT
        bx      r0

invalid:
        adr     r4, msg_invalid
print:  ldrb    r0, [r4], #1
        cmp     r0, #0
        beq     halt
        orr     r0, r0, #0x0E00
        mov     r1, #7
        svc     #0x10
        b       print
halt:   mcr     p15, 0, r0, c7, c0, 4          @ wait for interrupt
        b       halt

msg_invalid: .asciz "Invalid partition table"
msg_load:    .asciz "Error loading operating system"
msg_missing: .asciz "Missing operating system"
        .balign 4
        .ltorg
