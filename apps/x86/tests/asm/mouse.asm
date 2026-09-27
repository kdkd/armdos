; mouse.asm - MOUSE.COM test: INT 33h reset, an event handler (moves and the
; left button), waits for 5 events, prints the last position and the count.
        org     100h
        xor     ax, ax
        int     33h
        cmp     ax, 0FFFFh
        jne     nomouse
        mov     ax, 0Ch
        mov     cx, 3                   ; moves + left button down
        push    cs
        pop     es
        mov     dx, handler
        int     33h
.w:     hlt
        cmp     word [events], 5
        jb      .w
        mov     ax, 0Ch                 ; handler off
        xor     cx, cx
        int     33h
        mov     ah, 9
        mov     dx, m1
        int     21h
        mov     ax, [events]
        call    pnum
        mov     ah, 9
        mov     dx, m2
        int     21h
        mov     ax, [lastx]
        call    pnum
        mov     ah, 9
        mov     dx, crlf
        int     21h
        mov     ax, 4C00h
        int     21h
nomouse: mov    ah, 9
        mov     dx, m0
        int     21h
        mov     ax, 4C01h
        int     21h
handler: inc    word [cs:events]
        mov     [cs:lastx], cx
        retf
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
m0      db      'no mouse driver', 13, 10, '$'
m1      db      'mouse events: $'
m2      db      ', last x: $'
crlf    db      13, 10, '$'
events  dw      0
lastx   dw      0
