; pm.asm - PM.COM: tries to enter protected mode (must be refused cleanly)
        org     100h
        cpu     386
        mov     ah, 9
        mov     dx, msg
        int     21h
        cli
        mov     eax, cr0
        or      al, 1
        mov     cr0, eax
        jmp     $
msg     db      'switching to protected mode...', 13, 10, '$'
