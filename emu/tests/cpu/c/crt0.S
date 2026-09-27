@ Bare-metal startup shared by the C CPU tests. Runs on QEMU versatilepb
@ (r1 = 0x183 at entry, output/exit via semihosting) and on our CPU
@ (r1 != 0x183, output to port 0xE9, exit via port 0xF4).
    .syntax unified
    .arm
    .section .text.start
    .global _start
    .type _start, %function
_start:
    mov   r4, r1
    mrs   r0, cpsr
    bic   r0, r0, #0x100          @ QEMU sets the (v6) A bit at reset
    msr   cpsr_cx, r0
    .fpu  vfp
    mov   r0, #0x40000000         @ FPEXC.EN: enable the VFP (disabled at reset)
    vmsr  fpexc, r0
    mrc   p15, 0, r0, c1, c0, 0
    bic   r0, r0, #0x2000         @ low vectors on both machines
    mcr   p15, 0, r0, c1, c0, 0
    @ per-mode stacks
    msr   cpsr_c, #0xd1           @ FIQ
    ldr   sp, =0x7F0000
    msr   cpsr_c, #0xd2           @ IRQ
    ldr   sp, =0x7E0000
    msr   cpsr_c, #0xd7           @ ABT
    ldr   sp, =0x7D0000
    msr   cpsr_c, #0xdb           @ UND
    ldr   sp, =0x7C0000
    msr   cpsr_c, #0xdf           @ SYS (== USR bank)
    ldr   sp, =0x7B0000
    msr   cpsr_c, #0xd3           @ SVC
    ldr   sp, =0x800000
    @ clear bss
    ldr   r0, =__bss_start
    ldr   r1, =__bss_end
    mov   r2, #0
1:  cmp   r0, r1
    strlo r2, [r0], #4
    blo   1b
    ldr   r2, =board_id
    str   r4, [r2]
    bl    main
    bl    exit
2:  b     2b

    .global semihost
    .type semihost, %function
semihost:                         @ r0 = op, r1 = arg
    svc   0x123456
    bx    lr

    .global exit
    .type exit, %function
exit:
    ldr   r2, =board_id
    ldr   r2, [r2]
    ldr   r3, =0x183
    cmp   r2, r3
    bne   1f
    mov   r1, r0
    ldr   r0, =0x18
    ldr   r1, =0x20026
    svc   0x123456
1:  ldr   r2, =0x100000F4
    strb  r0, [r2]
2:  b     2b
    .ltorg

    .bss
    .global board_id
board_id: .word 0
