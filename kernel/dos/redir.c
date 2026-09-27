/*
 * redir.c - the network redirector callouts, INT 2Fh AH=11h (DOS 4's
 * interface, REMOTE.ASM/NETWORK.INC), in ARM-DOS form: a drive whose CDS has
 * CDS_NET|CDS_VALID belongs to a redirector (a TSR such as the CD-ROM
 * extensions), and every file-system operation on it is passed to INT 2Fh.
 * DOS 4 passes most inputs in SDA fields; ARM-DOS passes them in registers
 * (apps/cdrom/README.md, "The kernel's redirector interface"):
 *
 *   r0 = 11xxh   r4 = FN1 (canonical path)   r3 = FN2 / buffer
 *   r5 = SFT     r6 = the drive's CDS        r1, r2 per function
 *
 * CF clear = success; CF set = failure, AX = the DOS error code.
 */
#include "dos.h"

int drive_remote(int drive)
{
    struct cds *c = get_cds(drive);
    return c && (c->flags & (CDS_NET | CDS_VALID)) == (CDS_NET | CDS_VALID);
}

/* issue 11xxh with the registers in *r (r0 is set here); 0 or -error */
int redir_call(int fn, struct armregs *r)
{
    r->r0 = 0x1100 | fn;
    if (!kint(0x2F, r)) return 0;
    unsigned e = r->r0 & 0xFFFF;
    if (!e || e >= 0x100) e = E_NOTSUP;         /* nobody answered */
    return -(int)e;
}

/* a path call: r4 = FN1, r6 = CDS, r2 = cx */
int redir_path(int fn, const struct pathinfo *pi, unsigned cx, struct armregs *r)
{
    memset(r, 0, sizeof *r);
    r->r4 = (uint32_t)pi->full;
    r->r6 = (uint32_t)get_cds(pi->drive);
    r->r2 = cx;
    return redir_call(fn, r);
}

/* 1116h open / 1117h create into s (the kernel keeps ref_count, sets mode/owner) */
int redir_open(const struct pathinfo *pi, struct sft *s, int mode, int create, int attr, int newonly)
{
    struct armregs r;
    int e, exists = 0;
    if (create) {
        e = redir_path(0x0F, pi, 0, &r);
        exists = e == 0;
        if (exists && newonly) return -E_EXISTS;
    }
    memset(&r, 0, sizeof r);
    r.r4 = (uint32_t)pi->full;
    r.r5 = (uint32_t)s;
    r.r6 = (uint32_t)get_cds(pi->drive);
    r.r2 = create ? (unsigned)attr : (unsigned)mode;
    e = redir_call(create ? 0x17 : 0x16, &r);
    if (e < 0) return e;
    s->mode = mode;
    s->flags |= SF_REMOTE;
    s->owner_psp = cur_psp;
    return create ? (exists ? 3 : 2) : 1;
}

/* 1108h read / 1109h write: bytes done or -error */
int redir_rw(int write, struct sft *s, const uint8_t *buf, unsigned n)
{
    struct armregs r;
    memset(&r, 0, sizeof r);
    r.r5 = (uint32_t)s;
    r.r2 = n;
    r.r3 = (uint32_t)buf;
    int e = redir_call(write ? 0x09 : 0x08, &r);
    if (e < 0) return e;
    return r.r2 & 0xFFFF;
}

/* 1106h close / 1107h commit */
int redir_sft(int fn, struct sft *s)
{
    struct armregs r;
    memset(&r, 0, sizeof r);
    r.r5 = (uint32_t)s;
    return redir_call(fn, &r);
}

/* 110Ch: *spc, *total, *bps, *avail */
int redir_space(int drive, unsigned *spc, unsigned *total, unsigned *bps, unsigned *avail)
{
    struct armregs r;
    memset(&r, 0, sizeof r);
    r.r6 = (uint32_t)get_cds(drive);
    int e = redir_call(0x0C, &r);
    if (e < 0) return e;
    *spc = r.r0 & 0xFFFF; *total = r.r1 & 0xFFFF; *bps = r.r2 & 0xFFFF; *avail = r.r3 & 0xFFFF;
    return 0;
}

/* 111Bh (pattern != 0) / 111Ch on a 43-byte find DTA */
int redir_find(const struct pathinfo *pi, unsigned sattr, uint8_t *dta)
{
    struct armregs r;
    memset(&r, 0, sizeof r);
    r.r1 = (uint32_t)dta;
    if (pi) {
        r.r4 = (uint32_t)pi->full;
        r.r6 = (uint32_t)get_cds(pi->drive);
        r.r2 = sattr;
    }
    return redir_call(pi ? 0x1B : 0x1C, &r);
}

/* a found name ("NAME.EXT", ".", "..", a label "SAMPLER9.3") -> FCB form,
   without validity checks (the redirector said so) */
void redir_name11(const char *s, char *n11)
{
    memset(n11, ' ', 11);
    if (s[0] == '.') {
        n11[0] = '.';
        if (s[1] == '.') n11[1] = '.';
        return;
    }
    const char *dot = 0;
    for (const char *p = s; *p; p++) if (*p == '.') dot = p;
    int i = 0;
    for (const char *p = s; *p && p != dot && i < 8; p++) n11[i++] = *p;
    if (dot) { i = 8; for (const char *p = dot + 1; *p && i < 11; p++) n11[i++] = *p; }
    if (n11[0] == (char)0xE5) n11[0] = 0x05;
}
