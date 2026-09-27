; TRASH.COM (x86 twin of tests/progs/trash.c): fill the top 96 KB of our
; memory block, where COMMAND's transient part lives
        org 100h
        mov ax, [2]             ; first segment beyond our block
        sub ax, 96 * 64         ; 96 KB below it
        mov cx, 96 * 64         ; paragraphs to fill
fill:   mov es, ax
        xor di, di
        push cx
        mov cx, 16
        mov al, 0AAh
        rep stosb
        pop cx
        mov ax, es
        inc ax
        loop fill
        mov ax, 4C00h
        int 21h
