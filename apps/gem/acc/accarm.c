/*
 * accarm.c - ARM-DOS machine layer of the GEM CalClock desk accessory.
 *
 * Replaces ACCSTART.A86 and CALCASM.A86 of the GEM/3 CalClock sources
 * (Copyright 1985 Digital Research; GNU GPL v2 via Caldera 1999).  The
 * accessory is an ordinary ARM-DOS program image that the AES loads into
 * memory of its own (aes/gemarm.c, pgmld) and starts as a GEM process;
 * the SDK start-up code runs as for any program.
 */
#include <portab.h>
#include "machine.h"
#include "accarm.h"
#include <armdos.h>

/* GEM stays resident under the DOS programs it runs: never let a heap that
 * outgrows conventional memory take the extended memory (libdos startup.c
 * would grab the largest free XMS block, leaving none for, say, EDIT). */
unsigned _armdos_xms_kb = ~0u;

EXTERN ULONG	DOS_AX, DOS_BX, DOS_CX, DOS_DX, DOS_SI, DOS_DI, DOS_ES, DOS_DS, DOS_ERR;
EXTERN WORD	spol_prn;
EXTERN VOID	ccs_main(VOID);

extern void *memset(void *, int, unsigned long);

/* the AES: svc #0xEF, CX (r2) = 200, BX (r1) = the parameter block */
VOID crystal(LONG pb)
{
	register LONG r1 __asm__("r1") = pb;
	register unsigned r2 __asm__("r2") = 200;
	__asm__ volatile("svc #0xEF" : : "r"(r1), "r"(r2) : "memory", "cc");
}

VOID __DOS(VOID)
{
	struct armregs r;
	int cf;
	memset(&r, 0, sizeof(r));
	r.r0 = DOS_AX & 0xFFFF;
	r.r1 = DOS_BX;
	r.r2 = DOS_CX;
	r.r3 = DOS_DX;
	r.r4 = DOS_SI;
	r.r5 = DOS_DI;
	r.r7 = DOS_DS;
	r.r8 = DOS_ES;
	cf = _armdos_int21(&r);
	DOS_AX = r.r0 & 0xFFFF;
	DOS_BX = r.r1;
	DOS_CX = r.r2;
	DOS_DX = r.r3;
	DOS_SI = r.r4;
	DOS_DI = r.r5;
	DOS_DS = r.r7;
	DOS_ES = r.r8;
	DOS_ERR = cf ? 1 : 0;
}

VOID LBCOPY(LONG d, LONG s, WORD n)
{
	UBYTE *pd = (UBYTE *) d, *ps = (UBYTE *) s;
	while (n-- > 0)
		*pd++ = *ps++;
}

WORD acc_toupper(WORD ch)
{
	ch &= 0xFF;
	if (ch >= 'a' && ch <= 'z')
		ch -= 32;
	return ch;
}

/* TRUE if equal */
WORD acc_strcmp(BYTE *p1, BYTE *p2)
{
	while (*p1 == *p2)
	{
		if (!*p1)
			return TRUE;
		p1++;
		p2++;
	}
	return FALSE;
}

WORD acc_strlen(BYTE *p)
{
	WORD n = 0;
	while (*p++)
		n++;
	return n;
}

VOID beep(VOID)
{
	struct armregs r;
	memset(&r, 0, sizeof(r));
	r.r0 = 0x0200;
	r.r3 = 7;
	_armdos_int21(&r);
}

/* the print spooler talks to INT 17h */
VOID spol_int(VOID)
{
}

static unsigned int17(unsigned ax)
{
	register unsigned r0 __asm__("r0") = ax;
	register unsigned r3 __asm__("r3") = spol_prn;
	__asm__ volatile("svc #0x17" : "+r"(r0) : "r"(r3) : "r1", "r2", "memory", "cc");
	return r0;
}

/* 1 if the printer took the character */
WORD spol_out(WORD ch)
{
	struct armregs r;
	if ((int17(0x0200) >> 8 & 0x90) != 0x90)
		return 0;
	return (int17(ch & 0xFF) & 0x100) ? 0 : 1;
}

/* ACCSTART.A86 called main(); the C main is in ccsmain.c */
int main(int argc, char **argv)
{
	(void)argc; (void)argv;
	ccs_main();
	for (;;)
		;
}
