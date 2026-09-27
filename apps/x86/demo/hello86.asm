; hello86.asm - HELLO86.COM, the ELBOW demo's first x86 program.
;
; Identifies the processor it runs on the way 1980s programs did (FLAGS
; bits 12-15), writes a colour banner straight into the text screen at
; B800:0000, and prints a line through DOS.  Plain 8086 code.
;
; Copyright (c) 2026 Europa Micro Systems. Part of ARM-DOS (apps/x86/demo).
        org     100h
        cpu     8086

start:  ; which CPU?  8086: FLAGS bits 12-15 always set; 286 (real mode):
        ; always clear; 386: can be set
        pushf
        pop     ax
        and     ax, 0FFFh
        push    ax
        popf
        pushf
        pop     ax
        and     ax, 0F000h
        mov     si, cpu86
        cmp     ax, 0F000h
        je      .got
        or      ax, 7000h
        push    ax
        popf
        pushf
        pop     ax
        mov     si, cpu286
        test    ax, 7000h
        jz      .got
        mov     si, cpu386
.got:   mov     [which], si

        ; the banner: 3 rows at the cursor, written to video RAM.  Make room
        ; first with 3 line feeds through DOS (the screen scrolls if it must),
        ; then draw on the 3 rows above the cursor
        mov     ah, 0Fh                 ; current mode: text?  (BH = page)
        int     10h
        cmp     al, 3
        ja      .skip
        mov     ah, 9
        mov     dx, lf3
        int     21h
        mov     ah, 3                   ; cursor row -> DH
        int     10h
        sub     dh, 3
        mov     al, 160
        mul     dh
        add     ax, 10 * 2
        mov     di, ax
        mov     ax, 0B800h
        mov     es, ax
        mov     bx, banner
        mov     dl, 3                   ; rows
.row:   push    di
        mov     cx, 60
        mov     ah, 1Fh                 ; bright white on blue
.col:   mov     al, [bx]
        inc     bx
        stosw
        loop    .col
        pop     di
        add     di, 160
        dec     dl
        jnz     .row
.skip:
        mov     ah, 9
        mov     dx, msg1
        int     21h
        mov     dx, [which]
        int     21h
        mov     dx, msg2
        int     21h
        mov     ax, 4C00h
        int     21h

banner  db      '                                                            '
        db      '      Hello from a real x86 program on the ARM PC!          '
        db      '    (8086 machine code, written straight to B800:0000)      '
lf3     db      13, 10, 13, 10, 13, 10, '$'
msg1    db      'HELLO86: this program sees an $'
cpu86   db      'Intel 8086/8088$'
cpu286  db      '80286$'
cpu386  db      '80386$'
msg2    db      ' processor - the ELBOW compatibility box.', 13, 10, '$'
which   dw      0
