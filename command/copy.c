/*
 * copy.c - COPY (COPY.ASM, COPYPR1.ASM, COPYPR2.ASM), ported closely:
 * sources are found with FCB searches in their directory (COMMAND CHDIRs
 * there and back), data goes through a buffer in free memory (the "TPA"),
 * concatenation, ASCII/binary, devices, "+,," and the same error paths.
 * The 4.0 code-page extended attribute copying is left out.
 */
#include "cmd.h"

extern int cparse_comma;

struct cvars {
    char *buf;
    int   siz;
    int   info;         /* cparse flags: 2 ambiguous, 4 path separators */
    int   isdir;        /* -1 unknown, 0 file, 1 path/file, 2 d:/file */
    char *ttail;        /* the last element */
};

static char scanbuf[COMBUFLEN + 32];
static char destbuf[COMBUFLEN + 32];
static char srcxname[COMBUFLEN + 16], trgxname[COMBUFLEN + 16];
static uint8_t sdirbuf[16], destfcb[40];
static uint8_t cdirbuf[64];             /* DTA for the source searches */
static struct cvars srcv, destv;

static uint8_t *tpa;
static unsigned bytcnt;
#define tpa_seg (R->tpa_seg)
static unsigned nxtadd, written;
static int srchand, desthand;
static int cflag, destclosed, nowrite, rdeof, termread, srcisdev, destisdev;
static int concat, inexact, ascii, binary, plus, argc_, objcnt, melcopy;
static int filecnt, frstsrch, firstdest, plus_comma, comma;
static uint16_t destswitch, allswitch;
static uint16_t cpdate, cptime;
static const char *srcpt;

__attribute__((noreturn)) static void endcopy(void);
__attribute__((noreturn)) static void endcopy2(void);
__attribute__((noreturn)) static void coperr(void);

/* free the transfer buffer (also called from HEADFIX after an abort) */
void copy_cleanup(void)
{
    if (tpa_seg) dos_free(tpa_seg);
    tpa_seg = 0;
    tpa_seg = 0;
    tpa = 0;
}

/* SETASC */
static int setasc(int al)
{
    al &= SW_A | SW_B;
    if (al == SW_A || al == SW_B) {
        binary = al & SW_B;
        ascii = al & SW_A;
        inexact |= ascii;
    }
    return ascii;
}

/* SWITCH (TMISC1.ASM): the switches following the current position */
static uint16_t switches(const char **ps)
{
    const char *si = *ps;
    uint16_t bx = 0;
    for (;;) {
        si = scanoff(si);
        if (*si != (char)switchar) break;
        bx |= SW_ANY;
        si++;
        si = scanoff(si);
        if (*si == '\r') break;
        int c = upconv((uint8_t)*si++);
        const char *l = "VBAPW";
        const char *f = strchr(l, c);
        if (f && c) bx |= 1u << (4 - (f - l));
    }
    *ps = si;
    return bx;
}

static void setstars(struct cvars *v, char *di)
{
    v->ttail = di;
    v->siz += 12;
    memcpy(di, "????????.???", 13);
}

static void fcb_to_ascz(const uint8_t *f, char *d)
{
    for (int i = 0; i < 8; i++) if (f[i] != ' ') *d++ = f[i];
    if (f[8] != ' ') {
        *d++ = '.';
        for (int i = 8; i < 11; i++) if (f[i] != ' ') *d++ = f[i];
    }
    *d = 0;
}

/* SAVUDIR1 into userdir1 for a drive (1-based) */
static int savudir1(int drive)
{
    char *d = userdir1;
    *d++ = drive + '@';
    *d++ = ':';
    *d++ = dirchar;
    return dos_curdir(drive, d);
}

static void restudir1(void)
{
    if (restdir) restudir();
}

/* BUILDPATH: find out whether the spec names a file, a device or a
 * directory; CHDIR to its directory part */
static void buildpath(struct cvars *v)
{
    if (!(v->info & 2)) {
        int h = dos_open(v->buf, 0);
        if (h >= 0) {
            int dl = dos_ioctl_info(h);
            dos_close(h);
            if (dl >= 0 && (dl & 0x80)) { v->isdir = 0; return; }
            if (!(v->info & 4)) { v->isdir = 0; return; }
        } else {
            int e = dos_error();
            if (e != 2 && e != 3 && e != 5) cerror_ext(e, NULL);
        }
    }
    /* NOTPFILE */
    int drive = (v->buf[0] && v->buf[1] == ':') ? upconv(v->buf[0]) - '@' : curdrv + 1;
    if (savudir1(drive)) cerror_ext(dos_error(), NULL);
    char *si;
    int bh = v->info & 6;
    if (bh == 6) {
        si = v->ttail;
        if (si[-2] == ':') {
            v->isdir = 2;
        } else {
            v->isdir = 1;
            si--;
        }
        goto dopcd;
    }
    if (bh == 2) {
        v->isdir = 0;
        return;
    }
    /* CHECKCD */
    restdir = 1;
    if (!dos_chdir(v->buf)) {
        char *di = v->buf + strlen(v->buf);
        v->isdir = 2;
        if (di[-1] != dirchar) {
            *di++ = dirchar;
            v->isdir = 1;
        }
        v->info |= 6;
        setstars(v, di);
        return;
    }
    {
        int e = dos_error();
        if (e != 3 && e != 5) cerror_ext(e, NULL);
    }
    v->isdir = 0;
    if (!(v->info & 4)) return;
    v->isdir = 2;
    si = v->ttail;
    if (*si == 0 || *si == '.') cerror_ext(3, NULL);
    if (si[-2] != ':') {
        v->isdir = 1;
        si--;
    }
dopcd:
    {
        char bl = *si;
        *si = 0;
        restdir = 1;
        if (si > v->buf && bl && bl == si[-1]) {
            *si = bl;
            cerror_ext(3, NULL);
        }
        int r = dos_chdir(v->buf);
        *si = bl;
        if (r) cerror_ext(dos_error(), NULL);
    }
}

/* BUILDNAME: dest name with the '?'s filled from the source name */
static void buildname(const uint8_t *bx, const uint8_t *si, char *di)
{
    for (int i = 0; i < 8; i++) {
        int c = si[i];
        if (c == '?') c = bx[i];
        if (c != ' ') *di++ = c;
    }
    if (si[8] != ' ') {
        *di++ = '.';
        for (int i = 8; i < 11; i++) {
            int c = si[i];
            if (c == '?') c = bx[i];
            if (c != ' ') *di++ = c;
        }
    }
    *di = 0;
}

static int compname(void)
{
    trgxname[0] = 0;
    dos_truename(destbuf, trgxname);
    return strcmp(srcxname, trgxname) == 0;
}

/* BUILDDEST */
static void builddest(void)
{
    if (destv.isdir == -1) {
        buildpath(&destv);
        restudir1();
    }
    if (firstdest) {
        firstdest = 0;
        const char *si = destv.ttail;
        dos_fcb_parse(&si, destfcb, 0);
        if (*si != 0) {
            std_eprintf(M_FULDIR);
            coperr();
        }
        int drv = (destbuf[1] == ':') ? (destbuf[0] | 0x20) - 0x60 : 0;
        int cl = ascii;
        destfcb[0] = drv;
        int al = destv.info & 2, ah = srcv.info & 2;
        if (al && al == ah && plus) {
            melcopy = 1;
            al = 0;
        } else {
            al = ((al ^ 2) & ah) >> 1;
        }
        concat = al | plus;
        inexact = concat << 2;
        if (!binary) {
            ascii = inexact;
            if (!cl && ascii && nxtadd) {
                uint8_t *z = memchr(tpa, 0x1A, nxtadd);
                if (z) nxtadd = z - tpa;
            }
        }
    }
    const uint8_t *bx = concat ? sdirbuf + 1 : cdirbuf + 1;
    buildname(bx, destfcb + 1, destv.ttail);
}

/* FLSHFIL: create/open the destination and write the buffer */
static void flshfil(void)
{
    termread = 0;
    if (!cflag) {
        builddest();
        if (compname() && !srcisdev) {
            if (!concat) {
                std_eprintf(M_OVERWR);
                coperr();
            }
            nowrite = 1;
        }
        /* PROCDEST: extended open, replace/create, or open (NOWRITE) */
        REGS r = {0};
        r.r0 = 0x6C00;
        r.r1 = 0x0001;                      /* write_open_mode */
        r.r2 = 0;
        r.r3 = nowrite ? 0x0001 : 0x0012;
        r.r4 = (uint32_t)destbuf;
        if (int21(&r)) {
            ext_error_out(dos_error(), destbuf);
            coperr();
        }
        desthand = r.r0 & 0xFFFF;
        cflag = 1;
        int dl = dos_ioctl_info(desthand);
        destisdev = dl < 0 ? 0 : dl;
        if (destisdev & 0x80) {
            int al = destswitch & (SW_A | SW_B);
            if (!al) {
                al = ascii | binary;
                if (!al) {
                    ascii = SW_A;
                    inexact |= SW_A;
                    goto exists;
                }
            }
            if (al == (SW_A | SW_B)) goto exists;
            if (al & SW_B) {
                destisdev |= 0x20;
                dos_ioctl_set(desthand, destisdev & 0xFF);
            }
        }
    }
exists:
    if (!nowrite && !plus_comma) {
        if (compname() && !srcisdev) {
            std_eprintf(M_LOSTERR);
            nxtadd = 0;
            termread++;
            return;
        }
    }
    unsigned cx = nxtadd;
    nxtadd = 0;
    if (!cx) return;
    written++;
    if (nowrite) {
        dos_lseek(desthand, (long)cx, 1);
        if (rdeof) dos_write(desthand, "", 0);
        return;
    }
    int w = dos_write(desthand, tpa, cx);
    if (w < 0) {
        ext_error_out(dos_error(), destbuf);
        coperr();
    }
    unsigned left = cx - w;
    if (!left) return;
    if (!(destisdev & 0x80)) {
        std_eprintf(M_NOSPACE);
        coperr();
    }
    if (!(destisdev & 0x20)) {
        if (inexact) return;
        if (left == 1) return;
    }
    std_eprintf(M_DEVWMES);
    coperr();
}

/* TRYFLUSH: flush; returns nonzero if the CONCAT flag changed */
static int tryflush(void)
{
    int c = concat;
    flshfil();
    return c != concat;
}

static void destdelete(void)
{
    dos_unlink(destbuf);
}

/* DODCLOSE: date/time, close; count the file unless it came out empty */
static void dodclose(void)
{
    uint16_t tm = cptime, dt = cpdate;
    if (inexact) {
        int h, m, s, hs, y, mo, d, wd;
        dos_gettime(&h, &m, &s, &hs);
        dos_getdate(&y, &mo, &d, &wd);
        tm = (h << 11) | (m << 5) | (s >> 1);
        dt = ((y - 1980) << 9) | (mo << 5) | d;
    }
    if (desthand <= 0) {
        filecnt++;
        destclosed++;
        return;
    }
    REGS r = {0};
    r.r0 = 0x5701;
    r.r1 = desthand;
    r.r2 = tm;
    r.r3 = dt;
    int21(&r);
    long size = dos_lseek(desthand, 0, 2);
    int dl = dos_ioctl_info(desthand);
    int ce = dos_close(desthand);
    if (ce < 0) {
        ext_error_out(-ce, destbuf);
        destdelete();
        destclosed++;
        return;
    }
    if (size == 0 && !(dl >= 0 && (dl & 0x80))) {
        destdelete();
        destclosed++;
        return;
    }
    filecnt++;
    destclosed++;
}

/* CLOSEDEST: returns 1 (CF) if the concatenation state changed */
static int closedest(void)
{
    if (destclosed) return 0;
    if (setasc(destswitch)) {
        if (nxtadd == bytcnt) {
            if (tryflush()) return 1;
            nxtadd = 0;
        }
        tpa[nxtadd] = 0x1A;
        nxtadd++;
        nowrite = 0;
        if (written + nxtadd == 1) goto forgetit;
    }
    if (tryflush()) return 1;
    if (written == 0) goto forgetit;
    dodclose();
    return 0;
forgetit:
    dodclose();
    destdelete();
    filecnt = 0;
    return 0;
}

__attribute__((noreturn)) static void coperr(void)
{
    destclosed++;
    if (cflag) {
        if (desthand > 0) dos_close(desthand);
        destdelete();
        cflag = 0;
    }
    endcopy();
}

__attribute__((noreturn)) static void endcopy(void)
{
    closedest();
    endcopy2();
}

__attribute__((noreturn)) static void endcopy2(void)
{
    char n[16];
    copy_cleanup();
    fmt_uint(n, filecnt, 9, ' ');
    msgout(1, M_COPIED, n, 0, 0);
    tcommand();
}

static void closesrc(void)
{
    dos_close(srchand);
    srchand = 0;
}

__attribute__((noreturn)) static void error_on_source(void)
{
    ext_error_out(dos_error(), srcbuf);
    if (srchand) closesrc();
    if (cflag) endcopy();
    endcopy2();
}

/* DOCOPY: read one source into the buffer (flushing as needed) */
static void docopy(void)
{
    srcxname[0] = 0;
    dos_truename(srcbuf, srcxname);
    rdeof = 0;
    int h = dos_open(srcbuf, 0);
    if (h < 0) {
        srchand = 0;
        error_on_source();
    }
    srchand = h;
    REGS r = {0};
    r.r0 = 0x5700;
    r.r1 = h;
    if (int21(&r)) error_on_source();
    cpdate = r.r3 & 0xFFFF;
    cptime = r.r2 & 0xFFFF;
    int dl = dos_ioctl_info(h);
    srcisdev = dl >= 0 && (dl & 0x80);
    if (srcisdev && binary) {
        std_eprintf(M_INBDEV);
        coperr();
    }
    for (;;) {
        unsigned cx = bytcnt - nxtadd;
        if (!cx) {
            flshfil();
            if (termread) break;
            cx = bytcnt;
        }
        int n = dos_read(srchand, tpa + nxtadd, cx);
        if (n < 0) error_on_source();
        if (n == 0) break;
        unsigned cnt = n;
        if (srcisdev || ascii) {
            uint8_t *z = memchr(tpa + nxtadd, 0x1A, cnt);
            if (z) {
                rdeof++;
                cnt = z - (tpa + nxtadd);
            }
        }
        nxtadd += cnt;
        if (nxtadd >= bytcnt) {
            flshfil();
            if (termread) break;
            continue;
        }
        if (!srcisdev) break;
        if (rdeof) break;
    }
    closesrc();
}

/* source_set: SRCBUF from SCANBUF, with its switches */
static void source_set(const char **ps, int cx, int bh, char *startel, uint16_t bp)
{
    memcpy(srcbuf, scanbuf, cx + 1);
    srcv.buf = srcbuf;
    srcv.ttail = srcbuf + (startel - scanbuf);
    srcv.siz = cx;
    srcv.info = bh;
    setasc(bp);
    setasc(switches(ps));
}

/* the search for a source (FIRSTENT); 0 = found */
static int firstent(void)
{
    uint8_t *f = FCB;
    const char *si = srcv.ttail;
    dos_fcb_parse(&si, f, 0);
    int found = 0;
    if (*si == 0) {
        int drv = srcbuf[1] == ':' ? srcbuf[0] : '@';
        f[0] = (drv | 0x20) - 0x60;
        dos_setdta(cdirbuf);
        REGS r = {0};
        r.r0 = 0x1100;
        r.r3 = (uint32_t)f;
        int21(&r);
        found = (r.r0 & 0xFF) == 0;
    }
    restudir1();
    return found;
}

static int searchnext(void)
{
    if (!(srcv.info & 2)) return 0;
    dos_setdta(cdirbuf);
    REGS r = {0};
    r.r0 = 0x1200;
    r.r3 = (uint32_t)FCB;
    int21(&r);
    return (r.r0 & 0xFF) == 0;
}

void c_copy(void)
{
    const char *si;
    int flags, cx;
    uint16_t bp;
    char *startel;

    copy_cleanup();
    nxtadd = written = 0;
    cflag = destclosed = nowrite = rdeof = termread = srcisdev = destisdev = 0;
    concat = inexact = ascii = binary = plus = argc_ = objcnt = melcopy = 0;
    filecnt = 0;
    plus_comma = comma = 0;
    destswitch = allswitch = 0;
    cpdate = cptime = 0;
    srchand = 0;
    desthand = -1;
    frstsrch = 1;
    firstdest = 1;
    destv.isdir = -1;
    srcv.isdir = -1;
    userdir1[0] = 0;
    destbuf[0] = 0;
    srcbuf[0] = 0;

    /* the buffer: free memory, up to 64K-512, a multiple of 512 */
    {
        unsigned largest = 0;
        dos_alloc(0xFFFF, &largest);
        if (largest > 0x1000) largest = 0x1000;
        if (largest >= 0x40) {
            tpa_seg = dos_alloc(largest, 0);
            if (tpa_seg) {
                tpa = (uint8_t *)ARMDOS_SEG2PTR(tpa_seg);
                bytcnt = (unsigned)largest << 4;
                if (bytcnt > 0xFFFF) bytcnt = 0xFFFF;
                if (bytcnt > 512) bytcnt &= ~511u;
            }
        }
        if (!tpa) cerror_ext(8, NULL);
    }

    /* DESTSCAN: find the destination (the last argument not joined by '+') */
    si = (const char *)TAIL + 1;
    for (;;) {
        bp = 0;
        const char *parse_last = si;
        char *t = cparse(&si, scanbuf, '+', 1, &flags, &bp, &startel, &cx, 1);
        objcnt++;
        if (flags & 0x80) plus = 1;
        if (flags & AF_SWITCH) {
            if ((bp & SW_V) && (allswitch & SW_V)) bp |= SW_BAD;
            destswitch |= bp;
            allswitch |= bp;
            if (bp & ~(SW_V | SW_A | SW_B | SW_ANY)) {
                char sub[COMBUFLEN];
                int n = si - parse_last;
                if (n > COMBUFLEN - 1) n = COMBUFLEN - 1;
                memcpy(sub, parse_last, n);
                sub[n] = 0;
                char *cr = strchr(sub, '\r');
                if (cr) *cr = 0;
                parse_error_out(P_BADSWITCH, sub);
                copy_cleanup();
                tcommand();
            }
            if (!t) break;
            continue;
        }
        if (!t) break;
        if (!(flags & 0x80)) argc_++;
        memcpy(destbuf, scanbuf, cx + 1);
        destv.buf = destbuf;
        destv.ttail = destbuf + (startel - scanbuf);
        destv.siz = cx;
        destv.info = flags;
        destswitch = 0;
    }
    /* CHECKDONE */
    if (plus == 1 && argc_ == 1 && objcnt == 2) {
        std_eprintf(M_OVERWR);
        coperr();
    }
    concat = plus;
    inexact = concat << 2;
    if (argc_ == 0) {
        copy_cleanup();
        cerror_parse(P_MISSING, NULL);
    }
    if (argc_ > 2) {
        copy_cleanup();
        cerror_parse(P_TOOMANY, NULL);
    }
    if (argc_ == 1) {
        destbuf[0] = curdrv + 'A';
        destbuf[1] = ':';
        destv.buf = destbuf;
        destv.siz = 2;
        destswitch = 0;
        destv.info = 2;
        destv.isdir = 0;
        setstars(&destv, destbuf + 2);
    }
    if (destv.siz == 2 && destbuf[1] == ':') {
        destv.info |= 2;
        destv.isdir = 0;
        setstars(&destv, destbuf + 2);
    }
    if (*destv.ttail == 0) {
        if (destv.ttail[-2] != ':') {
            copy_cleanup();
            cerror_msg(M_BADCD);
        }
        destv.isdir = 2;
        destv.info |= 6;
        setstars(&destv, destv.ttail);
    }
    if (allswitch & SW_V) {
        REGS r = {0};
        r.r0 = 0x5400;
        int21(&r);
        verval = r.r0 & 0xFF;
        r.r0 = 0x2E01;
        int21(&r);
    }
    /* SCANFSRC: the first source */
    bp = 0;
    si = (const char *)TAIL + 1;
    for (;;) {
        cparse(&si, scanbuf, '+', 1, &flags, &bp, &startel, &cx, 1);
        if (!(flags & AF_SWITCH)) break;
    }
    destswitch |= bp;
    if (!(bp & SW_B) && concat) ascii = SW_A;
    source_set(&si, cx, flags, startel, bp);
    cflag = 0;
    nxtadd = 0;
    destclosed = 0;
    srcpt = si;
    buildpath(&srcv);
    goto firstent_;

nextsrc:
    if (!plus) endcopy();
    {
        /* MORECP */
        bp = 0;
        si = srcpt;
        for (;;) {
            char *t = cparse(&si, scanbuf, '+', 1, &flags, &bp, &startel, &cx, 1);
            if (!t) endcopy();
            if (!(flags & 0x80)) endcopy();
            if (!(flags & AF_SWITCH)) break;
        }
        comma = cparse_comma;
        source_set(&si, cx, flags, startel, bp);
        if (!concat) {
            cflag = 0;
            nxtadd = 0;
            destclosed = 0;
        }
        srcpt = si;
        buildpath(&srcv);
        if (comma) {
            plus_comma = 1;
            goto srcnonexist;
        }
        plus_comma = 0;
    }
firstent_:
    if (!firstent()) goto srcnonexist;
    if (frstsrch) {
        frstsrch = 0;
        memcpy(sdirbuf, cdirbuf, 12);
    }
    for (;;) {
        /* NEXTAMBIG */
        nowrite = 0;
        fcb_to_ascz(cdirbuf + 1, srcv.ttail);
        if (concat || (srcv.info & 2)) {
            out(1, srcbuf);
            crlf2();
        }
        docopy();
        if (!concat) {
            if (!closedest()) cflag = 0;
        }
        if (concat) flshfil();
        if (!searchnext()) break;
        destclosed = 0;
    }
    goto nextsrc;

srcnonexist:
    if (concat) goto nextsrc;
    ext_error_out(2, srcbuf);
    coperr();
}
