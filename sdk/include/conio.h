/*
 * conio.h - console I/O (Microsoft C style).
 * Input goes through DOS (INT 21h AH=07h/01h/0Bh), so it honours input
 * redirection as MS C's did; output goes straight to the screen through the
 * BIOS teletype (INT 10h AH=0Eh), bypassing stdout redirection.
 */
#ifndef _ARMDOS_CONIO_H
#define _ARMDOS_CONIO_H
#include <stdarg.h>
#ifdef __cplusplus
extern "C" {
#endif
int   getch(void);          /* no echo; extended keys: 0 then scan code */
int   getche(void);         /* echo */
int   kbhit(void);          /* nonzero if a key is waiting */
int   ungetch(int c);
int   putch(int c);
int   cputs(const char *s);
int   cprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int   vcprintf(const char *fmt, va_list ap);
char *cgets(char *buf);     /* buf[0] = max; returns &buf[2], buf[1] = length */
#define _getch getch
#define _getche getche
#define _kbhit kbhit
#define _ungetch ungetch
#define _putch putch
#define _cputs cputs
#define _cprintf cprintf
#define _cgets cgets
#ifdef __cplusplus
}
#endif
#endif
