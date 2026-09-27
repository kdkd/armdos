/*
 * msc.c - Microsoft C 5.1 run-time pieces the MS-DOS 4.0 utilities use and
 * the ARM-DOS SDK (newlib) does not have.
 */
#include <errno.h>
#include <string.h>
#include "mslib.h"

/* MS C's crt0 keeps PSP:0002 (the top of the memory block DOS gave the
   program at load time) in DOS_TopOfMemory; MEM uses it for "largest
   executable program size".  Read it before main() - the SDK start-up
   shrinks the block, but DOS does not change PSP:0002 when it does. */
unsigned DOS_TopOfMemory;

__attribute__((constructor)) static void mslib_init(void)
{
    const unsigned char *psp = (const unsigned char *)((uint32_t)_psp << 4);
    DOS_TopOfMemory = psp[2] | (psp[3] << 8);
}

/* MS C 5.1 sys_errlist (the texts of STDLIB.H's errno values) */
char *sys_errlist[] = {
    "Error 0",                      /* 0 */
    "",                             /* 1 */
    "No such file or directory",    /* 2 ENOENT */
    "",                             /* 3 */
    "",                             /* 4 */
    "",                             /* 5 */
    "",                             /* 6 */
    "Arg list too long",            /* 7 E2BIG */
    "Exec format error",            /* 8 ENOEXEC */
    "Bad file number",              /* 9 EBADF */
    "",                             /* 10 */
    "",                             /* 11 */
    "Not enough core",              /* 12 ENOMEM */
    "Permission denied",            /* 13 EACCES */
    "",                             /* 14 */
    "",                             /* 15 */
    "",                             /* 16 */
    "File exists",                  /* 17 EEXIST */
    "Cross-device link",            /* 18 EXDEV */
    "",                             /* 19 */
    "",                             /* 20 */
    "",                             /* 21 */
    "Invalid argument",             /* 22 EINVAL */
    "",                             /* 23 */
    "Too many open files",          /* 24 EMFILE */
    "",                             /* 25 */
    "",                             /* 26 */
    "",                             /* 27 */
    "No space left on device",      /* 28 ENOSPC */
    "",                             /* 29 */
    "",                             /* 30 */
    "",                             /* 31 */
    "",                             /* 32 */
    "Math argument",                /* 33 EDOM */
    "Result too large",             /* 34 ERANGE */
    "",                             /* 35 */
    "Resource deadlock would occur",/* 36 EDEADLOCK */
};
int sys_nerr = sizeof sys_errlist / sizeof sys_errlist[0];

/* COMSUBS.LIB com_substr: the first occurrence of a substring (NULL if none;
   JOIN/SUBST's strbscan searches "\\" and "/\\" with it) */
unsigned char *com_substr(unsigned char *str, unsigned char *sub)
{
    return (unsigned char *)strstr((char *)str, (char *)sub);
}
