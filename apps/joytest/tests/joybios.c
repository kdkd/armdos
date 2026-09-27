/* JOYBIOS.EXE - the BIOS's game adapter services, for apps/joytest/tests/run.mjs:
 * INT 11h bit 12, INT 15h AH=84h DX=0 (switches), DX=1 (resistive inputs), DX=2 (error).
 * JOYBIOS [n]: n readings (default 1). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>

int main(int argc, char **argv)
{
    union REGS r;
    int n = argc > 1 ? atoi(argv[1]) : 1;
    memset(&r, 0, sizeof r);
    int86(0x11, &r, &r);
    printf("INT 11h: %04Xh, game adapter %s\n", r.x.ax & 0xFFFF, (r.x.ax & 0x1000) ? "yes" : "no");
    for (int i = 0; i < n; i++) {
        memset(&r, 0, sizeof r); r.x.ax = 0x8400; r.x.dx = 0;
        int86(0x15, &r, &r);
        printf("84h/0: CF=%d AL=%02Xh\n", r.x.cflag ? 1 : 0, r.h.al);
        memset(&r, 0, sizeof r); r.x.ax = 0x8400; r.x.dx = 1;
        int86(0x15, &r, &r);
        printf("84h/1: CF=%d AX=%u BX=%u CX=%u DX=%u\n", r.x.cflag ? 1 : 0, r.x.ax & 0xFFFF, r.x.bx & 0xFFFF, r.x.cx & 0xFFFF, r.x.dx & 0xFFFF);
    }
    memset(&r, 0, sizeof r); r.x.ax = 0x8400; r.x.dx = 2;
    int86(0x15, &r, &r);
    printf("84h/2: CF=%d AH=%02Xh\n", r.x.cflag ? 1 : 0, r.h.ah);
    return 0;
}
