/*
 * mdres.c - MODE.COM's resident portion (MODE 4.00's RESCODE): printer
 * redirection (LPTn:=COMm) and the retry options of serial and parallel
 * ports, hooked into INT 14h and INT 17h. Nothing here may call the C
 * library (see mode.h).
 */
#include "mode.h"

struct moderes mode_res RES = { { 'M', 'O', 'D', 'E' }, { 0 }, { 0 }, { 0 } };
armdos_vect_t mode_old14 RES, mode_old17 RES;

RESFN static void copyregs(struct armregs *d, const struct armregs *s)
{
    const uint32_t *a = (const uint32_t *)s;
    uint32_t *b = (uint32_t *)d;
    for (unsigned i = 0; i < sizeof *s / 4; i++) b[i] = a[i];
}

#define AH_(f) (((f)->r0 >> 8) & 0xFF)
#define SETAH(f, v) ((f)->r0 = ((f)->r0 & ~0xFF00u) | ((v) << 8))

/* INT 14h: serial retry. B: keep retrying a timed-out send; R: report the
   port ready; E: return the error (as the BIOS does) */
RESFN void mode_int14(struct armregs *f)
{
    unsigned port = f->r3 & 0xFFFF, fn = AH_(f);
    uint8_t retry = port < 4 ? mode_res.comretry[port] : 0;
    struct armregs r;
    for (;;) {
        copyregs(&r, f);
        mode_old14(&r);
        if (!retry || fn == 0 || !(AH_(&r) & 0x80)) break;
        if (retry == 'B') continue;
        if (retry == 'R') SETAH(&r, 0x60);
        break;
    }
    copyregs(f, &r);
}

/* INT 17h: LPTn -> COMm redirection and printer retry */
RESFN void mode_int17(struct armregs *f)
{
    if ((f->r0 & 0xFFFF) == 0xDD00) {           /* is the resident portion there? */
        f->r0 = 0x4D4F;
        f->r1 = (uint32_t)&mode_res;
        return;
    }
    unsigned lpt = f->r3 & 0xFFFF, fn = AH_(f);
    if (lpt < 3 && mode_res.reroute[lpt]) {
        int st = 0x90;                          /* not busy, selected */
        if (fn == 0) {
            struct armregs r;
            copyregs(&r, f);
            r.r0 = 0x0100 | (f->r0 & 0xFF);
            r.r3 = mode_res.reroute[lpt] - 1;
            ((armdos_vect_t)((volatile uint32_t *)0)[0x14])(&r);
            if (AH_(&r) & 0x80) st = 0x01;      /* time-out */
        }
        SETAH(f, st);
        return;
    }
    uint8_t retry = lpt < 3 ? mode_res.lptretry[lpt] : 0;
    struct armregs r;
    for (;;) {
        copyregs(&r, f);
        mode_old17(&r);
        if (!retry || fn != 0 || !(AH_(&r) & 0x01)) break;
        if (retry == 'B') continue;             /* infinite retry */
        if (retry == 'R') SETAH(&r, 0x90);
        break;
    }
    copyregs(f, &r);
}
