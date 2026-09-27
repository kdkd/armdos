/* entry.S - the XMS entry point: registers in, registers out (ARCH.md 15).
   Called with BLX from programs; may clobber anything but sp and r7-r11. */
        .syntax unified
        .arm
        .section .text.xms_entry, "ax"
        .global xms_entry
        .type   xms_entry, %function
xms_entry:
        push    {r7-r11, lr}
        sub     sp, sp, #32
        stmia   sp, {r0-r6}
        mov     r0, sp
        bl      xms_dispatch
        ldmia   sp, {r0-r6}
        add     sp, sp, #32
        pop     {r7-r11, lr}
        bx      lr
