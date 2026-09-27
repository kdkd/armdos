/*
 * armdos.c - ARM-DOS: C stand-ins for FDISK's 8086 assembly (orig/*.ASM)
 * and the MS C run-time behaviour it relied on.
 *
 *  - reboot()            REBOOT.ASM: BIOS data 0040:0072 = 1234h (warm boot,
 *                        no memory test) and a jump to the reset vector
 *                        (FFFF:0000 on the PC, 0xFFFF0000 on the ARM-PC) -
 *                        what the BIOS's own Ctrl-Alt-Del does;
 *  - armdos_ignore_ctrl_c()  signal(SIGINT, SIG_IGN) in MS C: an INT 23h
 *                        handler that lets DOS carry on;
 *  - _mslib_parse_features   FDISK's _PARSE.ASM feature switches.
 *
 * BOOTREC.ASM/FDBOOT.ASM (the x86 master boot record FDISK writes to an
 * unpartitioned disk) are replaced by the ARM-DOS MBR (disk/mbr.S), turned
 * into bootrec.c at build time (app.mk).
 *
 * Portions (c) Microsoft Corp. (MS-DOS 4.0 CMD/FDISK), MIT License.
 */
#include <dos.h>
#include <armdos.h>
#include "mslib.h"

/* FDISK's _PARSE.ASM: only switches, numbers and value lists 1-2 (DateSW
   stays at its default) */
const unsigned _mslib_parse_features = MSLIB_PARSE_DATE | MSLIB_PARSE_NUM |
    MSLIB_PARSE_SW | MSLIB_PARSE_VAL1 | MSLIB_PARSE_VAL2;

void reboot(void)
{
    armdos_disable();
    *(volatile unsigned short *)0x472 = 0x1234;
    ((void (*)(void))0xFFFF0000u)();
}

static void ignore_break(struct armregs *f)
{
    f->cpsr &= ~ARM_CPSR_C;             /* carry on */
}

void armdos_ignore_ctrl_c(void)
{
    armdos_setvect(0x23, ignore_break);
}
