/* pic.S - a hand-written position-independent .COM (no C runtime).
 * Entered at PSP+0x100 with r0 = PSP, lr = PSP. Prints via INT 21h AH=09h
 * and returns to PSP:0 ("bx lr" = INT 20h), like a 1981 .COM. */
        .syntax unified
        .arm
        .section .text._start, "ax", %progbits
        .global _start
_start:
        mov     r4, lr
        adr     r3, msg                 @ DS:DX (flat, PC-relative)
        mov     r0, #0x0900
        svc     #0x21
        bx      r4
msg:    .ascii  "Position-independent ARM .COM\r\n$"
