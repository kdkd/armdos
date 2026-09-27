; Fixed-work dot products using Second Reality's IMUL / ADD / ADC / SHRD
; patterns. Synthetic coefficients and checked results, no timers or audio.
; Run with ELBOW /STATS to compare ARM work across translator builds.
        org 100h
        cpu 386
        mov bp, 60000
again:
        mov eax, 1000
        imul dword [v32]
        mov ebx, eax
        mov ecx, edx
        mov eax, -2000
        imul dword [v32+4]
        add ebx, eax
        adc ecx, edx
        mov eax, 3000
        imul dword [v32+8]
        add ebx, eax
        adc ecx, edx
        shrd ebx, ecx, 14
        add ebx, 123
        cmp ebx, 1088123
        jne bad
        mov ax, 1000
        imul word [v16]
        mov bx, ax
        mov cx, dx
        mov ax, -2000
        imul word [v16+2]
        add bx, ax
        adc cx, dx
        mov ax, 3000
        imul word [v16+4]
        add bx, ax
        adc cx, dx
        shrd bx, cx, 14
        cmp bx, 4000
        jne bad
        dec bp
        jnz again
        mov dx, ok
        mov ah, 9
        int 21h
        mov ax, 4c00h
        int 21h
bad:    mov dx, fail
        mov ah, 9
        int 21h
        mov ax, 4c01h
        int 21h
v32     dd 100000h, -200000h, 400000h
v16     dw 8192, -4096, 16384
ok      db 'MATH OK',13,10,'$'
fail    db 'MATH BAD',13,10,'$'
