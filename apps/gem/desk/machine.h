/*	MACHINE.H		09/29/84-02/08/85	Lee Lorenzen	*/
/*				3/14/85 -1/22/87	MDF		*/
/*	merge source		5/26/87	- 5/28/87	mdf		*/
/*	to far pointers		10/13/87		mdf		*/
/*	ARM-DOS			2026			flat 32-bit pointers */

/*
*       Copyright 1999, Caldera Thin Clients, Inc.
*       This software is licenced under the GNU Public License.
*       Please see LICENSE.TXT for further information.
*
*                  Historical Copyright
*	-------------------------------------------------------------
*	GEM Desktop					  Version 2.3
*	Serial No.  XXXX-0000-654321		  All Rights Reserved
*	Copyright (C) 1985 - 1987		Digital Research Inc.
*	-------------------------------------------------------------
*/

/*
 * ARM-DOS: see apps/gem/aes/machine.h.  PC byte order and DOS (I8086),
 * flat 32-bit pointers (PTR32): a long address is the pointer; 32-bit
 * values in GEM data may be 2-byte aligned, so LLGET/LLSET use halfwords.
 */
#ifndef GEMDESK_MACHINE_H
#define GEMDESK_MACHINE_H

/* GEM's string routines have the source first: strcpy(src, dst) etc. */
#define strcpy	gem_strcpy
#define strcat	gem_strcat
#define strcmp	gem_strcmp
#define strlen	gem_strlen
#define strchk	gem_strchk
#define strscn	gem_strscn
#define toupper	gem_toupper
#define min	gem_min
#define max	gem_max

#define	PCDOS	1	/* IBM PC DOS */
#define	CPM	0	/* CP/M version 2.2 */

#define HILO 0		/* how bytes are stored */

#define I8086	1	/* Intel byte order and DOS */
#define	MC68K	0	/* Motorola 68000 */
#define PTR32	1	/* ARM-DOS: 32-bit flat pointers */

#define ALCYON	0	/* Alcyon C Compiler */
#define HIGH_C	1	/* for use with MetaWare High-C compiler	*/

#define ALPHA	1	/* if character screen	*/

#define LINKED	0	/* if desktop linked with GEM	*/
#define UNLINKED 1

#define DEBUG 0

#define MULTIAPP 0
#define SINGLAPP 1

#if MULTIAPP
#define NUM_WIN 12		/* 12 for 11 process entries		*/
#define NUM_ACCS 10		/* 10 for multi-process version		*/
#else
#define NUM_WIN 8		/* 8 for main app and 3 desk accs	*/
#define NUM_ACCS 3		/* 3 for number of desk accs	*/
#endif

#define NUM_DESKACC 12		/* at least 12 slots for	*/
				/*   3 desk accessories or	*/
				/*   11 process entries		*/
				/* requires new string array	*/
				/*   in gemmnlib.c if num != 12	*/

						/* in OPTIMIZE.C	*/
EXTERN BYTE	*gem_strcpy(BYTE *ps, BYTE *pd);
EXTERN BYTE	*gem_strcat(BYTE *ps, BYTE *pd);
EXTERN BYTE	*gem_strscn(BYTE *ps, BYTE *pd, BYTE stop);
						/* in LONGASM / deskarm.c */
EXTERN WORD	LSTRLEN(LONG s);
EXTERN VOID	LWCOPY(LONG d, LONG s, WORD n);
EXTERN VOID	LBCOPY(LONG d, LONG s, WORD n);
EXTERN WORD	LBWMOV(LONG d, LONG s);
EXTERN WORD	LSTCPY(LONG d, LONG s);

#define LW(x) ( (LONG)((UWORD)(x)) )
#define HW(x) ((LONG)((UWORD)(x)) << 16)
#define LLOWD(x) ((UWORD)(x))
#define LHIWD(x) ((UWORD)(((ULONG)(x)) >> 16))
#define LLOBT(x) ((BYTE)((x) & 0x00ff))
#define LHIBT(x) ((BYTE)( ((x) >> 8) & 0x00ff))

#define FAR	/**/
#define NEAR	/**/
#define LPOINTER(x) (x)
#define ADDR(x) ((LONG)(x))
#define LLDS() (0L)
#define LLCS() (0L)
#define LOFFSET(x) (x)
#define LSEGOFF(x) (x)

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
