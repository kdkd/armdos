/*
 * boot.S - the ARM-DOS 4.00 FAT boot sector (ARCH.md 11, 14.1, 15).
 *
 * The ROM (or the MBR) enters at 0x7C00 in SYS mode with r3 = boot drive
 * (DL) and sp = 0x7C00.  Like MSBOOT.ASM we move out of the way (to 0x9F000,
 * just under the video hole), read the first root directory sector, insist
 * that the first two entries are IO.SYS and ARMDOS.SYS, and load IO.SYS -
 * all of it, contiguous from its first cluster, as DOS 4 requires - to
 * 0x00700 (0070:0000).  IO.SYS is entered with r3 = boot drive and
 * r4 = the BPB inside this (moved) sector.
 *
 * mkimage/FORMAT write the BPB (0x0B-0x3D) and the 55AA signature; this
 * file provides the branch at 0, the OEM name and the code from 0x40.
 */
        .syntax unified
        .arm
        .text

        .equ    ORIG,   0x7C00
        .equ    HOME,   0x9F000         /* where we run (linked here) */
        .equ    DIRBUF, 0x0500          /* root directory sector, as DOS */
        .equ    LOAD,   0x0700          /* IO.SYS */
        .equ    DAP,    HOME + 0x200    /* disk address packet */

sector:
        .global _start
_start:
        b       start                   /* bytes 0-3: "jmp" over the BPB */
        .ascii  "ARMDOS4"               /* 04-0A OEM name */
        .org    0x40

start:
        and     r9, r3, #0xFF           /* r9 = boot drive for good */
        ldr     r0, =ORIG
        ldr     r1, =HOME
        mov     r2, #512
1:      ldr     r3, [r0], #4
        str     r3, [r1], #4
        subs    r2, r2, #4
        bne     1b
        ldr     pc, =moved              /* continue in the copy */

moved:
        ldr     sp, =HOME
        ldr     r10, =HOME              /* r10 = BPB base (sector) */

        /* root directory LBA = hidden + reserved + nfats * sectors per FAT */
        ldr     r5, [r10, #0x1C]        /* hidden sectors (aligned) */
        ldrh    r0, [r10, #0x0E]        /* reserved */
        add     r5, r5, r0
        ldrb    r0, [r10, #0x10]        /* FATs */
        ldrh    r1, [r10, #0x16]        /* sectors per FAT */
        mla     r5, r0, r1, r5          /* r5 = root dir LBA */
        ldrb    r0, [r10, #0x11]        /* root entries (unaligned word) */
        ldrb    r1, [r10, #0x12]
        orr     r0, r0, r1, lsl #8
        add     r0, r0, #15
        add     r6, r5, r0, lsr #4      /* r6 = first data sector LBA */

        mov     r7, r5                  /* read the first directory sector */
        mov     r8, #1
        ldr     r11, =DIRBUF
        bl      readsec

        ldr     r0, =DIRBUF             /* entry 0 must be IO.SYS ... */
        adr     r1, sysnames
        mov     r2, #11
        bl      match
        ldr     r0, =DIRBUF + 32        /* ... and entry 1 ARMDOS.SYS */
        mov     r2, #11
        bl      match

        ldr     r0, =DIRBUF
        ldrh    r1, [r0, #0x1A]         /* first cluster */
        ldr     r2, [r0, #0x1C]         /* size */
        sub     r1, r1, #2
        ldrb    r3, [r10, #0x0D]        /* sectors per cluster */
        mla     r7, r1, r3, r6          /* LBA of IO.SYS */
        add     r2, r2, #512
        sub     r2, r2, #1
        mov     r8, r2, lsr #9          /* whole file */
        ldr     r11, =LOAD
        bl      readsec

        mov     r3, r9                  /* DL = boot drive */
        add     r4, r10, #0x0B          /* the BPB */
        ldr     pc, =LOAD

/* compare r2 bytes at r0 and r1, advancing r1; mismatch -> error */
match:  ldrb    r3, [r0], #1
        ldrb    r12, [r1], #1
        cmp     r3, r12
        bne     fail
        subs    r2, r2, #1
        bne     match
        bx      lr

/* read r8 sectors from LBA r7 to r11 (INT 13h AH=42h), 3 tries */
readsec:
        mov     r12, lr
        mov     r5, #3
2:      ldr     r4, =DAP
        mov     r0, #0x10
        str     r0, [r4]                /* size 16, count 0 ... */
        strh    r8, [r4, #2]            /* ... sector count */
        str     r11, [r4, #4]           /* buffer (flat) */
        str     r7, [r4, #8]            /* LBA */
        mov     r0, #0
        str     r0, [r4, #12]
        mov     r0, #0x4200
        mov     r3, r9
        svc     #0x13
        bxcc    r12
        mov     r0, #0                  /* reset and retry */
        mov     r3, r9
        svc     #0x13
        subs    r5, r5, #1
        bne     2b
        /* fall through */

fail:   adr     r4, msg
3:      ldrb    r0, [r4], #1
        cmp     r0, #0
        beq     4f
        orr     r0, r0, #0x0E00         /* INT 10h AH=0Eh teletype */
        mov     r1, #7
        svc     #0x10
        b       3b
4:      mov     r0, #0                  /* INT 16h AH=00h: wait for a key */
        svc     #0x16
        svc     #0x19                   /* and try again */
5:      b       5b

sysnames:
        .ascii  "IO      SYS"
        .ascii  "ARMDOS  SYS"
msg:    .ascii  "\r\nNon-System disk or disk error\r\n"
        .ascii  "Replace and press any key when ready\r\n"
        .byte   0
        .balign 4
        .ltorg

        .org    0x1FE
        .byte   0x55, 0xAA
