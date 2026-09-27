/* k_round4.c - COMMAND.COM's round-4 requests: ^C read inside INT 24h,
   69h without INT 24h, CHDIR with a trailing backslash */
#include "t.h"

static struct armregs R;
#define AXV (R.r0 & 0xFFFF)
static int d21(unsigned ax, unsigned bx, unsigned cx, uint32_t dx)
{ memset(&R, 0, sizeof R); R.r0 = ax; R.r1 = bx; R.r2 = cx; R.r3 = dx; return _armdos_int21(&R); }

static volatile int c24, got = -1;
static void quiet24(struct armregs *f) { c24++; f->r0 = (f->r0 & ~0xFFu) | 3; }
static void ask24(struct armregs *f)
{
    t_log("T:IN24\n");
    struct armregs r = { 0 };
    r.r0 = 0x0C01;                  /* flush, then read a key with echo, as COMMAND does */
    _armdos_int21(&r);
    got = r.r0 & 0xFF;
    f->r0 = (f->r0 & ~0xFFu) | 3;
}

int main(void)
{
    t_begin("round4");
    d21(0x3900, 0, 0, (uint32_t)"SUB4");
    T_EQ(d21(0x3B00, 0, 0, (uint32_t)"SUB4\\"), 1);
    T_EQ(AXV, 3);
    T_EQ(d21(0x3B00, 0, 0, (uint32_t)"SUB4/"), 1);
    T_EQ(d21(0x3B00, 0, 0, (uint32_t)"SUB4"), 0);
    T_EQ(d21(0x3B00, 0, 0, (uint32_t)"\\"), 0);
    T_EQ(d21(0x3B00, 0, 0, (uint32_t)"C:\\"), 0);
    d21(0x3A00, 0, 0, (uint32_t)"SUB4");

    d21(0x2524, 0, 0, (uint32_t)quiet24);
    uint8_t mid[32];
    T_EQ(d21(0x6900, 1, 0, (uint32_t)mid), 1);   /* empty A: */
    T_EQ(c24, 0);

    d21(0x2524, 0, 0, (uint32_t)ask24);
    d21(0x3D00, 0, 0, (uint32_t)"A:\\X.TXT");
    T_EQ(got, 3);
    return t_end();
}
