/* k_misc.c - IOCTL, INT 25h/26h, date/time, country, 2Fh, 33h/34h/37h,
   AX=4B01h run by hand like a debugger, INT 27h, 5Dh, 65h/66h */
#include "t.h"
#include <setjmp.h>

static struct armregs R;
#define AXV (R.r0 & 0xFFFF)
static int d21(unsigned ax, unsigned bx, unsigned cx, uint32_t dx)
{ memset(&R, 0, sizeof R); R.r0 = ax; R.r1 = bx; R.r2 = cx; R.r3 = dx; return _armdos_int21(&R); }

static void test_ioctl(void)
{
    static uint8_t p[64];
    /* 00h on CON and a file, 01h raw bit */
    d21(0x4400, 0, 0, 0);
    unsigned info = R.r3 & 0xFFFF;
    T_EQ(info & 0x80, 0x80);
    T_EQ(info & 0x10, 0x10);                  /* INT 29h fast output */
    T_EQ(d21(0x4401, 0, 0, (info & 0xFF) | 0x20), 0);
    d21(0x4400, 0, 0, 0);
    T_EQ(R.r3 & 0x20, 0x20);
    d21(0x4401, 0, 0, info & 0xDF);
    T_EQ(d21(0x4401, 0, 0, 0x0100), 1);       /* DH must be 0 */
    T_EQ(AXV, 13);
    /* 06h/07h on a file */
    d21(0x3C00, 0, 0, (uint32_t)"IOC.TXT");
    int h = AXV;
    d21(0x4000, h, 3, (uint32_t)"abc");
    d21(0x4200, h, 0, 0);
    d21(0x4406, h, 0, 0);
    T_EQ(R.r0 & 0xFF, 0xFF);
    d21(0x4202, h, 0, 0);
    d21(0x4406, h, 0, 0);
    T_EQ(R.r0 & 0xFF, 0x00);
    d21(0x4407, h, 0, 0);
    T_EQ(R.r0 & 0xFF, 0xFF);
    T_EQ(d21(0x4401, h, 0, 0x20), 1);         /* not for files */
    d21(0x3E00, h, 0, 0);
    d21(0x4100, 0, 0, (uint32_t)"IOC.TXT");
    /* 08h removable, 09h remote */
    d21(0x4408, 1, 0, 0);
    T_EQ(AXV, 0);                             /* A: removable */
    d21(0x4408, 3, 0, 0);
    T_EQ(AXV, 1);                             /* C: fixed */
    T_EQ(d21(0x4408, 20, 0, 0), 1);
    T_EQ(AXV, 15);
    d21(0x4409, 3, 0, 0);
    T_EQ(R.r3 & 0x1000, 0);
    /* 0Dh 60h: device parameters of C: and A: */
    memset(p, 0, sizeof p);
    p[0] = 1;
    T_EQ(d21(0x440D, 3, 0x0860, (uint32_t)p), 0);
    T_EQ(p[1], 5);                            /* fixed disk */
    T_EQ(p[2] & 1, 1);
    T_EQ(p[7] | (p[8] << 8), 512);
    T_EQ(p[9], 4);                            /* 2 KB clusters */
    T_EQ(p[7 + 10], 0xF8);
    memset(p, 0, sizeof p);
    T_EQ(d21(0x440D, 1, 0x0860, (uint32_t)p), 0);
    T_EQ(p[1], 7);                            /* 1.44 MB */
    T_EQ(p[4] | (p[5] << 8), 80);
    T_EQ(p[7 + 10], 0xF0);
    /* 0Dh 66h: media id of C: */
    T_EQ(d21(0x440D, 3, 0x0866, (uint32_t)p), 0);
    T_CHECK(!memcmp(p + 6, "KTEST      ", 11), "volume %.11s", p + 6);
    T_CHECK(!memcmp(p + 17, "FAT16   ", 8), "fs %.8s", p + 17);
    /* 69h (4.0) */
    memset(p, 0, sizeof p);
    T_EQ(d21(0x6900, 3, 0, (uint32_t)p), 0);
    T_CHECK(!memcmp(p + 17, "FAT16   ", 8), "69h fs %.8s", p + 17);
    /* 0Eh: A: and B: share a drive */
    d21(0x440E, 1, 0, 0);
    T_CHECK((R.r0 & 0xFF) == 1 || (R.r0 & 0xFF) == 2, "logical map %u", (unsigned)(R.r0 & 0xFF));
    d21(0x440E, 3, 0, 0);
    T_EQ(R.r0 & 0xFF, 0);
    /* 0Ch on CON: display info (MODE CON) */
    memset(p, 0, sizeof p);
    T_EQ(d21(0x440C, 1, 0x037F, (uint32_t)p), 0);
    T_EQ(p[14] | (p[15] << 8), 80);
    T_EQ(p[16] | (p[17] << 8), 25);
}

static void test_absdisk(void)
{
    static uint8_t sec[1024];
    static struct { uint32_t start; uint16_t n; uint32_t buf; } __attribute__((packed)) pk;
    pk.start = 0; pk.n = 2; pk.buf = (uint32_t)sec;
    struct armregs r = { 0 };
    r.r0 = 2; r.r2 = 0xFFFF; r.r1 = (uint32_t)&pk;
    T_EQ(_armdos_intr(0x25, &r), 0);
    T_EQ(sec[510], 0x55);
    T_EQ(sec[0x15], 0xF8);
    T_EQ(sec[512], 0xF8);                     /* FAT starts at sector 1 */
    /* the old form is refused on a big partition */
    memset(&r, 0, sizeof r);
    r.r0 = 2; r.r2 = 1; r.r3 = 0; r.r1 = (uint32_t)sec;
    T_EQ(_armdos_intr(0x25, &r), 1);
    T_EQ(r.r0 & 0xFFFF, 0x0207);
    /* write a sector of the last cluster and read it back */
    d21(0x3200, 0, 0, 3);
    const uint8_t *dpb = (const uint8_t *)R.r1;
    uint32_t last = (dpb[0x0B] | (dpb[0x0C] << 8)) + ((uint32_t)((dpb[0x0D] | (dpb[0x0E] << 8)) - 2) << dpb[5]) + 3;
    memset(sec, 0xA5, 512);
    pk.start = last; pk.n = 1; pk.buf = (uint32_t)sec;
    memset(&r, 0, sizeof r);
    r.r0 = 2; r.r2 = 0xFFFF; r.r1 = (uint32_t)&pk;
    T_EQ(_armdos_intr(0x26, &r), 0);
    memset(sec, 0, 512);
    memset(&r, 0, sizeof r);
    r.r0 = 2; r.r2 = 0xFFFF; r.r1 = (uint32_t)&pk;
    _armdos_intr(0x25, &r);
    T_EQ(sec[100], 0xA5);
}

static void test_datetime(void)
{
    d21(0x2A00, 0, 0, 0);
    unsigned y = R.r2 & 0xFFFF, m = (R.r3 >> 8) & 0xFF, dd = R.r3 & 0xFF, wd = R.r0 & 0xFF;
    T_EQ(y, 2026);                            /* the harness RTC: 2026-01-01 12:00 */
    T_EQ(m, 1);
    T_EQ(dd, 1);
    T_EQ(wd, 4);                              /* a Thursday */
    d21(0x2C00, 0, 0, 0);
    T_EQ((R.r2 >> 8) & 0xFF, 12);
    /* set a date and a time, read them back, stamp a file */
    T_EQ(d21(0x2B00, 0, 1989, (6 << 8) | 17), 0);
    T_EQ(R.r0 & 0xFF, 0);
    T_EQ(d21(0x2B00, 0, 1989, (2 << 8) | 30), 0);
    T_EQ(R.r0 & 0xFF, 0xFF);
    d21(0x2D00, 0, (13 << 8) | 45, (30 << 8) | 50);
    T_EQ(R.r0 & 0xFF, 0);
    d21(0x2D00, 0, (25 << 8) | 0, 0);
    T_EQ(R.r0 & 0xFF, 0xFF);
    d21(0x2A00, 0, 0, 0);
    T_EQ(R.r2 & 0xFFFF, 1989);
    T_EQ(R.r3 & 0xFFFF, (6 << 8) | 17);
    T_EQ(R.r0 & 0xFF, 6);                     /* 17 June 1989: a Saturday */
    d21(0x2C00, 0, 0, 0);
    T_EQ(R.r2 & 0xFFFF, (13 << 8) | 45);
    d21(0x3C00, 0, 0, (uint32_t)"STAMP.TXT");
    int h = AXV;
    d21(0x4000, h, 1, (uint32_t)"x");
    d21(0x3E00, h, 0, 0);
    uint8_t dta[43];
    d21(0x1A00, 0, 0, (uint32_t)dta);
    d21(0x4E00, 0, 0, (uint32_t)"STAMP.TXT");
    unsigned fd = dta[0x18] | (dta[0x19] << 8), ft = dta[0x16] | (dta[0x17] << 8);
    T_EQ(fd, ((1989 - 1980) << 9) | (6 << 5) | 17);
    T_EQ(ft >> 11, 13);
    T_EQ((ft >> 5) & 63, 45);
    d21(0x4100, 0, 0, (uint32_t)"STAMP.TXT");
    /* the CMOS clock followed */
    struct armregs r = { 0 };
    r.r0 = 0x0400;
    _armdos_intr(0x1A, &r);
    T_EQ(r.r2 & 0xFFFF, 0x1989);
    /* put it back */
    d21(0x2B00, 0, 2026, (1 << 8) | 1);
}

static void test_info(void)
{
    uint8_t c[64];
    T_EQ(d21(0x3800, 0, 0, (uint32_t)c), 0);
    T_EQ(R.r1 & 0xFFFF, 1);
    T_EQ(c[0], 0);                            /* m-d-y */
    T_EQ(c[2], '$');
    T_EQ(c[7], ',');
    T_EQ(c[0x0B], '-');
    T_EQ(c[0x0D], ':');
    uint32_t (*mapcase)(uint32_t) = (void *)(c[0x12] | (c[0x13] << 8) | (c[0x14] << 16) | ((uint32_t)c[0x15] << 24));
    T_EQ(mapcase(0x81), 0x9A);                /* u umlaut -> U umlaut */
    memset(&R, 0, sizeof R); R.r0 = 0x6501; R.r1 = 0xFFFF; R.r3 = 0xFFFF; R.r2 = 41; R.r5 = (uint32_t)c;
    _armdos_int21(&R);
    T_EQ(R.r2 & 0xFFFF, 41);
    T_EQ(c[3] | (c[4] << 8), 1);
    T_EQ(c[5] | (c[6] << 8), 437);
    memset(&R, 0, sizeof R); R.r0 = 0x6520; R.r3 = 'q';
    _armdos_int21(&R);
    T_EQ(R.r3 & 0xFF, 'Q');
    memset(&R, 0, sizeof R); R.r0 = 0x6523; R.r3 = 'y';
    _armdos_int21(&R);
    T_EQ(AXV, 1);
    d21(0x6601, 0, 0, 0);
    T_EQ(R.r1 & 0xFFFF, 437);
    d21(0x3305, 0, 0, 0);
    T_EQ(R.r3 & 0xFF, 3);                     /* booted from C: */
    d21(0x3700, 0, 0, 0);
    T_EQ(R.r3 & 0xFF, '/');
    d21(0x3400, 0, 0, 0);
    const uint8_t *indos = (const uint8_t *)R.r1;
    T_EQ(*indos, 0);                          /* not in DOS now */
    T_EQ(indos[-1], 0);                       /* nor in an INT 24h */
    /* 2Fh: DOS internal install check, message hook, nobody at AH=01 */
    struct armregs r = { 0 };
    r.r0 = 0x1200;
    _armdos_int2f(&r);
    T_EQ(r.r0 & 0xFF, 0xFF);
    memset(&r, 0, sizeof r);
    r.r0 = 0x0100;
    _armdos_int2f(&r);
    T_EQ(r.r0 & 0xFF, 0);
    memset(&r, 0, sizeof r);
    r.r0 = 0x122E; r.r3 = 1; r.r5 = 0x12345678;
    _armdos_int2f(&r);
    memset(&r, 0, sizeof r);
    r.r0 = 0x122E; r.r3 = 0;
    _armdos_int2f(&r);
    T_EQ(r.r5, 0x12345678);
    /* 5Dh 0Ah sets the extended error, 59h reads it */
    static uint16_t ee[11] = { 0x1F, 0x0D04, 0x0100 };
    ee[2] = 1 << 8;
    d21(0x5D0A, 0, 0, (uint32_t)ee);
    d21(0x5900, 0, 0, 0);
    T_EQ(AXV, 0x1F);
    /* version, verify flag */
    d21(0x2E01, 0, 0, 0);
    d21(0x5400, 0, 0, 0);
    T_EQ(R.r0 & 0xFF, 1);
    d21(0x2E00, 0, 0, 0);
    /* an undefined function returns AL=0 */
    d21(0x7700, 0, 0, 0);
    T_EQ(R.r0 & 0xFF, 0);
}

/* AX=4B01h: load, then run the child by hand like a debugger does */
static jmp_buf back;
static void child_done(void) { longjmp(back, 1); }

extern void enter_child(uint32_t pc, uint32_t sp, uint32_t psp, uint32_t base);
__asm__(
    "   .arm\n"
    "   .global enter_child\n"
    "enter_child:\n"
    "   mov     sp, r1\n"
    "   mov     r12, r0\n"
    "   mov     r0, r2\n"
    "   mov     r1, r3\n"
    "   orr     lr, r2, #1\n"
    "   bx      r12\n");

static void test_debugger(void)
{
    static struct { uint16_t env; uint32_t tail, fcb1, fcb2, sp, pc; } __attribute__((packed)) pb;
    static uint8_t tail[] = { 8, ' ', 'G', 'R', 'A', 'N', 'D', ' ', '9', '\r' };
    pb.env = 0; pb.tail = (uint32_t)tail; pb.fcb1 = pb.fcb2 = 0;
    d21(0x6200, 0, 0, 0);
    unsigned me = R.r1 & 0xFFFF;
    memset(&R, 0, sizeof R);
    R.r0 = 0x4B01; R.r1 = (uint32_t)&pb; R.r3 = (uint32_t)"\\T\\K_EXEC.EXE";
    T_EQ(_armdos_int21(&R), 0);
    d21(0x6200, 0, 0, 0);
    unsigned child = R.r1 & 0xFFFF;
    struct psp *cp = (struct psp *)(child << 4);
    cp->int22 = (uint32_t)child_done;          /* where the child "returns" */
    if (!setjmp(back)) {
        enter_child(pb.pc, pb.sp, child << 4, (child << 4) + 0x100);
        T_CHECK(0, "not reached");
    }
    d21(0x6200, 0, 0, 0);
    T_EQ(R.r1 & 0xFFFF, me);                  /* the child's parent is current again */
    d21(0x4D00, 0, 0, 0);
    T_EQ(AXV, 9);
}

static void test_psp_calls(void)
{
    /* 26h: a copy of our PSP at a new segment; 55h: a child PSP (becomes current) */
    d21(0x4800, 16, 0, 0);
    unsigned seg = AXV;
    d21(0x6200, 0, 0, 0);
    unsigned me = R.r1 & 0xFFFF;
    d21(0x2600, 0, 0, seg);
    struct psp *p = (struct psp *)(seg << 4), *mine = (struct psp *)(me << 4);
    T_EQ(p->int20, 0xDF20);
    T_EQ(p->parent, mine->parent);
    T_EQ(p->envseg, mine->envseg);
    memset(&R, 0, sizeof R); R.r0 = 0x5500; R.r3 = seg; R.r4 = seg + 16;
    _armdos_int21(&R);
    d21(0x6200, 0, 0, 0);
    T_EQ(R.r1 & 0xFFFF, seg);
    T_EQ(p->parent, me);
    T_EQ(p->memtop, seg + 16);
    T_EQ(p->jft[1], mine->jft[1]);             /* handles inherited */
    d21(0x5000, me, 0, 0);
    /* the child PSP took references on our handles: close them through it */
    d21(0x5000, seg, 0, 0);
    for (int h = 0; h < 20; h++) if (p->jft[h] != 0xFF) d21(0x3E00, h, 0, 0);
    d21(0x5000, me, 0, 0);
    memset(&R, 0, sizeof R); R.r0 = 0x4900; R.r8 = seg;
    _armdos_int21(&R);
}

static void test_tracks(void)
{
    /* generic IOCTL track I/O on A: (a diskette is in the drive in this scenario) */
    static uint8_t trk[18 * 512], fmt[16];
    static struct { uint8_t special; uint16_t head, cyl, first, count; uint32_t buf; } __attribute__((packed)) rt;
    rt.special = 0; rt.head = 0; rt.cyl = 0; rt.first = 0; rt.count = 18; rt.buf = (uint32_t)trk;
    T_EQ(d21(0x440D, 1, 0x0861, (uint32_t)&rt), 0);
    T_EQ(trk[510], 0x55);
    T_EQ(trk[512], 0xF0);                      /* the FAT id on sector 2 */
    /* write track 79 head 1 (the last track: data area, empty) and read it back */
    for (int i = 0; i < 18 * 512; i++) trk[i] = i * 3;
    rt.head = 1; rt.cyl = 79;
    T_EQ(d21(0x440D, 1, 0x0841, (uint32_t)&rt), 0);
    memset(trk, 0, sizeof trk);
    T_EQ(d21(0x440D, 1, 0x0861, (uint32_t)&rt), 0);
    T_EQ(trk[1000], (uint8_t)(1000 * 3));
    /* verify and format track 79 head 1 */
    memset(fmt, 0, sizeof fmt);
    fmt[1] = 1; fmt[3] = 79;
    T_EQ(d21(0x440D, 1, 0x0862, (uint32_t)fmt), 0);
    T_EQ(d21(0x440D, 1, 0x0842, (uint32_t)fmt), 0);
    T_EQ(d21(0x440D, 1, 0x0861, (uint32_t)&rt), 0);
    T_EQ(trk[0], 0xF6);
}

int main(int argc, char **argv)
{
    t_begin("misc");
    test_psp_calls();
    if (argc > 1 && !strcmp(argv[1], "TRACKS")) { test_tracks(); return t_end(); }
    test_ioctl();
    test_absdisk();
    test_datetime();
    test_info();
    test_debugger();
    return t_end();
}
