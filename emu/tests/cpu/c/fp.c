/* Floating-point torture: float and double arithmetic, conversions and
   comparisons on random and special operands; prints bit patterns.
   Built with -mfpu=vfp -mfloat-abi=softfp (VFP instructions) and with
   -mfloat-abi=soft (libgcc soft-float); both must match QEMU. */
#include "lib.h"
static u32 seed = 99;
static u32 rnd(void) { seed = seed * 1664525u + 1013904223u; return seed ^ (seed >> 13); }
typedef union { float f; u32 u; } F;
typedef union { double d; unsigned long long u; } D;
static float rf(void) {
  static const u32 sp[] = {0, 0x80000000, 0x3f800000, 0xbf800000, 0x7f800000, 0xff800000, 0x7fc00000, 0x00000001, 0x00800000, 0x7f7fffff, 0x4f000000, 0xcf000000, 0x3effffff, 0x40490fdb};
  F x; u32 k = rnd() % 10;
  if (k == 0) x.u = sp[rnd() % 14];
  else if (k < 8) x.u = (rnd() & 0x80000000) | ((97 + rnd() % 60) << 23) | (rnd() & 0x7fffff);
  else x.u = rnd();
  if ((x.u & 0x7f800000) == 0x7f800000 && (x.u & 0x7fffff)) x.u |= 0x400000;   /* quiet NaNs only: soft-float NaN payloads differ */
  return x.f;
}
static double rd(void) {
  D x; u32 k = rnd() % 10;
  if (k == 0) { static const unsigned long long sp[] = {0, 0x8000000000000000ull, 0x3ff0000000000000ull, 0x7ff0000000000000ull, 0x0000000000000001ull, 0x0010000000000000ull, 0x7fefffffffffffffull, 0x41e0000000000000ull, 0xc1e0000000000000ull, 0x3fe0000000000000ull};
    x.u = sp[rnd() % 10]; }
  else if (k < 9) x.u = ((unsigned long long)((rnd() & 0x80000000) | ((1023 - 40 + rnd() % 80) << 20) | (rnd() & 0xfffff)) << 32) | rnd();
  else x.u = ((unsigned long long)rnd() << 32) | rnd();
  if ((x.u >> 52 & 0x7ff) == 0x7ff && (x.u << 12)) x.u = 0x7ff8000000000000ull;
  return x.d;
}
static u32 ck;
static void mixf(float f) { F x; x.f = f; if (x.u << 1 > 0xff000000u) x.u = 0x7fc00000; ck = (ck ^ x.u) * 0x01000193u; ck ^= ck >> 11; }
static void mixd(double d) { D x; x.d = d; if ((x.u << 1) > 0xffe0000000000000ull) x.u = 0x7ff8000000000000ull; ck = (ck ^ (u32)x.u) * 0x01000193u; ck = (ck ^ (u32)(x.u >> 32)) * 0x01000193u; ck ^= ck >> 11; }
static void mixi(u32 v) { ck = (ck ^ v) * 0x01000193u; ck ^= ck >> 11; }

int main(void) {
  printf_("fp start\n");
  volatile float vf; volatile double vd;
  for (int it = 0; it < 20000; it++) {
    float a = rf(), b = rf(), c = rf();
    double x = rd(), y = rd(), z = rd();
    mixf(a + b); mixf(a - b); mixf(a * b); mixf(a / b); mixf(a * b + c); mixf(c - a * b); mixf(-(a * b));
    mixd(x + y); mixd(x - y); mixd(x * y); mixd(x / y); mixd(x * y + z); mixd(z - x * y);
    mixf(__builtin_sqrtf(a < 0 ? -a : a)); mixd(__builtin_sqrt(x < 0 ? -x : x));
    mixd((double)a); mixf((float)x); mixf((float)y * a);
    if (a == a && a > -2e9f && a < 2e9f) mixi((u32)(int)a);
    if (x == x && x > -2e9 && x < 2e9) mixi((u32)(int)x);
    if (x == x && x >= 0 && x < 4e9) mixi((u32)x);
    int i = (int)rnd(); unsigned u = rnd();
    mixf((float)i); mixf((float)u); mixd((double)i); mixd((double)u);
    mixi((a < b) | (a <= b) << 1 | (a == b) << 2 | (a != b) << 3 | (a > b) << 4 | (a >= b) << 5);
    mixi((x < y) | (x <= y) << 1 | (x == y) << 2 | (x != y) << 3 | (x > y) << 4 | (x >= y) << 5);
    vf = a; vd = x; mixf(vf * 3.0f); mixd(vd * 0.1);
    if (it % 2000 == 1999) printf_("it %d ck %08x\n", it + 1, ck);
  }
  /* accumulation loops */
  double s = 0; float sf = 0;
  for (int k = 1; k < 200000; k++) { s += 1.0 / ((double)k * k); sf += 1.0f / (float)k; }
  D sd; sd.d = s; F sff; sff.f = sf;
  printf_("zeta2 %08x%08x harm %08x\n", (u32)(sd.u >> 32), (u32)sd.u, sff.u);
  printf_("fp done ck %08x\n", ck);
  return 0;
}
