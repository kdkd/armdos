/*
 * bench.c - ARMINFO's timing and benchmarks.
 *
 * The clock: like the period benchmark programs, we reprogram PIT channel 0
 * to 1 kHz (mode 2, divisor 1193), count milliseconds in our own INT 08h
 * handler (calling the BIOS's handler every 65536/1193 ticks so the time of
 * day keeps going at 18.2 Hz) and read the counter latch for the fraction:
 * a resolution of one PIT clock, 0.84 us.
 *
 * Copyright (C) 1989 Europa Micro Systems (ARM-DOS project).
 */
#include <string.h>
#include "armdos.h"
#include "arminfo.h"

#define DIVISOR 1193u

static volatile uint32_t ms_ticks;
static uint32_t chain_acc, last_pit;
static armdos_vect_t old08;

static void int08(struct armregs *f)
{
    ms_ticks++;
    chain_acc += DIVISOR;
    if (chain_acc >= 65536) {
        chain_acc -= 65536;
        old08(f);                       /* BIOS tick (sends the EOI) */
    } else {
        armdos_outb(0x20, 0x20);
    }
}

void timer_start(void)
{
    old08 = armdos_getvect(0x08);
    armdos_disable();
    armdos_setvect(0x08, int08);
    armdos_outb(0x43, 0x34);            /* ch0, lo/hi, mode 2 */
    armdos_outb(0x40, DIVISOR & 0xFF);
    armdos_outb(0x40, DIVISOR >> 8);
    armdos_enable();
}

void timer_stop(void)
{
    armdos_disable();
    armdos_outb(0x43, 0x36);            /* back to 18.2 Hz, mode 3 */
    armdos_outb(0x40, 0);
    armdos_outb(0x40, 0);
    armdos_setvect(0x08, old08);
    armdos_enable();
}

/* PIT clocks (1.193182 MHz) since timer_start */
uint32_t timer_pit(void)
{
    uint32_t t1, t2, c;
    do {
        t1 = ms_ticks;
        armdos_outb(0x43, 0x00);        /* latch ch0 */
        c = armdos_inb(0x40);
        c |= armdos_inb(0x40) << 8;
        t2 = ms_ticks;
    } while (t1 != t2);
    if (c > DIVISOR) c = DIVISOR;
    uint32_t t = t1 * DIVISOR + (DIVISOR - c);
    if (t < last_pit) t += DIVISOR;     /* counter reloaded, IRQ not taken yet */
    last_pit = t;
    return t;
}

uint32_t pit_to_us(uint32_t pit) { return (uint32_t)(((uint64_t)pit * 1000000u) / 1193182u); }

/* ---------------------------------------------------- Computing Index */
/*
 * A small integer mix in the spirit of Dhrystone: record assignment, a call,
 * string compare/copy, array indexing, a switch and some arithmetic.
 */
struct rec { struct rec *next; int kind, a, b; char name[20]; };

static struct rec r1, r2;
static int arr[64];
static const char s1[] = "ARM-DOS, 1ST STRING";
static const char s2[] = "ARM-DOS, 2ND STRING";

__attribute__((noinline)) static int proc1(int x, int y) { return (x * 3 + y) ^ (x >> 2); }

__attribute__((noinline)) static int strcomp(const char *a, const char *b)
{
    while (*a && *a == *b) a++, b++;
    return *a - *b;
}

__attribute__((noinline)) static void strcopy(char *d, const char *s) { while ((*d++ = *s++)) ; }

uint32_t ci_work(uint32_t n)
{
    uint32_t sum = 0;
    r1.next = &r2;
    for (uint32_t i = 0; i < n; i++) {
        r1.a = i;
        r1.b = proc1(i, r1.b);
        if (r1.b & 1) { r2 = r1; r2.next = 0; }
        else r2.a += 3;
        strcopy(r1.name, (i & 1) ? s1 : s2);
        if (strcomp(r1.name, s1) > 0) r1.kind++;
        arr[i & 63] += r2.a;
        switch (i & 3) {
        case 0: sum += arr[(i * 7) & 63]; break;
        case 1: sum ^= r1.b; break;
        case 2: sum += r1.kind << 1; break;
        default: sum -= i / 3; break;
        }
    }
    return sum;
}

/* Run `fn` on growing counts until it takes >= min_us; returns units/second
 * (x100 for two decimals). */
static uint64_t rate_x100(uint32_t (*fn)(uint32_t), uint32_t units_per_call, uint32_t min_us)
{
    uint32_t n = 1;
    for (;;) {
        uint32_t t0 = timer_pit();
        fn(n);
        uint32_t us = pit_to_us(timer_pit() - t0);
        if (us >= min_us || n >= (1u << 24)) {
            if (!us) us = 1;
            return ((uint64_t)n * units_per_call * 100000000u) / us;
        }
        n = us < min_us / 16 ? n * 8 : n * 2;
    }
}

/*
 * Calibration: one XT unit = the ci_work iterations per second of an IBM
 * PC/XT, set from the Dhrystone 1.1 ratio of an ARM926 at 100 MHz
 * (~110 DMIPS at 1.1 DMIPS/MHz) to a 4.77 MHz 8088 (~0.22 DMIPS): 500.
 * ci_work is about 220 instructions per iteration (the string routines
 * dominate), so at 100 MHz it does ~454,600/s and the XT figure is 909/s.
 */
#define XT_CI_RATE 909u

uint32_t bench_ci_x10(void)
{
    uint64_t r = rate_x100(ci_work, 1, 400000);   /* iterations/s x100 */
    return (uint32_t)((r * 10 / 100 + XT_CI_RATE / 2) / XT_CI_RATE);
}

/* -------------------------------------------------------- Disk Index */
/*
 * INT 13h on the first hard disk: 32 single-sector reads spread across the
 * disk (seek test) and 8 x 63 sequential sectors (transfer test). An XT with
 * its 10 MB ST-412 drive takes, per seek, 85 ms average access plus 8.3 ms
 * rotational latency, and transfers about 85 KB/s through its interleave.
 */
static int bios_read(unsigned c, unsigned h, unsigned s, unsigned n, void *buf)
{
    struct armregs r = { 0 };
    r.r0 = 0x0200 | n;
    r.r1 = (uint32_t)buf;
    r.r2 = ((c & 0xFF) << 8) | ((c >> 2) & 0xC0) | s;
    r.r3 = (h << 8) | 0x80;
    return _armdos_int13(&r) ? -1 : 0;
}

uint32_t bench_di_x10(uint8_t *buf, uint32_t *seek_us, uint32_t *kbs)
{
    struct armregs r = { 0 };
    r.r0 = 0x0800; r.r3 = 0x80;
    if (_armdos_int13(&r)) return 0;
    unsigned heads = ((r.r3 >> 8) & 0xFF) + 1, spt = r.r2 & 0x3F;
    unsigned cyls = (((r.r2 >> 8) & 0xFF) | ((r.r2 & 0xC0) << 2)) + 1;
    if (!spt || !heads || cyls < 2) return 0;

    uint32_t t0 = timer_pit();
    for (unsigned i = 0; i < 32; i++) {
        unsigned c = (i & 1) ? (i * 37) % cyls : (cyls - 1 - (i * 11) % cyls);
        if (bios_read(c, i % heads, 1 + i % spt, 1, buf)) return 0;
    }
    uint32_t tseek = timer_pit() - t0;
    t0 = timer_pit();
    for (unsigned i = 0; i < 8; i++)
        if (bios_read(i / heads, i % heads, 1, spt, buf)) return 0;
    uint32_t txfer = timer_pit() - t0;

    uint32_t us_seek = pit_to_us(tseek), us_xfer = pit_to_us(txfer);
    if (!us_seek) us_seek = 1;
    if (!us_xfer) us_xfer = 1;
    uint32_t bytes = 8 * spt * 512;
    *seek_us = us_seek / 32;
    *kbs = (uint32_t)((uint64_t)bytes * 1000000u / 1024 / us_xfer);
    /* XT time for the same work, in microseconds */
    uint64_t xt_us = 32ull * 93300 + (uint64_t)bytes * 1000000u / (85 * 1024);
    return (uint32_t)(xt_us * 10 / (us_seek + us_xfer));
}

/* --------------------------------------------------- ARM feature tests */

#define N_ELEM 1024
static int32_t xs[N_ELEM], ys[N_ELEM];

__attribute__((noinline)) static void bs_c(const int32_t *x, int32_t *y, unsigned n)
{
    for (unsigned i = 0; i < n; i++) y[i] = x[i] * 5 + (x[i] >> 3);
}

static uint32_t run_bs_sep(uint32_t n) { while (n--) bs_separate(xs, ys, N_ELEM); return 0; }
static uint32_t run_bs_bar(uint32_t n) { while (n--) bs_barrel(xs, ys, N_ELEM); return 0; }
static uint32_t run_bs_c(uint32_t n) { while (n--) bs_c(xs, ys, N_ELEM); return 0; }

__attribute__((noinline)) static uint32_t gcd_c(uint32_t a, uint32_t b)
{
    while (a != b) { if (a > b) a -= b; else b -= a; }
    return a;
}

/* pairs with a fixed amount of work: gcd(F(k+1), F(k)) style inputs */
static const uint32_t gcd_pairs[8][2] = {
    { 1071, 462 }, { 832040, 514229 }, { 99991, 65537 }, { 123456, 7890 },
    { 1000000, 999 }, { 46368, 28657 }, { 360, 2310 }, { 65535, 4097 },
};
static volatile uint32_t sink;
#define GCD_RUN(name, fn) static uint32_t name(uint32_t n) { \
        uint32_t s = 0; \
        while (n--) \
            for (int k = 0; k < 8; k++) s += fn(gcd_pairs[k][0], gcd_pairs[k][1]); \
        sink = s; \
        return s; \
    }
GCD_RUN(run_gcd_b, gcd_branchy)
GCD_RUN(run_gcd_c, gcd_cond)
GCD_RUN(run_gcd_cc, gcd_c)

#define MOVE_BYTES 16384
static uint8_t *mv_src, *mv_dst;
static uint32_t run_mv_b(uint32_t n) { while (n--) move_bytes(mv_dst, mv_src, MOVE_BYTES); return 0; }
static uint32_t run_mv_m(uint32_t n) { while (n--) move_ldmstm(mv_dst, mv_src, MOVE_BYTES); return 0; }
static uint32_t run_mv_c(uint32_t n) { while (n--) memcpy(mv_dst, mv_src, MOVE_BYTES); return 0; }

void bench_arm(struct armbench *b, uint8_t *buf32k)
{
    for (int i = 0; i < N_ELEM; i++) xs[i] = i * 2654435761u;
    /* barrel shifter: results in elements per second x100 -> M/s x100 */
    b->bs_sep = rate_x100(run_bs_sep, N_ELEM, 150000) / 1000000;
    b->bs_bar = rate_x100(run_bs_bar, N_ELEM, 150000) / 1000000;
    b->bs_c   = rate_x100(run_bs_c,   N_ELEM, 150000) / 1000000;
    /* check the three agree */
    bs_separate(xs, ys, N_ELEM); int32_t a = ys[77];
    bs_barrel(xs, ys, N_ELEM);   int32_t c = ys[77];
    b->bs_ok = (a == c) && (a == xs[77] * 5 + (xs[77] >> 3));
    /* conditional execution: sets of 8 GCDs -> GCDs per second */
    b->gcd_b = rate_x100(run_gcd_b, 8, 150000) / 100;
    b->gcd_c = rate_x100(run_gcd_c, 8, 150000) / 100;
    b->gcd_cc = rate_x100(run_gcd_cc, 8, 150000) / 100;
    b->gcd_ok = gcd_cond(1071, 462) == 21 && gcd_branchy(1071, 462) == 21;
    /* block moves: bytes per second -> KB/s */
    mv_src = buf32k; mv_dst = buf32k + MOVE_BYTES;
    b->mv_b = rate_x100(run_mv_b, MOVE_BYTES, 150000) / 102400;
    b->mv_c = rate_x100(run_mv_c, MOVE_BYTES, 150000) / 102400;
    b->mv_m = rate_x100(run_mv_m, MOVE_BYTES, 150000) / 102400;
}
