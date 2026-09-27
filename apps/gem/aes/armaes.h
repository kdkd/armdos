/*
 * armaes.h - ARM-DOS glue for the GEM AES: the routines that were 8086
 * assembler in GEM/3 (GEMSTART, GEMGMAIN, GEMASM, GEMDOSIF, GSX2, LARGE,
 * OPTIMOPT) and are now gemarm.c / gemasm.S.
 */
#ifndef GEMAES_ARMAES_H
#define GEMAES_ARMAES_H

#include <gemrt.h>

/* gemasm.S: the dispatcher */
EXTERN VOID	dsptch(VOID);
EXTERN VOID	savestat(UDA *u);
EXTERN VOID	switchto(UDA *u);
EXTERN VOID	psetup(PD *p, VOID (*pcode)());
EXTERN VOID	gotopgm(VOID);
EXTERN VOID	aes_tramp(VOID);
EXTERN LONG	get_sp(VOID);
EXTERN VOID	call_on_stack(VOID (*fn)(VOID), LONG *sp);

/* gemarm.c: DOS interface, interrupts, GSX calls */
EXTERN VOID	__EXEC(VOID);
EXTERN VOID	gem_idle(VOID);
EXTERN VOID	cli(VOID);
EXTERN VOID	sti(VOID);
EXTERN VOID	takeerr(VOID);
EXTERN WORD	crit_alert(WORD opcode);
EXTERN VOID	giveerr(VOID);
EXTERN VOID	takecpm(VOID);
EXTERN VOID	givecpm(VOID);
EXTERN VOID	retake(VOID);
EXTERN VOID	setdsss(UDA *u);
EXTERN VOID	supret(WORD x);
EXTERN VOID	far_bcha(WORD buttons);
EXTERN VOID	far_mcha(WORD *x, WORD *y);
EXTERN WORD	drawrat(WORD x, WORD y);
EXTERN VOID	justretf(VOID);
EXTERN VOID	tikcod(VOID);
EXTERN WORD	far_call(WORD (*fcode)(LONG), LONG fdata);
EXTERN WORD	pgmld(WORD handle, BYTE *ploadname, LONG *pldaddr, WORD *paccroom);

/* GEMSTART.A86 data */
EXTERN LONG	ad_psp;
EXTERN WORD	PARABEG;
EXTERN WORD	gintstk;
EXTERN LONG	tikaddr;
EXTERN LONG	tiksav;
EXTERN LONG	drwaddr;
EXTERN LONG	CMP_TICK, NUM_TICK;


#endif
