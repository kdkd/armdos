/* k_round3.c - logical drives in the extended partition, SUBST and JOIN
   through the CDS (as SUBST.EXE/JOIN.EXE write it), extended attributes */
#include "t.h"

static struct armregs R;
#define AXV (R.r0 & 0xFFFF)
static int d21(unsigned ax, unsigned bx, unsigned cx, uint32_t dx)
{ memset(&R, 0, sizeof R); R.r0 = ax; R.r1 = bx; R.r2 = cx; R.r3 = dx; return _armdos_int21(&R); }

static volatile int c24, di24;
static void fail24(struct armregs *f) { c24++; di24 = f->r5 & 0xFF; f->r0 = (f->r0 & ~0xFFu) | 3; }

struct cdsx { char path[67]; uint16_t flags; uint32_t dpb; uint16_t cluster, n1, n2, bsoffset; uint8_t type; uint32_t ifs; uint16_t fsda; } __attribute__((packed));

static int writefile(const char *n, const char *t)
{
    if (d21(0x3C00, 0, 0, (uint32_t)n)) return -1;
    int h = AXV;
    d21(0x4000, h, strlen(t), (uint32_t)t);
    d21(0x3E00, h, 0, 0);
    return 0;
}
static int readfile(const char *n, char *b, int max)
{
    if (d21(0x3D00, 0, 0, (uint32_t)n)) return -1;
    int h = AXV;
    d21(0x3F00, h, max, (uint32_t)b);
    int k = AXV;
    b[k] = 0;
    d21(0x3E00, h, 0, 0);
    return k;
}

int main(void)
{
    t_begin("round3");
    char b[128];
    uint8_t dta[64];
    d21(0x1A00, 0, 0, (uint32_t)dta);
    d21(0x5200, 0, 0, 0);
    uint8_t *lol = (uint8_t *)R.r1;
    T_EQ(lol[0x20], 5);                          /* A: B: C: D: E: */
    struct cdsx *cds = (struct cdsx *)(lol[0x16] | (lol[0x17] << 8) | (lol[0x18] << 16) | ((uint32_t)lol[0x19] << 24));

    /* D: - a formatted logical drive */
    T_EQ(writefile("D:\\D.TXT", "on drive d"), 0);
    T_EQ(readfile("D:\\D.TXT", b, 100), 10);
    d21(0x4E00, 0, 8, (uint32_t)"D:\\*.*");
    T_CHECK(!strcmp((char *)dta + 0x1E, "LOGICAL1"), "D: label [%s]", dta + 0x1E);
    d21(0x3600, 0, 0, 4);
    T_CHECK(AXV != 0xFFFF && (R.r1 & 0xFFFF) > 100, "D: free %u", (unsigned)(R.r1 & 0xFFFF));
    uint8_t mid[32];
    T_EQ(d21(0x440D, 4, 0x0866, (uint32_t)mid), 0);
    T_CHECK(!memcmp(mid + 17, "FAT12   ", 8), "D: fs [%.8s]", mid + 17);

    /* E: - an unformatted logical drive */
    d21(0x2524, 0, 0, (uint32_t)fail24);
    T_EQ(d21(0x440D, 5, 0x0866, (uint32_t)mid), 0);
    T_CHECK(!memcmp(mid + 17, "FAT12   ", 8), "E: default fs [%.8s]", mid + 17);
    T_CHECK(!memcmp(mid + 6, "NO NAME    ", 11), "E: label [%.11s]", mid + 6);
    T_EQ(d21(0x3D00, 0, 0, (uint32_t)"E:\\X.TXT"), 1);
    T_CHECK(c24 >= 1 && di24 == 7, "INT 24h unknown media: %d %d", c24, di24);
    static uint8_t p60[64];
    p60[0] = 1;
    T_EQ(d21(0x440D, 5, 0x0860, (uint32_t)p60), 0);
    T_EQ(p60[7] | (p60[8] << 8), 512);            /* the default BPB for FORMAT */

    /* SUBST G: C:\SUBDIR (what SUBST.EXE writes into the CDS) */
    d21(0x3900, 0, 0, (uint32_t)"C:\\SUBDIR");
    d21(0x3900, 0, 0, (uint32_t)"C:\\SUBDIR\\INNER");
    writefile("C:\\A.TXT", "root a");
    struct cdsx *g = &cds[6];
    strcpy(g->path, "C:\\SUBDIR");
    g->flags = 0x5000;
    g->dpb = cds[2].dpb;
    g->bsoffset = 9;
    g->cluster = 0xFFFF;
    T_EQ(writefile("G:\\A.TXT", "subst a"), 0);
    T_EQ(readfile("C:\\SUBDIR\\A.TXT", b, 100), 7);
    T_EQ(readfile("C:\\A.TXT", b, 100), 6);      /* untouched */
    char out[128];
    memset(&R, 0, sizeof R); R.r0 = 0x6000; R.r4 = (uint32_t)"G:\\A.TXT"; R.r5 = (uint32_t)out;
    _armdos_int21(&R);
    T_CHECK(!strcmp(out, "C:\\SUBDIR\\A.TXT"), "truename [%s]", out);
    d21(0x4700, 0, 0, 0); memset(&R, 0, sizeof R); R.r0 = 0x4700; R.r3 = 7; R.r4 = (uint32_t)out;
    T_EQ(_armdos_int21(&R), 0);
    T_CHECK(out[0] == 0, "G: cwd [%s]", out);
    T_EQ(d21(0x3B00, 0, 0, (uint32_t)"G:\\INNER"), 0);
    memset(&R, 0, sizeof R); R.r0 = 0x4700; R.r3 = 7; R.r4 = (uint32_t)out;
    _armdos_int21(&R);
    T_CHECK(!strcmp(out, "INNER"), "G: cwd [%s]", out);
    T_CHECK(!strcmp(g->path, "C:\\SUBDIR\\INNER"), "CDS text [%s]", g->path);
    T_EQ(writefile("G:REL.TXT", "rel"), 0);
    T_EQ(readfile("C:\\SUBDIR\\INNER\\REL.TXT", b, 100), 3);
    T_EQ(d21(0x3B00, 0, 0, (uint32_t)"G:.."), 0);
    T_CHECK(!strcmp(g->path, "C:\\SUBDIR"), "back to the root [%s]", g->path);
    T_EQ(d21(0x3B00, 0, 0, (uint32_t)"G:\\.."), 1);   /* cannot climb out of the SUBST root */
    d21(0x4409, 7, 0, 0);
    T_EQ(R.r3 & 0x8000, 0x8000);
    T_EQ(d21(0x4100, 0, 0, (uint32_t)"G:\\A.TXT"), 0);
    T_EQ(readfile("C:\\A.TXT", b, 100), 6);      /* DEL G:A.TXT left C:\A.TXT alone */
    T_EQ(readfile("C:\\SUBDIR\\A.TXT", b, 100), -1);
    /* SUBST G: /D */
    strcpy(g->path, "G:\\");
    g->flags = 0;
    g->bsoffset = 2;
    T_EQ(d21(0x3D00, 0, 0, (uint32_t)"G:\\X"), 1);
    T_EQ(AXV, 3);

    /* JOIN D: C:\JOINPT */
    d21(0x3900, 0, 0, (uint32_t)"C:\\JOINPT");
    struct cdsx *dd = &cds[3];
    strcpy(dd->path, "C:\\JOINPT");
    dd->flags = 0x6000;
    dd->bsoffset = 9;
    lol[0x3C]++;
    T_EQ(readfile("C:\\JOINPT\\D.TXT", b, 100), 10);
    T_EQ(d21(0x3D00, 0, 0, (uint32_t)"D:\\D.TXT"), 1);   /* D: itself is gone */
    T_EQ(AXV, 3);
    d21(0x3600, 0, 0, 4);
    T_EQ(AXV, 0xFFFF);
    d21(0x0E00, 0, 0, 3);
    d21(0x1900, 0, 0, 0);
    T_EQ(R.r0 & 0xFF, 2);                         /* still C: */
    T_EQ(d21(0x4E00, 0, 0, (uint32_t)"C:\\JOINPT\\*.TXT"), 0);
    T_CHECK(!strcmp((char *)dta + 0x1E, "D.TXT"), "join find [%s]", dta + 0x1E);
    T_EQ(writefile("C:\\JOINPT\\NEW.TXT", "via join"), 0);
    memset(&R, 0, sizeof R); R.r0 = 0x6000; R.r4 = (uint32_t)"C:\\JOINPT\\NEW.TXT"; R.r5 = (uint32_t)out;
    _armdos_int21(&R);
    T_CHECK(!strcmp(out, "C:\\JOINPT\\NEW.TXT"), "truename join [%s]", out);
    T_EQ(d21(0x3B00, 0, 0, (uint32_t)"C:\\JOINPT"), 0);
    T_EQ(readfile("NEW.TXT", b, 100), 8);
    d21(0x3B00, 0, 0, (uint32_t)"C:\\");
    /* JOIN /D */
    strcpy(dd->path, "D:\\");
    dd->flags = 0x4000;
    dd->bsoffset = 2;
    lol[0x3C]--;
    T_EQ(readfile("D:\\NEW.TXT", b, 100), 8);
    d21(0x4100, 0, 0, (uint32_t)"D:\\NEW.TXT");
    d21(0x3A00, 0, 0, (uint32_t)"C:\\JOINPT");

    /* extended attributes: an empty list on FAT */
    d21(0x3D00, 0, 0, (uint32_t)"C:\\A.TXT");
    int h = AXV;
    uint8_t ea[16];
    memset(ea, 0xEE, sizeof ea);
    memset(&R, 0, sizeof R); R.r0 = 0x5703; R.r1 = h; R.r2 = 16; R.r5 = (uint32_t)ea;
    T_EQ(_armdos_int21(&R), 0);
    T_EQ(R.r2 & 0xFFFF, 2);
    T_EQ(ea[0] | (ea[1] << 8), 0);
    memset(&R, 0, sizeof R); R.r0 = 0x5702; R.r1 = h; R.r2 = 16; R.r5 = (uint32_t)ea;
    T_EQ(_armdos_int21(&R), 0);
    memset(&R, 0, sizeof R); R.r0 = 0x5704; R.r1 = h; R.r5 = (uint32_t)ea;
    T_EQ(_armdos_int21(&R), 0);
    d21(0x3E00, h, 0, 0);
    memset(&R, 0, sizeof R); R.r0 = 0x4303; R.r3 = (uint32_t)"C:\\A.TXT"; R.r2 = 16; R.r5 = (uint32_t)ea;
    T_EQ(_armdos_int21(&R), 0);
    return t_end();
}
