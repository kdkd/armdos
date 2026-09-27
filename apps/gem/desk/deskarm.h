/*
 * deskarm.h - ARM-DOS glue for the GEM Desktop: the routines that were
 * 8086 assembler in GEM/3 (DESKSTAR, DESKOSIF, GSX2, LONGASM, OPTIMOPT).
 */
#ifndef GEMDESK_DESKARM_H
#define GEMDESK_DESKARM_H

#include <gemrt.h>

EXTERN WORD	gem(LONG pb);			/* the AES call	*/
EXTERN VOID	__EXEC(VOID);
EXTERN VOID	chrout(WORD ch);
EXTERN WORD	chrin(VOID);
EXTERN VOID	takedos(VOID);
EXTERN VOID	givedos(VOID);
EXTERN VOID	takekey(VOID);
EXTERN WORD	givekey(VOID);
EXTERN VOID	takevid(VOID);
EXTERN VOID	givevid(VOID);
EXTERN WORD	gemain(VOID);

#endif
