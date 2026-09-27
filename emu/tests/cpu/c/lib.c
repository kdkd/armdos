#include <stdarg.h>
#include "lib.h"
static char obuf[256];
static int olen;
void flush(void) {
  if (!olen) return;
  obuf[olen] = 0;
  if (on_qemu()) semihost(4, obuf);                /* SYS_WRITE0 */
  else { volatile unsigned char *e9 = (volatile unsigned char *)0x100000E9; for (int i = 0; i < olen; i++) *e9 = obuf[i]; }
  olen = 0;
}
void putch(int c) { obuf[olen++] = c; if (c == '\n' || olen >= 250) flush(); }
void puts_(const char *s) { while (*s) putch(*s++); }
static void pnum(unsigned long long v, int base, int width, int pad, int neg) {
  char b[24]; int n = 0;
  do { int d = v % base; b[n++] = d < 10 ? '0' + d : 'a' + d - 10; v /= base; } while (v);
  if (neg) b[n++] = '-';
  while (n < width) { putch(pad); width--; }
  while (n) putch(b[--n]);
}
void printf_(const char *f, ...) {
  va_list ap; va_start(ap, f);
  for (; *f; f++) {
    if (*f != '%') { putch(*f); continue; }
    f++;
    int pad = ' ', w = 0, ll = 0;
    if (*f == '0') { pad = '0'; f++; }
    while (*f >= '0' && *f <= '9') w = w * 10 + *f++ - '0';
    while (*f == 'l') { ll++; f++; }
    switch (*f) {
      case 'd': { long long v = ll >= 2 ? va_arg(ap, long long) : va_arg(ap, int); pnum(v < 0 ? -v : v, 10, w, pad, v < 0); break; }
      case 'u': { unsigned long long v = ll >= 2 ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned); pnum(v, 10, w, pad, 0); break; }
      case 'x': { unsigned long long v = ll >= 2 ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned); pnum(v, 16, w, pad, 0); break; }
      case 's': puts_(va_arg(ap, const char *)); break;
      case 'c': putch(va_arg(ap, int)); break;
      default: putch(*f);
    }
  }
  va_end(ap);
}
/* libc bits gcc may emit calls to */
void *memcpy(void *d, const void *s, unsigned n) { char *a = d; const char *b = s; while (n--) *a++ = *b++; return d; }
void *memset(void *d, int c, unsigned n) { char *a = d; while (n--) *a++ = c; return d; }
int memcmp(const void *x, const void *y, unsigned n) { const unsigned char *a = x, *b = y; for (; n; n--, a++, b++) if (*a != *b) return *a - *b; return 0; }
unsigned strlen(const char *s) { unsigned n = 0; while (s[n]) n++; return n; }
/* sqrt for builds without inline VFP (Thumb, soft-float); only needs to be
   deterministic, the test compares against QEMU running the same code. */
__attribute__((weak)) double sqrt(double x) {
  if (!(x > 0) || x != x || x > 1.7e308) return x != x || x >= 0 ? x : (x - x) / (x - x);
  double r = x > 1 ? x : 1;
  for (int i = 0; i < 80; i++) { double n = 0.5 * (r + x / r); if (n == r) break; r = n; }
  return r;
}
__attribute__((weak)) float sqrtf(float x) { return (float)sqrt(x); }
