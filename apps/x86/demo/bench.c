/*
 * bench.c - one benchmark, two machines.
 *
 * Built twice from this same source:
 *   BENCHARM.EXE  with the ARM-DOS SDK (gcc, ARM native)
 *   BENCH86.EXE   with OpenWatcom for 16-bit x86 DOS (runs under ELBOW)
 *
 *   BENCHxxx [label]   runs four workloads for 2 seconds each, appends the
 *                      results to BENCH.DAT under the label and prints a
 *                      comparison of every run recorded there.
 *   BENCHxxx /CLEAR    forgets the recorded runs.
 *
 * Copyright (c) 2026 Europa Micro Systems. Part of ARM-DOS (apps/x86/demo).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>

#ifdef __WATCOMC__
#include <i86.h>
#define FAR __far
#define TICKS (*(volatile unsigned long FAR *)MK_FP(0x40, 0x6C))
#define VRAM ((unsigned char FAR *)MK_FP(0xA000, 0))
#define MACHINE "x86"
#else
#define FAR
#define TICKS (*(volatile unsigned long *)0x46C)
#define VRAM ((unsigned char *)0xA0000)
#define MACHINE "ARM"
#endif

#define SECS_TICKS 36                   /* 2 seconds of 18.2 Hz ticks */

static unsigned char flags[8191];
static unsigned char src[16384], dst[16384];

static unsigned sieve(void)
{
    unsigned i, k, prime, count = 0;
    for (i = 0; i <= 8190; i++) flags[i] = 1;
    for (i = 0; i <= 8190; i++) {
        if (!flags[i]) continue;
        prime = i + i + 3;
        for (k = i + prime; k <= 8190; k += prime) flags[k] = 0;
        count++;
    }
    return count;
}

static unsigned copy(void)
{
    memcpy(dst, src, sizeof dst);
    memcpy(src, dst, sizeof src);
    return dst[100];
}

static unsigned crc(void)
{
    unsigned c = 0xFFFF, i;
    int b;
    for (i = 0; i < 4096; i++) {
        c ^= src[i];
        for (b = 0; b < 8; b++) c = (c & 1) ? (c >> 1) ^ 0xA001 : c >> 1;
    }
    return c;
}

static unsigned frame;
static unsigned pixels(void)
{
    unsigned char FAR *v = VRAM;
    unsigned x, y, f = frame++;
    for (y = 0; y < 200; y++) {
        unsigned char c = (unsigned char)(y + f);
        for (x = 0; x < 320; x++) *v++ = (unsigned char)(c + (x >> 2));
    }
    return f;
}

static void mode(int m)
{
    union REGS r;
    r.x.ax = m;
    int86(0x10, &r, &r);
}

/* iterations of fn per second, measured over 2 seconds */
static unsigned long rate(unsigned (*fn)(void))
{
    unsigned long t0, t, n = 0;
    t0 = TICKS;
    while (TICKS == t0) ;
    t0 = TICKS;
    do { fn(); n++; t = TICKS; } while (t - t0 < SECS_TICKS);
    return (n * 182UL + (t - t0) * 5UL) / ((t - t0) * 10UL);
}

static const char *names[4] = { "Sieve of Eratosthenes (8190)", "Memory copy (2 x 16 KB)",
                                "CRC-16 (4 KB)", "Mode 13h screen fill (64000 px)" };

int main(int argc, char **argv)
{
    unsigned long r[4];
    char label[40];
    FILE *f;
    unsigned i;

    if (argc > 1 && !strcmp(argv[1], "/CLEAR")) { remove("BENCH.DAT"); puts("Results cleared."); return 0; }
    if (argc > 1 && !strcmp(argv[1], "/?")) {
        puts("BENCH [label] - the same C benchmark, built for ARM and for x86.\n"
             "Runs 4 workloads for 2 seconds each and compares with earlier runs (BENCH.DAT).");
        return 0;
    }
    strcpy(label, argc > 1 ? argv[1] : MACHINE);
    for (i = 0; i < sizeof src; i++) src[i] = (unsigned char)(i * 7);

    printf("Benchmark: %s build, run \"%s\"\n", MACHINE, label);
    r[0] = rate(sieve);  printf("  %-32s %8lu /s\n", names[0], r[0]);
    r[1] = rate(copy);   printf("  %-32s %8lu /s\n", names[1], r[1]);
    r[2] = rate(crc);    printf("  %-32s %8lu /s\n", names[2], r[2]);
    mode(0x13);
    r[3] = rate(pixels);
    mode(0x03);
    printf("  %-32s %8lu /s\n", names[3], r[3]);

    f = fopen("BENCH.DAT", "a");
    if (f) { fprintf(f, "%s %lu %lu %lu %lu\n", label, r[0], r[1], r[2], r[3]); fclose(f); }

    /* the comparison table */
    f = fopen("BENCH.DAT", "r");
    if (f) {
        char l[8][16];
        unsigned long v[8][4];
        int n = 0, k, j;
        while (n < 8 && fscanf(f, "%15s %lu %lu %lu %lu", l[n], &v[n][0], &v[n][1], &v[n][2], &v[n][3]) == 5) n++;
        fclose(f);
        if (n > 1) {
            printf("\nLoops per second        ");
            for (k = 0; k < n; k++) printf(" %10s", l[k]);
            printf("\n");
            for (j = 0; j < 4; j++) {
                static const char *sh[4] = { "sieve", "memcpy", "crc16", "mode 13h fill" };
                printf("  %-21s", sh[j]);
                for (k = 0; k < n; k++) printf(" %10lu", v[k][j]);
                printf("\n");
            }
            printf("  %-21s", "slower than the first");
            for (k = 0; k < n; k++) {
                unsigned long s = 0;
                for (j = 0; j < 4; j++) if (v[k][j]) s += v[0][j] * 10UL / v[k][j];
                printf("   %5lu.%lux", s / 40, (s / 4) % 10);
            }
            printf("\n");
        }
    }
    return 0;
}
