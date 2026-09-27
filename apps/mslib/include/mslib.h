/*
 * mslib.h - Microsoft C 5.1 runtime compatibility for the MS-DOS 4.0
 * utilities compiled for ARM-DOS (apps/mslib).  The SDK (sdk/include) has
 * dos.h/conio.h/bios.h; this adds what the MS sources expect beyond it.
 */
#ifndef MSLIB_H
#define MSLIB_H

#include <dos.h>
#include <stdint.h>

/* SysParse assembly-time switches (INC/PSDATA.INC), for _mslib_parse_features */
#define MSLIB_PARSE_DATE  0x0001
#define MSLIB_PARSE_TIME  0x0002
#define MSLIB_PARSE_FILE  0x0004
#define MSLIB_PARSE_CAPS  0x0008
#define MSLIB_PARSE_CMPX  0x0010
#define MSLIB_PARSE_NUM   0x0020
#define MSLIB_PARSE_KEY   0x0040
#define MSLIB_PARSE_SW    0x0080
#define MSLIB_PARSE_VAL1  0x0100
#define MSLIB_PARSE_VAL2  0x0200
#define MSLIB_PARSE_VAL3  0x0400
#define MSLIB_PARSE_DRV   0x0800
#define MSLIB_PARSE_QUOTE 0x1000
#define MSLIB_PARSE_ALL   0x1FFF
extern const unsigned _mslib_parse_features;

/* MS C start-up globals */
extern unsigned DOS_TopOfMemory;        /* PSP:0002 at load (paragraph) */

/* MS C 5.1 error texts (sys_errlist is indexed by errno) */
extern char *sys_errlist[];
extern int sys_nerr;

/* a far pointer from a segment:offset pair (ARCH.md 5): the flat address */
#define MSLIB_FP(seg, off) ((void *)((((uint32_t)(uint16_t)(seg)) << 4) + (uint32_t)(uint16_t)(off)))

/* MS C code restores ES with strcpy(fix_es_reg, NULL); a NULL source is
   nothing to copy here (address 0 is the interrupt vector table) */
#include <string.h>
static inline char *mslib_strcpy(char *d, const char *s) { return s ? strcpy(d, s) : d; }

/* INC/COMSUBS.LIB (binary only in the MS-DOS 4.0 release) */
unsigned char *com_substr(unsigned char *str, unsigned char *sub);

#endif
