/* k_call5.S - a raw .COM that calls DOS through PSP:0050h, then AH=4Ch */
        .syntax unified
        .arm
        .section .text._start, "ax"
        .global _start
_start: mov     r10, r0                 @ PSP
        adr     r3, msg                 @ DS:DX
        mov     r0, #0x0900
        add     r12, r10, #0x50
        blx     r12                     @ "call PSP:0050h"
        mov     r0, #0x4C00
        orr     r0, r0, #0x21
        svc     #0x21
msg:    .ascii  "PSP:50h call ok\r\n$"
