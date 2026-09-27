/*
 * trap.S - how DEBUG hands the CPU to the program being debugged and gets it
 * back.
 *
 * DEBUG runs in SYS mode like every ARM-DOS program.  To start the program it
 * executes a BKPT at dbg_trap: the prefetch abort becomes INT 0Eh (bios/start.S),
 * DEBUG's INT 0Eh handler (run.c) recognises the address, replaces the whole
 * register frame with the program's registers (r0-r15 and CPSR, T bit
 * included) and returns - the BIOS's exception return "resumes" the program.
 * When the program reaches a planted BKPT, the same handler saves the frame as
 * the program's registers and rewrites it so that the exception return lands
 * just after dbg_trap, on DEBUG's stack, with r0 = the reason.  So from C,
 * dbg_run() is simply a call that returns when the program stops.
 *
 * When the program terminates (INT 20h, INT 21h AH=4Ch, ^C abort, a fault),
 * DOS continues at the INT 22h address in its PSP: dbg_term22, which unwinds
 * to the same place with r0 = STOP_TERM (the stack pointer DOS gives us there
 * is its own idea; we use the one saved by dbg_run).
 */
        .syntax unified
        .arm
        .text

        .global dbg_run
        .global dbg_trap
        .global dbg_term22
        .global dbg_ret_sp

        .type   dbg_run, %function
dbg_run:
        push    {r4-r11, lr}
        ldr     r12, =dbg_ret_sp
        str     sp, [r12]
dbg_trap:
        bkpt    #0xDBDB                 @ -> INT 0Eh -> the program runs
        pop     {r4-r11, pc}            @ r0 = why it stopped
        .size   dbg_run, . - dbg_run

        .type   dbg_term22, %function
dbg_term22:
        ldr     r12, =dbg_ret_sp
        ldr     sp, [r12]
        mov     r0, #1                  @ STOP_TERM
        pop     {r4-r11, pc}
        .size   dbg_term22, . - dbg_term22

        .ltorg

/* int dbg_setjmp(uint32_t b[10]) / void dbg_longjmp(uint32_t b[10], int v) */
        .global dbg_setjmp
        .type   dbg_setjmp, %function
dbg_setjmp:
        stmia   r0, {r4-r11, sp, lr}
        mov     r0, #0
        bx      lr
        .size   dbg_setjmp, . - dbg_setjmp

        .global dbg_longjmp
        .type   dbg_longjmp, %function
dbg_longjmp:
        ldmia   r0, {r4-r11, sp, lr}
        movs    r0, r1
        moveq   r0, #1
        bx      lr
        .size   dbg_longjmp, . - dbg_longjmp

/* int dbg_absdisk(int write, struct armregs *r): INT 25h / 26h with r0-r3, -> CF */
        .global dbg_absdisk
        .type   dbg_absdisk, %function
dbg_absdisk:
        push    {r4-r11, lr}
        mov     r4, r1
        cmp     r0, #0
        ldmia   r4, {r0-r3}
        bne     1f
        svc     #0x25
        b       2f
1:      svc     #0x26
2:      stmia   r4, {r0-r3}
        movcs   r0, #1
        movcc   r0, #0
        pop     {r4-r11, pc}
        .size   dbg_absdisk, . - dbg_absdisk

        .bss

        .align  2
dbg_ret_sp:
        .space  4
