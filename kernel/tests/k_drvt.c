/* k_drvt.c - checks on what CONFIG.SYS set up: DEVICE= drivers (the RAM disk
   and the replacement CON), INSTALL=, FILES/BUFFERS/LASTDRIVE in the LoL, the
   system block's DEVMARK sub-blocks */
#include "t.h"

static struct armregs R;
#define AXV (R.r0 & 0xFFFF)
static int d21(unsigned ax, unsigned bx, unsigned cx, uint32_t dx)
{ memset(&R, 0, sizeof R); R.r0 = ax; R.r1 = bx; R.r2 = cx; R.r3 = dx; return _armdos_int21(&R); }

static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }

int main(int argc, char **argv)
{
    t_begin("drivers");
    int want_files = argc > 1 ? atoi(argv[1]) : 8;
    int want_bufs = argc > 2 ? atoi(argv[2]) : 15;
    int want_last = argc > 3 ? argv[3][0] - 'A' + 1 : 5;
    d21(0x5200, 0, 0, 0);
    const uint8_t *lol = (const uint8_t *)R.r1;
    T_EQ((uint32_t)lol & 3, 0);
    T_EQ(lol[0x20], 4);                               /* A: B: C: + the RAM disk */
    T_EQ(lol[0x21], want_last);
    T_EQ(rd16(lol + 0x47), want_bufs);
    T_EQ(lol[0x4B], 3);                               /* booted from C: */
    /* the NUL device heads the chain, at LoL+24h */
    const struct { uint32_t next; uint16_t attr, pad; uint32_t s, i; char name[8]; } *d = (const void *)(lol + 0x24);
    T_CHECK(!memcmp(d->name, "NUL     ", 8), "NUL at +24h");
    int n = 0, cons = 0, xms = 0, ramd = 0;
    for (const uint8_t *p = (const uint8_t *)d; p && (uint32_t)p != 0xFFFFFFFF && n < 40; n++) {
        const char *name = (const char *)p + 16;
        if (!memcmp(name, "CON     ", 8)) cons++;
        if (!memcmp(name, "XMSXXXX0", 8)) xms++;
        if (!memcmp(name + 1, "RAMDSK", 6)) ramd++;
        p = (const uint8_t *)rd32(p);
    }
    T_EQ(cons, 2);                                    /* ours shadows IO.SYS's */
    T_EQ(xms, 1);
    T_EQ(ramd, 1);
    /* SFTs: FILES in total */
    int sfts = 0;
    for (const uint8_t *b = (const uint8_t *)rd32(lol + 4); (uint32_t)b != 0xFFFFFFFF; b = (const uint8_t *)rd32(b)) sfts += rd16(b + 4);
    T_EQ(sfts, want_files);
    /* the system block and its DEVMARKs */
    uint16_t first = rd16(lol - 2);
    const uint8_t *m = (const uint8_t *)((uint32_t)first << 4);
    T_EQ(m[0], 'M');
    T_EQ(rd16(m + 1), 8);
    char marks[16] = { 0 };
    int k = 0;
    const uint8_t *end = m + 16 + rd16(m + 3) * 16;
    for (const uint8_t *s = m + 16; s < end && k < 15; ) {
        marks[k++] = s[0];
        s += 16 + rd16(s + 3) * 16;
    }
    t_log("T:MARKS %s\n", marks);
    /* the RAM disk: D: */
    int cf = d21(0x3C00, 0, 0, (uint32_t)"D:\\RAM.TXT");
    T_EQ(cf, 0);
    int h = AXV;
    static char big[20000];
    for (int i = 0; i < 20000; i++) big[i] = i * 7;
    d21(0x4000, h, 20000, (uint32_t)big);
    T_EQ(AXV, 20000);
    d21(0x3E00, h, 0, 0);
    static char back[20000];
    d21(0x3D00, 0, 0, (uint32_t)"D:\\RAM.TXT");
    h = AXV;
    d21(0x3F00, h, 20000, (uint32_t)back);
    T_EQ(AXV, 20000);
    T_CHECK(!memcmp(big, back, 20000), "RAM disk data");
    d21(0x3E00, h, 0, 0);
    d21(0x3600, 0, 0, 4);
    T_EQ(R.r3 & 0xFFFF, 124);                         /* clusters on the RAM disk */
    uint8_t dta[43];
    d21(0x1A00, 0, 0, (uint32_t)dta);
    d21(0x4E00, 0, 8, (uint32_t)"D:\\*.*");
    T_CHECK(!strcmp((char *)dta + 0x1E, "RAMDISK"), "label %s", dta + 0x1E);
    /* INSTALL=K_TSR.EXE hooked INT 2Fh */
    struct armregs r = { 0 };
    r.r0 = 0xD000;
    _armdos_int2f(&r);
    T_EQ(r.r0 & 0xFF, 0xFF);
    T_EQ(r.r1 & 0xFFFF, 0x4B54);
    /* HIMEM hooked INT 15h AH=88h */
    memset(&r, 0, sizeof r);
    r.r0 = 0x8800;
    _armdos_intr(0x15, &r);
    T_EQ(r.r0 & 0xFFFF, 0);
    /* lowercase output goes through the replacement CON */
    printf("lowercase through ucon\n");
    return t_end();
}
