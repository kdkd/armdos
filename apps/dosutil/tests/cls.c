/* CLS.EXE - test helper for the DOS 4 utility tests (the kernel's test shell
 * has no CLS): clear the screen with INT 10h AH=06h, cursor home. */
#include "u4.h"
int main(void)
{
    struct armregs r;
    u4_clr(&r);
    r.r0 = 0x0600; r.r1 = 0x0700; r.r2 = 0; r.r3 = 0x184F;
    u4_int10(&r);
    u4_clr(&r);
    r.r0 = 0x0200; r.r1 = 0; r.r3 = 0;
    u4_int10(&r);
    return 0;
}
