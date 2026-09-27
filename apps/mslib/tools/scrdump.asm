; scrdump.asm - SCRDUMP.COM for the REAL MS-DOS 4.00 (8086, nasm -f bin):
; "SCRDUMP FILE" writes the 80x25 colour text screen (B800:0000, 4000 bytes
; of character/attribute pairs) to FILE.  Used by dos400run.sh to capture
; what utilities print on the console (stderr cannot be redirected in DOS 4).
        org 100h
        mov si, 81h
skip:   lodsb
        cmp al, ' '
        je skip
        cmp al, 0Dh
        je done
        lea dx, [si-1]
find:   lodsb
        cmp al, ' '
        je term
        cmp al, 0Dh
        jne find
term:   mov byte [si-1], 0
        mov ah, 3Ch             ; create
        xor cx, cx
        int 21h
        jc done
        mov bx, ax
        push ds
        mov ax, 0B800h
        mov ds, ax
        xor dx, dx
        mov cx, 4000
        mov ah, 40h             ; write
        int 21h
        pop ds
        mov ah, 3Eh
        int 21h
done:   mov ax, 4C00h
        int 21h
