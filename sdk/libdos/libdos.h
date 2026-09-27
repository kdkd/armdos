/* libdos.h - internal helpers shared by the libdos sources. */
#ifndef _LIBDOS_INTERNAL_H
#define _LIBDOS_INTERNAL_H

#include <string.h>
#include "armdos.h"

/* INT 21h with AH=ah, AL=al and the given BX/CX/DX; returns -1 (errno set)
 * on carry, else r0 (AX). */
static inline int _dos21(unsigned ax, unsigned bx, unsigned cx, unsigned dx, struct armregs *r)
{
    memset(r, 0, sizeof *r);
    r->r0 = ax; r->r1 = bx; r->r2 = cx; r->r3 = dx;
    if (_armdos_int21(r))
        return _armdos_seterr(r->r0 & 0xFFFF);
    return (int)r->r0;
}

/* DOS date/time words <-> time_t (local time = UTC on DOS). */
long _armdos_dostime_to_unix(unsigned date, unsigned time);
void _armdos_unix_to_dostime(long t, unsigned *date, unsigned *time);
long _armdos_days_from_civil(int y, unsigned m, unsigned d);

#define ARMDOS_MAXFD 64

#endif
