; child.asm - CHILD.COM for exectest: prints its tail, closes ITS stdout, exits 42
        org     100h
        mov     ah, 9
        mov     dx, msg
        int     21h
        mov     si, 81h                 ; echo the command tail
.t:     lodsb
        cmp     al, 13
        je      .e
        mov     dl, al
        mov     ah, 2
        int     21h
        jmp     .t
.e:     mov     ah, 9
        mov     dx, crlf
        int     21h
        mov     ah, 3Eh                 ; close handle 1 (only ours)
        mov     bx, 1
        int     21h
        mov     ax, 4C2Ah
        int     21h
msg     db      'child: tail=[$'
crlf    db      ']', 13, 10, '$'
