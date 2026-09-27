/* WAITKEY.COM: print "waiting", wait for a key (INT 21h 08h, so ^C is
 * seen), exit with its code (twin of ref/x86/waitkey.asm) */
#include <dos.h>

int main(void)
{
    union REGS r;
    r.x.ax = 0x0900;
    r.x.dx = (unsigned)"waiting\r\n$";
    intdos(&r, &r);
    r.x.ax = 0x0800;
    intdos(&r, &r);
    return r.h.al;
}
