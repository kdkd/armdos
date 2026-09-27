; smc.asm - SMC.COM: self-modifying code, the way old programs patch an
; instruction just before running it (the translator must notice).
        org     100h
        mov     cx, 26
l1:     mov     al, cl
        add     al, 'A' - 1
        mov     [patch + 1], al         ; the immediate of the next MOV
patch:  mov     dl, 0
        mov     ah, 2
        int     21h
        loop    l1
        ; and a routine patched from outside (a different block)
        mov     cx, 10
l2:     mov     al, cl
        add     al, '0' - 1
        mov     [digit + 1], al
        call    show
        loop    l2
        mov     ah, 9
        mov     dx, crlf
        int     21h
        mov     ax, 4C00h
        int     21h
show:
digit:  mov     dl, 0
        mov     ah, 2
        int     21h
        ret
crlf    db      13, 10, '$'
