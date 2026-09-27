@ ARM-PC device test ROM: startup and exception vectors.
@ The ROM is linked at 0xFFF00000; the high vectors live at 0xFFFF0000.
    .syntax unified
    .arm
    .section .header, "ax"
    .ascii "ARM-PC DEVICE TEST ROM\0"

    .section .vectors, "ax"
    .global _vectors
_vectors:
    ldr pc, =reset
    ldr pc, =und_entry
    ldr pc, =svc_entry
    ldr pc, =pabt_entry
    ldr pc, =dabt_entry
    nop
    ldr pc, =irq_entry
    ldr pc, =fiq_entry
    .ltorg

    .text
reset:
    msr   cpsr_c, #0xd2          @ IRQ mode stack
    ldr   sp, =0x9E000
    msr   cpsr_c, #0xd7          @ ABT
    ldr   sp, =0x9D000
    msr   cpsr_c, #0xdb          @ UND
    ldr   sp, =0x9C000
    msr   cpsr_c, #0xd3          @ SVC
    ldr   sp, =0x9F000
    ldr   r0, =__data_start
    ldr   r1, =__data_end
    ldr   r2, =__data_load
3:  cmp   r0, r1
    ldrlo r3, [r2], #4
    strlo r3, [r0], #4
    blo   3b
    ldr   r0, =__bss_start
    ldr   r1, =__bss_end
    mov   r2, #0
1:  cmp   r0, r1
    strlo r2, [r0], #4
    blo   1b
    bl    main
2:  b     2b

irq_entry:
    sub   lr, lr, #4
    stmfd sp!, {r0-r3, r12, lr}
    bl    irq_handler
    ldmfd sp!, {r0-r3, r12, pc}^

svc_entry:
    movs  pc, lr
und_entry:
    stmfd sp!, {r0-r3, r12, lr}
    mov   r0, #1
    sub   r1, lr, #4
    bl    fault
    ldmfd sp!, {r0-r3, r12, pc}^
pabt_entry:
    mov   r0, #3
    sub   r1, lr, #4
    bl    fault
    b     .
dabt_entry:
    stmfd sp!, {r0-r3, r12, lr}
    mov   r0, #4
    sub   r1, lr, #8
    bl    fault
    ldmfd sp!, {r0-r3, r12, lr}
    subs  pc, lr, #4             @ skip the faulting instruction
fiq_entry:
    subs  pc, lr, #4

    .global wfi
wfi:
    mov   r0, #0
    mcr   p15, 0, r0, c7, c0, 4
    bx    lr
    .global irq_on
irq_on:
    mrs   r0, cpsr
    bic   r0, r0, #0x80
    msr   cpsr_c, r0
    bx    lr
    .global irq_off
irq_off:
    mrs   r0, cpsr
    orr   r0, r0, #0x80
    msr   cpsr_c, r0
    bx    lr
    .ltorg

    .section .rodata
    .global font8x16
font8x16:
    .incbin FONTFILE
