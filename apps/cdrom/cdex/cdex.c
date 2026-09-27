/*
 * ARMCDEX.EXE - ARM CD-ROM Extensions, Version 2.21.
 *
 *   ARMCDEX /D:devname [/D:devname ...] [/L:letter] [/M:buffers] [/V] [/E] [/S] [/K]
 *
 * A re-creation, for ARM-DOS, of what the 1990s CD-ROM extensions did (no
 * Microsoft code): it finds the CD-ROM device drivers named with /D (ARMCD.SYS's
 * ARMCD001), gives each drive unit a drive letter (the first free one, or from
 * /L), and stays resident:
 *
 *  * INT 2Fh AH=15h, the CD-ROM extensions API: 1500h installation check (BX =
 *    number of CD-ROM drive letters, CX = the first), 1501h drive device list,
 *    1502h-1504h copyright / abstract / bibliographic file names, 1505h read
 *    volume descriptor, 1508h absolute read, 1509h write (refused), 150Bh drive
 *    check (BX = ADADh), 150Ch version (BX = 0215h), 150Dh drive letters, 150Eh
 *    volume descriptor preference, 150Fh directory entry, 1510h send device request.
 *
 *  * the network-redirector interface, INT 2Fh AH=11h (README.md, "The kernel's redirector interface"):
 *    each CD drive is a CDS marked "network" (C000h), and the kernel passes every
 *    file-system call on it here, where an ISO 9660 (or High Sierra) file system is
 *    read through the device driver: DIR, TYPE, COPY, CD, running programs from the
 *    CD. Writes answer "access denied". "Drive not ready" goes through DOS's own
 *    critical-error handling (INT 2Fh AX=1206h) as the original's did.
 *
 * Freestanding (no C library) so that the resident part stays small: the code,
 * its variables and /M sector buffers of 2 KB (default 4).
 *
 * Copyright (C) 1986-1993 Europa Micro Systems (ARM-DOS project).
 */
#include <stdint.h>
#include <stddef.h>
#include "armdos.h"
#include "../inc/cdrom.h"

#define MAXDRV   8
#define MAGIC    0x58444341u            /* "ACDX": marks our SFTs (+37h) */

/* ------------------------------------------------------------- helpers */

void *memcpy(void *d, const void *s, size_t n) { uint8_t *p = d; const uint8_t *q = s; while (n--) *p++ = *q++; return d; }
void *memset(void *d, int c, size_t n) { uint8_t *p = d; while (n--) *p++ = c; return d; }
static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void clr(struct armregs *r) { memset(r, 0, sizeof *r); }
static int memcmp_(const void *a, const void *b, size_t n)
{
    const uint8_t *p = a, *q = b;
    for (; n; n--, p++, q++) if (*p != *q) return *p - *q;
    return 0;
}
static uint8_t up(uint8_t c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }

/* ------------------------------------------------------------- DOS structures */

struct sft {                            /* kernel/inc/kabi.h (DOS 4 SF.INC, 3Bh bytes) */
    uint16_t ref_count;                 /* 00 */
    uint16_t mode;                      /* 02 */
    uint8_t  attr;                      /* 04 */
    uint16_t flags;                     /* 05 */
    uint32_t devptr;                    /* 07 */
    uint16_t first_cluster;             /* 0B */
    uint16_t time;                      /* 0D */
    uint16_t date;                      /* 0F */
    uint32_t size;                      /* 11 */
    uint32_t position;                  /* 15 */
    uint16_t rel_cluster;               /* 19 */
    uint32_t dir_sector;                /* 1B ours: the file's extent (LBA) */
    uint8_t  dir_index;                 /* 1F */
    char     name[11];                  /* 20 */
    uint32_t share_prev;                /* 2B */
    uint16_t machine;                   /* 2F */
    uint16_t owner_psp;                 /* 31 */
    uint16_t mft;                       /* 33 */
    uint16_t last_cluster;              /* 35 */
    uint32_t ifs;                       /* 37 ours: MAGIC */
} __attribute__((packed));

struct cds {                            /* INC/CURDIR.INC, 58h bytes */
    char     path[67];                  /* 00 */
    uint16_t flags;                     /* 43 */
    uint32_t dpb;                       /* 45 */
    uint16_t cluster;                   /* 49 */
    uint16_t net1, net2;                /* 4B 4D */
    uint16_t bsoffset;                  /* 4F */
    uint8_t  type;                      /* 51 */
    uint32_t ifs;                       /* 52 */
    uint16_t fsda;                      /* 56 */
} __attribute__((packed));

/* ------------------------------------------------------------- resident state */

struct drive {
    uint8_t  letter;                    /* 0 = A: */
    uint8_t  unit;                      /* driver subunit */
    struct cddevhdr *dev;
    struct cds *cds;
    uint8_t  mounted, iso;              /* volume read; ISO 9660 (1) or High Sierra (0) */
    uint32_t pvd;                       /* LBA of the primary volume descriptor */
    uint32_t root, rootsize;            /* root directory extent */
    uint32_t volsize;
    uint8_t  rootdate[7];
    char     label[12];
};

static struct drive drv[MAXDRV];
static int ndrv;
static armdos_vect_t old2f;
static uint16_t vdpref = 0x0100;        /* 150Eh */
/* sector cache */
static uint8_t *cbuf;                   /* nbuf x 2048 */
static int nbuf;
static uint32_t ctag[32];               /* drive << 24 | LBA; 0xFFFFFFFF empty */
static uint32_t cage[32], clock_;
static int busy;
static uint32_t resident_bytes;

/* ------------------------------------------------------------- device calls */

static void devcall(struct drive *d, void *rq)
{
    struct cdreq *r = rq;
    r->unit = d->unit;
    r->status = 0;
    d->dev->strategy(r);
    d->dev->interrupt();
}

static unsigned dev_ioctl_in(struct drive *d, uint8_t *cb, unsigned n)
{
    struct cdreq_ioctl q;
    memset(&q, 0, sizeof q);
    q.h.len = sizeof q; q.h.cmd = C_IOCTL_IN; q.buf = (uint32_t)cb; q.count = n;
    devcall(d, &q);
    return q.h.status & ST_ERROR ? (q.h.status & 0xFF) | 0x100 : 0;
}

/* READ LONG cooked; 0 or the driver error code | 100h */
static unsigned dev_read(struct drive *d, uint32_t lba, unsigned n, void *buf)
{
    struct cdreq_read q;
    memset(&q, 0, sizeof q);
    q.h.len = sizeof q; q.h.cmd = C_READ_LONG; q.buf = (uint32_t)buf; q.count = n; q.start = lba;
    devcall(d, &q);
    return q.h.status & ST_ERROR ? (q.h.status & 0xFF) | 0x100 : 0;
}

static void cache_drop(struct drive *d)
{
    uint32_t id = (uint32_t)(d - drv) << 24;
    for (int i = 0; i < nbuf; i++) if ((ctag[i] & 0xFF000000u) == id) ctag[i] = 0xFFFFFFFFu;
}

/* a sector through the cache; NULL on error (*err = driver code | 100h) */
static uint8_t *sector(struct drive *d, uint32_t lba, unsigned *err)
{
    uint32_t tag = ((uint32_t)(d - drv) << 24) | (lba & 0xFFFFFF);
    int victim = 0;
    for (int i = 0; i < nbuf; i++) {
        if (ctag[i] == tag) { cage[i] = ++clock_; return cbuf + i * 2048; }
        if (cage[i] < cage[victim]) victim = i;
    }
    uint8_t *b = cbuf + victim * 2048;
    ctag[victim] = 0xFFFFFFFFu;
    unsigned e = dev_read(d, lba, 1, b);
    if (e) { *err = e; return 0; }
    ctag[victim] = tag; cage[victim] = ++clock_;
    return b;
}

/* ------------------------------------------------------------- errors */

#define DE_ACCESS   5
#define DE_NOFILE   2
#define DE_NOPATH   3
#define DE_NOMORE   0x12
#define DE_NOTREADY 0x15
#define DE_GENFAIL  0x1F
#define DE_FAIL24   0x53

/* a driver error during a file-system call: DOS's critical-error handling
   (INT 2Fh AX=1206h) - returns 1 = retry, else the DOS error to return */
static int crit(struct drive *d, unsigned e)
{
    unsigned code = e & 0xFF;
    struct armregs r;
    clr(&r);
    r.r0 = 0x1206;
    r.r1 = ((0x08 | 0x10 | 0x06) << 8) | d->letter;   /* disk, read, data area; fail + retry allowed */
    r.r5 = code;                        /* SI = 0: no device header, so the message names the drive */
    _armdos_int2f(&r);
    unsigned al = r.r0 & 0xFF;          /* DOS answers AL = 0 ignore, 1 retry, 3 fail (AH stays 12h) */
    if (!(r.cpsr & ARM_CPSR_C) && (al == 0 || al == 1 || al == 3)) {
        if (al == 1) return 1;
        if (al == 3) return -DE_FAIL24;
        if (al == 0) return -(code == E_NOTREADY ? DE_NOTREADY : DE_GENFAIL);
    }
    return -(code == E_NOTREADY ? DE_NOTREADY : code == E_DISKCHG ? 0x22 : DE_GENFAIL);
}

/* ------------------------------------------------------------- the volume */

static void trim_copy(char *out, const uint8_t *s, int n)
{
    int k = n;
    while (k > 0 && (s[k - 1] == ' ' || s[k - 1] == 0)) k--;
    for (int i = 0; i < k; i++) out[i] = s[i];
    out[k] = 0;
}

/* read the volume descriptors; 0 or -DOS error (after INT 24h) */
static int mount(struct drive *d)
{
    for (;;) {
        unsigned e = 0;
        d->mounted = 0;
        cache_drop(d);
        for (uint32_t lba = 16; lba < 32; lba++) {
            uint8_t *s = sector(d, lba, &e);
            if (!s) break;
            if (s[0] == 0xFF && !memcmp_(s + 1, "CD001", 5)) { e = 0x100 | E_MEDIA; break; }
            int iso = !memcmp_(s + 1, "CD001", 5), hsg = !memcmp_(s + 9, "CDROM", 5);
            if (!iso && !hsg) { e = 0x100 | E_MEDIA; break; }
            unsigned type = iso ? s[0] : s[8];
            if (type == 1) {
                const uint8_t *root = s + (iso ? 156 : 180);
                d->iso = iso;
                d->pvd = lba;
                d->root = rd32le(root + 2);
                d->rootsize = rd32le(root + 10);
                memcpy(d->rootdate, root + 18, 7);
                d->volsize = rd32le(s + (iso ? 80 : 88));
                const uint8_t *vid = s + (iso ? 40 : 48);
                int k = 0;
                for (int i = 0; i < 11 && vid[i] && vid[i] != ' '; i++) d->label[k++] = up(vid[i]);
                d->label[k] = 0;
                d->mounted = 1;
                return 0;
            }
            if (type == 0xFF) { e = 0x100 | E_MEDIA; break; }
        }
        if (!e) e = 0x100 | E_MEDIA;
        int r = crit(d, e);
        if (r == 1) continue;
        return r;
    }
}


/* media check before every file-system call */
static int check_media(struct drive *d)
{
    for (;;) {
        uint8_t cb[2] = { IOI_MEDIACHG, 0 };
        unsigned e = dev_ioctl_in(d, cb, 2);
        if (!e && (int8_t)cb[1] == 1 && d->mounted) return 0;
        if (!e && (int8_t)cb[1] == -1) d->mounted = 0;
        if (!e && cb[1] == 0) {             /* "don't know": is there a disc at all? */
            uint8_t st[5] = { IOI_DEVSTAT };
            dev_ioctl_in(d, st, 5);
            if (rd32le(st + 1) & DS_NODISC) { d->mounted = 0; e = 0x100 | E_NOTREADY; }
        }
        if (e) {
            d->mounted = 0;
            int r = crit(d, e);
            if (r == 1) continue;
            return r;
        }
        if (d->mounted) return 0;
        return mount(d);
    }
}

/* ------------------------------------------------------------- ISO 9660 directories */

struct found {
    uint32_t lba, size;
    uint8_t  attr;
    uint16_t time, date;
    char     name11[11];
    const uint8_t *rec;                 /* the directory record (in the cache) */
    unsigned reclen;
};

static void dos_stamp(const uint8_t *t, uint16_t *time, uint16_t *date)
{
    unsigned y = t[0] + 1900;
    if (y < 1980) y = 1980;
    *date = ((y - 1980) << 9) | ((t[1] & 15) << 5) | (t[2] & 31);
    *time = (t[3] << 11) | (t[4] << 5) | (t[5] >> 1);
}

static int fchar(uint8_t c)
{
    c = up(c);
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return c;
    switch (c) { case '_': case '$': case '~': case '!': case '#': case '%': case '&': case '-':
                 case '{': case '}': case '(': case ')': case '@': case '\'': case '`': case '^': return c; }
    return c >= 0x80 ? c : '_';
}

/* ISO identifier -> FCB-form 8.3 name */
static void iso_name11(const uint8_t *id, unsigned n, char *n11)
{
    memset(n11, ' ', 11);
    unsigned end = n;
    for (unsigned i = 0; i < n; i++) if (id[i] == ';') { end = i; break; }
    if (end && id[end - 1] == '.') end--;
    unsigned i = 0, k = 0;
    while (i < end && id[i] != '.') { if (k < 8) n11[k++] = fchar(id[i]); i++; }
    if (i < end && id[i] == '.') {
        i++; k = 8;
        while (i < end) { if (id[i] != '.' && k < 11) n11[k++] = fchar(id[i]); i++; }
    }
}

static void fcb_to_name(const char *n11, char *out)
{
    int k = 0, nl = 8, el = 11;
    while (nl > 0 && n11[nl - 1] == ' ') nl--;
    while (el > 8 && n11[el - 1] == ' ') el--;
    for (int i = 0; i < nl; i++) out[k++] = n11[i];
    if (el > 8) { out[k++] = '.'; for (int i = 8; i < el; i++) out[k++] = n11[i]; }
    out[k] = 0;
}

/* one path component -> FCB form; 1 if it has wildcards, -1 invalid */
static int comp11(const char *s, int len, char *n11)
{
    int wild = 0, i = 0, k = 0;
    memset(n11, ' ', 11);
    if (len == 1 && s[0] == '.') { n11[0] = '.'; return 0; }
    if (len == 2 && s[0] == '.' && s[1] == '.') { n11[0] = n11[1] = '.'; return 0; }
    while (i < len && s[i] != '.') {
        char c = up(s[i++]);
        if (c == '*') { while (k < 8) n11[k++] = '?'; wild = 1; continue; }
        if (c == '?') wild = 1;
        if (k < 8) n11[k++] = c;
    }
    if (i < len && s[i] == '.') {
        i++; k = 8;
        while (i < len) {
            char c = up(s[i++]);
            if (c == '*') { while (k < 11) n11[k++] = '?'; wild = 1; continue; }
            if (c == '?') wild = 1;
            if (k < 11) n11[k++] = c;
        }
    }
    return wild;
}

static int match11(const char *pat, const char *name)
{
    for (int i = 0; i < 11; i++) if (pat[i] != '?' && pat[i] != name[i]) return 0;
    return 1;
}

/*
 * Next directory record at byte offset *pos of the directory (lba, size) that
 * matches pat (FCB form, wildcards) and the search attributes. root: skip "." and
 * "..". Returns 1 found, 0 end, <0 -DOS error.
 */
static int dir_next(struct drive *d, uint32_t lba, uint32_t size, uint32_t *pos, const char *pat, int sattr, int root, struct found *f)
{
    unsigned e = 0;
    while (*pos < size) {
        uint32_t off = *pos;
        uint8_t *s = sector(d, lba + off / 2048, &e);
        if (!s) { int r = crit(d, e); if (r == 1) continue; return r; }
        const uint8_t *rec = s + off % 2048;
        unsigned len = rec[0];
        if (len == 0 || off % 2048 + len > 2048) { *pos = (off / 2048 + 1) * 2048; continue; }
        *pos = off + len;
        unsigned nlen = rec[32];
        const uint8_t *id = rec + 33;
        uint8_t flags = rec[d->iso ? 25 : 24];
        if (flags & 0x04) continue;                          /* associated file */
        if (nlen == 1 && (id[0] == 0 || id[0] == 1)) {
            if (root) continue;
            memset(f->name11, ' ', 11);
            f->name11[0] = '.';
            if (id[0] == 1) f->name11[1] = '.';
        } else iso_name11(id, nlen, f->name11);
        f->attr = (flags & 0x02) ? 0x10 : 0x01;              /* directories; files read-only */
        if (flags & 0x01) f->attr |= 0x02;                   /* "existence" bit: hidden */
        if ((f->attr & ~sattr) & 0x16) continue;             /* DOS attribute rules */
        if (!match11(pat, f->name11)) continue;
        f->lba = rd32le(rec + 2);
        f->size = rd32le(rec + 10);
        dos_stamp(rec + 18, &f->time, &f->date);
        f->rec = rec; f->reclen = len;
        if (f->attr & 0x10) f->size = f->size;               /* the extent size, for walking */
        return 1;
    }
    return 0;
}

/* the size of a directory = the size in its own "." record */
static int dir_size(struct drive *d, uint32_t lba, uint32_t *size)
{
    unsigned e = 0;
    for (;;) {
        uint8_t *s = sector(d, lba, &e);
        if (s) { *size = rd32le(s + 10); return 0; }
        int r = crit(d, e);
        if (r != 1) return r;
    }
}

/*
 * Walk a canonical path "D:\A\B\NAME". Sets the parent directory (plb, psz),
 * whether it is the root, and the last component (FCB form) in last11; *wild
 * says whether it has wildcards; *isroot when the path is the root itself.
 * Returns 0 or -DOS error.
 */
static int walk(struct drive *d, const char *path, uint32_t *plb, uint32_t *psz, int *proot, char *last11, int *wild, int *isroot)
{
    const char *p = path + 2;
    while (*p == '\\') p++;
    uint32_t lba = d->root, size = d->rootsize;
    int root = 1;
    *isroot = !*p;
    memset(last11, ' ', 11);
    *wild = 0;
    while (*p) {
        const char *s = p;
        while (*p && *p != '\\') p++;
        int len = p - s;
        while (*p == '\\') p++;
        char n11[11];
        int w = comp11(s, len, n11);
        if (!*p) { memcpy(last11, n11, 11); *wild = w; break; }
        if (w) return -DE_NOPATH;
        struct found f;
        uint32_t pos = 0;
        int r = dir_next(d, lba, size, &pos, n11, 0x16, root, &f);
        if (r < 0) return r;
        if (!r || !(f.attr & 0x10)) return -DE_NOPATH;
        lba = f.lba; size = f.size; root = 0;
    }
    *plb = lba; *psz = size; *proot = root;
    return 0;
}

/* look a whole path up: 1 found (f), 0 no, <0 error; a root fills f as a directory */
static int lookup(struct drive *d, const char *path, struct found *f)
{
    uint32_t lba, size;
    int root, wild, isroot;
    char n11[11];
    int r = walk(d, path, &lba, &size, &root, n11, &wild, &isroot);
    if (r < 0) return r;
    if (isroot) {
        memset(f, 0, sizeof *f);
        f->lba = d->root; f->size = d->rootsize; f->attr = 0x10;
        dos_stamp(d->rootdate, &f->time, &f->date);
        return 1;
    }
    if (wild) return -DE_NOFILE;
    uint32_t pos = 0;
    return dir_next(d, lba, size, &pos, n11, 0x16, root, f);
}

/* ------------------------------------------------------------- the redirector */

static void ok(struct armregs *f) { f->cpsr &= ~ARM_CPSR_C; }
static void fail(struct armregs *f, int e) { f->r0 = e; f->cpsr |= ARM_CPSR_C; }

static struct drive *by_letter(int l)
{
    for (int i = 0; i < ndrv; i++) if (drv[i].letter == l) return &drv[i];
    return 0;
}
static struct drive *by_path(const char *p)
{
    if (!p || !p[0] || p[1] != ':') return 0;
    return by_letter(up(p[0]) - 'A');
}

/* fill a find DTA with a found entry */
static void dta_fill(uint8_t *t, const struct found *f)
{
    t[0x15] = f->attr;
    wr16le(t + 0x16, f->time); wr16le(t + 0x18, f->date);
    wr32le(t + 0x1A, (f->attr & 0x10) ? 0 : f->size);
    char nm[13];
    fcb_to_name(f->name11, nm);
    memset(t + 0x1E, 0, 13);
    memcpy(t + 0x1E, nm, slen(nm));
}

/* 111Bh / 111Ch: DTA +00 drive|80h, +01 pattern, +0C sattr, +0D dir LBA, +11 offset */
static int find(struct drive *d, uint8_t *t)
{
    uint32_t lba = rd32le(t + 0x0D), pos = rd32le(t + 0x11), size;
    if (pos == 0xFFFFFFFFu) return -DE_NOMORE;
    int r;
    if (lba == 0xFFFFFFFFu) {                   /* the volume label */
        struct found f;
        memset(&f, 0, sizeof f);
        memset(f.name11, ' ', 11);
        memcpy(f.name11, d->label, slen(d->label));
        f.attr = 0x08;
        dos_stamp(d->rootdate, &f.time, &f.date);
        dta_fill(t, &f);
        wr32le(t + 0x11, 0xFFFFFFFFu);
        return d->label[0] ? 0 : -DE_NOMORE;
    }
    if ((r = dir_size(d, lba, &size)) < 0) return r;
    struct found f;
    r = dir_next(d, lba, size, &pos, (const char *)t + 1, t[0x0C], lba == d->root, &f);
    if (r < 0) return r;
    wr32le(t + 0x11, r ? pos : 0xFFFFFFFFu);
    if (!r) return -DE_NOMORE;
    dta_fill(t, &f);
    return 0;
}

static void redirector(struct armregs *f, struct drive *d)
{
    unsigned fn = f->r0 & 0xFF;
    const char *fn1 = (const char *)f->r4;
    struct sft *s = (struct sft *)f->r5;
    int r;
    struct found fd;

    switch (fn) {                       /* write-type calls: a CD-ROM is read-only */
    case 0x01: case 0x03: case 0x09: case 0x0E: case 0x11: case 0x13: case 0x17: case 0x18:
        fail(f, DE_ACCESS); return;
    }
    if ((r = check_media(d)) < 0) { fail(f, -r); return; }

    switch (fn) {
    case 0x05:                          /* CHDIR */
        r = lookup(d, fn1, &fd);
        if (r < 0) { fail(f, -r); return; }
        if (!r || !(fd.attr & 0x10)) { fail(f, DE_NOPATH); return; }
        ok(f); return;
    case 0x06: ok(f); return;           /* CLOSE */
    case 0x07: ok(f); return;           /* COMMIT */
    case 0x08: {                        /* READ: CX bytes at sft->position to r3 */
        uint32_t pos = s->position, n = f->r2 & 0xFFFF, size = s->size;
        uint8_t *p = (uint8_t *)f->r3;
        if (pos >= size) n = 0; else if (n > size - pos) n = size - pos;
        uint32_t done = 0, ext = s->dir_sector;
        while (done < n) {
            uint32_t at = pos + done, lba = ext + at / 2048, o = at % 2048, left = n - done;
            unsigned e = 0;
            if (o == 0 && left >= 2048) {         /* whole sectors straight into the buffer */
                unsigned k = left / 2048;
                if (k > 32) k = 32;
                e = dev_read(d, lba, k, p + done);
                if (!e) { done += k * 2048; continue; }
            } else {
                uint8_t *b = sector(d, lba, &e);
                if (b) {
                    unsigned c = 2048 - o;
                    if (c > left) c = left;
                    memcpy(p + done, b + o, c);
                    done += c;
                    continue;
                }
            }
            r = crit(d, e);
            if (r == 1) continue;
            s->position = pos + done;
            fail(f, -r); return;
        }
        s->position = pos + done;
        f->r2 = done;
        ok(f); return;
    }
    case 0x0C:                          /* disk space: all used */
        f->r0 = 1; f->r1 = d->volsize > 0xFFFF ? 0xFFFF : d->volsize; f->r2 = 2048; f->r3 = 0;
        ok(f); return;
    case 0x0F:                          /* get attributes */
        r = lookup(d, fn1, &fd);
        if (r < 0) { fail(f, -r); return; }
        if (!r) { fail(f, DE_NOFILE); return; }
        f->r0 = fd.attr; f->r1 = (fd.attr & 0x10) ? 0 : fd.size; f->r2 = fd.time; f->r3 = fd.date;
        ok(f); return;
    case 0x16: {                        /* OPEN */
        if ((f->r2 & 7) != 0) { fail(f, DE_ACCESS); return; }
        r = lookup(d, fn1, &fd);
        if (r < 0) { fail(f, -r); return; }
        if (!r) { fail(f, DE_NOFILE); return; }
        if (fd.attr & 0x10) { fail(f, DE_ACCESS); return; }
        s->attr = fd.attr;
        s->flags = 0x8000 | 0x40 | d->letter;
        s->devptr = (uint32_t)d;
        s->first_cluster = 0;
        s->time = fd.time; s->date = fd.date;
        s->size = fd.size; s->position = 0;
        s->rel_cluster = 0; s->dir_sector = fd.lba; s->dir_index = 0;
        memcpy(s->name, fd.name11, 11);
        s->last_cluster = 0;
        s->ifs = MAGIC;
        ok(f); return;
    }
    case 0x1B: {                        /* FIND FIRST */
        uint8_t *t = (uint8_t *)f->r1;
        uint32_t lba, size;
        int root, wild, isroot;
        char n11[11];
        memset(t, 0, 0x15);
        t[0] = (d->letter + 1) | 0x80;
        t[0x0C] = f->r2;
        if ((f->r2 & 0xFF) == 0x08 || ((f->r2 & 0x08) && 0)) {   /* the label */
            memset(t + 1, '?', 11);
            wr32le(t + 0x0D, 0xFFFFFFFFu); wr32le(t + 0x11, 0);
        } else {
            r = walk(d, fn1, &lba, &size, &root, n11, &wild, &isroot);
            if (r < 0) { fail(f, -r); return; }
            if (isroot) { fail(f, DE_NOPATH); return; }
            memcpy(t + 1, n11, 11);
            wr32le(t + 0x0D, lba); wr32le(t + 0x11, 0);
        }
        r = find(d, t);
        if (r < 0) { fail(f, r == -DE_NOMORE ? DE_NOFILE : -r); return; }
        ok(f); return;
    }
    case 0x1C:                          /* FIND NEXT */
        r = find(d, (uint8_t *)f->r1);
        if (r < 0) { fail(f, -r); return; }
        ok(f); return;
    case 0x21: {                        /* seek from end */
        int32_t off = (int32_t)(((f->r2 & 0xFFFF) << 16) | (f->r3 & 0xFFFF));
        uint32_t np = s->size + off;
        s->position = np;
        f->r0 = np & 0xFFFF; f->r3 = np >> 16;
        ok(f); return;
    }
    case 0x22: ok(f); return;           /* process termination */
    }
    fail(f, 1);
}

/* ------------------------------------------------------------- INT 2Fh AH=15h */

static struct drive *by_cx(struct armregs *f) { return by_letter(f->r2 & 0xFFFF); }

static void api(struct armregs *f)
{
    unsigned fn = f->r0 & 0xFF;
    struct drive *d;
    unsigned e;
    switch (fn) {
    case 0x00:                          /* installation check */
        f->r1 = ndrv; f->r2 = ndrv ? drv[0].letter : 0;
        return;
    case 0x01: {                        /* drive device list */
        uint8_t *b = (uint8_t *)f->r1;
        for (int i = 0; i < ndrv; i++) { b[i * 5] = drv[i].unit; wr32le(b + i * 5 + 1, (uint32_t)drv[i].dev); }
        return;
    }
    case 0x02: case 0x03: case 0x04: {  /* copyright / abstract / bibliographic file name */
        if (!(d = by_cx(f))) { fail(f, 0x0F); return; }
        int r = check_media(d);
        if (r < 0) { fail(f, -r); return; }
        uint8_t *s = sector(d, d->pvd, &e);
        if (!s) { fail(f, DE_NOTREADY); return; }
        static const uint16_t off_iso[3] = { 702, 739, 776 }, off_hsg[3] = { 710, 742, 774 };
        int n = d->iso ? 37 : 32;
        trim_copy((char *)f->r1, s + (d->iso ? off_iso : off_hsg)[fn - 2], n);
        ok(f); return;
    }
    case 0x05: {                        /* read volume descriptor DX */
        if (!(d = by_cx(f))) { fail(f, 0x0F); return; }
        int r = check_media(d);
        if (r < 0 && r != -DE_GENFAIL) { fail(f, -r); return; }
        uint8_t *b = (uint8_t *)f->r1;
        if ((e = dev_read(d, 16 + (f->r3 & 0xFFFF), 1, b))) { fail(f, DE_NOTREADY); return; }
        unsigned t = !memcmp_(b + 1, "CD001", 5) ? b[0] : !memcmp_(b + 9, "CDROM", 5) ? b[8] : 0;
        f->r0 = t == 1 ? 1 : t == 0xFF ? 0xFF : 0;
        ok(f); return;
    }
    case 0x06: case 0x07: return;       /* debugging on/off: reserved */
    case 0x08: {                        /* absolute read: SI:DI sector, DX count */
        if (!(d = by_cx(f))) { fail(f, 0x0F); return; }
        uint32_t lba = ((f->r4 & 0xFFFF) << 16) | (f->r5 & 0xFFFF);
        if ((e = dev_read(d, lba, f->r3 & 0xFFFF, (void *)f->r1))) { fail(f, DE_NOTREADY); return; }
        ok(f); return;
    }
    case 0x09:                          /* absolute write */
        if (!(d = by_cx(f))) { fail(f, 0x0F); return; }
        fail(f, DE_ACCESS); return;
    case 0x0B:                          /* drive check */
        f->r1 = 0xADAD;
        f->r0 = by_cx(f) ? 0x5AD8 : 0;
        return;
    case 0x0C: f->r1 = CDEX_VERSION; return;
    case 0x0D: {
        uint8_t *b = (uint8_t *)f->r1;
        for (int i = 0; i < ndrv; i++) b[i] = drv[i].letter;
        return;
    }
    case 0x0E:                          /* volume descriptor preference */
        if (!by_cx(f)) { fail(f, 0x0F); return; }
        if ((f->r1 & 0xFFFF) == 0) f->r3 = vdpref;
        else if ((f->r1 & 0xFFFF) == 1) vdpref = f->r3;
        else { fail(f, 1); return; }
        ok(f); return;
    case 0x0F: {                        /* directory entry: CL drive, ES:BX path, SI:DI (flat in DI) buffer */
        if (!(d = by_letter(f->r2 & 0xFF))) { fail(f, 0x0F); return; }
        int r = check_media(d);
        if (r < 0) { fail(f, -r); return; }
        char full[80];
        const char *p = (const char *)f->r1;
        int k = 0;
        full[k++] = 'A' + d->letter; full[k++] = ':';
        if (p[0] && p[1] == ':') p += 2;
        if (*p != '\\' && *p != '/') full[k++] = '\\';
        while (*p && k < 78) { full[k++] = *p == '/' ? '\\' : up(*p); p++; }
        full[k] = 0;
        struct found fd;
        r = lookup(d, full, &fd);
        if (r < 0) { fail(f, -r); return; }
        if (!r || !fd.rec) { fail(f, DE_NOFILE); return; }
        memcpy((void *)f->r5, fd.rec, fd.reclen);
        f->r0 = d->iso ? 1 : 0;
        ok(f); return;
    }
    case 0x10: {                        /* send device request: CX drive, ES:BX request */
        if (!(d = by_cx(f))) { fail(f, 0x0F); return; }
        struct cdreq *q = (struct cdreq *)f->r1;
        q->unit = d->unit;
        d->dev->strategy(q);
        d->dev->interrupt();
        /* a request that ejected, played or changed the disc: let the file system look again */
        if (q->cmd == C_IOCTL_OUT) d->mounted = 0;
        ok(f); return;
    }
    }
    fail(f, 1);
}

/* ------------------------------------------------------------- INT 2Fh */

static void int2f(struct armregs *f)
{
    unsigned ah = (f->r0 >> 8) & 0xFF;
    if (ah == 0x15) {
        busy++;
        api(f);
        busy--;
        return;
    }
    if (ah == 0x11 && ndrv) {
        unsigned fn = f->r0 & 0xFF;
        struct drive *d = 0;
        switch (fn) {
        case 0x06: case 0x07: case 0x08: case 0x09: case 0x21: {
            struct sft *s = (struct sft *)f->r5;
            if (s && s->ifs == MAGIC) {
                for (int i = 0; i < ndrv; i++) if ((uint32_t)&drv[i] == s->devptr) d = &drv[i];
            }
            break;
        }
        case 0x0C:
            for (int i = 0; i < ndrv; i++) if ((uint32_t)drv[i].cds == f->r6) d = &drv[i];
            break;
        case 0x1C: {
            uint8_t *t = (uint8_t *)f->r1;
            if (t && (t[0] & 0x80)) d = by_letter((t[0] & 0x1F) - 1);
            break;
        }
        case 0x22: break;               /* not a drive call: pass it on */
        default: d = by_path((const char *)f->r4); break;
        }
        if (d) {
            busy++;
            redirector(f, d);
            busy--;
            return;
        }
    }
    old2f(f);
}

/* ================================================================ transient part */

static void say(const char *s)
{
    struct armregs r; clr(&r);
    r.r0 = 0x4000; r.r1 = 1; r.r2 = slen(s); r.r3 = (uint32_t)s;
    _armdos_int21(&r);
}

__attribute__((noreturn)) static void leave(int code)
{
    struct armregs r; clr(&r);
    r.r0 = 0x4C00 | code;
    _armdos_int21(&r);
    for (;;) ;
}

static char *udec(char *b, uint32_t v)
{
    char t[12]; int n = 0;
    do { t[n++] = '0' + v % 10; v /= 10; } while (v);
    while (n) *b++ = t[--n];
    *b = 0;
    return b;
}
static char *commas(char *b, uint32_t v)
{
    char t[16]; int n = 0, k = 0;
    do { t[n++] = '0' + v % 10; v /= 10; } while (v);
    for (int i = n - 1; i >= 0; i--) { b[k++] = t[i]; if (i && i % 3 == 0) b[k++] = ','; }
    b[k] = 0;
    return b + k;
}

/* the header of a character device, found as MSCDEX found it: open the device
   by name and ask it with IOCTL input 0 (INT 21h AX=4402h) */
static struct cddevhdr *find_driver(const char *name)
{
    struct armregs r; clr(&r);
    r.r0 = 0x3D00; r.r3 = (uint32_t)name;
    if (_armdos_int21(&r)) return 0;
    unsigned h = r.r0 & 0xFFFF;
    clr(&r);
    r.r0 = 0x4400; r.r1 = h;
    _armdos_int21(&r);
    struct cddevhdr *dev = 0;
    if (r.r3 & 0x80) {                   /* a character device */
        uint8_t cb[5] = { IOI_DEVHDR };
        clr(&r);
        r.r0 = 0x4402; r.r1 = h; r.r2 = 5; r.r3 = (uint32_t)cb;
        if (!_armdos_int21(&r)) dev = (struct cddevhdr *)rd32le(cb + 1);
    }
    clr(&r); r.r0 = 0x3E00; r.r1 = h; _armdos_int21(&r);
    return dev;
}

static uint32_t rd32u(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

void __armdos_start(struct psp *psp, uint8_t *base, uint8_t *blockend, uint8_t *stacktop)
{
    (void)base;
    const uint8_t *tail = psp->cmdtail;
    char names[MAXDRV][9];
    int nnames = 0, letter = -1, verbose = 0, bufs = 4;
    char buf[160];

    say("ARM CD-ROM Extensions Version 2.21\r\n"
        "Copyright (C) Europa Micro Systems 1986-1993. All rights reserved.\r\n");

    struct armregs r; clr(&r);
    r.r0 = 0x3000; _armdos_int21(&r);
    if ((r.r0 & 0xFF) < 3 || ((r.r0 & 0xFF) == 3 && ((r.r0 >> 8) & 0xFF) < 10)) { say("Incorrect DOS version\r\n"); leave(1); }

    for (int i = 1; i <= tail[0]; i++) {
        if (tail[i] != '/') continue;
        uint8_t c = up(tail[i + 1]);
        if (c == 'D' && tail[i + 2] == ':' && nnames < MAXDRV) {
            int k = 0, j = i + 3;
            while (j <= tail[0] && tail[j] > ' ' && tail[j] != '/' && k < 8) names[nnames][k++] = up(tail[j++]);
            names[nnames][k] = 0;
            if (k) nnames++;
        } else if (c == 'L' && tail[i + 2] == ':') {
            uint8_t l = up(tail[i + 3]);
            if (l >= 'A' && l <= 'Z') letter = l - 'A';
        } else if (c == 'M' && tail[i + 2] == ':') {
            int v = 0, j = i + 3;
            while (tail[j] >= '0' && tail[j] <= '9') v = v * 10 + tail[j++] - '0';
            if (v >= 1 && v <= 32) bufs = v;
        } else if (c == 'V') verbose = 1;
        else if (c == '?') {
            say("\r\nARMCDEX /D:devname [/D:devname ...] [/L:letter] [/M:buffers] [/V] [/E] [/S] [/K]\r\n\r\n"
                "  /D:devname  a CD-ROM device driver's name (DEVICE=ARMCD.SYS /D:devname)\r\n"
                "  /L:letter   the first drive letter to use\r\n"
                "  /M:buffers  sector buffers (1-32, 2 KB each; default 4)\r\n"
                "  /V          show memory usage\r\n"
                "  /E /S /K    accepted for compatibility (expanded memory, sharing, Kanji)\r\n");
            leave(0);
        }
    }

    clr(&r); r.r0 = 0x1500; r.r1 = 0;
    _armdos_int2f(&r);
    if ((r.r1 & 0xFFFF) != 0) { say("ARM CD-ROM Extensions already installed\r\n"); leave(1); }

    /* the drivers */
    for (int i = 0; i < nnames; i++) {
        struct cddevhdr *dev = find_driver(names[i]);
        if (!dev || memcmp_(dev->name, names[i], slen(names[i]))) {
            char *b = buf;
            const char *m = "Device driver not found: '";
            while (*m) *b++ = *m++;
            for (const char *p = names[i]; *p; p++) *b++ = *p;
            m = "'.\r\n";
            while (*m) *b++ = *m++;
            *b = 0;
            say(buf);
            continue;
        }
        int units = dev->units ? dev->units : 1;
        for (int u = 0; u < units && ndrv < MAXDRV; u++) {
            drv[ndrv].dev = dev; drv[ndrv].unit = u;
            ndrv++;
        }
    }
    if (!ndrv) { say("No valid CDROM device drivers selected\r\n"); leave(1); }

    /* drive letters: the CDS array in the List of Lists */
    clr(&r); r.r0 = 0x5200; _armdos_int21(&r);
    const uint8_t *lol = (const uint8_t *)r.r1;
    struct cds *cdsa = (struct cds *)rd32u(lol + 0x16);
    int lastdrive = lol[0x21], nblock = lol[0x20];
    int next = letter >= 0 ? letter : nblock;
    int got = 0;
    for (int i = 0; i < ndrv; i++) {
        while (next < lastdrive && (cdsa[next].flags & 0xC000)) next++;
        if (next >= lastdrive) break;
        drv[i].letter = next++;
        got++;
    }
    if (got < ndrv) {
        say("Not enough drive letters available\r\n");
        ndrv = got;
        if (!ndrv) leave(1);
    }

    /* sector buffers after the stack (the install stack is dead once resident) */
    nbuf = bufs;
    cbuf = (uint8_t *)(((uint32_t)stacktop + 15) & ~15u);
    if (cbuf + nbuf * 2048 > blockend) { say("Not enough memory\r\n"); leave(8); }
    for (int i = 0; i < nbuf; i++) { ctag[i] = 0xFFFFFFFFu; cage[i] = 0; }

    old2f = (armdos_vect_t)ARMDOS_IVT[0x2F];
    armdos_disable();
    for (int i = 0; i < ndrv; i++) {
        struct cds *c = &cdsa[drv[i].letter];
        drv[i].cds = c;
        memset(c, 0, sizeof *c);
        c->path[0] = 'A' + drv[i].letter; c->path[1] = ':'; c->path[2] = '\\';
        c->flags = 0xC000;                          /* network + valid: a redirected drive */
        c->cluster = 0xFFFF;
        c->bsoffset = 2;
        if (!drv[i].dev->drive) drv[i].dev->drive = drv[i].letter + 1;
    }
    clr(&r); r.r0 = 0x252F; r.r3 = (uint32_t)int2f; _armdos_int21(&r);
    armdos_enable();

    for (int i = 0; i < ndrv; i++) {
        char *b = buf;
        const char *m = "        Drive ";
        while (*m) *b++ = *m++;
        *b++ = 'A' + drv[i].letter;
        m = ": = Driver ";
        while (*m) *b++ = *m++;
        for (int k = 0; k < 8 && drv[i].dev->name[k] != ' '; k++) *b++ = drv[i].dev->name[k];
        m = " unit ";
        while (*m) *b++ = *m++;
        b = udec(b, drv[i].unit);
        *b++ = '\r'; *b++ = '\n'; *b = 0;
        say(buf);
    }

    /* stay resident: PSP .. the sector buffers; free the environment */
    uint32_t end = (uint32_t)(cbuf + nbuf * 2048);
    uint32_t paras = (end - (uint32_t)psp + 15) >> 4;
    resident_bytes = paras << 4;
    if (verbose) {
        char *b = buf;
        const char *m = "        Sector buffers  : ";
        while (*m) *b++ = *m++;
        b = udec(b, nbuf);
        m = " x 2048 bytes\r\n        Resident size   : ";
        while (*m) *b++ = *m++;
        b = commas(b, resident_bytes);
        m = " bytes\r\n";
        while (*m) *b++ = *m++;
        *b = 0;
        say(buf);
    }
    clr(&r);
    r.r0 = 0x4900; r.r8 = psp->envseg;
    if (psp->envseg && !_armdos_int21(&r)) psp->envseg = 0;
    clr(&r);
    r.r0 = 0x3100; r.r3 = paras;
    _armdos_int21(&r);
    for (;;) ;
}
