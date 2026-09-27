; UPCASE.COM (x86 twin of tests/progs/upcase.c): stdin to stdout, a-z upper-cased
        org 100h
again:  mov ah, 3Fh
        xor bx, bx
        mov cx, 128
        mov dx, buf
        int 21h
        jc out
        or ax, ax
        jz out
        mov cx, ax
        mov di, ax
        mov si, buf
up:     mov al, [si]
        cmp al, 'a'
        jb nx
        cmp al, 'z'
        ja nx
        sub byte [si], 20h
nx:     inc si
        loop up
        mov cx, di
        mov ah, 40h
        mov bx, 1
        mov dx, buf
        int 21h
        jmp again
out:    mov ax, 4C00h
        int 21h
buf:
