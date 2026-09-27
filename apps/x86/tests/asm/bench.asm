; bench.asm - a CPU-bound x86 benchmark for X86.EXE (interpreter vs /JIT).
; The BYTE sieve (8190 flags) x 50, then a mixed loop (string compare,
; multiply/divide, table lookups, calls).  Prints a checksum so every mode
; must compute the same thing.
        org     100h
        cpu     386
start:  mov     word [iter], 50
.again: call    sieve
        add     [sum], ax
        dec     word [iter]
        jnz     .again
        mov     cx, 3000
.mix:   push    cx
        call    mixed
        pop     cx
        loop    .mix
        ; print the checksum
        mov     ax, [sum]
        call    hex4
        mov     ax, [sum2]
        call    hex4
        mov     dx, crlf
        mov     ah, 9
        int     21h
        mov     ax, 4C00h
        int     21h

sieve:  mov     di, flags               ; the classic: flags[i] = 1
        mov     cx, 8191
        mov     al, 1
        push    ds
        pop     es
        cld
        rep     stosb
        xor     bx, bx                  ; count
        xor     si, si                  ; i
.l1:    cmp     byte [flags+si], 0
        je      .n1
        mov     ax, si
        add     ax, ax
        add     ax, 3                   ; prime = i + i + 3
        mov     di, si
        add     di, ax                  ; k = i + prime
.l2:    cmp     di, 8190
        ja      .c
        mov     byte [flags+di], 0
        add     di, ax
        jmp     .l2
.c:     inc     bx
.n1:    inc     si
        cmp     si, 8190
        jbe     .l1
        mov     ax, bx
        ret

mixed:  mov     si, str1
        mov     di, str2
        mov     cx, 40
        repe    cmpsb
        mov     ax, cx
        mov     bx, 1234
        mul     bx
        mov     bx, 77
        div     bx
        add     [sum2], dx
        xor     bx, bx
        mov     cx, 64
.t:     mov     al, bl
        and     al, 15
        push    bx
        mov     bx, table
        xlat
        pop     bx
        add     [sum2], al
        call    small
        inc     bx
        loop    .t
        ret
small:  rol     word [sum2], 1
        xor     word [sum2], 0x5A5A
        ret

hex4:   mov     cx, 4
.h:     rol     ax, 4
        push    ax
        and     al, 15
        add     al, '0'
        cmp     al, '9'
        jbe     .d
        add     al, 7
.d:     mov     dl, al
        mov     ah, 2
        int     21h
        pop     ax
        loop    .h
        ret

iter    dw      0
sum     dw      0
sum2    dw      0
crlf    db      13, 10, '$'
str1    db      'The quick brown fox jumps over the lazy dog'
str2    db      'The quick brown fox jumps over the lazy cat'
table   db      3, 1, 4, 1, 5, 9, 2, 6, 5, 3, 5, 8, 9, 7, 9, 3
flags   times 8192 db 0
