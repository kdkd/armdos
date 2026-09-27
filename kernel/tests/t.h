/*
 * t.h - tiny test framework for the kernel test programs.  Results go to
 * COM1 (the node harness reads them from the serial log) as lines:
 *     T:PASS <test> / T:FAIL <test>: <why> / T:LOG ...
 * Programs are built with the ARM-DOS SDK.
 */
#ifndef KTEST_T_H
#define KTEST_T_H

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include "armdos.h"

static int t_fails, t_checks;
static const char *t_name = "?";

__attribute__((unused)) static void t_ser(const char *s)
{
    for (; *s; s++) {
        if (*s == '\n') armdos_outb(0x3F8, '\r');
        armdos_outb(0x3F8, (uint8_t)*s);
    }
}

__attribute__((unused)) static void t_log(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    t_ser(buf);
}

#define T_CHECK(cond, ...) do {                                         \
        t_checks++;                                                     \
        if (!(cond)) {                                                  \
            char _m[160];                                               \
            snprintf(_m, sizeof _m, __VA_ARGS__);                       \
            t_log("T:FAIL %s: line %d: %s\n", t_name, __LINE__, _m);    \
            t_fails++;                                                  \
        }                                                               \
    } while (0)

#define T_EQ(a, b) do {                                                 \
        long _a = (long)(a), _b = (long)(b);                            \
        T_CHECK(_a == _b, "%s == %ld, expected %ld", #a, _a, _b);      \
    } while (0)

__attribute__((unused)) static void t_begin(const char *name) { t_name = name; t_log("T:BEGIN %s\n", name); }

__attribute__((unused)) static int t_end(void)
{
    if (!t_fails) t_log("T:PASS %s (%d checks)\n", t_name, t_checks);
    else t_log("T:FAILED %s (%d of %d checks failed)\n", t_name, t_fails, t_checks);
    return t_fails ? 1 : 0;
}

/* INT 21h with a register struct; returns CF */
static inline int t_dos(struct armregs *r) { return _armdos_int21(r); }

static inline int t_dos3(unsigned ax, unsigned bx, unsigned cx, unsigned dx, struct armregs *r)
{
    memset(r, 0, sizeof *r);
    r->r0 = ax; r->r1 = bx; r->r2 = cx; r->r3 = dx;
    return _armdos_int21(r);
}

#endif
