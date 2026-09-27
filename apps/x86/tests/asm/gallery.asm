; gallery.asm - GALLERY.COM: what the ELBOW gallery's real programs needed.
;  1. ZZT: a retrace poll (port 3DAh) with interrupts off while a hooked
;     timer interrupt is pending must make progress (the translator used
;     to leave every block at once and never run the poll).
;  2. Apogee's CGA games: INT 10h text in mode 4 draws characters 80h-FFh
;     from the program's own table at INT 1Fh.
;  3. GW-BASIC: AX=1002h from a program that has not asked AX=1A00h (and so
;     believes it is on a CGA) must not load its buffer into the palette.
        org     100h
        ; ---- 1. CLI'd retrace poll with IRQ0/1Ch pending
        mov     ax, 351Ch
        int     21h
        mov     [old1c], bx
        mov     [old1c + 2], es
        mov     ax, 251Ch
        mov     dx, int1c
        int     21h
        mov     dx, 3DAh
        mov     si, 20000               ; horizontal blanking periods (~0.6 s)
        cli
.edge:  mov     cx, 0FFFFh
.w1:    in      al, dx                  ; wait while in blanking
        test    al, 1
        loopnz  .w1
        jcxz    .fail1
        mov     cx, 0FFFFh
.w0:    in      al, dx                  ; wait for blanking
        test    al, 1
        loopz   .w0
        jcxz    .fail1
        dec     si
        jnz     .edge
        sti
        mov     dx, ok1
        jmp     .r1
.fail1: sti
        mov     dx, bad1
.r1:    mov     [msg1], dx
        push    ds
        lds     dx, [old1c]
        mov     ax, 251Ch
        int     21h
        pop     ds
        cmp     word [ticks], 0
        je      .noticks
        mov     dx, ok1b
        jmp     .r1b
.noticks:
        mov     dx, bad1b
.r1b:   mov     [msg1b], dx

        ; ---- 2. INT 1Fh font in mode 4
        mov     ax, 351Fh
        int     21h
        mov     [old1f], bx
        mov     [old1f + 2], es
        mov     ax, 251Fh
        mov     dx, font
        int     21h
        mov     ax, 0004h
        int     10h
        mov     ah, 2
        xor     bh, bh
        xor     dx, dx
        int     10h
        mov     ax, 0E80h               ; character 80h = the first glyph at INT 1Fh
        mov     bx, 0003h
        int     10h
        mov     ax, 0B800h
        mov     es, ax
        mov     ax, [es:0000h]          ; row 0: 11111111 -> four pixels of colour 3 twice
        mov     bx, [es:2000h]          ; row 1: 00000000
        mov     [g0], ax
        mov     [g1], bx
        mov     ah, 3                   ; the cursor moved on by one
        xor     bh, bh
        int     10h
        mov     [cur], dx
        push    ds
        lds     dx, [old1f]
        mov     ax, 251Fh
        int     21h
        pop     ds

        ; ---- 2b. AH=08h reads a character back in mode 4 (GW-BASIC's editor)
        mov     ax, 0E41h               ; 'A' at row 0, column 1
        mov     bx, 0003h
        int     10h
        mov     ah, 2
        xor     bh, bh
        mov     dx, 0001h
        int     10h
        mov     ah, 8
        xor     bh, bh
        int     10h
        mov     [rd], al

        ; ---- 3. AX=1002h from a "CGA" program is ignored
        push    ds
        pop     es
        mov     ax, 1002h
        mov     dx, zeros
        int     10h
        mov     ax, 1007h
        mov     bl, 1
        int     10h
        mov     [pal1], bh

        mov     ax, 0003h
        int     10h
        mov     dx, [msg1]
        mov     ah, 9
        int     21h
        mov     dx, [msg1b]
        mov     ah, 9
        int     21h
        mov     dx, bad2
        cmp     word [g0], 0FFFFh
        jne     .r2
        cmp     word [g1], 0
        jne     .r2
        cmp     word [cur], 0001h
        jne     .r2
        mov     dx, ok2
.r2:    mov     ah, 9
        int     21h
        mov     dx, bad4
        cmp     byte [rd], 'A'
        jne     .r4
        mov     dx, ok4
.r4:    mov     ah, 9
        int     21h
        mov     dx, bad3
        cmp     byte [pal1], 0
        je      .r3
        mov     dx, ok3
.r3:    mov     ah, 9
        int     21h
        mov     ax, 4C00h
        int     21h

int1c:  inc     word [cs:ticks]
        iret

ok1     db      'CLI POLL OK', 13, 10, '$'
bad1    db      'CLI POLL TIMEOUT', 13, 10, '$'
ok1b    db      'TICKS AFTER STI OK', 13, 10, '$'
bad1b   db      'NO TICKS', 13, 10, '$'
ok2     db      'FONT 1FH OK', 13, 10, '$'
bad2    db      'FONT 1FH WRONG', 13, 10, '$'
ok3     db      'CGA PALETTE CALL IGNORED', 13, 10, '$'
bad3    db      'CGA PALETTE CALL APPLIED', 13, 10, '$'
ticks   dw      0
msg1    dw      0
msg1b   dw      0
old1c   dd      0
old1f   dd      0
g0      dw      0
g1      dw      0
cur     dw      0
pal1    db      0
rd      db      0
ok4     db      'READ CHAR OK', 13, 10, '$'
bad4    db      'READ CHAR WRONG', 13, 10, '$'
font    db      0FFh, 0, 0FFh, 0, 0FFh, 0, 0FFh, 0      ; glyph 80h
        times   127 * 8 db 0
zeros   times   17 db 0
