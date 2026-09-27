; mzstub.asm - the 8086 program inside every ARM-DOS .EXE (nasm -f bin).
; On a real x86 PC, DOS runs this: print the message and exit with code 1.
; elf2exe.mjs embeds the assembled bytes; tests/run-tests.mjs checks they match.
        bits 16
        org 0
start:  push cs
        pop ds
        mov dx, msg
        mov ah, 09h
        int 21h
        mov ax, 4C01h
        int 21h
msg:    db "This program requires an ARM processor.", 13, 10, "$"
