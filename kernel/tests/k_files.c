/* k_files.c - handle file functions, directories, find first/next, errors */
#include "t.h"

static struct armregs R;
#define AXV (R.r0 & 0xFFFF)

static int d21(unsigned ax, unsigned bx, unsigned cx, uint32_t dx)
{
    memset(&R, 0, sizeof R);
    R.r0 = ax; R.r1 = bx; R.r2 = cx; R.r3 = dx;
    return _armdos_int21(&R);
}

static int create(const char *n, int attr) { return d21(0x3C00, 0, attr, (uint32_t)n) ? -(int)AXV : (int)AXV; }
static int open_(const char *n, int mode) { return d21(0x3D00 | mode, 0, 0, (uint32_t)n) ? -(int)AXV : (int)AXV; }
static int close_(int h) { return d21(0x3E00, h, 0, 0) ? -(int)AXV : 0; }
static int write_(int h, const void *b, unsigned n) { return d21(0x4000, h, n, (uint32_t)b) ? -(int)AXV : (int)AXV; }
static int read_(int h, void *b, unsigned n) { return d21(0x3F00, h, n, (uint32_t)b) ? -(int)AXV : (int)AXV; }
static long seek_(int h, int how, long off)
{
    if (d21(0x4200 | how, h, ((uint32_t)off >> 16) & 0xFFFF, (uint32_t)off & 0xFFFF)) return -(long)AXV;
    return (long)(((R.r3 & 0xFFFF) << 16) | (R.r0 & 0xFFFF));
}
static int unlink_(const char *n) { return d21(0x4100, 0, 0, (uint32_t)n) ? -(int)AXV : 0; }

static uint8_t big[70016];
static uint8_t back[70016];

static void pattern(uint8_t *p, unsigned n, unsigned seed)
{
    for (unsigned i = 0; i < n; i++) p[i] = (uint8_t)((i * 7 + seed * 13 + (i >> 8)) ^ (i >> 3));
}

static void test_rw(void)
{
    static const unsigned sizes[] = { 0, 1, 511, 512, 513, 2047, 2048, 2049, 4096, 10000, 70000 };
    for (unsigned k = 0; k < sizeof sizes / sizeof sizes[0]; k++) {
        unsigned n = sizes[k];
        char name[16];
        sprintf(name, "RW%u.DAT", n);
        pattern(big, n, n);
        int h = create(name, 0);
        T_CHECK(h >= 5, "create %s -> %d", name, h);
        if (h < 0) continue;
        unsigned done = 0;
        while (done < n) {
            unsigned c = n - done > 30000 ? 30000 : n - done;
            int w = write_(h, big + done, c);
            T_EQ(w, (int)c);
            if (w <= 0) break;
            done += w;
        }
        T_EQ(seek_(h, 2, 0), n);
        T_EQ(seek_(h, 0, 0), 0);
        memset(back, 0xEE, n + 1);
        done = 0;
        while (done < n) {
            unsigned c = n - done > 30000 ? 30000 : n - done;
            int r = read_(h, back + done, c);
            T_EQ(r, (int)c);
            if (r <= 0) break;
            done += r;
        }
        T_EQ(read_(h, back + n, 10), 0);            /* at EOF */
        T_CHECK(!memcmp(big, back, n), "data %s", name);
        T_EQ(close_(h), 0);
        /* again from a fresh open, in odd-sized pieces */
        h = open_(name, 0);
        T_CHECK(h >= 0, "reopen %s", name);
        memset(back, 0, n);
        done = 0;
        unsigned piece = 37;
        for (;;) {
            int r = read_(h, back + done, piece);
            if (r <= 0) break;
            done += r;
            piece = piece * 3 % 4099 + 1;
        }
        T_EQ(done, n);
        T_CHECK(!memcmp(big, back, n), "data2 %s", name);
        close_(h);
    }
}

static void test_seek_trunc(void)
{
    int h = create("SEEK.DAT", 0);
    pattern(big, 5000, 1);
    write_(h, big, 5000);
    T_EQ(seek_(h, 0, 100), 100);
    T_EQ(seek_(h, 1, 50), 150);
    T_EQ(seek_(h, 1, -20), 130);
    T_EQ(seek_(h, 2, -1000), 4000);
    uint8_t b[10];
    read_(h, b, 10);
    T_CHECK(!memcmp(b, big + 4000, 10), "seek read");
    /* truncate with a 0-byte write */
    seek_(h, 0, 3000);
    T_EQ(write_(h, b, 0), 0);
    T_EQ(seek_(h, 2, 0), 3000);
    /* extend past the end with a write */
    seek_(h, 0, 9000);
    T_EQ(write_(h, "XYZ", 3), 3);
    T_EQ(seek_(h, 2, 0), 9003);
    seek_(h, 0, 8998);
    read_(h, b, 5);
    T_CHECK(b[2] == 'X' && b[4] == 'Z', "extended write");
    close_(h);
    /* truncate to 0 frees everything */
    h = open_("SEEK.DAT", 2);
    write_(h, b, 0);
    T_EQ(seek_(h, 2, 0), 0);
    close_(h);
    unlink_("SEEK.DAT");
}

static void test_errors(void)
{
    T_EQ(open_("NOSUCH.FIL", 0), -2);
    T_EQ(open_("\\NODIR\\X.TXT", 0), -3);
    T_EQ(open_("X.TXT", 7), -12);                   /* invalid access code */
    T_EQ(close_(99), -6);
    T_EQ(read_(99, big, 1), -6);
    T_EQ(unlink_("NOSUCH.FIL"), -2);
    /* read-only file */
    int h = create("RO.TXT", 1);
    write_(h, "ro", 2);
    close_(h);
    T_EQ(open_("RO.TXT", 2), -5);
    T_EQ(unlink_("RO.TXT"), -5);
    T_EQ(create("RO.TXT", 0), -5);
    d21(0x4300, 0, 0, (uint32_t)"RO.TXT");
    T_EQ(R.r2 & 0xFF, 0x21);                        /* read-only + archive */
    T_EQ(d21(0x4301, 0, 0x20, (uint32_t)"RO.TXT"), 0);
    T_EQ(unlink_("RO.TXT"), 0);
    /* 5Bh create new */
    h = d21(0x5B00, 0, 0, (uint32_t)"NEW.TXT") ? -1 : (int)AXV;
    T_CHECK(h > 0, "create new");
    close_(h);
    T_EQ(d21(0x5B00, 0, 0, (uint32_t)"NEW.TXT"), 1);
    T_EQ(AXV, 80);
    /* extended error after a failure */
    open_("NOSUCH.FIL", 0);
    d21(0x5900, 0, 0, 0);
    T_EQ(AXV, 2);
    T_EQ((R.r1 >> 8) & 0xFF, 8);                    /* class: not found */
    unlink_("NEW.TXT");
    /* wildcards are not file names */
    T_EQ(open_("*.TXT", 0), -2);
}

static void test_handles(void)
{
    int h = create("DUP.TXT", 0);
    write_(h, "abcdef", 6);
    d21(0x4500, h, 0, 0);
    int h2 = AXV;
    T_CHECK(h2 != h, "dup gives a new handle");
    T_EQ(seek_(h2, 1, 0), 6);                       /* shares the position */
    seek_(h, 0, 2);
    T_EQ(seek_(h2, 1, 0), 2);
    close_(h);
    T_EQ(seek_(h2, 1, 0), 2);                       /* still open through the dup */
    /* force dup onto 19 */
    T_EQ(d21(0x4600, h2, 19, 0), 0);
    T_EQ(seek_(19, 1, 0), 2);
    close_(19);
    close_(h2);
    /* run out of handles: 20 per process, 5 preopened */
    int hs[20], n = 0;
    for (int i = 0; i < 20; i++) {
        int x = open_("DUP.TXT", 0);
        if (x < 0) { T_EQ(x, -4); break; }
        hs[n++] = x;
    }
    {
        /* 20 JFT slots: 5 standard ones, whatever the shell passed down, ours */
        int used = 0;
        for (int i = 5; i < 20; i++) if (_armdos_psp->jft[i] != 0xFF) used++;
        T_EQ(used, 15);
        T_CHECK(n >= 13, "opened %d", n);
    }
    for (int i = 0; i < n; i++) close_(hs[i]);
    /* set handle count (67h) then open more than 20 */
    T_EQ(d21(0x6700, 30, 0, 0), 0);
    n = 0;
    for (int i = 0; i < 20; i++) {
        int x = open_("DUP.TXT", 0);
        if (x < 0) break;
        hs[n++] = x;
    }
    T_CHECK(n > 15, "more handles after 67h: %d", n);
    for (int i = 0; i < n; i++) close_(hs[i]);
    unlink_("DUP.TXT");
    /* date/time */
    h = create("TIME.TXT", 0);
    T_EQ(d21(0x5701, h, 0x1234, 0x2345), 0);
    close_(h);
    h = open_("TIME.TXT", 0);
    d21(0x5700, h, 0, 0);
    T_EQ(R.r2 & 0xFFFF, 0x1234);
    T_EQ(R.r3 & 0xFFFF, 0x2345);
    close_(h);
    unlink_("TIME.TXT");
    /* IOCTL get info on a file and on CON */
    h = create("IOC.TXT", 0);
    d21(0x4400, h, 0, 0);
    T_EQ(R.r3 & 0x80, 0);
    T_EQ(R.r3 & 0x3F, 2);                           /* drive C: */
    close_(h);
    unlink_("IOC.TXT");
    d21(0x4400, 1, 0, 0);
    T_EQ(R.r3 & 0x83, 0x83);                        /* device, stdin/stdout = CON */
}

static void test_dirs(void)
{
    char cwd[80];
    T_EQ(d21(0x3900, 0, 0, (uint32_t)"SUB1"), 0);
    T_EQ(d21(0x3900, 0, 0, (uint32_t)"SUB1"), 1);
    T_EQ(AXV, 5);
    T_EQ(d21(0x3900, 0, 0, (uint32_t)"SUB1\\SUB2"), 0);
    T_EQ(d21(0x3900, 0, 0, (uint32_t)"NOPE\\SUB2"), 1);
    T_EQ(AXV, 3);
    T_EQ(d21(0x3B00, 0, 0, (uint32_t)"SUB1/SUB2"), 0);     /* forward slash too */
    memset(&R, 0, sizeof R); R.r0 = 0x4700; R.r3 = 0; R.r4 = (uint32_t)cwd;
    _armdos_int21(&R);
    T_CHECK(!strcmp(cwd, "SUB1\\SUB2"), "cwd = %s", cwd);
    int h = create("INNER.TXT", 0);
    write_(h, "inner", 5);
    close_(h);
    T_EQ(d21(0x3B00, 0, 0, (uint32_t)".."), 0);
    T_EQ(open_("SUB2\\INNER.TXT", 0) >= 0, 1);
    close_(5);
    T_EQ(open_("..\\SUB1\\.\\SUB2\\INNER.TXT", 0) >= 0, 1);
    close_(5);
    T_EQ(d21(0x3B00, 0, 0, (uint32_t)"\\"), 0);
    T_EQ(d21(0x3A00, 0, 0, (uint32_t)"SUB1"), 1);          /* not empty */
    T_EQ(AXV, 5);
    d21(0x3B00, 0, 0, (uint32_t)"SUB1\\SUB2");
    T_EQ(d21(0x3A00, 0, 0, (uint32_t)"\\SUB1\\SUB2"), 1);  /* current directory */
    T_EQ(AXV, 16);
    d21(0x3B00, 0, 0, (uint32_t)"\\");
    T_EQ(unlink_("SUB1\\SUB2\\INNER.TXT"), 0);
    T_EQ(d21(0x3A00, 0, 0, (uint32_t)"SUB1\\SUB2"), 0);
    T_EQ(d21(0x3A00, 0, 0, (uint32_t)"SUB1"), 0);
    T_EQ(d21(0x3B00, 0, 0, (uint32_t)"SUB1"), 1);
    T_EQ(AXV, 3);

    /* a subdirectory that grows beyond one cluster (64 entries per 2 KB) */
    d21(0x3900, 0, 0, (uint32_t)"MANY");
    int made = 0;
    for (int i = 0; i < 150; i++) {
        char n[24];
        sprintf(n, "MANY\\F%03d.TXT", i);
        int x = create(n, 0);
        if (x < 0) break;
        write_(x, n, strlen(n));
        close_(x);
        made++;
    }
    T_EQ(made, 150);
    int found = 0;
    uint8_t dta[43];
    d21(0x1A00, 0, 0, (uint32_t)dta);
    int cf = d21(0x4E00, 0, 0x10, (uint32_t)"MANY\\*.*");
    while (!cf) { found++; cf = d21(0x4F00, 0, 0, 0); }
    T_EQ(AXV, 18);
    T_EQ(found, 152);                                     /* . and .. too */
    for (int i = 0; i < 150; i += 2) {
        char n[24];
        sprintf(n, "MANY\\F%03d.TXT", i);
        unlink_(n);
    }
    found = 0;
    cf = d21(0x4E00, 0, 0, (uint32_t)"MANY\\F*.TXT");
    while (!cf) { found++; cf = d21(0x4F00, 0, 0, 0); }
    T_EQ(found, 75);
    /* rename (56h), within and across directories */
    memset(&R, 0, sizeof R); R.r0 = 0x5600; R.r3 = (uint32_t)"MANY\\F001.TXT"; R.r5 = (uint32_t)"MANY\\G001.TXT";
    T_EQ(_armdos_int21(&R), 0);
    memset(&R, 0, sizeof R); R.r0 = 0x5600; R.r3 = (uint32_t)"MANY\\F003.TXT"; R.r5 = (uint32_t)"MOVED.TXT";
    T_EQ(_armdos_int21(&R), 0);
    h = open_("MOVED.TXT", 0);
    char b[20] = { 0 };
    read_(h, b, 13);
    T_CHECK(!strcmp(b, "MANY\\F003.TXT"), "moved content %s", b);
    close_(h);
    memset(&R, 0, sizeof R); R.r0 = 0x5600; R.r3 = (uint32_t)"MOVED.TXT"; R.r5 = (uint32_t)"MANY\\G001.TXT";
    T_EQ(_armdos_int21(&R), 1);
    T_EQ(AXV, 5);
    memset(&R, 0, sizeof R); R.r0 = 0x5600; R.r3 = (uint32_t)"MOVED.TXT"; R.r5 = (uint32_t)"A:\\MOVED.TXT";
    T_EQ(_armdos_int21(&R), 1);
    T_EQ(AXV, 17);
    unlink_("MOVED.TXT");
}

static void test_find(void)
{
    uint8_t dta[43], dta2[43];
    int h = create("FIND1.TXT", 0); close_(h);
    h = create("FIND2.TXT", 2); close_(h);          /* hidden */
    h = create("FIND3.DOC", 0); close_(h);
    d21(0x3900, 0, 0, (uint32_t)"FINDDIR");
    d21(0x1A00, 0, 0, (uint32_t)dta);
    int cf = d21(0x4E00, 0, 0, (uint32_t)"FIND?.TXT");
    T_EQ(cf, 0);
    T_CHECK(!strcmp((char *)dta + 0x1E, "FIND1.TXT"), "found %s", dta + 0x1E);
    T_EQ(d21(0x4F00, 0, 0, 0), 1);                  /* the hidden one is not seen */
    T_EQ(AXV, 18);
    cf = d21(0x4E00, 0, 2, (uint32_t)"FIND*.*");
    int n = 0, sawhidden = 0;
    while (!cf) { n++; if (dta[0x15] & 2) sawhidden = 1; cf = d21(0x4F00, 0, 0, 0); }
    T_EQ(n, 3);
    T_EQ(sawhidden, 1);
    cf = d21(0x4E00, 0, 0x10, (uint32_t)"FIND*");
    T_EQ(cf, 0);
    T_CHECK(!strcmp((char *)dta + 0x1E, "FINDDIR") && (dta[0x15] & 0x10), "dir %s", dta + 0x1E);
    /* the volume label */
    cf = d21(0x4E00, 0, 8, (uint32_t)"\\*.*");
    T_EQ(cf, 0);
    T_CHECK(!strcmp((char *)dta + 0x1E, "KTEST") && dta[0x15] == 8, "label %s", dta + 0x1E);
    /* two searches interleaved (state is in the DTA only) */
    cf = d21(0x4E00, 0, 0, (uint32_t)"FIND*.*");
    d21(0x1A00, 0, 0, (uint32_t)dta2);
    cf |= d21(0x4E00, 0, 0x16, (uint32_t)"*.*");
    d21(0x1A00, 0, 0, (uint32_t)dta);
    T_EQ(d21(0x4F00, 0, 0, 0), 0);
    T_CHECK(!strcmp((char *)dta + 0x1E, "FIND3.DOC"), "interleaved %s", dta + 0x1E);
    /* no match */
    T_EQ(d21(0x4E00, 0, 0, (uint32_t)"NOTHING.*"), 1);
    T_EQ(AXV, 18);
    T_EQ(d21(0x4E00, 0, 0, (uint32_t)"\\NODIR\\*.*"), 1);
    T_EQ(AXV, 3);
    unlink_("FIND1.TXT"); d21(0x4301, 0, 0, (uint32_t)"FIND2.TXT"); unlink_("FIND2.TXT"); unlink_("FIND3.DOC");
    d21(0x3A00, 0, 0, (uint32_t)"FINDDIR");
}

static void test_misc(void)
{
    char out[128];
    memset(&R, 0, sizeof R); R.r0 = 0x6000; R.r4 = (uint32_t)"sub\\..\\x.txt"; R.r5 = (uint32_t)out;
    T_EQ(_armdos_int21(&R), 0);
    T_CHECK(!strcmp(out, "C:\\X.TXT"), "truename %s", out);
    memset(&R, 0, sizeof R); R.r0 = 0x6000; R.r4 = (uint32_t)"con"; R.r5 = (uint32_t)out;
    _armdos_int21(&R);
    T_CHECK(!strcmp(out, "C:/CON"), "truename con %s", out);
    /* free space goes down by what a file takes and comes back */
    d21(0x3600, 0, 0, 3);
    unsigned long free1 = (R.r1 & 0xFFFF);
    T_EQ(R.r0 & 0xFFFF, 4);                          /* 2 KB clusters */
    int h = create("SPACE.DAT", 0);
    write_(h, big, 10000);
    close_(h);
    d21(0x3600, 0, 0, 0);
    T_EQ(free1 - (R.r1 & 0xFFFF), 5);
    unlink_("SPACE.DAT");
    d21(0x3600, 0, 0, 0);
    T_EQ(R.r1 & 0xFFFF, free1);
    /* temp files */
    char tmp[80] = "C:\\";
    T_EQ(d21(0x5A00, 0, 0, (uint32_t)tmp), 0);
    h = AXV;
    T_CHECK(strlen(tmp) == 11, "temp name %s", tmp);
    close_(h);
    T_EQ(unlink_(tmp), 0);
    /* 6Ch extended open: create, open, replace, fail */
    memset(&R, 0, sizeof R); R.r0 = 0x6C00; R.r1 = 2; R.r3 = 0x10; R.r4 = (uint32_t)"EXT.TXT";
    T_EQ(_armdos_int21(&R), 0);
    T_EQ(R.r2 & 0xFFFF, 2);
    close_(R.r0 & 0xFFFF);
    memset(&R, 0, sizeof R); R.r0 = 0x6C00; R.r1 = 2; R.r3 = 0x01; R.r4 = (uint32_t)"EXT.TXT";
    T_EQ(_armdos_int21(&R), 0);
    T_EQ(R.r2 & 0xFFFF, 1);
    close_(R.r0 & 0xFFFF);
    memset(&R, 0, sizeof R); R.r0 = 0x6C00; R.r1 = 2; R.r3 = 0x12; R.r4 = (uint32_t)"EXT.TXT";
    T_EQ(_armdos_int21(&R), 0);
    T_EQ(R.r2 & 0xFFFF, 3);
    close_(R.r0 & 0xFFFF);
    memset(&R, 0, sizeof R); R.r0 = 0x6C00; R.r1 = 2; R.r3 = 0x10; R.r4 = (uint32_t)"EXT.TXT";
    T_EQ(_armdos_int21(&R), 1);
    T_EQ(AXV, 80);
    unlink_("EXT.TXT");
    /* current drive, drive letters */
    d21(0x1900, 0, 0, 0);
    T_EQ(R.r0 & 0xFF, 2);
    d21(0x0E00, 0, 0, 2);
    T_EQ(R.r0 & 0xFF, 5);                            /* LASTDRIVE=E */
    T_EQ(open_("Q:\\X", 0), -3);                     /* invalid drive -> path not found */
}

static void test_stdio(void)
{
    FILE *f = fopen("STDIO.TXT", "w");
    T_CHECK(f != 0, "fopen w");
    if (!f) return;
    for (int i = 0; i < 100; i++) fprintf(f, "line %d\n", i);
    fclose(f);
    f = fopen("STDIO.TXT", "r");
    T_CHECK(f != 0, "fopen r");
    if (!f) return;
    char line[40];
    int n = 0, ok = 1;
    while (fgets(line, sizeof line, f)) {
        char want[20];
        sprintf(want, "line %d\n", n);
        if (strcmp(line, want)) ok = 0;
        n++;
    }
    T_EQ(n, 100);
    T_EQ(ok, 1);
    fseek(f, 0, SEEK_END);
    T_EQ(ftell(f), 100 * 7 + 90 * 1 + 100 * 1);      /* "line N\r\n": 8 or 9 bytes */
    fclose(f);
    remove("STDIO.TXT");
}

int main(void)
{
    t_begin("files");
    /* stdio first: fopen/fprintf need heap (the FILE glue, the buffers),
     * and test_handles' AH=67h puts the new handle table in a DOS block
     * right after ours, after which our heap cannot grow (no XMS here).
     * Run afterwards it only worked while the heap's last 4 KB step
     * happened to leave room - a matter of the program's size (with Arm's
     * newlib it left ~300 bytes, and fopen returned NULL). */
    test_stdio();
    test_rw();
    test_seek_trunc();
    test_errors();
    test_handles();
    test_dirs();
    test_find();
    test_misc();
    return t_end();
}
