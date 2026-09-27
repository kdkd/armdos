/* acat.c - ACAT [/R] file: copy a file to standard output with INT 21h
   AH=40h, as TYPE does (/R: put the console in raw mode first, so the
   driver gets whole buffers through its WRITE command instead of INT 29h) */
#include <string.h>
#include "armdos.h"

int main(int argc, char **argv)
{
    int raw = argc > 2 && !strcmp(argv[1], "/R");
    const char *name = argv[argc - 1];
    struct armregs r = { 0 };
    if (raw) {
        r.r0 = 0x4400; r.r1 = 1;
        _armdos_int21(&r);
        unsigned dx = (r.r3 & 0xFF) | 0x20;
        memset(&r, 0, sizeof r);
        r.r0 = 0x4401; r.r1 = 1; r.r3 = dx;
        _armdos_int21(&r);
    }
    memset(&r, 0, sizeof r);
    r.r0 = 0x3D00; r.r3 = (uint32_t)name;
    if (_armdos_int21(&r)) return 2;
    int h = r.r0 & 0xFFFF;
    static char buf[512];
    for (;;) {
        memset(&r, 0, sizeof r);
        r.r0 = 0x3F00; r.r1 = h; r.r2 = sizeof buf; r.r3 = (uint32_t)buf;
        if (_armdos_int21(&r) || !(r.r0 & 0xFFFF)) break;
        unsigned n = r.r0 & 0xFFFF;
        memset(&r, 0, sizeof r);
        r.r0 = 0x4000; r.r1 = 1; r.r2 = n; r.r3 = (uint32_t)buf;
        _armdos_int21(&r);
    }
    if (raw) {
        memset(&r, 0, sizeof r);
        r.r0 = 0x4400; r.r1 = 1;
        _armdos_int21(&r);
        unsigned dx = r.r3 & 0xDF;
        memset(&r, 0, sizeof r);
        r.r0 = 0x4401; r.r1 = 1; r.r3 = dx;
        _armdos_int21(&r);
    }
    return 0;
}
