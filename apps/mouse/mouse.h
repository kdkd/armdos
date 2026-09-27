/* mouse.h - MOUSE.COM: declarations shared by the resident part and the installer */
#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>
#include <armdos.h>

/* The resident part lives in .text.unlikely.* sections: the SDK link script
   puts those right after crt0's _start, before all other code, in link
   order (mres.S, mres.c, then mend.S's mouse_res_end). The installer keeps
   the memory from the PSP to mouse_res_end with INT 21h AH=31h. Variables
   and tables of the resident part go into such sections as well. */
/* The flags are given as "ax" (gas insists on it for .text.* sections; the
   "@" turns the flags GCC appends into a comment). There is no memory
   protection: the variables are written all the same. */
#define RES   __attribute__((section(".text.unlikely.mouse_data,\"ax\",%progbits @")))
#define RESRO __attribute__((section(".text.unlikely.mouse_rodata,\"ax\",%progbits @")))
#define RESFN __attribute__((section(".text.unlikely.mouse_code"), noinline))

/* mres.S */
int m_int10(struct armregs *r);
int m_int15(struct armregs *r);
void m_callhandler(uint32_t fn, struct armregs *r);   /* r0-r5 in, blx fn */
uint32_t m_irqoff(void);
void m_irqrestore(uint32_t cpsr);

/* mres.c */
void mouse_int33(struct armregs *f);
void mouse_int10(struct armregs *f);
void mouse_event(uint32_t status, int32_t dx, int32_t dy, int32_t dz);
extern armdos_vect_t mouse_old10, mouse_old33;
extern const char mouse_sig[8];
void mouse_reset(void);

/* mend.S */
extern char mouse_res_end[];

#define MOUSE_VERSION 0x0100          /* INT 33h AX=0024h: BX = 1.00 */

#endif
