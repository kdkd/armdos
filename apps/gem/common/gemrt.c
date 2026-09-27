/*
 * gemrt.c - the small run-time routines shared by the ARM-DOS GEM AES and
 * the GEM Desktop: the DOS register interface (__DOS), the VDI call (gsx2)
 * and the long-pointer and string helpers that were 8086 assembler in
 * GEM/3 (GEMDOSIF.A86 / DESKOSIF.A86, GSX2.A86, LARGE.A86 / LONGASM.A86,
 * OPTIMOPT.A86; Copyright 1987 Digital Research, GNU GPL v2 via Caldera).
 */
#include <portab.h>
#include <armdos.h>
#include <gemrt.h>

/* GEM stays resident under the DOS programs it runs: never let a heap that
 * outgrows conventional memory take the extended memory (libdos startup.c
 * would grab the largest free XMS block, leaving none for, say, EDIT). */
unsigned _armdos_xms_kb = ~0u;

extern void *memcpy(void *, const void *, unsigned long);
extern void *memset(void *, int, unsigned long);

#define LLSET(x, y) gem_llset((LONG)(x), (LONG)(y))
static inline LONG gem_llset(LONG a, LONG v)
{
	*(UWORD *)a = (UWORD)v;
	*(UWORD *)(a + 2) = (UWORD)((ULONG)v >> 16);
	return v;
}

EXTERN ULONG	DOS_AX, DOS_BX, DOS_CX, DOS_DX, DOS_DS, DOS_ES, DOS_SI, DOS_DI, DOS_ERR;
EXTERN WORD	contrl[], intin[], ptsin[], intout[], ptsout[];

/* ------------------------------------------------------------- DOS --- */

WORD dos_regs(struct armregs *r)
{
	memset(r, 0, sizeof(*r));
	r->r0 = DOS_AX & 0xFFFF;
	r->r1 = DOS_BX;
	r->r2 = DOS_CX;
	r->r3 = DOS_DX;
	r->r4 = DOS_SI;
	r->r5 = DOS_DI;
	r->r7 = DOS_DS;
	r->r8 = DOS_ES;
	return 0;
}

void dos_back(struct armregs *r, int cf)
{
	DOS_AX = r->r0 & 0xFFFF;
	DOS_BX = r->r1;
	DOS_CX = r->r2;
	DOS_DX = r->r3;
	DOS_SI = r->r4;
	DOS_DI = r->r5;
	DOS_DS = r->r7;
	DOS_ES = r->r8;
	DOS_ERR = cf ? 1 : 0;
}

VOID __DOS(VOID)
{
	struct armregs r;
	int cf;
	dos_regs(&r);
#ifdef GEMDEBUG
	if ((DOS_AX >> 8) == 0x4E || (DOS_AX >> 8) == 0x1A)
	{
		gem_dbgx("DOS r0 ", r.r0);
		gem_dbgx("    r2 ", r.r2);
		gem_dbgx("    r3 ", r.r3);
	}
#endif
	cf = _armdos_int21(&r);
#ifdef GEMDEBUG
	if ((DOS_AX >> 8) == 0x4E)
		gem_dbgx("  -> r0 ", r.r0 | (cf << 31));
#endif
	dos_back(&r, cf);
}

/* ------------------------------------------------- the VDI (GSX2) ---- */

static WORD *pblock[5] = { contrl, intin, ptsin, intout, ptsout };

VOID gsx2(VOID)
{
	register WORD **r3 __asm__("r3") = pblock;
	register unsigned r2 __asm__("r2") = 0x473;
	__asm__ volatile("svc #0xEF" : : "r"(r2), "r"(r3) : "memory", "cc");
}

VOID i_ptsin(WORD *p) { pblock[2] = p; }
VOID i_intin(WORD *p) { pblock[1] = p; }
VOID i_ptsout(WORD *p) { pblock[4] = p; }
VOID i_intout(WORD *p) { pblock[3] = p; }

/* a pointer in contrl[7..8] (the source MFDB and friends) */
VOID i_ptr(VOID *p)
{
	contrl[7] = (UWORD) (ULONG) p;
	contrl[8] = (UWORD) ((ULONG) p >> 16);
}

VOID i_ptr2(VOID *p)
{
	contrl[9] = (UWORD) (ULONG) p;
	contrl[10] = (UWORD) ((ULONG) p >> 16);
}

VOID i_lptr1(VOID *p)
{
	i_ptr(p);
}

VOID m_lptr2(VOID *p)
{
	LONG v = (LONG) (UWORD) contrl[9] | ((LONG) (UWORD) contrl[10] << 16);
	LLSET(p, v);
}

/* (m1 * m2 * 2 / d1 + 1) / 2 */
WORD mul_div(WORD m1, UWORD m2, WORD d1)
{
	LONG q;
	if (!d1)
		return 0;
	q = ((LONG) m1 * (LONG) (m2 * 2)) / d1;
	if (q >= 0)
		return (WORD) ((q + 1) >> 1);
	return (WORD) -((-q + 1) >> 1);
}

UWORD umul_div(UWORD m1, UWORD m2, UWORD d1)
{
	if (!d1)
		return 0;
	return (UWORD) ((((ULONG) m1 * ((ULONG) m2 * 2)) / d1 + 1) >> 1);
}

/* ------------------------------------------------------ LARGE.A86 ---- */

WORD LSTRLEN(LONG s)
{
	const BYTE *p = (const BYTE *) s;
	WORD n = 0;
	while (p[n])
		n++;
	return n;
}

VOID LWCOPY(LONG d, LONG s, WORD n)
{
	UWORD *pd = (UWORD *) d, *ps = (UWORD *) s;
	while (n-- > 0)
		*pd++ = *ps++;
}

VOID LBCOPY(LONG d, LONG s, WORD n)
{
	UBYTE *pd = (UBYTE *) d, *ps = (UBYTE *) s;
	if (n > 0)
		memcpy(pd, ps, (UWORD) n);
}

/* bytes to words until a null; returns the count */
WORD LBWMOV(LONG d, LONG s)
{
	UWORD *pd = (UWORD *) d;
	UBYTE *ps = (UBYTE *) s;
	WORD n = 0;
	while (*ps)
	{
		*pd++ = *ps++;
		n++;
	}
	return n;
}

WORD LSTCPY(LONG d, LONG s)
{
	BYTE *pd = (BYTE *) d, *ps = (BYTE *) s;
	WORD n = 0;
	while (*ps)
	{
		*pd++ = *ps++;
		n++;
	}
	*pd = 0;
	return n;
}

/* --------------------------------------------------- OPTIMOPT.A86 ---- */

VOID r_get(WORD *pxywh, WORD *px, WORD *py, WORD *pw, WORD *ph)
{
	*px = pxywh[0];
	*py = pxywh[1];
	*pw = pxywh[2];
	*ph = pxywh[3];
}

VOID r_set(WORD *pxywh, WORD x, WORD y, WORD w, WORD h)
{
	pxywh[0] = x;
	pxywh[1] = y;
	pxywh[2] = w;
	pxywh[3] = h;
}

WORD rc_equal(WORD *p1, WORD *p2)
{
	return p1[0] == p2[0] && p1[1] == p2[1] && p1[2] == p2[2] && p1[3] == p2[3];
}

VOID rc_copy(WORD *psrc, WORD *pdst)
{
	pdst[0] = psrc[0];
	pdst[1] = psrc[1];
	pdst[2] = psrc[2];
	pdst[3] = psrc[3];
}

WORD inside(WORD x, WORD y, WORD *pt)
{
	return x >= pt[0] && y >= pt[1] && x < pt[0] + pt[2] && y < pt[1] + pt[3];
}

WORD gem_min(WORD a, WORD b) { return a <= b ? a : b; }
WORD gem_max(WORD a, WORD b) { return a >= b ? a : b; }

WORD gem_toupper(WORD ch)
{
	ch &= 0xFF;
	if (ch >= 'a' && ch <= 'z')
		ch -= 32;
	return ch;
}

/* scan for b or a null; returns the pointer to it */
BYTE *scasb(BYTE *p, BYTE b)
{
	while (*p != b && *p)
		p++;
	return p;
}

VOID movs(WORD num, BYTE *ps, BYTE *pd)
{
	while (num-- > 0)
		*pd++ = *ps++;
}

VOID bfill(WORD num, BYTE bval, BYTE *addr)
{
	while (num-- > 0)
		*addr++ = bval;
}

WORD gem_strlen(BYTE *p1)
{
	WORD n = 0;
	while (*p1++)
		n++;
	return n;
}

/* TRUE if equal */
WORD gem_strcmp(BYTE *p1, BYTE *p2)
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

/* like the C strcmp: difference of the first differing bytes */
WORD gem_strchk(BYTE *s, BYTE *t)
{
	while (*s == *t)
	{
		if (!*s)
			return 0;
		s++;
		t++;
	}
	return (UBYTE) *s - (UBYTE) *t;
}

/* copy ps to pd; returns the pointer past the null */
BYTE *gem_strcpy(BYTE *ps, BYTE *pd)
{
	while ((*pd++ = *ps++) != 0)
		;
	return pd;
}

