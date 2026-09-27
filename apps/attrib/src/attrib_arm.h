/*
 * attrib_arm.h - ARM-DOS port of ATTRIB (see attrib_arm.c): the C start-up that
 * replaces ATTRIBA.ASM calls the MS code's main(line), renamed here.
 */
#ifndef ATTRIB_ARMDOS_H
#define ATTRIB_ARMDOS_H

#include <dos.h>
#include "mslib.h"

#define main attrib_main

unsigned getpspbyte(unsigned offset);
void putpspbyte(unsigned offset, unsigned value);

#endif
