/*
 * dos.c - thin INT 21h wrappers (ARCH.md 5: pointers are flat, in the
 * offset register; segment values in r7/r8).
 */
#include "cmd.h"

int int21(REGS *r) { return _armdos_int21(r); }
int int21_raw(REGS *r) { return _armdos_int21(r); }
int intr(int n, REGS *r) { return _armdos_intr(n, r); }

static int call(unsigned ax, REGS *r)
{
    r->r0 = ax;
    return _armdos_int21(r);
}

unsigned dos_error(void)
{
    REGS r = {0};
    r.r1 = 0;
    call(0x5900, &r);
    return r.r0 & 0xFFFF;
}

int dos_getdrv(void)
{
    REGS r = {0};
    call(0x1900, &r);
    return r.r0 & 0xFF;
}

int dos_setdrv(int d)
{
    REGS r = {0};
    r.r3 = d;
    call(0x0E00, &r);
    return r.r0 & 0xFF;
}

int dos_curdir(int drive1, char *buf)
{
    REGS r = {0};
    r.r3 = drive1;
    r.r4 = (uint32_t)buf;
    if (call(0x4700, &r)) return r.r0 & 0xFFFF;
    return 0;
}

static int pathcall(unsigned ax, const char *p)
{
    REGS r = {0};
    r.r3 = (uint32_t)p;
    if (call(ax, &r)) return r.r0 & 0xFFFF;
    return 0;
}

/* DOS 4's CHDIR fails for a name ending in a path separator (other than
 * "\" and "d:\"); checked here as well as in the kernel */
int dos_chdir(const char *p)
{
    size_t l = strlen(p);
    if (l > 1 && (p[l - 1] == '\\' || p[l - 1] == '/') && !(l == 3 && p[1] == ':')) {
        /* set the extended error as DOS would (5D0Ah: code, class/action,
         * locus: path not found, not found, user, disk) */
        static const uint16_t e[11] = { 3, (8 << 8) | 3, 2 << 8 };
        REGS r = {0};
        r.r0 = 0x5D0A;
        r.r3 = (uint32_t)e;
        int21(&r);
        return 3;
    }
    return pathcall(0x3B00, p);
}
int dos_mkdir(const char *p) { return pathcall(0x3900, p); }
int dos_rmdir(const char *p) { return pathcall(0x3A00, p); }
int dos_unlink(const char *p) { return pathcall(0x4100, p); }

int dos_open(const char *p, int mode)
{
    REGS r = {0};
    r.r3 = (uint32_t)p;
    if (call(0x3D00 | (mode & 0xFF), &r)) return -(int)(r.r0 & 0xFFFF);
    return r.r0 & 0xFFFF;
}

int dos_creat(const char *p, int attr)
{
    REGS r = {0};
    r.r2 = attr;
    r.r3 = (uint32_t)p;
    if (call(0x3C00, &r)) return -(int)(r.r0 & 0xFFFF);
    return r.r0 & 0xFFFF;
}

int dos_close(int h)
{
    REGS r = {0};
    r.r1 = h;
    if (call(0x3E00, &r)) return -(int)(r.r0 & 0xFFFF);
    return 0;
}

int dos_read(int h, void *buf, unsigned n)
{
    REGS r = {0};
    r.r1 = h;
    r.r2 = n;
    r.r3 = (uint32_t)buf;
    if (call(0x3F00, &r)) return -(int)(r.r0 & 0xFFFF);
    return r.r0 & 0xFFFF;
}

int dos_write(int h, const void *buf, unsigned n)
{
    REGS r = {0};
    r.r1 = h;
    r.r2 = n;
    r.r3 = (uint32_t)buf;
    if (call(0x4000, &r)) return -(int)(r.r0 & 0xFFFF);
    return r.r0 & 0xFFFF;
}

long dos_lseek(int h, long off, int whence)
{
    REGS r = {0};
    r.r1 = h;
    r.r2 = ((unsigned long)off >> 16) & 0xFFFF;
    r.r3 = (unsigned long)off & 0xFFFF;
    if (call(0x4200 | whence, &r)) return -(long)(r.r0 & 0xFFFF);
    return (long)(((r.r3 & 0xFFFF) << 16) | (r.r0 & 0xFFFF));
}

int dos_ioctl_info(int h)
{
    REGS r = {0};
    r.r1 = h;
    if (call(0x4400, &r)) return -1;
    return r.r3 & 0xFFFF;
}

int dos_ioctl_set(int h, unsigned info)
{
    REGS r = {0};
    r.r1 = h;
    r.r3 = info & 0xFF;
    if (call(0x4401, &r)) return -1;
    return 0;
}

int dos_dup(int h)
{
    REGS r = {0};
    r.r1 = h;
    if (call(0x4500, &r)) return -1;
    return r.r0 & 0xFFFF;
}

int dos_dup2(int h, int h2)
{
    REGS r = {0};
    r.r1 = h;
    r.r2 = h2;
    if (call(0x4600, &r)) return -1;
    return 0;
}

void dos_setdta(void *p)
{
    REGS r = {0};
    r.r3 = (uint32_t)p;
    call(0x1A00, &r);
}

int dos_findfirst(const char *p, int attr)
{
    REGS r = {0};
    r.r2 = attr;
    r.r3 = (uint32_t)p;
    if (call(0x4E00, &r)) return r.r0 & 0xFFFF;
    return 0;
}

int dos_findnext(void)
{
    REGS r = {0};
    if (call(0x4F00, &r)) return r.r0 & 0xFFFF;
    return 0;
}

int dos_fcb_parse(const char **s, uint8_t *fcb, int al)
{
    REGS r = {0};
    r.r4 = (uint32_t)*s;
    r.r5 = (uint32_t)fcb;
    call(0x2900 | (al & 0xFF), &r);
    *s = (const char *)r.r4;
    return r.r0 & 0xFF;
}

int dos_truename(const char *in, char *out)
{
    REGS r = {0};
    r.r4 = (uint32_t)in;
    r.r5 = (uint32_t)out;
    if (call(0x6000, &r)) return r.r0 & 0xFFFF;
    return 0;
}

unsigned dos_alloc(unsigned paras, unsigned *largest)
{
    REGS r = {0};
    r.r1 = paras;
    if (call(0x4800, &r)) {
        if (largest) *largest = r.r1 & 0xFFFF;
        return 0;
    }
    return r.r0 & 0xFFFF;
}

int dos_free(unsigned seg)
{
    REGS r = {0};
    r.r8 = seg;
    if (call(0x4900, &r)) return r.r0 & 0xFFFF;
    return 0;
}

int dos_setblock(unsigned seg, unsigned paras, unsigned *max)
{
    REGS r = {0};
    r.r1 = paras;
    r.r8 = seg;
    if (call(0x4A00, &r)) {
        if (max) *max = r.r1 & 0xFFFF;
        return r.r0 & 0xFFFF;
    }
    return 0;
}

void dos_putc(int c)
{
    REGS r = {0};
    r.r3 = c & 0xFF;
    call(0x0200, &r);
}

int dos_getc_echo_flush(void)
{
    REGS r = {0};
    call(0x0C01, &r);
    return r.r0 & 0xFF;
}

int dos_getc_noecho_flush(void)
{
    REGS r = {0};
    call(0x0C08, &r);
    return r.r0 & 0xFF;
}

void dos_flush_kbd(void)
{
    REGS r = {0};
    call(0x0C00, &r);
}

void dos_bufinput(uint8_t *buf)
{
    REGS r = {0};
    r.r3 = (uint32_t)buf;
    call(0x0A00, &r);
}

void dos_bufinput_flush(uint8_t *buf)
{
    REGS r = {0};
    r.r3 = (uint32_t)buf;
    call(0x0C0A, &r);
}

int dos_yesno(int c)
{
    REGS r = {0};
    r.r3 = c & 0xFF;
    if (call(0x6523, &r)) {
        /* fall back if the kernel lacks 6523h */
        c = upconv(c);
        return c == 'Y' ? 1 : c == 'N' ? 0 : 2;
    }
    return r.r0 & 0xFFFF;
}

int dos_upcase(int c)
{
    return upconv(c);
}

void dos_getdate(int *y, int *m, int *d, int *wd)
{
    REGS r = {0};
    call(0x2A00, &r);
    *y = r.r2 & 0xFFFF;
    *m = (r.r3 >> 8) & 0xFF;
    *d = r.r3 & 0xFF;
    if (wd) *wd = r.r0 & 0xFF;
}

void dos_gettime(int *h, int *m, int *s, int *hs)
{
    REGS r = {0};
    call(0x2C00, &r);
    *h = (r.r2 >> 8) & 0xFF;
    *m = r.r2 & 0xFF;
    *s = (r.r3 >> 8) & 0xFF;
    *hs = r.r3 & 0xFF;
}

int dos_setdate(int y, int m, int d)
{
    REGS r = {0};
    r.r2 = y;
    r.r3 = (m << 8) | d;
    call(0x2B00, &r);
    return r.r0 & 0xFF;
}

int dos_settime(int h, int m, int s, int hs)
{
    REGS r = {0};
    r.r2 = (h << 8) | m;
    r.r3 = (s << 8) | hs;
    call(0x2D00, &r);
    return r.r0 & 0xFF;
}

int dos_country(uint8_t *buf34)
{
    REGS r = {0};
    r.r3 = (uint32_t)buf34;
    if (call(0x3800, &r)) return -1;
    return 0;
}

uint16_t dos_psp(void)
{
    REGS r = {0};
    call(0x6200, &r);
    return r.r1 & 0xFFFF;
}

void dos_setvect(int n, void *h)
{
    REGS r = {0};
    r.r3 = (uint32_t)h;
    call(0x2500 | n, &r);
}

void *dos_getvect(int n)
{
    REGS r = {0};
    call(0x3500 | n, &r);
    return (void *)r.r1;
}

size_t xstrlcpy(char *d, const char *s, size_t n)
{
    size_t l = strlen(s);
    if (n) {
        size_t c = l < n - 1 ? l : n - 1;
        memcpy(d, s, c);
        d[c] = 0;
    }
    return l;
}
