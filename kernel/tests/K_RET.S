/* k_ret.S - a raw .COM that returns with "bx lr" (lr = PSP|1 -> INT 20h) */
        .syntax unified
        .arm
        .section .text._start, "ax"
        .global _start
_start: bx      lr
