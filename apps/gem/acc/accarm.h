/*
 * accarm.h - ARM-DOS glue for the GEM CalClock desk accessory (the
 * routines of ACCSTART.A86 and CALCASM.A86, and CBARITH.OBJ).
 */
#ifndef GEMACC_ACCARM_H
#define GEMACC_ACCARM_H
EXTERN VOID	crystal(LONG pb);
EXTERN VOID	__DOS(VOID);
EXTERN WORD	acc_toupper(WORD ch);
EXTERN WORD	acc_strcmp(BYTE *p1, BYTE *p2);
EXTERN WORD	acc_strlen(BYTE *p);
EXTERN VOID	beep(VOID);
EXTERN VOID	spol_int(VOID);
EXTERN WORD	spol_out(WORD ch);
/* CBARITH.OBJ: decimal floating point, fld.c */
EXTERN VOID	_FLD_ADD(UBYTE *res, UBYTE *a, UBYTE *b);
EXTERN VOID	_FLD_SUB(UBYTE *res, UBYTE *a, UBYTE *b);
EXTERN VOID	_FLD_MUL(UBYTE *res, UBYTE *a, UBYTE *b);
EXTERN VOID	_FLD_DIV(UBYTE *res, UBYTE *a, UBYTE *b);
#endif
