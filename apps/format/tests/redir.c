/*
 * REDIR.EXE - test helper: run a program with stdin and/or stdout redirected.
 *     REDIR out|- in|- program [arguments]
 * (the kernel's test shell cannot take both "<" and ">" on one line)
 */
#include <string.h>
#include "armdos.h"

static int dos(struct armregs *r) { return _armdos_int21(r); }

static int redirect(int h, const char *name, int create)
{
    struct armregs r = { 0 };
    r.r0 = create ? 0x3C00 : 0x3D00; r.r3 = (uint32_t)name;
    if (dos(&r)) return -1;
    int fh = r.r0 & 0xFFFF;
    struct armregs d = { 0 };
    d.r0 = 0x4600; d.r1 = fh; d.r2 = h;
    dos(&d);
    memset(&d, 0, sizeof d);
    d.r0 = 0x3E00; d.r1 = fh;
    dos(&d);
    return 0;
}

int main(int argc, char **argv)
{
    static uint8_t tail[130], fcb1[20], fcb2[20];
    static struct { uint16_t env; uint32_t tail, fcb1, fcb2; } __attribute__((packed)) pb;
    if (argc < 4) return 99;
    if (strcmp(argv[1], "-")) redirect(1, argv[1], 1);
    if (strcmp(argv[2], "-")) redirect(0, argv[2], 0);
    /* the tail: everything after the program name, with its leading blank */
    const uint8_t *t = _armdos_psp->cmdtail;
    int len = t[0], i = 0, words = 0;
    const char *s = (const char *)t + 1;
    while (i < len && words < 3) {
        while (i < len && s[i] == ' ') i++;
        while (i < len && s[i] != ' ') i++;
        words++;
    }
    int n = len - i;
    tail[0] = n;
    memcpy(tail + 1, s + i, n);
    tail[n + 1] = '\r';
    struct armregs r = { 0 };
    const char *rest;
    memset(fcb1, 0, sizeof fcb1); memset(fcb1 + 1, ' ', 11);
    memset(fcb2, 0, sizeof fcb2); memset(fcb2 + 1, ' ', 11);
    r.r0 = 0x2901; r.r4 = (uint32_t)(tail + 1); r.r5 = (uint32_t)fcb1; dos(&r); rest = (const char *)r.r4;
    memset(&r, 0, sizeof r);
    r.r0 = 0x2901; r.r4 = (uint32_t)rest; r.r5 = (uint32_t)fcb2; dos(&r);
    pb.env = 0; pb.tail = (uint32_t)tail; pb.fcb1 = (uint32_t)fcb1; pb.fcb2 = (uint32_t)fcb2;
    memset(&r, 0, sizeof r);
    r.r0 = 0x4B00; r.r1 = (uint32_t)&pb; r.r3 = (uint32_t)argv[3];
    if (dos(&r)) return 98;
    memset(&r, 0, sizeof r);
    r.r0 = 0x4D00;
    dos(&r);
    return r.r0 & 0xFF;
}
