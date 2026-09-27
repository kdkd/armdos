; WAITKEY.COM (x86 twin of tests/progs/waitkey.c): print "waiting", wait for
; a key with INT 21h 08h (so ^C is seen), exit with its code
        org 100h
        mov dx, msg
        mov ah, 9
        int 21h
        mov ah, 8
        int 21h
        mov ah, 4Ch
        int 21h
msg:    db 'waiting', 13, 10, '$'
