/* k_tsr.c - loaded by CONFIG.SYS INSTALL=: hooks INT 2Fh AH=D0h and stays resident */
#include "t.h"

static armdos_vect_t old2f;
static void my2f(struct armregs *f)
{
    if (((f->r0 >> 8) & 0xFF) == 0xD0) { f->r0 = (f->r0 & ~0xFF) | 0xFF; f->r1 = 0x4B54; return; }
    armdos_callold(old2f, f);
}

/* INT 28h: DOS is idle (waiting for a key in 01h-0Ch); functions above 0Ch are
   safe to call from here even though InDOS is set */
static armdos_vect_t old28;
static volatile int did28;
static void my28(struct armregs *f)
{
    if (!did28) {
        did28 = 1;
        struct armregs r = { 0 };
        r.r0 = 0x3C00; r.r3 = (uint32_t)"C:\\IDLE.TXT";
        if (!_armdos_int21(&r)) {
            int h = r.r0 & 0xFFFF;
            memset(&r, 0, sizeof r);
            r.r0 = 0x4000; r.r1 = h; r.r2 = 4; r.r3 = (uint32_t)"idle";
            _armdos_int21(&r);
            memset(&r, 0, sizeof r);
            r.r0 = 0x3E00; r.r1 = h;
            _armdos_int21(&r);
        }
    }
    armdos_callold(old28, f);
    f->cpsr &= ~ARM_CPSR_C;
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "IDLE")) {
        old28 = armdos_getvect(0x28);
        armdos_setvect(0x28, my28);
    }
    old2f = armdos_getvect(0x2F);
    armdos_setvect(0x2F, my2f);
    t_log("T:TSR installed argc=%d %s\n", argc, argc > 1 ? argv[1] : "");
    /* keep everything up to the end of our block */
    struct armregs r = { 0 };
    r.r0 = 0x3100;
    r.r3 = ((uint32_t)_armdos_blockend - (uint32_t)_armdos_psp) >> 4;
    _armdos_int21(&r);
    return 1;
}
