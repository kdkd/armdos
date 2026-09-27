/*
 * CHKDSK - ARM-DOS 4.00 disk checker.
 *
 * A re-creation in C of MS-DOS 4.00 CHKDSK.COM (CMD/CHKDSK in the MIT-licensed
 * MS-DOS 4.0 source): the same checks, questions, messages and report,
 * verified against the real CHKDSK.COM under DOSBox-X on deliberately
 * damaged diskettes.  It reads the FAT with INT 25h, walks the directory
 * tree (depth first, entering each subdirectory as it is found), marks every
 * cluster it reaches, and reports lost chains, cross-links, invalid clusters
 * and allocation (size) errors; with /F the fixes go back with INT 26h.
 * Unlike 4.00 it reads directories sector by sector instead of with FCB
 * searches and CHDIR, which gives the same results and path names.
 *
 * CHKDSK [d:][path][filename] [/F] [/V]
 */
#include "dosutil.h"
#include <stdlib.h>

/* texts (USA-MS.MSG section CHKDSK) */
static const char M_ERRFOUND[] = "Errors found, F parameter not specified\r\nCorrections will not be written to disk\r\n";
static const char M_CONVERT[]  = "Convert lost chains to files (Y/N)?";
static const char M_CONVDIR[]  = "Convert directory to file (Y/N)?";
static const char M_NONDOS[]   = "Probable non-DOS disk\r\nContinue (Y/N)?";
static const char M_BADCLUS[]  = "   Has invalid allocation unit, file truncated";
static const char M_FIRSTBAD[] = "   First allocation unit is invalid, entry truncated";
static const char M_ALLOCERR[] = "   Allocation error, size adjusted";
static const char M_INVSUB[]   = "   Invalid sub-directory entry";
static const char M_NOTEXIST[] = "   Does not exist";
static const char M_BADATTR[]  = "   Entry has a bad attribute";
static const char M_BADLINK[]  = "   Entry has a bad link";
static const char M_BADSIZE[]  = "   Entry has a bad size";
static const char M_EMPTY[]    = "   Directory is totally empty, no . or ..";
static const char M_TREEPAST[] = "   tree past this point not processed";
static const char M_UNRECDIR[] = "Unrecoverable error in directory";
static const char M_ROOTFULL[] = "   Insufficient room in root directory\r\n   Move files from root directory and repeat CHKDSK";
static const char M_CROSS[]    = "   Is cross linked on allocation unit ";

/* ---- options and the drive ---------------------------------------- */
static int drive, dofix, noisy;
static char filespec[80];
static int have_spec;
static struct kdpb dpb;
static unsigned bps, spc, csize, mclus, dsize, rootsecs;
static int fat16;
static unsigned eofval, badval;
static uint8_t *fat, *map;
static uint32_t fatbytes;

/* ---- state -------------------------------------------------------- */
static int fixmflg, dirtyfat, ftrunc, pass2;
static uint32_t hidcnt, hidclus, dircnt, dirclus, filecnt, filclus, badclus;
static uint32_t orphsiz, orphcnt, lclus, crosscnt;
static char path[160];                  /* "A:\DIR\NAME" being processed */
static uint8_t secbuf[512] __attribute__((aligned(4)));
static uint8_t line[20];

#define MAP_USED  0x01
#define MAP_CROSS 0x10
#define MAP_ORPH  0x20
#define MAP_SEEN  0x40
#define MAP_HEAD  0x80

struct dent {
    char name[11];
    uint8_t attr;
    uint8_t res[10];
    uint16_t time, date, cluster;
    uint32_t size;
} __attribute__((packed));

/* ------------------------------------------------------------- FAT */

static unsigned fget(unsigned c)
{
    if (fat16) return fat[c * 2] | (fat[c * 2 + 1] << 8);
    uint32_t o = c + c / 2;
    unsigned v = fat[o] | (fat[o + 1] << 8);
    return (c & 1) ? v >> 4 : v & 0xFFF;
}

static void fset(unsigned c, unsigned v)
{
    dirtyfat = 1;
    if (fat16) { fat[c * 2] = v; fat[c * 2 + 1] = v >> 8; return; }
    uint32_t o = c + c / 2;
    if (c & 1) { fat[o] = (fat[o] & 0x0F) | ((v & 0x0F) << 4); fat[o + 1] = v >> 4; }
    else { fat[o] = v; fat[o + 1] = (fat[o + 1] & 0xF0) | ((v >> 8) & 0x0F); }
}

static int is_eof(unsigned v) { return v >= eofval; }

/* -------------------------------------------------------- messages */

static void checkerr(void)
{
    if (dofix || fixmflg) return;
    fixmflg = 1;
    outs(M_ERRFOUND);
    crlf();
}

/* print the path (STDERR) and a message; handle 2 for the few messages
   4.00 sends to STDERR; the CR LF always goes to STDOUT (PRINTF_CRLF) */
static void eprint2(const char *msg, int h, const char *msg2)
{
    checkerr();
    if (pass2) return;
    errs(path);
    errs("\r\n");
    wr(h, msg);
    crlf();
    if (msg2) { outs(msg2); crlf(); }
}
static void eprint(const char *msg) { eprint2(msg, STDOUT, 0); }

static void num(char *b, uint32_t v) { fmtnum(b, v, 1, ' '); }

/* 11-char FCB name -> "NAME.EXT" appended to p */
static void fcbname(char *p, const char *n)
{
    int i, e = 8;
    while (e > 0 && n[e - 1] == ' ') e--;
    p += strlen(p);
    for (i = 0; i < e; i++) *p++ = n[i];
    int x = 3;
    while (x > 0 && n[8 + x - 1] == ' ') x--;
    if (x) { *p++ = '.'; for (i = 0; i < x; i++) *p++ = n[8 + i]; }
    *p = 0;
}

/* ------------------------------------------------------ Y/N prompt */

static int prompt_yn(const char *msg)
{
    for (;;) {
        outs(msg);
        R r = { 0 };
        line[0] = 15; line[1] = 0;
        r.r0 = 0x0A00; r.r3 = (uint32_t)line;
        dos(&r);
        crlf();
        if (!line[1]) continue;
        int yn = yesno(line[2]);
        if (yn >= 0) return yn;
    }
}

/* ------------------------------------------------- directory sectors */

static int rdsec(uint32_t s, void *b) { return abs_rw(drive, 0, s, 1, b); }
static int wrsec(uint32_t s, void *b) { return abs_rw(drive, 1, s, 1, b); }
static uint32_t clus_sec(unsigned c) { return dpb.first_data + (uint32_t)(c - 2) * spc; }

/* FIXENT: write a changed entry back (only with /F) */
static void fixent(uint32_t sector, unsigned off, const struct dent *e)
{
    if (!dofix) return;
    static uint8_t b[512] __attribute__((aligned(4)));
    if (rdsec(sector, b)) return;
    memcpy(b + off, e, 32);
    if (wrsec(sector, b)) { outs("Write fault error"); crlf(); }
}

/* ------------------------------------------------------------ marking */

static int valid(unsigned c) { return c >= 2 && c <= mclus; }

/* MarkFAT: returns the clusters counted; *crossed = hit a marked cluster */
static unsigned markfat(struct dent *e, uint32_t sector, unsigned off, int isdir, int *zerotrunc)
{
    unsigned first = e->cluster, n = 0;
    *zerotrunc = 0;
    if (!valid(first)) {
        if (first == 0 && (isdir || e->size == 0)) return 0;
        /* Bad_Cluster, case A */
        checkerr();
        if (first) { eprint(M_FIRSTBAD); *zerotrunc = 1; }
        e->cluster = 0;
        e->size = 0;
        fixent(sector, off, e);
        return 0;
    }
    unsigned c = first;
    for (;;) {
        if (map[c] & MAP_USED) {
            map[c] |= MAP_CROSS;
            crosscnt++;
            return n;                   /* cross-linked: no size check */
        }
        map[c] |= MAP_USED;
        n++;
        unsigned nx = fget(c);
        if (is_eof(nx)) break;
        if (!valid(nx) || nx == badval) {
            if (c == first) {
                /* Bad_Cluster, case A: the first cluster's link is bad */
                checkerr();
                eprint(M_FIRSTBAD);
                *zerotrunc = 1;
                e->cluster = 0;
                e->size = 0;
                fixent(sector, off, e);
            } else {
                checkerr();
                eprint(M_BADCLUS);
                fset(c, eofval | (fat16 ? 0xF : 0xF));
            }
            return n;
        }
        c = nx;
    }
    /* Check_Chain_Sizes */
    if (!isdir) {
        uint32_t chain = n * csize;
        if (e->size > chain || chain - e->size >= csize) {
            e->size = chain;
            fixent(sector, off, e);
            checkerr();
            eprint(M_ALLOCERR);
        }
    }
    return n;
}

/* pass 2: the first cross-marked cluster of a chain, or 0 */
static unsigned cross_in(unsigned first)
{
    unsigned c = first, guard = dsize + 2;
    while (valid(c) && guard--) {
        if (map[c] & MAP_CROSS) return c;
        unsigned nx = fget(c);
        if (is_eof(nx) || !valid(nx)) break;
        c = nx;
    }
    return 0;
}

/* ------------------------------------------------------------ the walk */

struct dirpos { unsigned cluster; uint32_t sector; unsigned idx; unsigned in_clus; int root; unsigned rootleft; };

static void dir_open(struct dirpos *p, unsigned cluster)
{
    memset(p, 0, sizeof *p);
    p->cluster = cluster;
    if (!cluster) { p->root = 1; p->sector = dpb.dir_sector; p->rootleft = rootsecs; }
    else p->sector = clus_sec(cluster);
}

/* next directory sector into b: 1 ok, 0 end */
static int dir_next_sector(struct dirpos *p, uint8_t *b, uint32_t *sec)
{
    if (p->root) {
        if (!p->rootleft) return 0;
        *sec = p->sector++;
        p->rootleft--;
    } else {
        if (p->in_clus == spc) {
            unsigned nx = fget(p->cluster);
            if (is_eof(nx) || !valid(nx)) return 0;
            p->cluster = nx;
            p->sector = clus_sec(nx);
            p->in_clus = 0;
        }
        *sec = p->sector++;
        p->in_clus++;
    }
    return rdsec(*sec, b) == 0;
}

static void walk(unsigned cluster, unsigned parent);

static void check_dots(unsigned cluster, unsigned parent, uint8_t *b, uint32_t sec, int *skip)
{
    struct dent *d0 = (struct dent *)b, *d1 = (struct dent *)(b + 32);
    size_t plen = strlen(path);
    int reported = 0;
    *skip = 0;
    if (d0->name[0] == 0) {             /* totally empty */
        if (noisy) {
            checkerr();
            eprint2(M_EMPTY, STDOUT, M_TREEPAST);
            ftrunc = 1;
        } else eprint2(M_INVSUB, STDERR, 0);
        *skip = 1;
        return;
    }
    for (int k = 0; k < 2; k++) {
        struct dent *d = k ? d1 : d0;
        const char *want = k ? "..         " : ".          ";
        unsigned wantc = k ? parent : cluster;
        strcpy(path + plen, k ? "\\.." : "\\.");
        if (memcmp(d->name, want, 11)) {
            if (noisy) eprint(M_NOTEXIST);
            else if (!reported++) { path[plen] = 0; eprint2(M_INVSUB, STDERR, 0); }
            continue;
        }
        const char *what[3] = { 0, 0, 0 };
        int changed = 0;
        if (!(d->attr & 0x10)) { what[0] = M_BADATTR; d->attr |= 0x10; changed = 1; }
        if (d->cluster != wantc) { what[1] = M_BADLINK; d->cluster = wantc; changed = 1; }
        if (d->size) { what[2] = M_BADSIZE; d->size = 0; changed = 1; }
        if (!changed) continue;
        for (int i = 0; i < 3; i++) {
            if (!what[i]) continue;
            if (noisy) eprint(what[i]);
            else if (!reported++) { path[plen] = 0; eprint2(M_INVSUB, STDERR, 0); strcpy(path + plen, k ? "\\.." : "\\."); }
        }
        fixent(sec, k * 32, d);
    }
    path[plen] = 0;
}

static void walk(unsigned cluster, unsigned parent)
{
    static uint8_t dummy;
    (void)dummy;
    struct dirpos p;
    uint8_t *b = malloc(512);
    if (!b) return;
    size_t plen = strlen(path);
    if (noisy && !pass2) {
        outs("Directory ");
        if (plen == 2) { outs(path); outs("\\"); } else outs(path);
        crlf();
    }
    dir_open(&p, cluster);
    uint32_t sec;
    int first_sector = 1;
    while (dir_next_sector(&p, b, &sec)) {
        int start = 0;
        if (first_sector && cluster && !pass2) {
            int skip;
            check_dots(cluster, parent, b, sec, &skip);
            if (skip) break;
        }
        if (first_sector && cluster) start = 2;
        first_sector = 0;
        for (unsigned i = start; i < 16; i++) {
            struct dent *e = (struct dent *)(b + i * 32);
            if (e->name[0] == 0) goto done;
            if ((uint8_t)e->name[0] == 0xE5) continue;
            if (e->name[0] == '.') continue;
            path[plen] = 0;
            strcat(path, "\\");
            fcbname(path, e->name);
            if (e->attr & 0x08) {
                if (!pass2) {
                    if (noisy) { outs("        "); outs(path); crlf(); }
                    hidcnt++;
                }
                continue;
            }
            int isdir = (e->attr & 0x10) != 0;
            if (pass2) {
                unsigned x = cross_in(e->cluster);
                if (x) {
                    char nb[12];
                    num(nb, x);
                    errs(path); errs("\r\n");
                    outs(M_CROSS); outs(nb); crlf();
                }
                if (isdir && valid(e->cluster)) walk(e->cluster, cluster);
                continue;
            }
            if (!isdir && noisy) { outs("        "); outs(path); crlf(); }
            int zt;
            unsigned n = markfat(e, sec, i * 32, isdir, &zt);
            if (isdir) {
                dircnt++;
                if (zt || !e->cluster) {
                    if (!zt && !e->cluster) {
                        eprint2(M_INVSUB, STDERR, 0);
                        if (noisy) { outs(M_UNRECDIR); crlf(); }
                        if (prompt_yn(M_CONVDIR)) {
                            e->attr &= ~0x10;
                            e->size = 0;
                            fixent(sec, i * 32, e);
                        }
                    } else {
                        e->attr &= ~0x10;       /* CONVDIR: now a file */
                        fixent(sec, i * 32, e);
                    }
                    continue;
                }
                dirclus += n;
                walk(e->cluster, cluster);
            } else if (e->attr & 0x02) { hidcnt++; hidclus += n; }
            else { filecnt++; filclus += n; }
        }
    }
done:
    path[plen] = 0;
    free(b);
}

/* --------------------------------------------------- lost clusters */

static void find_chains(void)
{
    for (unsigned c = 2; c <= mclus; c++) {
        if (!(map[c] & MAP_ORPH) || (map[c] & MAP_SEEN)) continue;
        map[c] |= MAP_SEEN | MAP_HEAD;
        orphcnt++;
        unsigned cur = c;
        for (;;) {
            unsigned nx = fget(cur);
            if (is_eof(nx)) break;
            if (nx < 2 || nx > dsize || nx == cur || !(map[nx] & MAP_ORPH)) { fset(cur, eofval | 7); break; }
            if (map[nx] & MAP_HEAD) {   /* an earlier chain continues this one */
                map[nx] &= ~MAP_HEAD;
                orphcnt--;
                break;
            }
            if (map[nx] & MAP_SEEN) { fset(cur, eofval | 7); break; }
            map[nx] |= MAP_SEEN;
            cur = nx;
        }
    }
}

static unsigned chain_len(unsigned c)
{
    unsigned n = 0, guard = dsize + 1;
    while (valid(c) && guard--) {
        n++;
        unsigned nx = fget(c);
        if (is_eof(nx)) break;
        c = nx;
    }
    return n;
}

static void recover_files(void)
{
    /* CHAINREC: a root entry FILEnnnn.CHK per chain */
    static uint8_t b[512] __attribute__((aligned(4)));
    unsigned next = 0, c = 2;
    uint16_t date = dos_date(), time = dos_time();
    for (unsigned s = 0; s < rootsecs; s++) {
        uint32_t sec = dpb.dir_sector + s;
        if (rdsec(sec, b)) return;
        int changed = 0;
        for (unsigned i = 0; i < 16; i++) {
            struct dent *e = (struct dent *)(b + i * 32);
            if (e->name[0] != 0 && (uint8_t)e->name[0] != 0xE5) continue;
            while (c <= mclus && !(map[c] & MAP_HEAD)) c++;
            if (c > mclus) break;
            /* a name nobody has */
            char nm[12];
            for (;;) {
                memcpy(nm, "FILE0000CHK", 11);
                nm[4] += next / 1000 % 10; nm[5] += next / 100 % 10; nm[6] += next / 10 % 10; nm[7] += next % 10;
                next++;
                static uint8_t t[512] __attribute__((aligned(4)));
                int dup = 0;
                for (unsigned s2 = 0; s2 < rootsecs && !dup; s2++) {
                    const uint8_t *q = s2 == s ? b : (rdsec(dpb.dir_sector + s2, t) ? 0 : t);
                    if (!q) continue;
                    for (unsigned j = 0; j < 16; j++) if (!memcmp(q + j * 32, nm, 11)) dup = 1;
                }
                if (!dup) break;
            }
            memset(e, 0, 32);
            memcpy(e->name, nm, 11);
            e->time = time; e->date = date;
            e->cluster = c;
            e->size = chain_len(c) * csize;
            map[c] &= ~MAP_HEAD;
            changed = 1;
            c++;
        }
        if (changed) wrsec(sec, b);
        while (c <= mclus && !(map[c] & MAP_HEAD)) c++;
        if (c > mclus) return;
    }
    /* the root is full */
    path[0] = 0;
    outs("\r\n");
    outs(M_ROOTFULL);
    crlf();
}

static void chkmap(void)
{
    for (unsigned c = 2; c <= mclus; c++) {
        if (map[c] & MAP_USED) continue;
        unsigned v = fget(c);
        if (!v) continue;
        if (v == badval) badclus++;
        else { map[c] |= MAP_ORPH; orphsiz++; }
    }
    if (!orphsiz) return;
    checkerr();
    crlf();
    find_chains();
    char a[12], b2[12];
    num(a, orphsiz); num(b2, orphcnt);
    errs("   "); errs(a); errs(" lost allocation units found in "); errs(b2); errs(" chains.");
    crlf();
    int save = dofix;
    if (ftrunc) dofix = 0;
    if (prompt_yn(M_CONVERT)) {
        if (dofix) recover_files();
    } else {
        for (unsigned c = 2; c <= mclus; c++) if (map[c] & MAP_ORPH) fset(c, 0);
        if (!dofix) { lclus = orphsiz; outnum(orphsiz * csize, " bytes disk space would be freed"); }
        else outnum(orphsiz * csize, " bytes disk space freed");
        orphsiz = 0;
    }
    dofix = save;
}

/* -------------------------------------------------------- the report */

static void outnum2(uint32_t bytes, const char *t1, uint32_t n, const char *t2)
{
    char b[16], c[12];
    fmtnum(b, bytes, 10, ' ');
    num(c, n);
    outs(b); outs(t1); outs(c); outs(t2); crlf();
}

static void report(void)
{
    R r = { 0 };
    r.r0 = 0x3600; r.r3 = drive + 1;
    dos(&r);
    uint32_t totc = r.r3 & 0xFFFF;
    outnum(totc * csize, " bytes total disk space");
    if (hidclus) outnum2(hidclus * csize, " bytes in ", hidcnt, " hidden files");
    if (dirclus) outnum2(dirclus * csize, " bytes in ", dircnt, " directories");
    if (filclus) outnum2(filclus * csize, " bytes in ", filecnt, " user files");
    if (orphsiz) outnum2(orphsiz * csize, dofix ? " bytes in " : " bytes would be in ", orphcnt, " recovered files");
    if (badclus) outnum(badclus * csize, " bytes in bad sectors");
    uint32_t freec = dsize - dirclus - filclus - hidclus - badclus - orphsiz - lclus;
    outnum(freec * csize, " bytes available on disk");
    crlf();
    outnum(csize, " bytes in each allocation unit");
    outnum(dsize, " total allocation units on disk");
    outnum(freec, " available allocation units on disk");
    crlf();
    struct psp *psp = _armdos_psp;
    uint32_t ebda = 0;
    R e = { 0 };
    e.r0 = 0xC100;
    if (!intr(0x15, &e) && (e.r8 & 0xFFFF)) ebda = *(uint8_t *)((e.r8 & 0xFFFF) << 4);
    outnum(((uint32_t)psp->memtop + ebda * 64) * 16, " total bytes memory");
    outnum(((uint32_t)psp->memtop - ((uint32_t)psp >> 4)) * 16, " bytes free");
}

/* ------------------------------------------------- volume identification */

static void printid(void)
{
    static uint8_t b[512] __attribute__((aligned(4)));
    for (unsigned s = 0; s < rootsecs; s++) {
        if (rdsec(dpb.dir_sector + s, b)) break;
        for (unsigned i = 0; i < 16; i++) {
            struct dent *e = (struct dent *)(b + i * 32);
            if (!e->name[0]) goto serial;
            if ((uint8_t)e->name[0] == 0xE5 || (e->attr & 0x1F) != 0x08) continue;
            char t[80], *p = t;
            memcpy(p, "Volume ", 7); p += 7;
            memcpy(p, e->name, 11); p += 11;
            memcpy(p, " created ", 9); p += 9;
            unsigned d = e->date, tm = e->time;
            p = fmtnum(p, (d >> 5) & 15, 2, '0'); *p++ = '-';
            p = fmtnum(p, d & 31, 2, '0'); *p++ = '-';
            p = fmtnum(p, 1980 + (d >> 9), 4, '0'); *p++ = ' ';
            unsigned h = tm >> 11, m = (tm >> 5) & 63;
            p = fmtnum(p, h % 12 ? h % 12 : 12, 1, ' '); *p++ = ':';
            p = fmtnum(p, m, 2, '0');
            *p++ = h >= 12 ? 'p' : 'a';
            *p++ = '\r'; *p++ = '\n'; *p = 0;
            crlf();
            outs(t);
            goto serial;
        }
    }
serial:;
    struct { uint16_t level; uint32_t serial; char label[11]; char fs[8]; } __attribute__((packed)) mid;
    memset(&mid, 0, sizeof mid);
    if (!gen_ioctl(drive, 0x66, &mid)) serial_line(mid.serial);
}

/* --------------------------------------------- CHKDSK filespec (contiguity) */

static int match11(const char *pat, const char *name)
{
    for (int i = 0; i < 11; i++) if (pat[i] != '?' && pat[i] != name[i]) return 0;
    return 1;
}

static void to_fcb(char *out, const char *s)
{
    memset(out, ' ', 11);
    int i = 0;
    for (; *s && *s != '.' && i < 8; s++) {
        if (*s == '*') { while (i < 8) out[i++] = '?'; }
        else out[i++] = *s;
    }
    while (*s && *s != '.') s++;
    if (*s == '.') {
        s++;
        for (i = 8; *s && i < 11; s++) {
            if (*s == '*') { while (i < 11) out[i++] = '?'; }
            else out[i++] = *s;
        }
    }
}

static void check_files(void)
{
    /* the directory: the path part of the filespec, from the root */
    char spec[80], *name;
    strcpy(spec, filespec);
    upcase(spec);
    name = strrchr(spec, '\\');
    char dirpart[80] = "";
    if (name) { *name = 0; strcpy(dirpart, spec); name++; }
    else name = spec;
    if (!*name) name = "*.*";
    unsigned cl = 0;
    char *p = dirpart;
    while (*p == '\\') p++;
    crlf();
    while (*p) {
        char comp[16], *q = comp;
        while (*p && *p != '\\' && q < comp + 12) *q++ = *p++;
        *q = 0;
        while (*p == '\\') p++;
        char want[11];
        to_fcb(want, comp);
        struct dirpos dp;
        dir_open(&dp, cl);
        uint32_t sec;
        int found = 0;
        while (!found && dir_next_sector(&dp, secbuf, &sec)) {
            for (unsigned i = 0; i < 16 && !found; i++) {
                struct dent *e = (struct dent *)(secbuf + i * 32);
                if (!e->name[0]) break;
                if ((e->attr & 0x10) && !memcmp(e->name, want, 11)) { found = 1; cl = e->cluster; }
            }
        }
        if (!found) { errs("Path not found\r\n"); return; }
    }
    char pat[11];
    to_fcb(pat, name);
    char base[100];
    base[0] = 'A' + drive; base[1] = ':'; base[2] = 0;
    if (*dirpart) { if (dirpart[0] != '\\') strcat(base, "\\"); strcat(base, dirpart); }
    struct dirpos dp;
    dir_open(&dp, cl);
    uint32_t sec;
    int any = 0, frag = 0;
    while (dir_next_sector(&dp, secbuf, &sec)) {
        for (unsigned i = 0; i < 16; i++) {
            struct dent *e = (struct dent *)(secbuf + i * 32);
            if (!e->name[0]) goto end;
            if ((uint8_t)e->name[0] == 0xE5 || (e->attr & 0x1E)) continue;
            if (!match11(pat, e->name)) continue;
            any = 1;
            unsigned breaks = 0, c = e->cluster, guard = dsize;
            while (valid(c) && guard--) {
                unsigned nx = fget(c);
                if (is_eof(nx) || !valid(nx)) break;
                if (nx != c + 1) breaks++;
                c = nx;
            }
            if (breaks) {
                char t[100], nb[12];
                strcpy(t, base); strcat(t, "\\"); fcbname(t, e->name);
                num(nb, breaks + 1);
                outs(t); outs(" Contains "); outs(nb); outs(" non-contiguous blocks"); crlf();
                frag = 1;
            }
        }
    }
end:
    if (!any) errs("File not found\r\n");
    else if (!frag) { outs("All specified file(s) are contiguous"); crlf(); }
}

/* ------------------------------------------------------------- main */

static void parse(void)
{
    static struct arg a[8];
    int n = parse_tail(a, 8), pos = 0;
    drive = cur_drive();
    for (int i = 0; i < n; i++) {
        struct arg *x = &a[i];
        if (x->sw) {
            char s[8];
            strncpy(s, x->text, 7); s[7] = 0;
            upcase(s);
            if (!strcmp(s, "/F") && !x->val) dofix = 1;
            else if (!strcmp(s, "/V") && !x->val) noisy = 1;
            else { parse_err(M_INVSW, x->shown); dos_exit(0); }
            continue;
        }
        if (pos++) { parse_err(M_TOOMANY, x->shown); dos_exit(0); }
        char *s = x->text;
        if (s[0] && s[1] == ':') {
            int d = (s[0] & 0xDF) - 'A';
            if (d < 0 || d > 25) { errs(M_INVDRIVE); errs("\r\n"); dos_exit(0); }
            drive = d;
            s += 2;
        }
        if (*s) {
            /* relative names are relative to the drive's current directory */
            if (*s != '\\') {
                char cwd[70];
                R r = { 0 };
                r.r0 = 0x4700; r.r3 = drive + 1; r.r4 = (uint32_t)cwd;
                if (dos(&r)) cwd[0] = 0;
                strcpy(filespec, "\\");
                strcat(filespec, cwd);
                if (cwd[0]) strcat(filespec, "\\");
                strcat(filespec, s);
            } else strcpy(filespec, s);
            have_spec = 1;
        }
    }
}

int main(void)
{
    check_version();
    disk_reset();
    parse();
    unsigned attr;
    if (ioctl_remote(drive, &attr)) { errs(M_INVDRIVE); errs("\r\n"); dos_exit(0); }
    if (attr & 0x1200) { errs("Cannot CHKDSK a network drive\r\n"); dos_exit(0); }
    if ((attr & 0x8000) || truename_letter(drive) != 'A' + drive) {
        errs("Cannot CHKDSK a SUBSTed or ASSIGNed drive\r\n"); dos_exit(0);
    }
    struct kdpb *d = get_dpb(drive);
    if (!d) { errs(M_INVDRIVE); errs("\r\n"); dos_exit(255); }
    dpb = *d;
    bps = dpb.sector_size;
    if (bps != 512) { errs("Invalid media type\r\n"); dos_exit(0); }
    spc = dpb.cluster_mask + 1;
    csize = spc * bps;
    mclus = dpb.max_cluster;
    dsize = mclus - 1;
    rootsecs = (dpb.root_ents * 32 + bps - 1) / bps;
    fat16 = mclus >= 4086;
    eofval = fat16 ? 0xFFF8 : 0xFF8;
    badval = fat16 ? 0xFFF7 : 0xFF7;
    fatbytes = (uint32_t)dpb.fat_size * bps;
    fat = malloc(fatbytes);
    map = calloc(mclus + 2, 1);
    if (!fat || !map) { errs("Insufficient memory\r\n"); dos_exit(0); }

    printid();
    int ok = 0;
    for (unsigned f = 0; f < dpb.nfats && !ok; f++)
        ok = abs_rw(drive, 0, dpb.first_fat + f * dpb.fat_size, dpb.fat_size, fat) == 0;
    if (!ok) {
        outs("   Processing cannot continue    File allocation table bad"); crlf();
        dos_exit(255);
    }
    if (fat[0] < 0xF8 && fat[0] != 0xF0) {
        if (!prompt_yn(M_NONDOS)) dos_exit(0);
    }

    path[0] = 'A' + drive; path[1] = ':'; path[2] = 0;
    walk(0, 0);
    chkmap();
    if (crosscnt) {
        crlf();
        pass2 = 1;
        path[2] = 0;
        walk(0, 0);
        pass2 = 0;
    }
    crlf();
    report();

    if (dirtyfat && dofix) {
        for (unsigned f = 0; f < dpb.nfats; f++) {
            if (abs_rw(drive, 1, dpb.first_fat + f * dpb.fat_size, dpb.fat_size, fat)) {
                char n[4] = { '1' + f, 0 };
                outs("   Disk error writing FAT "); outs(n); crlf();
            }
        }
    }
    disk_reset();
    if (dofix) forget_free(drive, -1);
    if (have_spec) check_files();
    return 0;
}
