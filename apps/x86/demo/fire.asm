; fire.asm - FIRE.COM, a demo-scene style fire effect in VGA mode 13h.
;
; Real-mode x86 code the way 1993 demos were written: its own INT 9
; keyboard handler (reads port 60h, sends its own EOI), its own INT 8 timer
; handler with the PIT reprogrammed to 70 Hz (chaining to the BIOS at
; 18.2 Hz so the clock keeps time), the VGA DAC programmed through ports
; 3C8h/3C9h, and vertical-retrace waits on 3DAh.  Runs unchanged on a PC,
; and on the ARM PC under ELBOW.
;
;   FIRE        until a key is pressed
;   FIRE n      for n seconds (automated tests)
;
; Copyright (c) 2026 Europa Micro Systems. Part of ARM-DOS (apps/x86/demo).
        org     100h
        cpu     186

PIT_DIV equ     17045                   ; 1193182 / 70 Hz

start:  cld
        ; optional duration from the command line
        mov     si, 81h
.sp:    lodsb
        cmp     al, ' '
        je      .sp
        xor     bx, bx
.dg:    sub     al, '0'
        jb      .dd
        cmp     al, 9
        ja      .dd
        imul    bx, bx, 10
        xor     ah, ah
        add     bx, ax
        lodsb
        jmp     .dg
.dd:    imul    bx, bx, 70
        mov     [limit], bx

        ; hook INT 8 and INT 9
        mov     ax, 3508h
        int     21h
        mov     [old8], bx
        mov     [old8 + 2], es
        mov     ax, 3509h
        int     21h
        mov     [old9], bx
        mov     [old9 + 2], es
        mov     ax, 2508h
        mov     dx, int8
        int     21h
        mov     ax, 2509h
        mov     dx, int9
        int     21h
        cli
        mov     al, 36h                 ; PIT channel 0, mode 3
        out     43h, al
        mov     ax, PIT_DIV
        out     40h, al
        mov     al, ah
        out     40h, al
        sti

        mov     ax, 13h
        int     10h
        ; palette: black -> red -> yellow -> white
        mov     dx, 3C8h
        xor     al, al
        out     dx, al
        inc     dx
        xor     cx, cx
.pal:   mov     al, cl                  ; red: 0..63 over the first 64
        cmp     al, 63
        jbe     .r
        mov     al, 63
.r:     out     dx, al
        mov     al, cl                  ; green from 64
        sub     al, 64
        jae     .g1
        xor     al, al
.g1:    cmp     al, 63
        jbe     .g
        mov     al, 63
.g:     out     dx, al
        mov     al, cl                  ; blue from 128
        sub     al, 128
        jae     .b1
        xor     al, al
.b1:    shr     al, 1
        out     dx, al
        inc     cx
        cmp     cx, 256
        jb      .pal

        push    0A000h
        pop     es
        mov     word [seed], 1234h

frame:  ; new random embers on the bottom line (buffer row 101)
        mov     di, buf + 101 * 160
        mov     cx, 160
.emb:   mov     ax, [seed]
        imul    ax, ax, 25173
        add     ax, 13849
        mov     [seed], ax
        mov     al, ah
        cmp     al, 150
        jb      .cold
        mov     al, 255
        jmp     .put
.cold:  shr     al, 2
.put:   mov     [di], al
        inc     di
        loop    .emb
        ; each cell = average of the four below it, cooling a little
        mov     si, buf + 160
        mov     di, buf
        mov     cx, 101 * 160
.av:    xor     ax, ax
        mov     al, [si - 1]
        xor     bx, bx
        mov     bl, [si]
        add     ax, bx
        mov     bl, [si + 1]
        add     ax, bx
        mov     bl, [si + 160]
        add     ax, bx
        shr     ax, 2
        jz      .z
        dec     ax
.z:     mov     [di], al
        inc     si
        inc     di
        loop    .av
        ; wait for the vertical retrace, then draw the buffer doubled
        mov     dx, 3DAh
.vr1:   in      al, dx
        test    al, 8
        jnz     .vr1
.vr2:   in      al, dx
        test    al, 8
        jz      .vr2
        mov     si, buf
        xor     di, di
        mov     bp, 100
.row:   mov     cx, 160
.px:    lodsb
        mov     ah, al
        stosw
        loop    .px
        sub     si, 160                 ; the same row again
        mov     cx, 160
.px2:   lodsb
        mov     ah, al
        stosw
        loop    .px2
        dec     bp
        jnz     .row
        inc     word [frames]

        cmp     byte [quit], 0
        jne     done
        mov     ax, [limit]
        or      ax, ax
        jz      frame
        cmp     [ticks70], ax
        jb      frame

done:   cli
        mov     al, 36h                 ; PIT back to 18.2 Hz
        out     43h, al
        xor     al, al
        out     40h, al
        out     40h, al
        sti
        push    ds
        lds     dx, [old8]
        mov     ax, 2508h
        int     21h
        pop     ds
        push    ds
        lds     dx, [old9]
        mov     ax, 2509h
        int     21h
        pop     ds
        mov     ax, 3
        int     10h
        ; "n frames in s seconds"
        mov     ax, [frames]
        call    pnum
        mov     dx, msg1
        mov     ah, 9
        int     21h
        mov     ax, [ticks70]
        xor     dx, dx
        mov     bx, 70
        div     bx
        call    pnum
        mov     dx, msg2
        mov     ah, 9
        int     21h
        mov     ax, 4C00h
        int     21h

pnum:   mov     bx, 10                  ; AX in decimal
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

; ---- the timer: 70 Hz; the BIOS gets every 70/18.2-th tick
int8:   push    ax
        inc     word [cs:ticks70]
        mov     ax, [cs:acc]
        add     ax, PIT_DIV
        mov     [cs:acc], ax
        jnc     .own                    ; 65536 / PIT_DIV ticks per BIOS tick
        pop     ax
        jmp     far [cs:old8]
.own:   mov     al, 20h
        out     20h, al
        pop     ax
        iret

; ---- the keyboard: any key (make code) quits; nothing reaches the BIOS
int9:   push    ax
        in      al, 60h
        test    al, 80h
        jnz     .brk
        mov     byte [cs:quit], 1
.brk:   in      al, 61h                 ; (XT-style acknowledge, harmless on an AT)
        mov     ah, al
        or      al, 80h
        out     61h, al
        mov     al, ah
        out     61h, al
        mov     al, 20h
        out     20h, al
        pop     ax
        iret

msg1    db      ' frames in $'
msg2    db      ' s', 13, 10, '$'
old8    dd      0
old9    dd      0
acc     dw      0
ticks70 dw      0
frames  dw      0
limit   dw      0
quit    db      0
seed    dw      0
buf     times 103 * 160 db 0
