/* Integer torture: random operations checked by printing checksums.
   Built as ARM and as Thumb at several optimisation levels. */
#include "lib.h"
static u32 seed = 12345;
static u32 rnd(void) { seed = seed * 1664525u + 1013904223u; return seed ^ (seed >> 15); }
static u32 rv(void) {
  static const u32 sp[] = {0, 1, 2, 3, 0xffffffff, 0x80000000, 0x7fffffff, 0x80000001, 31, 32, 33, 0x10000, 0xffff, 0x8000};
  u32 k = rnd() % 8;
  if (k == 0) return sp[rnd() % 14];
  if (k == 1) return rnd() & 0xff;
  return rnd();
}
static u32 ck = 0;
static void mix(u32 v) { ck = (ck ^ v) * 0x01000193u; ck ^= ck >> 13; }
static void mix64(unsigned long long v) { mix((u32)v); mix((u32)(v >> 32)); }

__attribute__((noinline)) static int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
__attribute__((noinline)) static s32 sdiv(s32 a, s32 b) { return b ? a / b : 0; }
__attribute__((noinline)) static u32 udiv(u32 a, u32 b) { return b ? a / b : 0; }
typedef u32 (*binop)(u32, u32);
static u32 op_add(u32 a, u32 b) { return a + b; }
static u32 op_sub(u32 a, u32 b) { return a - b; }
static u32 op_rot(u32 a, u32 b) { b &= 31; return (a >> b) | (a << ((32 - b) & 31)); }
static u32 op_sar(u32 a, u32 b) { return (u32)((s32)a >> (b & 31)); }
static u32 op_cmp(u32 a, u32 b) { return ((s32)a < (s32)b) * 4 + (a < b) * 2 + (a == b); }
static u32 op_clz(u32 a, u32 b) { return __builtin_clz(a | 1) + __builtin_ctz(b | 0x80000000u) * 64; }
static u32 op_pop(u32 a, u32 b) { return __builtin_popcount(a) * 33 + __builtin_parity(b); }
static const binop ops[] = {op_add, op_sub, op_rot, op_sar, op_cmp, op_clz, op_pop};

struct S { unsigned a : 3, b : 7, c : 13; signed d : 9; unsigned char e; short f; };
__attribute__((noinline)) static u32 sw(u32 x) {
  switch (x % 11) {
    case 0: return x * 3; case 1: return x >> 3; case 2: return ~x; case 3: return x ^ 0x5555;
    case 4: return x + 77; case 5: return -x; case 6: return x << 5; case 7: return x & 0xf0f0f0f0;
    case 8: return x | 1; case 9: return x * x; default: return 42;
  }
}

int main(void) {
  printf_("arith start\n");
  for (int it = 0; it < 20000; it++) {
    u32 a = rv(), b = rv();
    s32 sa = (s32)a, sb = (s32)b;
    mix(a + b); mix(a - b); mix(a * b); mix(a & ~b); mix(a | b); mix(a ^ b);
    mix(a << (b & 31)); mix(a >> (b & 31)); mix((u32)(sa >> (b & 31)));
    mix(udiv(a, b)); mix(sdiv(sa, sb == -1 && sa == (s32)0x80000000 ? 1 : sb)); mix(b ? a % b : 0);
    mix((u32)(sa < sb)); mix((u32)(a < b)); mix((u32)(sa <= sb)); mix((u32)(a >= b));
    unsigned long long p = (unsigned long long)a * b; mix64(p);
    long long sp = (long long)sa * sb; mix64((unsigned long long)sp);
    mix64(p + ((unsigned long long)b << 32));
    unsigned long long q = ((unsigned long long)a << 32) | b;
    unsigned long long d = ((unsigned long long)rv() << 16) | 1;
    mix64(q / d); mix64(q % d); mix64(q >> (a & 63)); mix64(q << (b & 63)); mix64((unsigned long long)((long long)q >> (a & 63)));
    mix(ops[it % 7](a, b));
    struct S s; s.a = a; s.b = b; s.c = a >> 7; s.d = b >> 3; s.e = a >> 24; s.f = b >> 9;
    mix(s.a + s.b * 7 + s.c * 13 + (u32)s.d * 3 + s.e + (u32)s.f);
    mix(sw(a)); mix(sw(b));
    unsigned short h = a; signed char c = b; mix((u32)h * (u32)(s32)c);
    if (it % 1000 == 999) printf_("it %d ck %08x\n", it + 1, ck);
  }
  printf_("fib(20)=%d\n", fib(20));
  /* memory ops */
  static u32 arr[1024];
  static unsigned char bytes[4096];
  for (int i = 0; i < 1024; i++) arr[i] = rnd();
  for (int i = 0; i < 4096; i++) bytes[i] = rnd();
  for (int r = 0; r < 50; r++) {
    for (int i = 1; i < 1024; i++) arr[i] += arr[i - 1] ^ (arr[(i * 7) & 1023] >> 3);
    for (int i = 0; i < 4000; i++) { bytes[i] ^= bytes[i + 13] + (unsigned char)arr[i & 1023]; }
    unsigned short *hw = (unsigned short *)bytes;
    for (int i = 0; i < 2000; i++) hw[i] = hw[i] * 3 + hw[(i + 5) % 2000];
    memcpy(bytes + 1, bytes + 100, 1500);
  }
  for (int i = 0; i < 1024; i++) mix(arr[i]);
  for (int i = 0; i < 4096; i++) mix(bytes[i]);
  printf_("final ck %08x\n", ck);
  return 0;
}
