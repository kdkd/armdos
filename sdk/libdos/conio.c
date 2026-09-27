/* conio.c - console I/O. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <conio.h>
#include "libdos.h"

static int pushed = -1;

static int dosin(unsigned ah)
{
    struct armregs r = {0};
    r.r0 = ah << 8;
    _armdos_int21(&r);
    return r.r0 & 0xFF;
}

int getch(void)
{
    int c = pushed;
    if (c >= 0) { pushed = -1; return c; }
    return dosin(0x07);
}

int getche(void)
{
    int c = pushed;
    if (c >= 0) { pushed = -1; putch(c); return c; }
    return dosin(0x01);
}

int kbhit(void)
{
    if (pushed >= 0) return 1;
    return dosin(0x0B) != 0;
}

int ungetch(int c)
{
    if (pushed >= 0 || c < 0) return EOF;
    pushed = c & 0xFF;
    return pushed;
}

int putch(int c)
{
    struct armregs r = {0};
    r.r0 = 0x0E00 | (c & 0xFF);
    r.r1 = 0x0007;              /* BH = page 0, BL = colour (graphics modes) */
    _armdos_int10(&r);
    return c & 0xFF;
}

int cputs(const char *s)
{
    while (*s) putch((unsigned char)*s++);
    return 0;
}

int vcprintf(const char *fmt, va_list ap)
{
    char buf[256], *p = buf;
    va_list aq;
    va_copy(aq, ap);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    if (n >= (int)sizeof buf && (p = malloc(n + 1)) != 0)
        vsnprintf(p, n + 1, fmt, aq);
    else if (n >= (int)sizeof buf)
        p = buf;
    va_end(aq);
    if (n > 0) cputs(p);
    if (p != buf) free(p);
    return n;
}

int cprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vcprintf(fmt, ap);
    va_end(ap);
    return n;
}

char *cgets(char *buf)
{
    struct armregs r = {0};
    r.r0 = 0x0A00;
    r.r3 = (uint32_t)buf;       /* DS:DX -> buffer, buf[0] = max */
    _armdos_int21(&r);
    buf[2 + (unsigned char)buf[1]] = 0;
    putch('\r'); putch('\n');
    return buf + 2;
}
