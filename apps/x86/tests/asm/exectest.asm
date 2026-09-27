; exectest.asm - EXECTEST.COM: EXEC an x86 child and an ARM child from x86 code
        org     100h
        mov     sp, stack_top
        mov     ah, 4Ah                 ; keep 4 KB
        mov     bx, 1000h / 16
        int     21h
        mov     dx, child
        mov     bx, pb
        mov     word [pb + 2], tail1
        mov     [pb + 4], cs
        mov     ax, 4B00h
        int     21h
        jc      fail
        mov     ah, 4Dh
        int     21h
        mov     [code], al
        mov     ah, 9
        mov     dx, m1
        int     21h
        mov     al, [code]
        call    pnum
        mov     ah, 9
        mov     dx, crlf
        int     21h
        ; an ARM program
        mov     dx, armprog
        mov     bx, pb
        mov     word [pb + 2], tail2
        mov     [pb + 4], cs
        mov     ax, 4B00h
        int     21h
        jc      fail
        mov     ah, 4Dh
        int     21h
        mov     [code], al
        mov     ah, 9
        mov     dx, m2
        int     21h
        mov     al, [code]
        call    pnum
        mov     ah, 9
        mov     dx, crlf
        int     21h
        mov     ax, 4C00h
        int     21h
fail:   push    ax
        mov     ah, 9
        mov     dx, mf
        int     21h
        pop     ax
        call    pnum
        mov     ax, 4C01h
        int     21h
pnum:   xor     ah, ah
        mov     bl, 10
        div     bl
        push    ax
        or      al, al
        jz      .one
        call    pnum
.one:   pop     ax
        mov     dl, ah
        add     dl, '0'
        mov     ah, 2
        int     21h
        ret
child   db      'CHILD.COM', 0
armprog db      '\DOS\FIND.EXE', 0
tail1   db      9, ' one two', 13
tail2   db      18, ' "fox" \WORK\A.TXT', 13
m1      db      'parent: x86 child exit code $'
m2      db      'parent: ARM child exit code $'
mf      db      'parent: EXEC failed $'
crlf    db      13, 10, '$'
code    db      0
pb      dw      0, 0, 0, 0, 0, 0, 0
        times 256 db 0
stack_top:
