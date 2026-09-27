/* ARM/AT BIOS — the few library routines a ROM needs */
#include "bios.h"

void *memset(void *d, int c, size_t n)
{
    uint8_t *p = d;
    if (n >= 16 && !((uintptr_t)p & 3)) {
        uint32_t w = (uint8_t)c * 0x01010101u;
        uint32_t *q = (uint32_t *)p;
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
    if (!(((uintptr_t)dp | (uintptr_t)sp) & 3)) {
        while (n >= 4) { *(uint32_t *)dp = *(const uint32_t *)sp; dp += 4; sp += 4; n -= 4; }
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

struct outbuf { char *p; size_t left; int count; };

static void emit(struct outbuf *o, char c)
{
    if (o->left > 1) { *o->p++ = c; o->left--; }
    o->count++;
}

int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap)
{
    struct outbuf o = { buf, n, 0 };
    for (; *fmt; fmt++) {
        if (*fmt != '%') { emit(&o, *fmt); continue; }
        fmt++;
        int left = 0, zero = 0, width = 0, lng = 0;
        for (;; fmt++) {
            if (*fmt == '-') left = 1;
            else if (*fmt == '0') zero = 1;
            else break;
        }
        if (*fmt == '*') { width = va_arg(ap, int); fmt++; }
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        while (*fmt == 'l') { lng = 1; fmt++; }
        (void)lng;
        char tmp[16];
        const char *s = tmp;
        int len = 0, neg = 0;
        switch (*fmt) {
        case 'c': tmp[0] = (char)va_arg(ap, int); len = 1; break;
        case 's': s = va_arg(ap, const char *); if (!s) s = "(null)"; len = strlen(s); break;
        case 'd': case 'i': case 'u': case 'x': case 'X': case 'p': {
            uint32_t v;
            int base = (*fmt == 'x' || *fmt == 'X' || *fmt == 'p') ? 16 : 10;
            if (*fmt == 'd' || *fmt == 'i') {
                int32_t sv = va_arg(ap, int32_t);
                if (sv < 0) { neg = 1; v = -(uint32_t)sv; } else v = sv;
            } else v = va_arg(ap, uint32_t);
            const char *digits = *fmt == 'x' ? "0123456789abcdef" : "0123456789ABCDEF";
            char *e = tmp + sizeof tmp;
            char *q = e;
            do { *--q = digits[v % base]; v /= base; } while (v);
            if (*fmt == 'p') while (e - q < 8) *--q = '0';
            if (neg) *--q = '-';
            s = q; len = e - q;
            break;
        }
        case '%': tmp[0] = '%'; len = 1; break;
        default: tmp[0] = '?'; len = 1; break;
        }
        int pad = width > len ? width - len : 0;
        if (!left) {
            if (zero && neg) { emit(&o, '-'); s++; len--; }
            while (pad--) emit(&o, zero ? '0' : ' ');
        }
        for (int i = 0; i < len; i++) emit(&o, s[i]);
        if (left) while (pad--) emit(&o, ' ');
    }
    if (n) *o.p = 0;
    return o.count;
}

int snprintf(char *buf, size_t n, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, n, fmt, ap);
    va_end(ap);
    return r;
}

void kprintf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    video_puts(buf);
}

void dprintf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    for (char *p = buf; *p; p++) outb(0xE9, *p);
}
