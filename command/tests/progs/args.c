/* ARGS.COM: show the command tail and the two FCBs COMMAND parsed (twin of
 * tests/ref/x86/args.asm):  [tail]  / 1:d NAMEEXT / 2:d NAMEEXT */
#include <dos.h>
#include <string.h>

static void wr(const void *p, unsigned n)
{
    union REGS r;
    r.x.ax = 0x4000;
    r.x.bx = 1;
    r.x.cx = n;
    r.x.dx = (unsigned)p;
    intdos(&r, &r);
}

static void fcb(char which, const unsigned char *f)
{
    char line[20];
    line[0] = which;
    line[1] = ':';
    line[2] = '0' + f[0];
    line[3] = ' ';
    memcpy(line + 4, f + 1, 11);
    line[15] = '\r';
    line[16] = '\n';
    wr(line, 17);
}

int main(void)
{
    const unsigned char *psp = (const unsigned char *)_armdos_psp;
    wr("[", 1);
    wr(psp + 0x81, psp[0x80]);
    wr("]\r\n", 3);
    fcb('1', psp + 0x5C);
    fcb('2', psp + 0x6C);
    return 0;
}
