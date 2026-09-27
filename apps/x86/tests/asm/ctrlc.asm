; ctrlc.asm - CTRLC.COM: an INT 23h handler that says so and continues;
; reads keys with AH=01h until 'q'.
        org     100h
        mov     ax, 2523h
        mov     dx, int23
        int     21h
.k:     mov     ah, 1
        int     21h
        cmp     al, 'q'
        jne     .k
        mov     ah, 9
        mov     dx, bye
        int     21h
        mov     ax, 4C05h
        int     21h
int23:  push    ax
        push    dx
        mov     ah, 9
        mov     dx, caught
        int     21h
        pop     dx
        pop     ax
        iret                            ; continue the program
caught  db      '[INT 23h: caught]$'
bye     db      13, 10, 'bye', 13, 10, '$'
