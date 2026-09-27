/*
 * jitasm.S - entry and exit of translated x86 code (jit.c).
 *
 * Inside translated code:  r11 = &cpu (the X86 struct), r10 = rpt (read
 * page table), r9 = wpt (write page table), r8 = &irq_pending; r0-r7, r12,
 * lr are scratch (r4-r7 survive calls to C helpers).  All x86 state lives in
 * the cpu struct between instructions, so a block can stop anywhere.
 */
        .syntax unified
        .arm
        .text

/* uint32_t jit_enter(void *code) - run translated code; returns 0 (eip set),
 * 1 (stopped for an interrupt / helper request) or the address of a chain
 * slot the dispatcher may link to the block for cpu.eip */
        .global jit_enter
        .type   jit_enter, %function
        .balign 4
jit_enter:
        push    {r3-r11, lr}            @ 40 bytes: sp stays 8-byte aligned
        ldr     r11, =cpu
        ldr     r10, =rpt
        ldr     r9, =wpt
        ldr     r8, =irq_pending
        bx      r0
        .size   jit_enter, . - jit_enter

        .global jit_exit
        .type   jit_exit, %function
jit_exit:
        pop     {r3-r11, pc}
        .size   jit_exit, . - jit_exit

/* a block ended at a computed target (RET, JMP r/m): cpu.eip is set; carry
 * on in the next block if it is translated and nothing is pending */
        .global jit_exit_dyn
        .type   jit_exit_dyn, %function
jit_exit_dyn:
        bl      jit_dyn_lookup
        cmp     r0, #0
        bxne    r0
        pop     {r3-r11, pc}
        .size   jit_exit_dyn, . - jit_exit_dyn
        .ltorg
