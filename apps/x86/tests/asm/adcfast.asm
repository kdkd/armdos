; adcfast.asm - ADCFAST.COM: ADC/SBB right after ADD/SUB/SHL/AND/CMP, 16 and
; 32 bits, in a hot loop (translated: the translator takes the carry straight
; from the ARM flags there).  Prints "ADC FAST OK" or "ADC FAST BAD n".
        org     100h
        cpu     386
        mov     cx, 200
.loop:  xor     bp, bp                  ; error count
        mov     ax, 0FFFFh
        add     ax, 1                   ; CF=1
        mov     bx, 5
        adc     bx, 0
        cmp     bx, 6
        je      .t2
        inc     bp
.t2:    mov     ax, 1
        sub     ax, 2                   ; CF=1 (borrow)
        mov     bx, 5
        sbb     bx, 0
        cmp     bx, 4
        je      .t3
        inc     bp
.t3:    mov     ax, 8000h
        shl     ax, 1                   ; CF=1
        mov     bx, 0
        adc     bx, 0
        cmp     bx, 1
        je      .t4
        inc     bp
.t4:    mov     ax, 3
        and     ax, 1                   ; CF=0
        mov     bx, 7
        adc     bx, 0
        cmp     bx, 7
        je      .t5
        inc     bp
.t5:    mov     eax, 0FFFFFFFFh
        add     eax, 1                  ; CF=1
        mov     ebx, 10
        adc     ebx, 0
        cmp     ebx, 11
        je      .t6
        inc     bp
.t6:    mov     ax, 5
        cmp     ax, 6                   ; CF=1
        mov     dx, 100
        sbb     dx, 1
        cmp     dx, 98
        je      .t7
        inc     bp
.t7:    mov     ax, 7
        add     ax, 1                   ; CF=0
        mov     bx, 9
        adc     bx, 0
        cmp     bx, 9
        je      .t8
        inc     bp
.t8:    mov     si, 0FFF0h              ; the mixing loop's pattern: add cx,bp / adc si,dx
        mov     cx, 0F000h
        mov     di, 2000h
        add     cx, di                  ; CF=1
        adc     si, 3                   ; FFF0+3+1 = FFF4
        cmp     si, 0FFF4h
        je      .t9
        inc     bp
.t9:    or      bp, bp
        jnz     .bad
        dec     cx
        mov     cx, [count]
        dec     word [count]
        jnz     .loop
        mov     dx, ok
        mov     ah, 9
        int     21h
        mov     ax, 4C00h
        int     21h
.bad:   mov     dx, bad
        mov     ah, 9
        int     21h
        mov     ax, bp
        add     al, '0'
        mov     dl, al
        mov     ah, 2
        int     21h
        mov     ax, 4C01h
        int     21h
count   dw      200
ok      db      'ADC FAST OK', 13, 10, '$'
bad     db      'ADC FAST BAD $'
