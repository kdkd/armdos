/* klib.c - the few C library routines the freestanding kernel parts need */
#include "klib.h"

void *memset(void *d, int c, size_t n)
{
    uint8_t *p = d;
    if (n >= 16) {
        while ((uintptr_t)p & 3) { *p++ = (uint8_t)c; n--; }
        uint32_t w = (uint8_t)c * 0x01010101u, *q = (uint32_t *)p;
        while (n >= 16) { q[0] = w; q[1] = w; q[2] = w; q[3] = w; q += 4; n -= 16; }
        while (n >= 4) { *q++ = w; n -= 4; }
        p = (uint8_t *)q;
    }
    while (n--) *p++ = (uint8_t)c;
    return d;
}

void *memcpy(void *d, const void *s, size_t n)
{
    uint8_t *dp = d;
    const uint8_t *sp = s;
    if (n >= 8 && !(((uintptr_t)dp ^ (uintptr_t)sp) & 3)) {
        while ((uintptr_t)dp & 3) { *dp++ = *sp++; n--; }
        uint32_t *dw = (uint32_t *)dp;
        const uint32_t *sw = (const uint32_t *)sp;
        while (n >= 16) { dw[0] = sw[0]; dw[1] = sw[1]; dw[2] = sw[2]; dw[3] = sw[3]; dw += 4; sw += 4; n -= 16; }
        while (n >= 4) { *dw++ = *sw++; n -= 4; }
        dp = (uint8_t *)dw; sp = (const uint8_t *)sw;
    }
    while (n--) *dp++ = *sp++;
    return d;
}

void *memmove(void *d, const void *s, size_t n)
{
    uint8_t *dp = d;
    const uint8_t *sp = s;
    if (dp <= sp || dp >= sp + n) return memcpy(d, s, n);
    while (n--) dp[n] = sp[n];
    return d;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *x = a, *y = b;
    for (; n; n--, x++, y++) if (*x != *y) return *x - *y;
    return 0;
}

size_t strlen(const char *s)
{
    const char *p = s;
    while (*p) p++;
    return p - s;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (uint8_t)*a - (uint8_t)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        if (*a != *b) return (uint8_t)*a - (uint8_t)*b;
        if (!*a) break;
    }
    return 0;
}

char *strcpy(char *d, const char *s)
{
    char *r = d;
    while ((*d++ = *s++)) ;
    return r;
}

char *strcat(char *d, const char *s)
{
    strcpy(d + strlen(d), s);
    return d;
}

char *strchr(const char *s, int c)
{
    for (;; s++) {
        if (*s == (char)c) return (char *)s;
        if (!*s) return 0;
    }
}

size_t strlcpy(char *d, const char *s, size_t n)
{
    size_t l = strlen(s);
    if (n) {
        size_t k = l < n - 1 ? l : n - 1;
        memcpy(d, s, k);
        d[k] = 0;
    }
    return l;
}

struct ob { char *p; size_t left; int n; };

static void put(struct ob *o, char c)
{
    if (o->left > 1) { *o->p++ = c; o->left--; }
    o->n++;
}

int kvsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    struct ob o = { buf, size, 0 };
    for (; *fmt; fmt++) {
        if (*fmt != '%') { put(&o, *fmt); continue; }
        fmt++;
        int left = 0, zero = 0, width = 0;
        for (;; fmt++) {
            if (*fmt == '-') left = 1;
            else if (*fmt == '0') zero = 1;
            else break;
        }
        if (*fmt == '*') { width = va_arg(ap, int); fmt++; }
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        while (*fmt == 'l' || *fmt == 'h') fmt++;
        char tmp[16];
        const char *s = tmp;
        int len = 0, neg = 0;
        switch (*fmt) {
        case 'c': tmp[0] = (char)va_arg(ap, int); len = 1; break;
        case 's': s = va_arg(ap, const char *); if (!s) s = "(null)"; len = strlen(s); break;
        case 'd': case 'u': case 'x': case 'X': {
            uint32_t v;
            unsigned base = (*fmt == 'x' || *fmt == 'X') ? 16 : 10;
            if (*fmt == 'd') {
                int32_t sv = va_arg(ap, int32_t);
                if (sv < 0) { neg = 1; v = -(uint32_t)sv; } else v = sv;
            } else v = va_arg(ap, uint32_t);
            const char *dig = *fmt == 'x' ? "0123456789abcdef" : "0123456789ABCDEF";
            char *e = tmp + sizeof tmp, *q = e;
            do { *--q = dig[v % base]; v /= base; } while (v);
            if (neg) *--q = '-';
            s = q; len = e - q;
            break;
        }
        case '%': tmp[0] = '%'; len = 1; break;
        default: tmp[0] = '?'; len = 1; break;
        }
        int pad = width > len ? width - len : 0;
        if (!left) {
            if (zero && neg) { put(&o, '-'); s++; len--; }
            while (pad--) put(&o, zero ? '0' : ' ');
        }
        for (int i = 0; i < len; i++) put(&o, s[i]);
        if (left) while (pad--) put(&o, ' ');
    }
    if (size) *o.p = 0;
    return o.n;
}

int ksnprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = kvsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return r;
}

/* call INT n's current handler directly with a frame (the vector may be
 * hooked; this is what a PC "INT n" does, without the trap) */
int kint(int n, struct armregs *r)
{
    int_handler h = (int_handler)IVT[n];
    r->cpsr = get_cpsr() & ~(CPSR_C | CPSR_Z);
    r->intno = n;
    if (!h) { r->cpsr |= CPSR_C; return 1; }
    h(r);
    return (r->cpsr & CPSR_C) != 0;
}

void kdebug(const char *fmt, ...)
{
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    kvsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    for (char *p = buf; *p; p++) IOPORT(0xE9) = *p;
}
