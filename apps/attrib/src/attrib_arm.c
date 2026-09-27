/*
 * attrib_arm.c - ARM-DOS: C stand-in for ATTRIB's 8086 start-up and helpers
 * (orig/ATTRIBA.ASM, kept for reference):
 *
 *  - XCMAIN: the program entry (instead of the MS C start-up) - passes the
 *    command tail (PSP:81h, PSP:80h bytes, NUL-terminated) to inmain() and
 *    ends with the value left in AL: the real ATTRIB.EXE returns errorlevel 7
 *    after a normal run (checked in DOS 4.00 under DOSBox-X);
 *  - _GETPSPBYTE / _PUTPSPBYTE: read and write the PSP (the default DTA);
 *  - _crit_err_handler: the INT 24h hook - calls the previous handler and,
 *    on Abort/Fail, restores APPEND /X (the original then ended ATTRIB with
 *    errorlevel 1; here DOS ends it through the Abort answer).
 *
 * Portions (c) Microsoft Corp. (MS-DOS 4.0 CMD/ATTRIB), MIT License.
 */
#include <string.h>
#include <dos.h>
#include <armdos.h>
#include "mslib.h"

extern int inmain(char *line);
extern unsigned long old_int24_off;
extern void Reset_appendx(void);

/* ATTRIB's _PARSE.ASM: TimeSW, CmpxSW and DrvSW are 0 */
const unsigned _mslib_parse_features =
    MSLIB_PARSE_ALL & ~(MSLIB_PARSE_TIME | MSLIB_PARSE_CMPX | MSLIB_PARSE_DRV);

static unsigned char *psp(void) { return (unsigned char *)((uint32_t)_psp << 4); }

unsigned getpspbyte(unsigned offset) { return psp()[offset & 0xFFFF]; }
void putpspbyte(unsigned offset, unsigned value) { psp()[offset & 0xFFFF] = (unsigned char)value; }

int main(void)
{
    static char line[130];
    unsigned n = psp()[0x80];
    if (n > 127) n = 127;
    memcpy(line, psp() + 0x81, n);
    line[n] = 0;
    inmain(line);
    return 7;
}

void crit_err_handler(struct armregs *f)
{
    armdos_vect_t old = (armdos_vect_t)old_int24_off;
    if (old) armdos_callold(old, f);
    else f->r0 = (f->r0 & ~0xFFu) | 3;          /* no handler: Fail */
    if ((f->r0 & 0xFF) >= 2) Reset_appendx();   /* ABORT (2) or FAIL (3) */
}
