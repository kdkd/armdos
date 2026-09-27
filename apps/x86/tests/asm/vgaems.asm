; vgaems.asm - VGAEMS.COM: the things Second Reality needed from ELBOW and the
; VGA's planar modes.  Prints one line per check ("... OK" or "... BAD").
;   PSP55    INT 21h AH=55h puts SI into the new PSP's memory size word
;   IRQRACE  a hooked INT 08h (1 kHz) keeps ticking while the main loop does
;            nothing but STI/CLI (the lost-IRQ race left IRQ0 in service)
;   EMS      INT 67h: the device name, EMMXXXX0 opens, status, frame, pages,
;            allocate, map two logical pages at one physical page, deallocate
;   EGA      INT 10h AH=12h BL=10h reports an EGA/VGA
;   PLANAR   mode 12h: write mode 2 through the ports, read map select back
;   LATCH    Mode X: REP MOVSB in write mode 1 copies four planes per byte
;   DMA      an 8237 page register reads back what the program wrote
        org     100h
        cpu     386

; ---------------------------------------------------------------- PSP55
        mov     dx, cs
        add     dx, 1000h               ; a new PSP 64 KB above ours (inside our block)
        mov     si, 9ABCh
        mov     ah, 55h
        int     21h
        mov     ah, 50h                 ; back to our own PSP
        mov     bx, cs
        int     21h
        mov     es, dx
        cmp     word [es:2], 9ABCh
        mov     dx, mpsp
        call    result

; ---------------------------------------------------------------- IRQRACE
        mov     ax, 3508h
        int     21h
        mov     [old8], bx
        mov     [old8 + 2], es
        mov     ax, 2508h
        mov     dx, int8
        int     21h
        cli
        mov     al, 36h                 ; PIT channel 0: 1193 -> 1000 Hz
        out     43h, al
        mov     ax, 1193
        out     40h, al
        mov     al, ah
        out     40h, al
        sti
        mov     ecx, 400000
.race:  sti
        cli
        sti
        nop
        cli
        sti
        dec     ecx
        jnz     .race
        mov     ax, [ticks]
        mov     [t1], ax
        mov     cx, 60000               ; and it still ticks afterwards
.more:  sti
        loop    .more
        cli
        mov     al, 36h
        out     43h, al
        xor     al, al
        out     40h, al
        out     40h, al
        push    ds
        lds     dx, [old8]
        mov     ax, 2508h
        int     21h
        pop     ds
        sti
        mov     ax, [t1]
        cmp     ax, 50
        jb      .rbad
        mov     ax, [ticks]
        cmp     ax, [t1]
.rbad:  mov     dx, mrace
        call    result_a                ; ZF/CF from the last compare: OK if above

; ---------------------------------------------------------------- EMS
        xor     ax, ax
        mov     es, ax
        mov     es, [es:67h * 4 + 2]
        mov     di, 0Ah
        mov     si, emmname
        mov     cx, 8
        repe    cmpsb
        jne     .ebad
        mov     ax, 3D00h
        mov     dx, emmname
        int     21h
        jc      .ebad
        mov     bx, ax
        mov     ah, 3Eh
        int     21h
        mov     ah, 40h
        int     67h
        or      ah, ah
        jnz     .ebad
        mov     ah, 41h
        int     67h
        cmp     bx, 0E000h
        jne     .ebad
        mov     ah, 42h
        int     67h
        cmp     bx, 64                  ; at least 1 MB free
        jb      .ebad
        mov     ah, 43h
        mov     bx, 4
        int     67h
        or      ah, ah
        jnz     .ebad
        mov     [emh], dx
        mov     ax, 4400h               ; logical 0 -> physical 0
        xor     bx, bx
        int     67h
        or      ah, ah
        jnz     .ebad
        mov     ax, 0E000h
        mov     es, ax
        mov     dword [es:100h], 11223344h
        mov     ax, 4400h               ; logical 3 -> physical 0
        mov     bx, 3
        mov     dx, [emh]
        int     67h
        mov     dword [es:100h], 55667788h
        mov     ax, 4401h               ; logical 0 -> physical 1
        xor     bx, bx
        mov     dx, [emh]
        int     67h
        cmp     dword [es:4100h], 11223344h
        jne     .ebad
        cmp     dword [es:100h], 55667788h
        jne     .ebad
        mov     ah, 45h
        mov     dx, [emh]
        int     67h
        or      ah, ah
        jnz     .ebad
        mov     ah, 46h
        int     67h
        cmp     al, 40h
.ebad:  mov     dx, mems
        call    result

; ---------------------------------------------------------------- EGA
        mov     ah, 12h
        mov     bl, 10h
        int     10h
        cmp     bl, 3
        mov     dx, mega
        call    result

; ---------------------------------------------------------------- PLANAR (mode 12h)
        mov     ax, 12h
        int     10h
        mov     ax, 0A000h
        mov     es, ax
        mov     dx, 3CEh
        mov     ax, 0205h               ; write mode 2
        out     dx, ax
        mov     ax, 2008h               ; bit mask: pixel 2 of the byte
        out     dx, ax
        mov     al, [es:1000]           ; latches
        mov     byte [es:1000], 0Bh     ; colour 1011b
        mov     ax, 0005h
        out     dx, ax
        mov     ax, 0FF08h
        out     dx, ax
        xor     bl, bl                  ; read planes 3..0 back
        mov     cx, 4
.rp:    mov     al, 4
        mov     ah, cl
        dec     ah
        out     dx, ax
        mov     al, [es:1000]
        shl     bl, 1
        test    al, 20h
        jz      .rz
        or      bl, 1
.rz:    loop    .rp
        mov     ax, 0004h
        out     dx, ax
        mov     [pl], bl
        mov     ax, 3
        int     10h
        cmp     byte [pl], 0Bh
        mov     dx, mplanar
        call    result

; ---------------------------------------------------------------- BIOS12: INT 10h pixels/scroll in mode 12h
        mov     ax, 12h
        int     10h
        mov     ax, 0C09h               ; (100, 50) = 9
        xor     bh, bh
        mov     cx, 100
        mov     dx, 50
        int     10h
        mov     ax, 0C83h               ; XOR 3 -> 10
        int     10h
        mov     ah, 0Dh
        int     10h
        mov     [pl], al
        mov     ax, 0601h               ; scroll the whole screen up one text row (16 lines)
        mov     bh, 0
        xor     cx, cx
        mov     dx, 1D4Fh
        int     10h
        mov     ah, 0Dh
        xor     bh, bh
        mov     cx, 100
        mov     dx, 34
        int     10h
        mov     [pl + 1], al
        mov     ax, 3
        int     10h
        cmp     word [pl], 0A0Ah
        mov     dx, mbios
        call    result

; ---------------------------------------------------------------- LATCH (Mode X)
        mov     ax, 13h
        int     10h
        mov     dx, 3C4h
        mov     ax, 0604h               ; chain-4 off
        out     dx, ax
        mov     ax, 0A000h
        mov     es, ax
        mov     ds, ax
        mov     cx, 4                   ; plane p of offsets 0..15 = p * 16 + offset
        mov     ah, 1
.lp:    mov     al, 2
        push    ax
        out     dx, ax
        pop     ax
        push    cx
        xor     di, di
        mov     al, 4
        sub     al, cl
        shl     al, 4
        mov     cx, 16
.lf:    stosb
        inc     al
        loop    .lf
        pop     cx
        shl     ah, 1
        loop    .lp
        mov     ax, 0F02h
        out     dx, ax
        mov     dx, 3CEh
        mov     ax, 4105h               ; write mode 1 (256-colour bit kept)
        out     dx, ax
        xor     si, si
        mov     di, 8000h
        mov     cx, 16
        cld
        rep     movsb
        mov     ax, 4005h
        out     dx, ax
        mov     ax, 0204h               ; read plane 2 of offset 8005h: 2 * 16 + 5
        out     dx, ax
        mov     al, [8005h]
        push    cs
        pop     ds
        mov     [pl], al
        mov     ax, 3
        int     10h
        cmp     byte [pl], 2 * 16 + 5
        mov     dx, mlatch
        call    result

; ---------------------------------------------------------------- DMA page register
        in      al, 83h
        mov     bl, al
        mov     al, 5
        out     83h, al
        in      al, 83h
        xchg    al, bl
        out     83h, al
        cmp     bl, 5
        mov     dx, mdma
        call    result

        mov     ax, 4C00h
        int     21h

; ZF set: OK
result: push    dx
        mov     ah, 9
        int     21h
        pop     dx
        mov     dx, mok
        je      .p
        mov     dx, mbad
.p:     mov     ah, 9
        int     21h
        ret
; CF clear and ZF clear (above): OK
result_a:
        push    dx
        mov     ah, 9
        int     21h
        pop     dx
        mov     dx, mok
        ja      .p
        mov     dx, mbad
.p:     mov     ah, 9
        int     21h
        ret

int8:   push    ax
        inc     word [cs:ticks]
        mov     al, 20h
        out     20h, al
        pop     ax
        iret

old8    dd      0
ticks   dw      0
t1      dw      0
emh     dw      0
pl      db      0, 0
emmname db      'EMMXXXX0', 0
mpsp    db      'PSP55 $'
mrace   db      'IRQRACE $'
mems    db      'EMS $'
mega    db      'EGA $'
mplanar db      'PLANAR $'
mlatch  db      'LATCH $'
mbios   db      'BIOS12 $'
mdma    db      'DMA $'
mok     db      'OK', 13, 10, '$'
mbad    db      'BAD', 13, 10, '$'
