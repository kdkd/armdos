/*
 * armdos/edplat.h - ARM-DOS platform layer hooks for EDIT (see hardware.cpp).
 */
#ifndef EDPLAT_H
#define EDPLAT_H

/* Switch the text screen to 25 (8x16 font) or 50 (8x8 font) lines. */
void armdosSetLines(int lines);
/* The DOS error code of the last critical error (INT 24h), -1 if none. */
extern int armdosCritError;
/* Is the drive (letter) valid? (IOCTL AX=4408h) */
Boolean armdosDriveValid( char drive ) noexcept;

#endif
