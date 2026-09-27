#ifndef GEMAES_MACHINE_H
#define GEMAES_MACHINE_H
/*	MACHINE.H		09/29/84-02/08/85	Lee Lorenzen	*/
/*	GEM20			12/17/85		Lowell Webster	*/
/*	ARM-DOS			2026			flat 32-bit pointers */

/*
*       Copyright 1999, Caldera Thin Clients, Inc.
*       This software is licenced under the GNU Public License.
*       Please see LICENSE.TXT for further information.
*
*                  Historical Copyright
*	-------------------------------------------------------------
*	GEM Application Environment Services		  Version 2.3
*	Serial No.  XXXX-0000-654321		  All Rights Reserved
*	Copyright (C) 1986			Digital Research Inc.
*	-------------------------------------------------------------
*/

/*
 * ARM-DOS: the machine is a PC as far as DOS and byte order go (I8086 = 1),
 * but pointers are flat 32-bit values as on the 68000 (PTR32 = 1): a "long
 * address" (LONG) is the pointer itself.  WORD is 16 bits (portab.h).
 * 32-bit values inside GEM's data (object specs, TEDINFOs, ICONBLKs in
 * resource files) are only 2-byte aligned, so LLGET/LLSET read and write
 * them as two halfwords.
 */
/* GEM's string routines have the source first: strcpy(src, dst) etc.
 * Rename them so they cannot be confused with the C library's. */
#define strcpy	gem_strcpy
#define strcat	gem_strcat
#define strcmp	gem_strcmp
#define strlen	gem_strlen
#define strchk	gem_strchk
#define strscn	gem_strscn
#define toupper	gem_toupper
#define min	gem_min
#define max	gem_max
#define signal	gem_signal
#define sound	gem_sound

#define	PCDOS	1	/* IBM PC DOS */
#define	CPM	0	/* CP/M version 2.2 */
#define GEMDOS	0	/* GEM DOS		*/

#define HILO 0		/* how bytes are stored */

#define I8086	1	/* Intel 8086/8088 byte order and DOS */
#define	MC68K	0	/* Motorola 68000 */
#define ALCYON	0	/* Alcyon C Compiler */
#define PTR32	1	/* ARM-DOS: 32-bit flat pointers */

#define MULTIAPP 0
#define SINGLAPP 1

#if SINGLAPP
#define NUM_WIN 8		/* 8 for main app and 3 desk accs	*/

#define NUM_ACCS 3		/* 3 for number of desk accs	*/

#define NUM_DESKACC 9		/* at least 9 slots for		*/
				/*   3 desk accessories		*/
				/* each desk acc can take 3 slots*/
				/* requires new string array	*/
				/*   in gemmnlib.c if num != 9	*/
#endif

#if MULTIAPP
#define NUM_WIN 12		/* 12 for 11 process entries		*/

#define NUM_ACCS 10		/* 10 for multi-process version	*/

#define NUM_DESKACC 17		/* at least 17 slots for	*/
				/*   3 desk accessories and	*/
				/*   11 process entries		*/
				/* each desk acc can take 3 slots*/
				/* requires new string array	*/
				/*   in gemmnlib.c if num != 17	*/
#endif
						/* in OPTIMIZE.C	*/
						/*  (the GEM argument	*/
						/*  order: dst last)	*/
EXTERN BYTE	*strcpy(BYTE *ps, BYTE *pd);
EXTERN BYTE	*strcat(BYTE *ps, BYTE *pd);
EXTERN BYTE	*strscn(BYTE *ps, BYTE *pd, BYTE stop);
						/* in LARGE.C		*/
EXTERN WORD	LSTRLEN(LONG s);
EXTERN VOID	LWCOPY(LONG d, LONG s, WORD n);
EXTERN VOID	LBCOPY(LONG d, LONG s, WORD n);
EXTERN WORD	LBWMOV(LONG d, LONG s);
EXTERN WORD	LSTCPY(LONG d, LONG s);
						/* coerce short ptr to	*/
						/*   low word  of long	*/
#define LW(x) ( (LONG)((UWORD)(x)) )
#define HW(x) ((LONG)((UWORD)(x)) << 16)
#define LLOWD(x) ((UWORD)(x))
#define LHIWD(x) ((UWORD)(((ULONG)(x)) >> 16))
#define LLOBT(x) ((BYTE)((x) & 0x00ff))
#define LHIBT(x) ((BYTE)( ((x) >> 8) & 0x00ff))

/************************************************************************/

#define ADDR(x) ((LONG)(x))
						/* the "segments" are 0	*/
#define LLDS() (0L)
#define LLCS() (0L)

#define LBGET(x) ( (UBYTE) *((UBYTE *)(x)) )
#define LBSET(x, y)  ( *((BYTE *)(x)) = (y))
#define LWGET(x) ( (WORD) *((WORD *)(x)) )
#define LWSET(x, y)  ( *((WORD *)(x)) = (y))

static inline LONG gem_llget(LONG a)
{
	return (LONG)((ULONG)*(UWORD *)a | ((ULONG)*(UWORD *)(a + 2) << 16));
}
static inline LONG gem_llset(LONG a, LONG v)
{
	*(UWORD *)a = (UWORD)v;
	*(UWORD *)(a + 2) = (UWORD)((ULONG)v >> 16);
	return v;
}
#define LLGET(x) gem_llget((LONG)(x))
#define LLSET(x, y) gem_llset((LONG)(x), (LONG)(y))

#define LBYTE0(x) (*(x))
#define LBYTE1(x) (*((x)+1))
#define LBYTE2(x) (*((x)+2))
#define LBYTE3(x) (*((x)+3))

#endif
