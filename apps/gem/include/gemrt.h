/*
 * gemrt.h - the run-time routines common to the ARM-DOS GEM AES and
 * Desktop (common/gemrt.c), replacing GEM/3's 8086 assembler helpers.
 */
#ifndef GEM_GEMRT_H
#define GEM_GEMRT_H

EXTERN VOID	__DOS(VOID);
EXTERN VOID	gsx2(VOID);
EXTERN WORD	mul_div(WORD m1, UWORD m2, WORD d1);
EXTERN UWORD	umul_div(UWORD m1, UWORD m2, UWORD d1);
EXTERN VOID	i_ptsin(WORD *p);
EXTERN VOID	i_intin(WORD *p);
EXTERN VOID	i_intout(WORD *p);
EXTERN VOID	i_ptsout(WORD *p);
EXTERN VOID	i_ptr(VOID *p);
EXTERN VOID	i_ptr2(VOID *p);
EXTERN VOID	i_lptr1(VOID *p);
EXTERN VOID	m_lptr2(VOID *p);

EXTERN VOID	r_get(WORD *pxywh, WORD *px, WORD *py, WORD *pw, WORD *ph);
EXTERN VOID	r_set(WORD *pxywh, WORD x, WORD y, WORD w, WORD h);
EXTERN WORD	rc_equal(WORD *p1, WORD *p2);
EXTERN VOID	rc_copy(WORD *psrc, WORD *pdst);
EXTERN WORD	inside(WORD x, WORD y, WORD *pt);
EXTERN WORD	gem_min(WORD a, WORD b);
EXTERN WORD	gem_max(WORD a, WORD b);
EXTERN WORD	gem_toupper(WORD ch);
EXTERN BYTE	*scasb(BYTE *p, BYTE b);
EXTERN VOID	movs(WORD num, BYTE *ps, BYTE *pd);
EXTERN VOID	bfill(WORD num, BYTE bval, BYTE *addr);
EXTERN WORD	gem_strlen(BYTE *p1);
EXTERN WORD	gem_strcmp(BYTE *p1, BYTE *p2);
EXTERN WORD	gem_strchk(BYTE *s, BYTE *t);

/* the segment (paragraph) of a flat pointer: DOS 4Bh environment etc. */
#define LSEG(x)	((UWORD)(((ULONG)(x)) >> 4))

/* debugging aid: text to the ARM-PC debug port (E9h) with -DGEMDEBUG */
#ifdef GEMDEBUG
static inline void gem_dbg(const char *s) { while (*s) *(volatile unsigned char *)(0x10000000 + 0xE9) = *s++; }
static inline void gem_dbgx(const char *s, unsigned long v)
{
	static const char hx[] = "0123456789ABCDEF";
	int i;
	gem_dbg(s);
	for (i = 28; i >= 0; i -= 4)
		*(volatile unsigned char *)(0x10000000 + 0xE9) = hx[(v >> i) & 15];
	gem_dbg("\n");
}
#else
#define gem_dbg(s) ((void)0)
#define gem_dbgx(s, v) ((void)0)
#endif

#endif
