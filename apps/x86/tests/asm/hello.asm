; hello.asm - the smallest x86 test: INT 21h AH=09h and AH=4Ch
        org     100h
        mov     ah, 9
        mov     dx, msg
        int     21h
        mov     ax, 4C07h
        int     21h
msg     db      'Hello from x86!', 13, 10, '$'
