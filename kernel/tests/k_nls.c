/* k_nls.c - national language support: COUNTRY= from
   COUNTRY.SYS, INT 21h AH=38h/65h/66h, and the same calls through NLSFUNC.
     K_NLS BOOT49   after COUNTRY=049 (Germany, code page 437 from COUNTRY.SYS)
     K_NLS NLSFUNC  after NLSFUNC is installed: 6602h, 38h set, others' info */
#include "t.h"

static struct armregs R;
#define AXV (R.r0 & 0xFFFF)
static int d21(unsigned ax, unsigned bx, unsigned cx, uint32_t dx)
{
    memset(&R, 0, sizeof R);
    R.r0 = ax; R.r1 = bx; R.r2 = cx; R.r3 = dx;
    return _armdos_int21(&R);
}
static int ext(unsigned al, unsigned cp, unsigned country, uint8_t *buf, unsigned len)
{
    memset(&R, 0, sizeof R);
    R.r0 = 0x6500 | al; R.r1 = cp; R.r3 = country; R.r2 = len; R.r5 = (uint32_t)buf;
    return _armdos_int21(&R);
}
static const uint8_t *table(unsigned al)
{
    uint8_t b[5];
    ext(al, 0xFFFF, 0xFFFF, b, 5);
    return (const uint8_t *)(b[1] | (b[2] << 8) | (b[3] << 16) | ((uint32_t)b[4] << 24));
}

static void boot49(int with_nlsfunc)
{
    uint8_t c[64];
    T_EQ(d21(0x3800, 0, 0, (uint32_t)c), 0);
    T_EQ(R.r1 & 0xFFFF, 49);
    T_EQ(AXV, 49);
    T_EQ(c[0], 1);                            /* d-m-y */
    T_EQ(c[2], 'D'); T_EQ(c[3], 'M'); T_EQ(c[4], 0);
    T_EQ(c[7], '.'); T_EQ(c[9], ','); T_EQ(c[0x0B], '.'); T_EQ(c[0x0D], '.');
    T_EQ(c[0x0F], 2); T_EQ(c[0x10], 2); T_EQ(c[0x11], 1);    /* "1,00 DM", 24-hour */
    T_EQ(c[0x16], ';');
    uint32_t (*mapcase)(uint32_t) = (void *)(c[0x12] | (c[0x13] << 8) | (c[0x14] << 16) | ((uint32_t)c[0x15] << 24));
    T_EQ(mapcase(0x84), 0x8E);                /* a umlaut -> A umlaut */
    T_EQ(ext(1, 0xFFFF, 0xFFFF, c, 41), 0);
    T_EQ(R.r2 & 0xFFFF, 41);
    T_EQ(AXV, 437);                           /* AX = the system code page */
    T_EQ(c[0], 1); T_EQ(c[1], 38);
    T_EQ(c[3] | (c[4] << 8), 49);
    T_EQ(c[5] | (c[6] << 8), 437);
    T_EQ(c[7], 1);
    T_EQ(ext(1, 0xFFFF, 0xFFFF, c, 10), 0);   /* a short buffer: truncated */
    T_EQ(R.r2 & 0xFFFF, 10);
    T_EQ(c[1], 7);
    const uint8_t *uc = table(2), *co = table(6);
    T_EQ(uc[0] | (uc[1] << 8), 128);
    T_EQ(uc[2 + 0x84 - 0x80], 0x8E);
    T_EQ(co[0] | (co[1] << 8), 256);
    T_EQ(co[2 + 'a'], co[2 + 'A']);
    T_EQ(ext(3, 0xFFFF, 0xFFFF, c, 5), 1);    /* no such table */
    T_EQ(AXV, 1);
    d21(0x6601, 0, 0, 0);
    T_EQ(R.r1 & 0xFFFF, 437);
    T_EQ(R.r3 & 0xFFFF, 437);
    /* without NLSFUNC: no code page change, no other country */
    if (with_nlsfunc) return;
    T_EQ(d21(0x6602, 850, 0, 0), 1);
    T_EQ(AXV, 1);
    T_EQ(d21(0x38FF, 44, 0, 0xFFFF), 1);
    T_EQ(AXV, 1);
    T_EQ(d21(0x382C, 0, 0, (uint32_t)c), 1);  /* another country's info (44) */
    T_EQ(AXV, 2);
    T_EQ(ext(1, 0xFFFF, 44, c, 41), 1);
    /* file names: the country's upper case table */
    FILE *f = fopen("\x84NDERN.TXT", "w");    /* a umlaut */
    T_CHECK(f != 0, "create");
    if (f) fclose(f);
    T_EQ(d21(0x4E00, 0, 0, (uint32_t)"\x8E" "NDERN.TXT"), 0);
    d21(0x4100, 0, 0, (uint32_t)"\x8E" "NDERN.TXT");
}

static void nlsfunc(void)
{
    uint8_t c[64];
    struct armregs r = { 0 };
    r.r0 = 0x1400;
    _armdos_int2f(&r);
    T_EQ(r.r0 & 0xFF, 0xFF);
    /* another country's information through NLSFUNC */
    T_EQ(d21(0x3821, 0, 0, (uint32_t)c), 0);  /* 33: France */
    T_EQ(R.r1 & 0xFFFF, 33);
    T_EQ(c[0], 1); T_EQ(c[2], 'F'); T_EQ(c[0x0B], '/');
    T_EQ(ext(1, 0xFFFF, 44, c, 41), 0);        /* 44: UK */
    T_EQ(c[3] | (c[4] << 8), 44);
    T_EQ(c[7 + 2], 0x9C);                     /* pound sign */
    /* the global code page */
    T_EQ(d21(0x6602, 865, 0, 0), 1);           /* Germany has no 865 */
    T_EQ(AXV, 2);
    d21(0x5900, 0, 0, 0);
    T_EQ(AXV, 13);                            /* invalid data: "Invalid code page" */
    T_EQ(d21(0x6602, 850, 0, 0), 0);
    d21(0x6601, 0, 0, 0);
    T_EQ(R.r1 & 0xFFFF, 850);
    T_EQ(R.r3 & 0xFFFF, 437);
    T_EQ(ext(1, 0xFFFF, 0xFFFF, c, 41), 0);
    T_EQ(c[5] | (c[6] << 8), 850);
    T_EQ(table(2)[2 + 0xE4 - 0x80], 'O');     /* o tilde -> O in Germany's 850 table (437: E4h is sigma) */
    /* set the country */
    T_EQ(d21(0x38FF, 44, 0, 0xFFFF), 0);
    T_EQ(d21(0x3800, 0, 0, (uint32_t)c), 0);
    T_EQ(R.r1 & 0xFFFF, 44);
    T_EQ(c[0x0B], '-');
    T_EQ(d21(0x6602, 437, 0, 0), 0);
    d21(0x6601, 0, 0, 0);
    T_EQ(R.r1 & 0xFFFF, 437);
    T_EQ(d21(0x38FF, 999, 0, 0xFFFF), 1);      /* no such country */
}

int main(int argc, char **argv)
{
    const char *what = argc > 1 ? argv[1] : "";
    if (!strcmp(what, "BOOT49")) { t_begin("nls-boot49"); boot49(0); }
    else if (!strcmp(what, "BOOT49F")) { t_begin("nls-boot49f"); boot49(1); }
    else { t_begin("nls-nlsfunc"); nlsfunc(); }
    return t_end();
}
