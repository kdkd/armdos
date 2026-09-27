/* CPU benchmark: a mix of typical compiled-C integer workloads.
   Every kernel prints a checksum so the run doubles as a differential test. */
#include "lib.h"

static u32 seed = 1;
static u32 rnd(void) { seed = seed * 1103515245u + 12345u; return seed >> 8; }

/* 1. sieve */
static unsigned char flags[65536];
static int sieve(void) {
  int count = 0;
  for (int i = 2; i < 65536; i++) flags[i] = 1;
  for (int i = 2; i < 65536; i++) if (flags[i]) { count++; for (int k = i + i; k < 65536; k += i) flags[k] = 0; }
  return count;
}

/* 2. crc32 */
static u32 crctab[256];
static unsigned char buf[32768];
static u32 crc32(const unsigned char *p, int n) {
  u32 c = 0xffffffff;
  while (n--) c = crctab[(c ^ *p++) & 0xff] ^ (c >> 8);
  return ~c;
}

/* 3. quicksort with a comparison callback */
static int arr[8192];
typedef int (*cmpf)(const int *, const int *);
static int cmpint(const int *a, const int *b) { return *a < *b ? -1 : *a > *b; }
static void qsort_(int *a, int n, cmpf cmp) {
  while (n > 1) {
    int p = a[n / 2], i = 0, j = n - 1;
    while (i <= j) {
      while (cmp(&a[i], &p) < 0) i++;
      while (cmp(&a[j], &p) > 0) j--;
      if (i <= j) { int t = a[i]; a[i] = a[j]; a[j] = t; i++; j--; }
    }
    if (j + 1 < n - i) { qsort_(a, j + 1, cmp); a += i; n -= i; }
    else { qsort_(a + i, n - i, cmp); n = j + 1; }
  }
}

/* 4. matrix multiply */
#define MN 48
static int ma[MN][MN], mb[MN][MN], mc[MN][MN];
static u32 matmul(void) {
  for (int i = 0; i < MN; i++) for (int j = 0; j < MN; j++) {
    int s = 0;
    for (int k = 0; k < MN; k++) s += ma[i][k] * mb[k][j];
    mc[i][j] = s;
  }
  u32 h = 0; for (int i = 0; i < MN; i++) for (int j = 0; j < MN; j++) h = h * 31 + mc[i][j];
  return h;
}

/* 5. struct / string work (dhrystone-flavoured) */
struct rec { struct rec *next; int kind, val; char name[32]; };
static struct rec pool[256];
static int strcmp_(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return (unsigned char)*a - (unsigned char)*b; }
static void strcpy_(char *d, const char *s) { while ((*d++ = *s++)) ; }
static u32 records(void) {
  static const char *names[] = {"DHRYSTONE PROGRAM, SOME STRING", "DHRYSTONE PROGRAM, 1'ST STRING", "DHRYSTONE PROGRAM, 2'ND STRING", "ARM-DOS"};
  u32 h = 0;
  for (int i = 0; i < 256; i++) { pool[i].next = &pool[(i * 37 + 11) & 255]; pool[i].kind = i % 5; pool[i].val = i * 7; strcpy_(pool[i].name, names[i & 3]); }
  struct rec *r = &pool[0];
  for (int it = 0; it < 20000; it++) {
    struct rec tmp = *r;
    switch (tmp.kind) {
      case 0: tmp.val += 3; break;
      case 1: tmp.val ^= it; break;
      case 2: tmp.val = tmp.val * 5 - 1; break;
      default: tmp.val >>= 1;
    }
    if (strcmp_(tmp.name, names[it & 3]) < 0) tmp.kind = (tmp.kind + 1) % 5;
    *r->next = tmp.kind == 3 ? *r->next : *r->next;
    r->val = tmp.val; r->kind = tmp.kind;
    h = h * 33 + (u32)tmp.val + tmp.kind;
    r = r->next;
  }
  return h;
}

/* 6. linked list sort (merge sort) */
struct node { struct node *next; u32 key; };
static struct node nodes[4096];
static struct node *merge(struct node *a, struct node *b) {
  struct node head, *t = &head;
  while (a && b) { if (a->key <= b->key) { t->next = a; a = a->next; } else { t->next = b; b = b->next; } t = t->next; }
  t->next = a ? a : b;
  return head.next;
}
static struct node *msort(struct node *h) {
  if (!h || !h->next) return h;
  struct node *slow = h, *fast = h->next;
  while (fast && fast->next) { slow = slow->next; fast = fast->next->next; }
  struct node *b = slow->next; slow->next = 0;
  return merge(msort(h), msort(b));
}

/* 7. block copy + fixed-point math (DOOM-style FixedMul/FixedDiv) */
typedef int fixed_t;
static fixed_t FixedMul(fixed_t a, fixed_t b) { return (fixed_t)(((long long)a * b) >> 16); }
static fixed_t FixedDiv(fixed_t a, fixed_t b) {
  if ((a < 0 ? -a : a) >> 14 >= (b < 0 ? -b : b)) return (a ^ b) < 0 ? (int)0x80000000 : 0x7fffffff;
  return (fixed_t)(((long long)a << 16) / b);
}
static u32 bigbuf[65536];
static u32 fixedwork(void) {
  u32 h = 0;
  for (int i = 0; i < 65536; i++) bigbuf[i] = rnd();
  for (int r = 0; r < 4; r++) memcpy(bigbuf + 32768, bigbuf, 32768 * 4 - 4 * r);
  for (int i = 0; i < 20000; i++) {
    fixed_t a = (fixed_t)bigbuf[i] >> 8, b = ((fixed_t)bigbuf[i + 1] >> 12) | 1;
    h += FixedMul(a, b) ^ FixedDiv(a, b);
  }
  return h;
}

/* 8. byte-oriented run-length / LZ-ish coder */
static unsigned char src[16384], dst[40000], back[16384];
static int rle_enc(const unsigned char *s, int n, unsigned char *d) {
  int o = 0;
  for (int i = 0; i < n;) {
    int j = i; while (j < n && j - i < 255 && s[j] == s[i]) j++;
    if (j - i >= 3) { d[o++] = 0xff; d[o++] = j - i; d[o++] = s[i]; i = j; }
    else { if (s[i] == 0xff) { d[o++] = 0xff; d[o++] = 1; d[o++] = 0xff; } else d[o++] = s[i]; i++; }
  }
  return o;
}
static int rle_dec(const unsigned char *s, int n, unsigned char *d) {
  int o = 0;
  for (int i = 0; i < n;) { if (s[i] == 0xff) { for (int k = 0; k < s[i + 1]; k++) d[o++] = s[i + 2]; i += 3; } else d[o++] = s[i++]; }
  return o;
}

int main(void) {
#ifndef ROUNDS
#define ROUNDS 3
#endif
  int rounds = ROUNDS;
  printf_("bench start\n");
  for (int r = 0; r < rounds; r++) {
    printf_("sieve %d\n", sieve());
    for (u32 i = 0; i < 256; i++) { u32 c = i; for (int k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >> 1) : c >> 1; crctab[i] = c; }
    for (int i = 0; i < 32768; i++) buf[i] = rnd();
    u32 c = 0; for (int k = 0; k < 4; k++) c ^= crc32(buf, 32768 - k);
    printf_("crc %08x\n", c);
    for (int i = 0; i < 8192; i++) arr[i] = rnd() % 100000 - 50000;
    qsort_(arr, 8192, cmpint);
    u32 h = 0; for (int i = 0; i < 8192; i++) h = h * 7 + arr[i];
    printf_("qsort %08x %d %d\n", h, arr[0], arr[8191]);
    for (int i = 0; i < MN; i++) for (int j = 0; j < MN; j++) { ma[i][j] = rnd() % 100 - 50; mb[i][j] = rnd() % 100 - 50; }
    printf_("matmul %08x\n", matmul());
    printf_("records %08x\n", records());
    for (int i = 0; i < 4096; i++) { nodes[i].key = rnd(); nodes[i].next = i < 4095 ? &nodes[i + 1] : 0; }
    struct node *l = msort(&nodes[0]); h = 0; for (; l; l = l->next) h = h * 3 + l->key;
    printf_("msort %08x\n", h);
    printf_("fixed %08x\n", fixedwork());
    for (int i = 0; i < 16384; i++) src[i] = (rnd() % 7 == 0) ? rnd() : (i / 37) & 0xff;
    int n = rle_enc(src, 16384, dst); int m = rle_dec(dst, n, back);
    printf_("rle %d %d %d\n", n, m, memcmp(src, back, 16384));
  }
  printf_("bench done\n");
  return 0;
}
