/* UPCASE.COM: copy stdin to stdout with a-z upper-cased (a pipe filter;
 * twin of tests/ref/x86/upcase.asm) */
#include <dos.h>

int main(void)
{
    static unsigned char buf[128];
    for (;;) {
        union REGS r;
        r.x.ax = 0x3F00;
        r.x.bx = 0;
        r.x.cx = sizeof buf;
        r.x.dx = (unsigned)buf;
        intdos(&r, &r);
        if (r.x.cflag || !(r.x.ax & 0xFFFF)) break;
        unsigned n = r.x.ax & 0xFFFF;
        for (unsigned i = 0; i < n; i++)
            if (buf[i] >= 'a' && buf[i] <= 'z') buf[i] -= 32;
        r.x.ax = 0x4000;
        r.x.bx = 1;
        r.x.cx = n;
        r.x.dx = (unsigned)buf;
        intdos(&r, &r);
    }
    return 0;
}
