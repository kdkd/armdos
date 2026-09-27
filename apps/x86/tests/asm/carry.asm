; carry.asm - CARRY.COM: multi-word ADC/SBB chains whose carry passes through
; INC and LOOP (GW-BASIC's double-precision arithmetic).  The translator once
; lost the carry-out of ADC/SBB when the next INC had to save CF.
        org     100h
        mov     di, 300                 ; hot enough to be translated
.again: mov     word [s], 0
        mov     word [s+2], 0
        mov     word [s+4], 5
        mov     word [s+6], 0
        mov     word [t], 0FFFFh
        mov     word [t+2], 0FFFFh
        mov     word [t+4], 0FFFEh
        mov     word [t+6], 0
        mov     bx, s                   ; s -= a  (64 bits)
        mov     si, a
        mov     cx, 4
        clc
        cld
.sub:   lodsw
        sbb     [bx], ax
        inc     bx
        inc     bx
        loop    .sub
        mov     bx, t                   ; t += a
        mov     si, a
        mov     cx, 4
        clc
.add:   lodsw
        adc     [bx], ax
        inc     bx
        inc     bx
        loop    .add
        dec     di
        jnz     .again
        mov     si, s
        mov     di, want
        mov     cx, 8
        push    ds
        pop     es
        repe    cmpsw
        mov     dx, ok
        je      .p
        mov     dx, bad
.p:     mov     ah, 9
        int     21h
        mov     ax, 4C00h
        int     21h
a       dw      1, 0, 0, 0FFFFh
s       dw      0, 0, 0, 0
t       dw      0, 0, 0, 0
want    dw      0FFFFh, 0FFFFh, 4, 1        ; 0005_0000_0000 - FFFF_0000_0000_0001
        dw      0, 0, 0FFFFh, 0FFFFh        ; 0000_FFFE_FFFF_FFFF + FFFF_0000_0000_0001
ok      db      'CARRY CHAIN OK', 13, 10, '$'
bad     db      'CARRY CHAIN WRONG', 13, 10, '$'
