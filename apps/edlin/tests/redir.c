/*
 * EDREDIR.EXE - test helper: run a program with STDIN and STDOUT redirected.
 *     EDREDIR infile outfile program [arguments...]
 * (Kept for the tests; the kernel's test shell can redirect both now.)
 */
#include <stdio.h>
#include <string.h>
#include <process.h>
#include "armdos.h"

static int dos(struct armregs *r) { return _armdos_int21(r); }

static int force(int fh, int h)
{
    struct armregs r = { 0 };
    r.r0 = 0x4600; r.r1 = fh; r.r2 = h;
    if (dos(&r)) return -1;
    memset(&r, 0, sizeof r);
    r.r0 = 0x3E00; r.r1 = fh;
    dos(&r);
    return 0;
}

int main(int argc, char **argv)
{
    struct armregs r = { 0 };
    if (argc < 4) { printf("usage: EDREDIR in out program [args]\n"); return 1; }
    r.r0 = 0x3D00; r.r3 = (uint32_t)argv[1];
    if (dos(&r)) { printf("EDREDIR: cannot open %s\n", argv[1]); return 1; }
    int in = r.r0 & 0xFFFF;
    memset(&r, 0, sizeof r);
    r.r0 = 0x3C00; r.r3 = (uint32_t)argv[2];
    if (dos(&r)) { printf("EDREDIR: cannot create %s\n", argv[2]); return 1; }
    int out = r.r0 & 0xFFFF;
    fflush(stdout);
    force(in, 0);
    force(out, 1);
    int rc = spawnv(P_WAIT, argv[3], argv + 3);
    return rc < 0 ? 255 : rc;
}
