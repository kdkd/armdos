/* k_fcb.c - FCB functions 0Fh-17h, 21h-24h, 27h, 28h and 29h (parse) */
#include "t.h"

static struct armregs R;
static int d21(unsigned ax, unsigned bx, unsigned cx, uint32_t dx)
{ memset(&R, 0, sizeof R); R.r0 = ax; R.r1 = bx; R.r2 = cx; R.r3 = dx; return _armdos_int21(&R); }
#define ALV (R.r0 & 0xFF)

struct fcb {
    uint8_t drive; char name[11]; uint16_t cur_block, rec_size; uint32_t size; uint16_t date, time;
    uint8_t res[8]; uint8_t cur_rec; uint32_t rand_rec;
} __attribute__((packed));

static int parse(const char *s, struct fcb *f, int flags, const char **end)
{
    memset(&R, 0, sizeof R);
    R.r0 = 0x2900 | flags; R.r4 = (uint32_t)s; R.r5 = (uint32_t)f;
    _armdos_int21(&R);
    if (end) *end = (const char *)R.r4;
    return R.r0 & 0xFF;
}

static uint8_t dta[1024];

int main(void)
{
    t_begin("fcb");
    struct fcb f;
    const char *end;
    /* 29h parse */
    memset(&f, 0, sizeof f);
    T_EQ(parse("  hello.txt rest", &f, 1, &end), 0);
    T_CHECK(!memcmp(f.name, "HELLO   TXT", 11), "parsed %.11s", f.name);
    T_CHECK(*end == ' ', "end at %s", end);
    T_EQ(parse("c:*.d?c", &f, 0, &end), 1);
    T_EQ(f.drive, 3);
    T_CHECK(!memcmp(f.name, "????????D?C", 11), "wild %.11s", f.name);
    T_EQ(parse("q:x", &f, 0, &end), 0xFF);
    memcpy(f.name, "KEEPNAMEEXT", 11);
    f.drive = 7;
    T_EQ(parse(".new", &f, 2 | 4, &end), 0);            /* keep drive and name */
    T_EQ(f.drive, 7);
    T_CHECK(!memcmp(f.name, "KEEPNAMENEW", 11), "keep %.11s", f.name);
    T_EQ(parse("verylongname.extension", &f, 0, &end), 0);
    T_CHECK(!memcmp(f.name, "VERYLONGEXT", 11), "cut %.11s", f.name);
    parse(" ; second", &f, 1, &end);
    T_CHECK(!memcmp(f.name, "SECOND     ", 11), "separators %.11s", f.name);

    d21(0x1A00, 0, 0, (uint32_t)dta);
    /* create, sequential write, close */
    memset(&f, 0, sizeof f);
    parse("FCBTEST.DAT", &f, 0, 0);
    T_EQ(d21(0x1600, 0, 0, (uint32_t)&f) || ALV, 0);
    T_EQ(f.rec_size, 128);
    for (int i = 0; i < 10; i++) {
        memset(dta, 'A' + i, 128);
        d21(0x1500, 0, 0, (uint32_t)&f);
        T_EQ(ALV, 0);
    }
    T_EQ(f.cur_rec, 10);
    T_EQ(f.size, 1280);
    d21(0x1000, 0, 0, (uint32_t)&f);
    T_EQ(ALV, 0);
    /* open, sequential read */
    struct fcb g;
    memset(&g, 0, sizeof g);
    parse("fcbtest.dat", &g, 0, 0);
    d21(0x0F00, 0, 0, (uint32_t)&g);
    T_EQ(ALV, 0);
    T_EQ(g.size, 1280);
    d21(0x1400, 0, 0, (uint32_t)&g);
    T_EQ(ALV, 0);
    T_EQ(dta[0], 'A');
    /* random read of record 7 */
    g.rand_rec = 7;
    d21(0x2100, 0, 0, (uint32_t)&g);
    T_EQ(ALV, 0);
    T_EQ(dta[5], 'H');
    T_EQ(g.cur_rec, 7);
    /* random write of record 12: extends the file */
    memset(dta, 'Z', 128);
    g.rand_rec = 12;
    d21(0x2200, 0, 0, (uint32_t)&g);
    T_EQ(ALV, 0);
    T_EQ(g.size, 13 * 128);
    /* random block read of 3 records from 8 */
    g.rand_rec = 8;
    d21(0x2700, 0, 3, (uint32_t)&g);
    T_EQ(ALV, 0);
    T_EQ(R.r2 & 0xFFFF, 3);
    T_EQ(dta[0], 'I');
    T_EQ(dta[256], ' ' - ' ');               /* record 10: never written, zeros on disk? */
    T_EQ(g.rand_rec, 11);
    /* read past the end: EOF */
    g.rand_rec = 20;
    d21(0x2100, 0, 0, (uint32_t)&g);
    T_EQ(ALV, 1);
    /* partial record */
    g.rec_size = 1000;
    g.rand_rec = 1;
    d21(0x2100, 0, 0, (uint32_t)&g);
    T_EQ(ALV, 3);
    g.rec_size = 128;
    /* set random record from the sequential position */
    g.cur_block = 0; g.cur_rec = 5;
    d21(0x2400, 0, 0, (uint32_t)&g);
    T_EQ(g.rand_rec, 5);
    d21(0x1000, 0, 0, (uint32_t)&g);
    /* file size */
    struct fcb s;
    memset(&s, 0, sizeof s);
    parse("FCBTEST.DAT", &s, 0, 0);
    s.rec_size = 100;
    d21(0x2300, 0, 0, (uint32_t)&s);
    T_EQ(ALV, 0);
    T_EQ(s.rand_rec, (13 * 128 + 99) / 100);
    /* search first/next with wildcards: DTA = drive + directory entry */
    struct fcb w;
    memset(&w, 0, sizeof w);
    parse("FCB*.*", &w, 0, 0);
    d21(0x1100, 0, 0, (uint32_t)&w);
    T_EQ(ALV, 0);
    T_EQ(dta[0], 3);
    T_CHECK(!memcmp(dta + 1, "FCBTEST DAT", 11), "found %.11s", dta + 1);
    T_EQ(*(uint32_t *)(dta + 1 + 28), 13 * 128);
    d21(0x1200, 0, 0, (uint32_t)&w);
    T_EQ(ALV, 0xFF);
    d21(0x5900, 0, 0, 0);
    T_EQ(R.r0 & 0xFFFF, 18);                 /* FCB calls set the extended error too */
    /* extended FCB: find the volume label */
    uint8_t xf[44];
    memset(xf, 0, sizeof xf);
    xf[0] = 0xFF; xf[6] = 0x08;
    memset(xf + 8, '?', 11);
    d21(0x1100, 0, 0, (uint32_t)xf);
    T_EQ(ALV, 0);
    T_EQ(dta[0], 0xFF);
    T_CHECK(!memcmp(dta + 8, "KTEST      ", 11), "label %.11s", dta + 8);
    /* rename with a ? template */
    uint8_t rf[40];
    memset(rf, 0, sizeof rf);
    memcpy(rf + 1, "FCBTEST DAT", 11);
    memcpy(rf + 17, "????XXXXBIN", 11);
    d21(0x1700, 0, 0, (uint32_t)rf);
    T_EQ(ALV, 0);
    T_EQ(d21(0x3D00, 0, 0, (uint32_t)"FCBTXXXX.BIN"), 0);
    d21(0x3E00, R.r0 & 0xFFFF, 0, 0);
    /* random block write with CX=0 sets the size */
    memset(&g, 0, sizeof g);
    parse("FCBTXXXX.BIN", &g, 0, 0);
    d21(0x0F00, 0, 0, (uint32_t)&g);
    g.rand_rec = 2;
    d21(0x2800, 0, 0, (uint32_t)&g);
    T_EQ(g.size, 256);
    d21(0x1000, 0, 0, (uint32_t)&g);
    /* delete with wildcards */
    memset(&w, 0, sizeof w);
    parse("FCBT????.BIN", &w, 0, 0);
    d21(0x1300, 0, 0, (uint32_t)&w);
    T_EQ(ALV, 0);
    T_EQ(d21(0x3D00, 0, 0, (uint32_t)"FCBTXXXX.BIN"), 1);
    d21(0x1300, 0, 0, (uint32_t)&w);
    T_EQ(ALV, 0xFF);
    /* an FCB on a device */
    memset(&f, 0, sizeof f);
    parse("NUL", &f, 0, 0);
    d21(0x0F00, 0, 0, (uint32_t)&f);
    T_EQ(ALV, 0);
    d21(0x1500, 0, 0, (uint32_t)&f);
    T_EQ(ALV, 0);
    return t_end();
}
