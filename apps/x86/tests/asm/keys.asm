; keys.asm - KEYS.COM: an INT 9 hook that reads port 60h and chains to the
; BIOS (the TSR way); reads 5 keys with INT 16h, echoes them and reports how
; many scan codes the hook saw.
        org     100h
        mov     ax, 3509h
        int     21h
        mov     [old9], bx
        mov     [old9 + 2], es
        mov     ax, 2509h
        mov     dx, int9
        int     21h
        mov     cx, 5
.k:     xor     ah, ah
        int     16h
        mov     dl, al
        mov     ah, 2
        int     21h
        loop    .k
        push    ds
        lds     dx, [old9]
        mov     ax, 2509h
        int     21h
        pop     ds
        mov     dx, msg
        mov     ah, 9
        int     21h
        mov     ax, [codes]
        aam
        add     ax, 3030h
        xchg    al, ah
        mov     [num], ax
        mov     dx, num
        mov     ah, 9
        int     21h
        mov     ax, 4C00h
        int     21h
int9:   push    ax
        in      al, 60h
        inc     word [cs:codes]
        pop     ax
        jmp     far [cs:old9]
msg     db      13, 10, 'scan codes seen by the hook: $'
num     db      '00', 13, 10, '$'
old9    dd      0
codes   dw      0
