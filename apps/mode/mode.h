/* mode.h - MODE.COM: the resident part's interface */
#ifndef MODE_H
#define MODE_H

#include <stdint.h>
#include <armdos.h>

/* The resident part (mdres.c, up to mdend.S's mode_res_end) sits in
   .text.unlikely.* sections, which the SDK link script places right after
   crt0's _start: when MODE has to leave its "resident portion" behind (serial
   retry, printer retry, LPTn:=COMm) it keeps PSP..mode_res_end with INT 21h
   AH=31h, as MODE 4.00 keeps its RESCODE. The flags are given as "ax" (gas
   insists for .text.*; the "@" hides GCC's own); there is no memory
   protection, the variables are written all the same. */
#define RES   __attribute__((section(".text.unlikely.mode_data,\"ax\",%progbits @")))
#define RESFN __attribute__((section(".text.unlikely.mode_code"), noinline))

/* the resident data; the transient MODE finds a resident copy with
   INT 17h AX=DD00h -> AX=4D4Fh ("MO"), BX = pointer to this */
struct moderes {
    char sig[4];                /* "MODE" */
    uint8_t reroute[3];         /* LPT1-3: 0 or COM number 1-4 */
    uint8_t lptretry[3];        /* LPT1-3: 0 none, else 'E' 'B' 'R' */
    uint8_t comretry[4];        /* COM1-4: 0 none, else 'E' 'B' 'R' */
};

extern struct moderes mode_res;
extern armdos_vect_t mode_old14, mode_old17;
void mode_int14(struct armregs *f);
void mode_int17(struct armregs *f);
extern char mode_res_end[];

#endif
