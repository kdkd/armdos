/*
 * resstart.S - entry points of COMMAND's resident part (res.c).
 *
 *   _start           DOS enters here (ARCH.md 8): r0 = PSP, r1 = load base
 *   lodcom_entry     INT 22h (terminate address): DOS aborted COMMAND itself
 *   res_exec_entry   the transient asks us to run a program
 *   res_call_on_stack(fn, arg, reason, sp): switch stacks and call fn(arg, reason)
 *
 * The first three start on the resident stack; the transient's stack is in
 * its own block, which is freed while a program runs.
 */
        .syntax unified
        .arm

        .section .text._start, "ax", %progbits
        .global _start
        .type   _start, %function
_start:
        ldr     r3, =res_stack
        add     sp, r3, #1536
        mov     fp, #0
        mov     lr, #0
        bl      res_start               @ (psp, base) - never returns
1:      b       1b
        .size   _start, . - _start

        .section .text.lodcom_entry, "ax", %progbits
        .global lodcom_entry
        .type   lodcom_entry, %function
lodcom_entry:
        ldr     r3, =res_stack
        add     sp, r3, #1536
        bl      res_lodcom_c
1:      b       1b
        .size   lodcom_entry, . - lodcom_entry

        .section .text.res_exec_entry, "ax", %progbits
        .global res_exec_entry
        .type   res_exec_entry, %function
res_exec_entry:
        ldr     r3, =res_stack
        add     sp, r3, #1536
        bl      res_exec_c
1:      b       1b
        .size   res_exec_entry, . - res_exec_entry

        .section .text.res_call_on_stack, "ax", %progbits
        .global res_call_on_stack
        .type   res_call_on_stack, %function
res_call_on_stack:
        mov     sp, r3
        mov     r12, r0
        mov     r0, r1
        mov     r1, r2
        mov     fp, #0
        blx     r12
1:      b       1b
        .size   res_call_on_stack, . - res_call_on_stack
