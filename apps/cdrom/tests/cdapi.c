/*
 * CDAPI.EXE - exercises the CD-ROM extensions' INT 2Fh AX=15xxh API the way
 * 1993 programs did (tests/cdex.mjs reads its output):
 *
 *   CDAPI            the whole list below
 *   CDAPI PLAY n     play track n (1510h PLAY AUDIO) and exit
 *   CDAPI Q          print the Q channel and audio status once
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "armdos.h"
#include "cdrom.h"

static struct armregs r;
static int cf;
static void call(unsigned ax, unsigned bx, unsigned cx, unsigned dx, unsigned si, unsigned di)
{
    memset(&r, 0, sizeof r);
    r.r0 = ax; r.r1 = bx; r.r2 = cx; r.r3 = dx; r.r4 = si; r.r5 = di;
    cf = _armdos_int2f(&r);
}

static int drive;

static unsigned devreq(void *q)
{
    call(0x1510, (uint32_t)q, drive, 0, 0, 0);
    return ((struct cdreq *)q)->status;
}
static unsigned ioctl_in(uint8_t *cb, unsigned n)
{
    struct cdreq_ioctl q;
    memset(&q, 0, sizeof q);
    q.h.len = sizeof q; q.h.cmd = C_IOCTL_IN; q.buf = (uint32_t)cb; q.count = n;
    return devreq(&q);
}
static void qchan(void)
{
    uint8_t b[11] = { IOI_QCHAN };
    unsigned s = ioctl_in(b, 11);
    uint8_t a[11] = { IOI_AUDIOSTAT };
    ioctl_in(a, 11);
    printf("Q: status %04X busy %d track %02X index %d rel %02d:%02d.%02d abs %02d:%02d.%02d paused %d\n",
           s, (s & ST_BUSY) != 0, b[2], b[3], b[4], b[5], b[6], b[8], b[9], b[10], rd16le(a + 1) & 1);
}
static void play(int t)
{
    uint8_t b[7] = { IOI_TRACKINFO, (uint8_t)t };
    ioctl_in(b, 7);
    uint32_t start = rb_to_lba(rd32le(b + 2));
    uint8_t d[7] = { IOI_DISKINFO };
    ioctl_in(d, 7);
    uint32_t end = rb_to_lba(rd32le(d + 3));
    if (t < d[2]) { uint8_t n[7] = { IOI_TRACKINFO, (uint8_t)(t + 1) }; ioctl_in(n, 7); end = rb_to_lba(rd32le(n + 2)); }
    struct cdreq_play q;
    memset(&q, 0, sizeof q);
    q.h.len = sizeof q; q.h.cmd = C_PLAY; q.addrmode = 0; q.start = start; q.count = end - start;
    unsigned s = devreq(&q);
    printf("PLAY track %d: LBA %lu, %lu sectors, status %04X\n", t, (unsigned long)start, (unsigned long)(end - start), s);
}

int main(int argc, char **argv)
{
    call(0x1500, 0, 0, 0, 0, 0);
    printf("1500h: BX=%u CX=%u (%c:)\n", (unsigned)(r.r1 & 0xFFFF), (unsigned)(r.r2 & 0xFFFF), (int)('A' + (r.r2 & 0xFFFF)));
    if (!(r.r1 & 0xFFFF)) return 1;
    drive = r.r2 & 0xFFFF;
    if (argc > 2 && !strcmp(argv[1], "PLAY")) { play(atoi(argv[2])); return 0; }
    if (argc > 1 && !strcmp(argv[1], "Q")) { qchan(); return 0; }

    call(0x150C, 0, 0, 0, 0, 0);
    printf("150Ch: version %u.%02u\n", (r.r1 >> 8) & 0xFF, r.r1 & 0xFF);
    call(0x150B, 0, drive, 0, 0, 0);
    printf("150Bh: AX=%04X BX=%04X\n", r.r0 & 0xFFFF, r.r1 & 0xFFFF);
    call(0x150B, 0, 2, 0, 0, 0);
    printf("150Bh C: AX=%04X\n", r.r0 & 0xFFFF);
    uint8_t letters[26] = { 0 };
    call(0x150D, (uint32_t)letters, 0, 0, 0, 0);
    printf("150Dh: %c:\n", 'A' + letters[0]);
    uint8_t list[5 * 8] = { 0 };
    call(0x1501, (uint32_t)list, 0, 0, 0, 0);
    struct cddevhdr *dev = (struct cddevhdr *)rd32le(list + 1);
    printf("1501h: unit %u, device %.8s\n", list[0], dev->name);
    char name[40];
    static const char *const what[3] = { "copyright", "abstract", "bibliographic" };
    for (int i = 0; i < 3; i++) {
        memset(name, 0, sizeof name);
        call(0x1502 + i, (uint32_t)name, drive, 0, 0, 0);
        printf("%04Xh: %s file \"%s\"%s\n", 0x1502 + i, what[i], name, cf ? " (CF)" : "");
    }
    static uint8_t sec[2048 * 2];
    call(0x1505, (uint32_t)sec, drive, 0, 0, 0);
    printf("1505h: AX=%u type %u \"%.5s\" volume \"%.9s\"\n", r.r0 & 0xFFFF, sec[0], sec + 1, sec + 40);
    call(0x1505, (uint32_t)sec, drive, 1, 0, 0);
    printf("1505h DX=1: AX=%u type %u\n", r.r0 & 0xFFFF, sec[0]);
    call(0x1508, (uint32_t)sec, drive, 2, 0, 16);
    printf("1508h: %s \"%.5s\" \"%.5s\"\n", cf ? "CF" : "ok", sec + 1, sec + 2048 + 1);
    call(0x1509, (uint32_t)sec, drive, 1, 0, 16);
    printf("1509h: %s AX=%u\n", cf ? "CF" : "ok", r.r0 & 0xFFFF);
    uint8_t de[256] = { 0 };
    call(0x150F, (uint32_t)"\\README.TXT", drive, 0, 0, (uint32_t)de);
    printf("150Fh: %s AX=%u len %u extent %lu size %lu flags %02X name %.*s\n", cf ? "CF" : "ok", r.r0 & 0xFFFF, de[0],
           (unsigned long)rd32le(de + 2), (unsigned long)rd32le(de + 10), de[25], de[32], de + 33);
    call(0x150F, (uint32_t)"\\ZORK\\ZORK1.DAT", drive, 0, 0, (uint32_t)de);
    printf("150Fh ZORK: %s size %lu name %.*s\n", cf ? "CF" : "ok", (unsigned long)rd32le(de + 10), de[32], de + 33);
    call(0x150F, (uint32_t)"\\NOFILE.TXT", drive, 0, 0, (uint32_t)de);
    printf("150Fh missing: %s AX=%u\n", cf ? "CF" : "ok", r.r0 & 0xFFFF);
    call(0x150E, 0, drive, 0, 0, 0);
    printf("150Eh get: %s DX=%04X\n", cf ? "CF" : "ok", r.r3 & 0xFFFF);

    /* the device driver through 1510h */
    uint8_t b[11];
    memset(b, 0, sizeof b); b[0] = IOI_DEVSTAT;
    printf("IOCTL 6: status %04X", ioctl_in(b, 5));
    printf(" device status %08lX\n", (unsigned long)rd32le(b + 1));
    memset(b, 0, sizeof b); b[0] = IOI_SECTSIZE;
    ioctl_in(b, 4); printf("IOCTL 7: %u\n", rd16le(b + 2));
    memset(b, 0, sizeof b); b[0] = IOI_VOLSIZE;
    ioctl_in(b, 5); printf("IOCTL 8: %lu sectors\n", (unsigned long)rd32le(b + 1));
    memset(b, 0, sizeof b); b[0] = IOI_DISKINFO;
    ioctl_in(b, 7);
    uint32_t lo = rd32le(b + 3);
    printf("IOCTL 10: tracks %u-%u lead-out %02lu:%02lu.%02lu\n", b[1], b[2],
           (unsigned long)(lo >> 16 & 255), (unsigned long)(lo >> 8 & 255), (unsigned long)(lo & 255));
    int last = b[2];
    for (int t = 1; t <= last; t++) {
        memset(b, 0, sizeof b); b[0] = IOI_TRACKINFO; b[1] = t;
        ioctl_in(b, 7);
        uint32_t a = rd32le(b + 2);
        printf("IOCTL 11: track %d at %02lu:%02lu.%02lu ctl %02X\n", t,
               (unsigned long)(a >> 16 & 255), (unsigned long)(a >> 8 & 255), (unsigned long)(a & 255), b[6]);
    }
    memset(b, 0, sizeof b); b[0] = IOI_UPC;
    unsigned s = ioctl_in(b, 11);
    printf("IOCTL 14: status %04X UPC %02X%02X%02X%02X%02X%02X%02X\n", s, b[2], b[3], b[4], b[5], b[6], b[7], b[8]);
    memset(b, 0, sizeof b); b[0] = IOI_AUDIOCHAN;
    ioctl_in(b, 9);
    printf("IOCTL 4: in0 %u vol0 %u in1 %u vol1 %u\n", b[1], b[2], b[3], b[4]);
    qchan();
    return 0;
}
