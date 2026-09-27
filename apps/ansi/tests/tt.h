/* tt.h - test helpers: results to COM1 as T:PASS / T:FAIL / T:LOG lines */
#ifndef TT_H
#define TT_H
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "armdos.h"

static int tt_fails;

__attribute__((unused)) static void tt_log(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    for (char *s = buf; *s; s++) {
        if (*s == '\n') armdos_outb(0x3F8, '\r');
        armdos_outb(0x3F8, (uint8_t)*s);
    }
}

#define TT_CHECK(cond, ...) do { if (!(cond)) { char _m[200]; snprintf(_m, sizeof _m, __VA_ARGS__); \
        tt_log("T:FAIL line %d: %s\n", __LINE__, _m); tt_fails++; } } while (0)
#define TT_EQ(a, b) do { long _a = (long)(a), _b = (long)(b); \
        TT_CHECK(_a == _b, "%s == %ld (0x%lx), expected %ld (0x%lx)", #a, _a, _a, _b, _b); } while (0)

__attribute__((unused)) static int tt_dos(struct armregs *r) { return _armdos_int21(r); }

__attribute__((unused)) static void tt_out(const char *s)
{
    struct armregs r = { 0 };
    r.r0 = 0x4000; r.r1 = 1; r.r2 = strlen(s); r.r3 = (uint32_t)s;
    _armdos_int21(&r);
}

/* INT 21h AH=07h: one character, no echo, no ^C check */
__attribute__((unused)) static int tt_getc(void)
{
    struct armregs r = { 0 };
    r.r0 = 0x0700;
    _armdos_int21(&r);
    return r.r0 & 0xFF;
}
#endif
