/*
 * demoapp.h - the headers of the GEM Programmer's Toolkit DEMO sample for
 * ARM-DOS (every DEMO module includes this one).
 */
#ifndef GEMDEMO_DEMOAPP_H
#define GEMDEMO_DEMOAPP_H
#include <portab.h>
#include "machine.h"
#include "obdefs.h"
#include "treeaddr.h"
#include "gembind.h"
#include "demo.h"

EXTERN WORD	contrl[], intin[], ptsin[], intout[], ptsout[];
EXTERN UWORD	DOS_ERR;
EXTERN LONG	drawaddr;

/* demoarm.c: the machine layer */
EXTERN WORD	gem(LONG pb);
EXTERN VOID	vdi(VOID);
EXTERN WORD	dos_gdrv(VOID);
EXTERN VOID	dos_gdir(WORD drive, LONG pdrvpath);
EXTERN WORD	dos_open(LONG pname, WORD access);
EXTERN WORD	dos_create(LONG pname, WORD attr);
EXTERN WORD	dos_close(WORD handle);
EXTERN LONG	dos_read(WORD handle, LONG cnt, LONG pbuffer);
EXTERN LONG	dos_write(WORD handle, LONG cnt, LONG pbuffer);
EXTERN LONG	dos_alloc(LONG nbytes);
EXTERN WORD	dos_free(LONG maddr);
EXTERN WORD	UMUL_DIV(UWORD m1, UWORD m2, UWORD d1);
EXTERN WORD	GEMAIN(VOID);

#include "demoproto.h"
#endif
