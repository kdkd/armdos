; irqtest.asm - IRQTEST.COM: hooks INT 08h (chaining with JMP FAR) and INT 1Ch,
; waits one second by the BIOS clock (HLT between polls), prints the counts.
        org     100h
        mov     ax, 3508h
        int     21h
        mov     [old8], bx
        mov     [old8 + 2], es
        mov     ax, 351Ch
        int     21h
        mov     [old1c], bx
        mov     [old1c + 2], es
        mov     ax, 2508h
        mov     dx, int8
        int     21h
        mov     ax, 251Ch
        mov     dx, int1c
        int     21h
        xor     ah, ah
        int     1Ah
        mov     [t0], dx
.w:     hlt
        xor     ah, ah
        int     1Ah
        sub     dx, [t0]
        cmp     dx, 18
        jb      .w
        push    ds
        lds     dx, [old8]
        mov     ax, 2508h
        int     21h
        pop     ds
        push    ds
        lds     dx, [old1c]
        mov     ax, 251Ch
        int     21h
        pop     ds
        mov     dx, m8
        mov     ah, 9
        int     21h
        mov     ax, [c8]
        call    pnum
        mov     dx, m1c
        mov     ah, 9
        int     21h
        mov     ax, [c1c]
        call    pnum
        mov     dx, crlf
        mov     ah, 9
        int     21h
        mov     ax, 4C00h
        int     21h
int8:   inc     word [cs:c8]
        jmp     far [cs:old8]
int1c:  inc     word [cs:c1c]
        iret
pnum:   mov     bx, 10
        xor     cx, cx
.d:     xor     dx, dx
        div     bx
        push    dx
        inc     cx
        or      ax, ax
        jnz     .d
.o:     pop     dx
        add     dl, '0'
        mov     ah, 2
        int     21h
        loop    .o
        ret
m8      db      'int08=$'
m1c     db      ' int1c=$'
crlf    db      13, 10, '$'
old8    dd      0
old1c   dd      0
c8      dw      0
c1c     dw      0
t0      dw      0
