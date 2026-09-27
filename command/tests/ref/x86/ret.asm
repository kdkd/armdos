; RET.COM (x86 twin of tests/progs/ret.c): exit with the decimal code in the tail
        org 100h
        mov si, 81h
skip:   lodsb
        cmp al, ' '
        je skip
        cmp al, 9
        je skip
        xor bx, bx
num:    cmp al, '0'
        jb done
        cmp al, '9'
        ja done
        sub al, '0'
        xor ah, ah
        mov cx, ax
        mov ax, bx
        mov dx, 10
        mul dx
        add ax, cx
        mov bx, ax
        lodsb
        jmp num
done:   mov al, bl
        mov ah, 4Ch
        int 21h
