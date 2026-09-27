/*
 * fcasm.c - ARM-DOS: C translations of FC's 8086 assembly helpers
 * (orig/MESSAGES.ASM, MOVE.ASM, STRING.ASM, MAXMIN.ASM, ITOUPPER.ASM,
 * XTAB.ASM - the originals are kept in orig/ for reference; GETL.ASM is
 * linked by the original makefile but never called by FC).
 *
 * Portions (c) Microsoft Corp. (MS-DOS 4.0 CMD/FC), MIT License.
 */
#include <string.h>
#include <dos.h>

/* ------------------------------------------------------ MESSAGES.ASM */
char BadSw[]     = "Incompatible switches";
char Bad_ver[]   = "Incorrect DOS version";
char UseMes[]    = "usage: fc [/a] [/b] [/c] [/l] [/lbNN] [/w] [/t] [/n] [/NNNN] file1 file2\n";
char BadOpn[]    = "cannot open %s - %s";
char LngFil[]    = "%s longer than %s";
char NoDif[]     = "no differences encountered";
char NoMem[]     = "out of memory\n";
/* MASM does not know C escapes: the real FC prints a backslash and an n */
char ReSyncMes[] = "Resync failed.  Files are too different\\n";
char UnKnown[]   = "Unknown error";

/* ---------------------------------------------------------- MOVE.ASM */
void Move(char *src, char *dst, unsigned count)
{
    memmove(dst, src, count & 0xFFFF);
}

void Fill(char *dst, char value, unsigned count)
{
    memset(dst, value, count & 0xFFFF);
}

/* -------------------------------------------------------- MAXMIN.ASM */
int max(int a, int b) { return a > b ? a : b; }
int min(int a, int b) { return a < b ? a : b; }

/* -------------------------------------------------------- STRING.ASM */
/* strbscan (string, set): pointer to the first character in set, or the end */
char *strbscan(char *str, char *set)
{
    for (;; str++) {
        /* repnz scasb over strlen(set)+1 bytes: the NUL is in the set */
        if (memchr(set, *str, strlen(set) + 1)) return str;
    }
}

/* strbskip (string, set): pointer to the first character not in set */
char *strbskip(char *str, char *set)
{
    for (; *str; str++)
        if (!memchr(set, *str, strlen(set) + 1)) break;
    return str;
}

extern char XLTab[];

/* strpre (s1, s2): -1 if s1 is a prefix of s2 (ignoring case), else 0 */
int strpre(char *pref, char *str)
{
    for (;;) {
        unsigned char a = XLTab[(unsigned char)*pref++];
        unsigned char b = XLTab[(unsigned char)*str++];
        if (a != b) return b == 0 ? -1 : 0;
        if (!a) return -1;
    }
}

/* -------------------------------------------------------- XTAB.ASM */
char XLTab[256], XUTab[256];

__attribute__((constructor)) static void xtab_init(void)
{
    int i;
    for (i = 0; i < 256; i++) {
        XLTab[i] = (i >= 'A' && i <= 'Z') ? i + 32 : i;
        XUTab[i] = (i >= 'a' && i <= 'z') ? i - 32 : i;
    }
}

/* ------------------------------------------------------ ITOUPPER.ASM */
/* c = IToupper (c, routine): a-z by arithmetic, then the country case map
   routine (INT 21h AH=38h +12h; on ARM-DOS a C function uint32_t (uint32_t)) */
int IToupper(int c, unsigned long routine)
{
    unsigned ax = c & 0xFFFF;
    if (ax >> 8) return ax;
    if (ax >= 'a' && ax <= 'z') ax -= 0x20;
    if (routine) ax = (ax & 0xFF00) | (((unsigned (*)(unsigned))routine)(ax & 0xFF) & 0xFF);
    return ax;
}

/* get_lbtbl (&table): the DBCS lead byte table from INT 21h AX=6300h (DS:SI;
   left alone if the call does not change SI) */
void get_lbtbl(long *pointer_to_table)
{
    union REGS r;
    r.x.ax = 0x6300;
    r.x.si = (unsigned)*pointer_to_table;
    intdos(&r, &r);
    *pointer_to_table = (long)r.x.si;
}

/* test_ECS (char, table): -1 if char is a lead byte (ranges end with 0) */
int test_ecs(unsigned c, long table)
{
    const unsigned char *p = (const unsigned char *)table;
    c &= 0xFF;
    for (;;) {
        unsigned lo = *p++, hi;
        if (!lo || c < lo) return 0;
        hi = *p++;
        if (c <= hi) return 0xFFFF;
    }
}
