; ARGS.COM (x86 twin of tests/progs/args.c): show the command tail and the
; two FCBs COMMAND parsed:  [tail]  / 1:d NAMEEXT / 2:d NAMEEXT
        org 100h
        mov dx, lb
        mov cx, 1
        call wr
        mov dx, 81h
        xor cx, cx
        mov cl, [80h]
        call wr
        mov dx, rb
        mov cx, 3
        call wr
        mov byte [fc], '1'
        mov si, 5Ch
        call fcb
        mov byte [fc], '2'
        mov si, 6Ch
        call fcb
        mov ax, 4C00h
        int 21h
fcb:    mov al, [si]
        add al, '0'
        mov [fd], al
        mov di, fn
        inc si
        mov cx, 11
        rep movsb
        mov dx, fc
        mov cx, fend - fc
        call wr
        ret
wr:     mov ah, 40h
        mov bx, 1
        int 21h
        ret
lb:     db '['
rb:     db ']', 13, 10
fc:     db '1:'
fd:     db '0 '
fn:     times 11 db ' '
        db 13, 10
fend:
