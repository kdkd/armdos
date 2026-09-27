; Patch a hot routine once, then give its translation cooldown time to expire.
; The headless test inspects its dead and replacement blocks at the two pauses.
        org 100h
        cpu 386
        mov cx, 100
warm:   call worker
        loop warm
        mov dx, ready
        call pause
        mov word [worker+1], 4321h
        mov bp, 32
outer:  mov cx, 20000
inner:  call worker
        loop inner
        dec bp
        jnz outer
        cmp ax, 4324h
        jne bad
        mov dx, done
        call pause
        mov ax, 4c00h
        int 21h
bad:    mov dx, fail
        mov ah, 9
        int 21h
        mov ax, 4c01h
        int 21h
pause:  mov ah, 9
        int 21h
        xor ah, ah
        int 16h
        ret
ready   db 'COOL WARM',13,10,'$'
done    db 'COOL DONE',13,10,'$'
fail    db 'COOL BAD',13,10,'$'
        times 200h-($-$$+100h) db 90h
worker: mov ax, 1234h
        add ax, 3
        ret
