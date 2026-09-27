/* dos.c - dos.h: int86 family, vectors and the _dos_* functions. */
#include <errno.h>
#include <string.h>
#include <dos.h>
#include "libdos.h"

static void regs_in(struct armregs *r, const union REGS *in)
{
    memset(r, 0, sizeof *r);
    r->r0 = in->x.ax; r->r1 = in->x.bx; r->r2 = in->x.cx;
    r->r3 = in->x.dx; r->r4 = in->x.si; r->r5 = in->x.di;
}

static void regs_out(union REGS *out, const struct armregs *r, int carry)
{
    out->x.ax = r->r0; out->x.bx = r->r1; out->x.cx = r->r2;
    out->x.dx = r->r3; out->x.si = r->r4; out->x.di = r->r5;
    out->x.cflag = carry;
    out->x.flags = r->cpsr;
}

int int86(int intno, union REGS *in, union REGS *out)
{
    struct armregs r;
    int c;
    regs_in(&r, in);
    c = intno == 0x21 ? _armdos_int21(&r) : _armdos_intr(intno, &r);
    regs_out(out, &r, c);
    if (c) errno = _armdos_errno(r.r0);
    return (int)r.r0;
}

int int86x(int intno, union REGS *in, union REGS *out, struct SREGS *seg)
{
    struct armregs r;
    int c;
    regs_in(&r, in);
    r.r7 = seg->ds;
    r.r8 = seg->es;
    c = intno == 0x21 ? _armdos_int21(&r) : _armdos_intr(intno, &r);
    regs_out(out, &r, c);
    seg->ds = r.r7;
    seg->es = r.r8;
    if (c) errno = _armdos_errno(r.r0);
    return (int)r.r0;
}

int intdos(union REGS *in, union REGS *out)
{
    struct armregs r;
    int c;
    regs_in(&r, in);
    c = _armdos_int21(&r);
    regs_out(out, &r, c);
    if (c) errno = _armdos_errno(r.r0);
    return (int)r.r0;
}

int intdosx(union REGS *in, union REGS *out, struct SREGS *seg)
{
    return int86x(0x21, in, out, seg);
}

int bdos(int func, unsigned int dx, unsigned int al)
{
    struct armregs r = {0};
    r.r0 = ((func & 0xFF) << 8) | (al & 0xFF);
    r.r3 = dx;
    _armdos_int21(&r);
    return (int)r.r0;
}

void segread(struct SREGS *seg) { memset(seg, 0, sizeof *seg); }

/* ---------------------------------------------------------- vectors --- */

armdos_vect_t armdos_getvect(int intno)
{
    struct armregs r = {0};
    r.r0 = 0x3500 | (intno & 0xFF);
    _armdos_int21(&r);
    return (armdos_vect_t)r.r1;         /* ES:BX -> BX flat, ES = 0 */
}

void armdos_setvect(int intno, armdos_vect_t h)
{
    struct armregs r = {0};
    r.r0 = 0x2500 | (intno & 0xFF);
    r.r3 = (uint32_t)h;                 /* DS:DX */
    _armdos_int21(&r);
}

armdos_vect_t _dos_getvect(unsigned n) { return armdos_getvect((int)n); }
void _dos_setvect(unsigned n, armdos_vect_t h) { armdos_setvect((int)n, h); }

/* ------------------------------------------------------------ _dos_* -- */

/* INT 21h: returns 0 or the DOS error code (and sets errno). */
static unsigned d21(struct armregs *r)
{
    if (_armdos_int21(r)) {
        errno = _armdos_errno(r->r0);
        return r->r0 & 0xFFFF;
    }
    return 0;
}

static unsigned find(unsigned ax, const char *path, unsigned attrib, struct find_t *f)
{
    struct armregs r = {0};
    uint32_t old;
    unsigned e;
    r.r0 = 0x2F00; _armdos_int21(&r); old = r.r1;               /* get DTA */
    memset(&r, 0, sizeof r); r.r0 = 0x1A00; r.r3 = (uint32_t)f; _armdos_int21(&r);
    memset(&r, 0, sizeof r);
    r.r0 = ax; r.r2 = attrib; r.r3 = (uint32_t)path;
    e = d21(&r);
    memset(&r, 0, sizeof r); r.r0 = 0x1A00; r.r3 = old; _armdos_int21(&r);
    return e;
}

unsigned _dos_findfirst(const char *path, unsigned attrib, struct find_t *f)
{ return find(0x4E00, path, attrib, f); }

unsigned _dos_findnext(struct find_t *f)
{ return find(0x4F00, 0, 0, f); }

void _dos_getdate(struct dosdate_t *d)
{
    struct armregs r = {0};
    r.r0 = 0x2A00;
    _armdos_int21(&r);
    d->year = r.r2 & 0xFFFF;
    d->month = (r.r3 >> 8) & 0xFF;
    d->day = r.r3 & 0xFF;
    d->dayofweek = r.r0 & 0xFF;
}

unsigned _dos_setdate(struct dosdate_t *d)
{
    struct armregs r = {0};
    r.r0 = 0x2B00;
    r.r2 = d->year;
    r.r3 = (d->month << 8) | d->day;
    _armdos_int21(&r);
    if ((r.r0 & 0xFF) == 0xFF) { errno = EINVAL; return 1; }
    return 0;
}

void _dos_gettime(struct dostime_t *t)
{
    struct armregs r = {0};
    r.r0 = 0x2C00;
    _armdos_int21(&r);
    t->hour = (r.r2 >> 8) & 0xFF;
    t->minute = r.r2 & 0xFF;
    t->second = (r.r3 >> 8) & 0xFF;
    t->hsecond = r.r3 & 0xFF;
}

unsigned _dos_settime(struct dostime_t *t)
{
    struct armregs r = {0};
    r.r0 = 0x2D00;
    r.r2 = (t->hour << 8) | t->minute;
    r.r3 = (t->second << 8) | t->hsecond;
    _armdos_int21(&r);
    if ((r.r0 & 0xFF) == 0xFF) { errno = EINVAL; return 1; }
    return 0;
}

void _dos_getdrive(unsigned *drive)
{
    struct armregs r = {0};
    r.r0 = 0x1900;
    _armdos_int21(&r);
    *drive = (r.r0 & 0xFF) + 1;
}

void _dos_setdrive(unsigned drive, unsigned *ndrives)
{
    struct armregs r = {0};
    r.r0 = 0x0E00;
    r.r3 = (drive - 1) & 0xFF;
    _armdos_int21(&r);
    if (ndrives) *ndrives = r.r0 & 0xFF;
}

unsigned _dos_getdiskfree(unsigned drive, struct diskfree_t *df)
{
    struct armregs r = {0};
    r.r0 = 0x3600;
    r.r3 = drive;
    _armdos_int21(&r);
    if ((r.r0 & 0xFFFF) == 0xFFFF) { errno = ENODEV; return 0x0F; }
    df->sectors_per_cluster = r.r0 & 0xFFFF;
    df->avail_clusters = r.r1 & 0xFFFF;
    df->bytes_per_sector = r.r2 & 0xFFFF;
    df->total_clusters = r.r3 & 0xFFFF;
    return 0;
}

unsigned _dos_open(const char *path, unsigned mode, int *handle)
{
    struct armregs r = {0};
    unsigned e;
    r.r0 = 0x3D00 | (mode & 0xFF);
    r.r3 = (uint32_t)path;
    if (!(e = d21(&r))) *handle = r.r0 & 0xFFFF;
    return e;
}

static unsigned creat3(unsigned ah, const char *path, unsigned attrib, int *handle)
{
    struct armregs r = {0};
    unsigned e;
    r.r0 = ah << 8;
    r.r2 = attrib;
    r.r3 = (uint32_t)path;
    if (!(e = d21(&r))) *handle = r.r0 & 0xFFFF;
    return e;
}
unsigned _dos_creat(const char *p, unsigned a, int *h) { return creat3(0x3C, p, a, h); }
unsigned _dos_creatnew(const char *p, unsigned a, int *h) { return creat3(0x5B, p, a, h); }

unsigned _dos_close(int handle)
{
    struct armregs r = {0};
    r.r0 = 0x3E00;
    r.r1 = handle;
    return d21(&r);
}

unsigned _dos_read(int handle, void *buf, unsigned count, unsigned *nread)
{
    struct armregs r = {0};
    unsigned e;
    r.r0 = 0x3F00; r.r1 = handle; r.r2 = count; r.r3 = (uint32_t)buf;
    if (!(e = d21(&r))) *nread = r.r0 & 0xFFFF;
    return e;
}

unsigned _dos_write(int handle, const void *buf, unsigned count, unsigned *nwritten)
{
    struct armregs r = {0};
    unsigned e;
    r.r0 = 0x4000; r.r1 = handle; r.r2 = count; r.r3 = (uint32_t)buf;
    if (!(e = d21(&r))) *nwritten = r.r0 & 0xFFFF;
    return e;
}

unsigned _dos_getfileattr(const char *path, unsigned *attrib)
{
    struct armregs r = {0};
    unsigned e;
    r.r0 = 0x4300; r.r3 = (uint32_t)path;
    if (!(e = d21(&r))) *attrib = r.r2 & 0xFFFF;
    return e;
}

unsigned _dos_setfileattr(const char *path, unsigned attrib)
{
    struct armregs r = {0};
    r.r0 = 0x4301; r.r2 = attrib; r.r3 = (uint32_t)path;
    return d21(&r);
}

unsigned _dos_getftime(int handle, unsigned *date, unsigned *time)
{
    struct armregs r = {0};
    unsigned e;
    r.r0 = 0x5700; r.r1 = handle;
    if (!(e = d21(&r))) { *date = r.r3 & 0xFFFF; *time = r.r2 & 0xFFFF; }
    return e;
}

unsigned _dos_setftime(int handle, unsigned date, unsigned time)
{
    struct armregs r = {0};
    r.r0 = 0x5701; r.r1 = handle; r.r2 = time; r.r3 = date;
    return d21(&r);
}

void _dos_keep(unsigned status, unsigned paragraphs)
{
    struct armregs r = {0};
    r.r0 = 0x3100 | (status & 0xFF);
    r.r3 = paragraphs;
    _armdos_int21(&r);
}

unsigned _dos_allocmem(unsigned paragraphs, unsigned *seg)
{
    struct armregs r = {0};
    unsigned e;
    r.r0 = 0x4800; r.r1 = paragraphs;
    e = d21(&r);
    *seg = e ? (r.r1 & 0xFFFF) : (r.r0 & 0xFFFF);   /* on failure: largest block */
    return e;
}

unsigned _dos_freemem(unsigned seg)
{
    struct armregs r = {0};
    r.r0 = 0x4900; r.r8 = seg;              /* ES = segment */
    return d21(&r);
}

unsigned _dos_setblock(unsigned paragraphs, unsigned seg, unsigned *maxsize)
{
    struct armregs r = {0};
    unsigned e;
    r.r0 = 0x4A00; r.r1 = paragraphs; r.r8 = seg;
    if ((e = d21(&r)) && maxsize) *maxsize = r.r1 & 0xFFFF;
    return e;
}

int dosexterr(struct DOSERROR *e)
{
    struct armregs r = {0};
    r.r0 = 0x5900;
    _armdos_int21(&r);
    if (e) {
        e->exterror = r.r0 & 0xFFFF;
        e->eclass = (r.r1 >> 8) & 0xFF;
        e->action = r.r1 & 0xFF;
        e->locus = (r.r2 >> 8) & 0xFF;
    }
    return r.r0 & 0xFFFF;
}
