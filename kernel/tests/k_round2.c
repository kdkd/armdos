/* k_round2.c - regressions for the requests other components filed (round 2) */
#include "t.h"

static struct armregs R;
#define AXV (R.r0 & 0xFFFF)
static int d21(unsigned ax, unsigned bx, unsigned cx, uint32_t dx)
{ memset(&R, 0, sizeof R); R.r0 = ax; R.r1 = bx; R.r2 = cx; R.r3 = dx; return _armdos_int21(&R); }

static volatile int i24count;
static void count24(struct armregs *f) { i24count++; f->r0 = (f->r0 & ~0xFFu) | 3; }

int main(void)
{
    t_begin("round2");
    uint8_t dta[64];
    d21(0x1A00, 0, 0, (uint32_t)dta);

    /* 6Ch: DH flags (0100h "code page not checked") are accepted and ignored */
    memset(&R, 0, sizeof R); R.r0 = 0x6C00; R.r1 = 2; R.r3 = 0x0112; R.r4 = (uint32_t)"R2.TXT";
    T_EQ(_armdos_int21(&R), 0);
    d21(0x3E00, AXV, 0, 0);
    memset(&R, 0, sizeof R); R.r0 = 0x6C00; R.r1 = 0; R.r3 = 0x0101; R.r4 = (uint32_t)"R2.TXT";
    T_EQ(_armdos_int21(&R), 0);
    T_EQ(R.r2 & 0xFFFF, 1);
    d21(0x3E00, AXV, 0, 0);
    memset(&R, 0, sizeof R); R.r0 = 0x6C00; R.r1 = 0; R.r3 = 0x0121; R.r4 = (uint32_t)"R2.TXT";
    T_EQ(_armdos_int21(&R), 1);                   /* high nibble 2 is still invalid */
    d21(0x4100, 0, 0, (uint32_t)"R2.TXT");

    /* 6506h: DOS 4's collating table, the same table every time */
    uint8_t b[8], b2[8];
    memset(&R, 0, sizeof R); R.r0 = 0x6506; R.r1 = 0xFFFF; R.r3 = 0xFFFF; R.r2 = 5; R.r5 = (uint32_t)b;
    _armdos_int21(&R);
    memset(&R, 0, sizeof R); R.r0 = 0x6506; R.r1 = 0xFFFF; R.r3 = 0xFFFF; R.r2 = 5; R.r5 = (uint32_t)b2;
    _armdos_int21(&R);
    T_CHECK(!memcmp(b, b2, 5), "same table");
    const uint8_t *ct = (const uint8_t *)(b[1] | (b[2] << 8) | (b[3] << 16) | ((uint32_t)b[4] << 24));
    T_EQ(ct[0] | (ct[1] << 8), 256);
    T_EQ(ct[2 + 'a'], 'A');
    T_EQ(ct[2 + 0x81], 'U');
    T_EQ(ct[2 + 0xE1], 'S');
    T_EQ(ct[2 + 0x9B], '$');
    T_EQ(ct[2 + 0xA8], '?');
    T_EQ(ct[2 + 0xAE], '"');
    T_EQ(ct[2 + 0xB5], 0xB5);

    /* blanks inside a name (4.00 packs them in), a trailing blank is padding */
    T_EQ(d21(0x5B00, 0, 0, (uint32_t)"BLANK IN.TXT"), 0);
    d21(0x3E00, AXV, 0, 0);
    T_EQ(d21(0x4E00, 0, 0, (uint32_t)"BLANK*.*"), 0);
    T_CHECK(!strcmp((char *)dta + 0x1E, "BLANK IN.TXT"), "found [%s]", dta + 0x1E);
    T_EQ(d21(0x3D00, 0, 0, (uint32_t)"blank in.txt"), 0);
    d21(0x3E00, AXV, 0, 0);
    T_EQ(d21(0x3D00, 0, 0, (uint32_t)"BLANK IN.TXT "), 0);
    d21(0x3E00, AXV, 0, 0);
    d21(0x4100, 0, 0, (uint32_t)"BLANK IN.TXT");

    /* a new volume label goes to the boot record too */
    uint8_t xf[44];
    memset(xf, 0, sizeof xf);
    xf[0] = 0xFF; xf[6] = 0x08;
    memset(xf + 8, '?', 11);
    d21(0x1300, 0, 0, (uint32_t)xf);                  /* delete the label (LABEL does this) */
    T_EQ(R.r0 & 0xFF, 0);
    T_EQ(d21(0x3C00, 0, 0x08, (uint32_t)"C:\\NEW LAB.EL"), 0);
    d21(0x3E00, AXV, 0, 0);
    uint8_t mid[32];
    T_EQ(d21(0x6900, 3, 0, (uint32_t)mid), 0);
    T_CHECK(!memcmp(mid + 6, "NEW LAB EL ", 11), "boot label [%.11s]", mid + 6);
    T_EQ(d21(0x4E00, 0, 8, (uint32_t)"C:\\*.*"), 0);
    T_CHECK(!strcmp((char *)dta + 0x1E, "NEW LAB.EL"), "label [%s]", dta + 0x1E);

    /* CHDIR to a bare drive fails */
    T_EQ(d21(0x3B00, 0, 0, (uint32_t)"C:"), 1);
    T_EQ(AXV, 3);

    /* closing one of two handles writes the directory entry already */
    d21(0x3C00, 0, 0, (uint32_t)"TWO.TXT");
    int h = AXV;
    d21(0x4500, h, 0, 0);
    int h2 = AXV;
    d21(0x4000, h, 5, (uint32_t)"12345");
    d21(0x3E00, h, 0, 0);
    d21(0x4E00, 0, 0, (uint32_t)"TWO.TXT");
    T_EQ(*(uint32_t *)(dta + 0x1A), 5);
    d21(0x3E00, h2, 0, 0);
    d21(0x4100, 0, 0, (uint32_t)"TWO.TXT");

    /* generic IOCTL: a driver error comes back as its code + 13h */
    T_EQ(d21(0x440D, 3, 0x0899, (uint32_t)mid), 1);
    T_EQ(AXV, 0x16);

    /* FCB search first finds a device */
    uint8_t fcb[40];
    memset(fcb, 0, sizeof fcb);
    memcpy(fcb + 1, "CON        ", 11);
    d21(0x1100, 0, 0, (uint32_t)fcb);
    T_EQ(R.r0 & 0xFF, 0);
    T_CHECK(!memcmp(dta + 1, "CON     ", 8), "device found [%.11s]", dta + 1);
    d21(0x1200, 0, 0, (uint32_t)fcb);
    T_EQ(R.r0 & 0xFF, 0xFF);

    /* find first on a bare "A:" fails without touching the (empty) drive */
    d21(0x2524, 0, 0, (uint32_t)count24);
    T_EQ(d21(0x4E00, 0, 0, (uint32_t)"A:"), 1);
    T_EQ(i24count, 0);

    /* 33h/50h/51h/62h do not count as "in DOS" */
    d21(0x3400, 0, 0, 0);
    T_EQ(*(uint8_t *)R.r1, 0);

    /* our shell had no environment from SYSINIT and FCB1 = the boot drive */
    struct psp *sp = (struct psp *)((uint32_t)_armdos_psp->parent << 4);
    T_EQ(sp->fcb1[0], 3);
    return t_end();
}
