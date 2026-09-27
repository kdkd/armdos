/* iosys.h - IO.SYS internals (resident drivers + SYSINIT) */
#ifndef IOSYS_H
#define IOSYS_H

#include "klib.h"

/* the resident device chain (linked in this order, ARCH.md 14.2) */
extern struct devhdr con_dev, aux_dev, prn_dev, clock_dev, disk_dev;
extern struct devhdr com1_dev, lpt1_dev, lpt2_dev, lpt3_dev, com2_dev, com3_dev, com4_dev;

/* con.c */
void con_init(int enhanced_kbd);

/* clock.c */
void clock_init(void);

/* disk.c */
#define MAXUNITS 10     /* A: B: C: and up to 7 logical drives */
int  disk_init(int bootdrive, struct bpb **bpbs);   /* returns unit count */
int  disk_boot_unit(void);
extern uint8_t disk_boot_bios;

/* start.S */
extern char __res_end[], __init_start[], __init_end[];

/* SYSINIT */
void sysinit_main(int drive, struct bpb *bpb);
void sys_puts(const char *s);           /* to CON via INT 21h AH=40h (or BIOS before DOS) */

#define SYSINIT_BASE    0x90000u        /* SYSINIT's region: 0x90000-0x9FFFF */
#define SYSINIT_TEMP    0x40000u        /* scratch for loading files: 0x40000-0x8FFFF */

#endif
