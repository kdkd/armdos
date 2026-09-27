/*
 * portab.h - GEM portability types for ARM-DOS (32-bit flat ARM, gcc).
 *
 * Replaces the Caldera GEM/3 PORTAB.H (Copyright 1999 Caldera Thin Clients,
 * GNU GPL v2; historically Copyright 1982 Digital Research).  The GEM sources
 * were written for 16-bit x86 (Lattice C / MetaWare High C, int = 16 bits,
 * far pointers = 32-bit LONG values) with a parallel 68000 flavour (int 16
 * bits, flat 32-bit pointers).  ARM-DOS is the second case with the byte
 * order of the first: WORD stays 16 bits, LONG and every pointer are 32 bits
 * and a "long address" is simply the flat pointer.
 */
#ifndef GEM_PORTAB_H
#define GEM_PORTAB_H

#define mc68k 0

#define	BYTE	char			/* signed byte (unsigned on ARM, as    */
#define	UBYTE	unsigned char		/*   High C was used with UCHARA)     */
#define	BOOLEAN	short			/* 2 valued (true/false)   */
#define	WORD	short			/* signed word (16 bits)   */
#define	UWORD	unsigned short		/* unsigned word	   */
#define	LONG	long			/* signed long (32 bits)   */
#define	ULONG	unsigned long		/* unsigned long	   */

#define	REG	register
#define	LOCAL	auto
#define	EXTERN	extern
#define	MLOCAL	static
#define	GLOBAL	/**/
#define VOID	void
#define FAR	/**/
#define NEAR	/**/

#define	FAILURE	(-1)
#define SUCCESS	(0)
#define	YES	1
#define	NO	0
#define	FOREVER	for(;;)
#ifdef NULL
#undef NULL
#endif
#define	NULL	0
#define NULLPTR ((char *) 0)
#ifndef EOF
#define	EOF	(-1)
#endif
#define	TRUE	(1)
#define	FALSE	(0)

/* ARM-DOS: structures that GEM keeps in files or passes between programs
 * (OBJECT, TEDINFO, ICONBLK, MFDB ...) have 32-bit fields at 2-byte
 * boundaries.  Declare them GEM_PACKED so gcc emits halfword accesses. */
#define GEM_PACKED __attribute__((packed, aligned(2)))

#endif
