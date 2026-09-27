/*
 * doscmpat.h - platform definitions for ARM-DOS (the Build engine's
 * PLATFORM_DOS, which platform.h already knew about: Duke Nukem 3D was a
 * DOS program).
 *
 * "Build Engine & Tools" Copyright (c) 1993-1997 Ken Silverman
 * Ken Silverman's official web site: "http://www.advsys.net/ken"
 * See the included license file "BUILDLIC.TXT" for license info.
 * This file IS NOT A PART OF Ken Silverman's original release
 * (written for the ARM-DOS port, 2026).
 */
#ifndef _INCLUDE_DOSCMPAT_H_
#define _INCLUDE_DOSCMPAT_H_

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <inttypes.h>
#include <fcntl.h>
#include <io.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <assert.h>
#include <machine/endian.h>
#ifndef BYTE_ORDER
#define BYTE_ORDER 1234
#endif

#define PLATFORM_DOS 1

#define kmalloc(x) malloc(x)
#define kkmalloc(x) malloc(x)
#define kfree(x) free(x)
#define kkfree(x) free(x)

#ifdef FP_OFF
#undef FP_OFF
#endif
/* Watcom let a pointer be cast to a 32-bit integer; so does ARM-DOS. */
#define FP_OFF(x) ((int32_t) (x))

#ifndef max
#define max(x, y)  (((x) > (y)) ? (x) : (y))
#endif
#ifndef min
#define min(x, y)  (((x) < (y)) ? (x) : (y))
#endif

#define __int64 int64_t

#ifndef stricmp
#define stricmp strcasecmp
#endif
#ifndef strcmpi
#define strcmpi strcasecmp
#endif

#ifndef S_IREAD
#define S_IREAD  S_IRUSR
#endif
#ifndef S_IWRITE
#define S_IWRITE S_IWUSR
#endif

#define USER_DUMMY_NETWORK 1

/* Watcom's read() under DOS/4GW moved any length in one call; ARM-DOS's
   (like DOS itself, INT 21h AH=3Fh) returns at most 32 KB at a time, and the
   Build engine's kread() assumes it got everything: loop. */
int armdos_fullread(int fd, void *buf, unsigned n);
#define read(fd, buf, n) armdos_fullread((fd), (buf), (n))

/* Chocolate Duke prints progress to stdout while the game is on screen
   (DUKE3D.EXE did not): in mode 13h the text goes to the debug port E9h
   instead of being drawn over the picture by the BIOS. */
int armdos_printf(const char *fmt, ...);
int armdos_puts(const char *s);
#define printf armdos_printf
#define puts armdos_puts

/* chocolate's "not implemented" reports: silent (they are in the INT 09h path) */
#define STUBBED(x) ((void)0)

#endif
