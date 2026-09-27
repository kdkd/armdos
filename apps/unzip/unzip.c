/* unzip.c - UNZIP, a ZIP extract utility for ARM-DOS.
 *
 * Original code for ARM-DOS (no Info-ZIP or PKWARE code): a PKUNZIP-style
 * front end over a small streaming inflater (RFC 1951) written from the
 * DEFLATE specification. Methods: 0 stored, 8 deflated. CRC-32 checked.
 *
 *   UNZIP [options] zipfile[.ZIP] [d:\outdir\] [file specs...]
 *     -o overwrite   -n newer only   -d restore dirs   -v view
 *     -t test        -c extract to console
 *
 * Errorlevels (as PKUNZIP): 0 ok, 1 warning, 3 bad data / CRC, 9 zip not
 * found, 10 bad option, 11 no files found, 50 disk full, 51 unexpected end.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <io.h>
#include <dos.h>
#include <conio.h>
#include <direct.h>

#define E_OK      0
#define E_WARN    1
#define E_DATA    3
#define E_NOZIP   9
#define E_OPTION 10
#define E_NOFILES 11
#define E_FULL   50
#define E_EOF    51

static int opt_o, opt_n, opt_d, opt_v, opt_t, opt_c, all_yes;
static int errlevel;
static void seterr(int e) { if (e > errlevel) errlevel = e; }

/* ------------------------------------------------------------ CRC-32 */
static unsigned long crctab[256];
static void crc_init(void)
{
    for (unsigned n = 0; n < 256; n++) {
        unsigned long c = n;
        for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320UL ^ (c >> 1) : c >> 1;
        crctab[n] = c;
    }
}
static unsigned long crc;

/* ------------------------------------------------------------ zip input */
static int zfd = -1;
static unsigned char ibuf[4096];
static int ipos, ilen;
static unsigned long in_left;     /* compressed bytes of the member not yet read */
static int overrun, short_read;

static int inbyte(void)
{
    if (!in_left) { overrun++; return 0; }
    if (ipos >= ilen) {
        unsigned want = in_left < sizeof ibuf ? (unsigned)in_left : sizeof ibuf;
        ilen = read(zfd, ibuf, want);
        ipos = 0;
        if (ilen <= 0) { ilen = 0; short_read = 1; overrun++; in_left = 0; return 0; }
    }
    in_left--;
    return ibuf[ipos++];
}

static int zseek(unsigned long off) { ipos = ilen = 0; return lseek(zfd, (long)off, SEEK_SET) == (long)off ? 0 : -1; }
static int zread(void *b, int n) { ipos = ilen = 0; return read(zfd, b, n) == n ? 0 : -1; }
static unsigned g16(const unsigned char *p) { return p[0] | p[1] << 8; }
static unsigned long g32(const unsigned char *p) { return g16(p) | (unsigned long)g16(p + 2) << 16; }

/* ------------------------------------------------------------ output */
#define WSIZE 32768u
static unsigned char win[WSIZE];
static unsigned wpos;
static int out_fd = -1;           /* -1: test only; 1: console */
static unsigned long out_total;
static int write_fail;

static void flush_win(void)
{
    const unsigned char *p = win;
    unsigned long c = crc;
    for (unsigned i = 0; i < wpos; i++) c = crctab[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    crc = c;
    if (out_fd >= 0 && wpos && !write_fail) {
        int w = write(out_fd, win, wpos);
        if (w != (int)wpos) write_fail = 1;
    }
    out_total += wpos;
    wpos = 0;
}
#define PUT(ch) do { win[wpos++] = (unsigned char)(ch); if (wpos == WSIZE) flush_win(); } while (0)

/* ------------------------------------------------------------ inflate */
static unsigned long bitbuf;
static int bitcnt;

static inline void need(int n)
{
    while (bitcnt < n) { bitbuf |= (unsigned long)inbyte() << bitcnt; bitcnt += 8; }
}
static inline unsigned bits(int n)
{
    need(n);
    unsigned v = (unsigned)(bitbuf & ((1UL << n) - 1));
    bitbuf >>= n; bitcnt -= n;
    return v;
}

#define FASTBITS 9
struct huff {
    short count[16];
    short symbol[288];
    unsigned short fast[1 << FASTBITS];     /* symbol << 4 | length, 0 = slow path */
};

static int build(struct huff *h, const unsigned char *len, int n)
{
    short offs[16];
    memset(h->count, 0, sizeof h->count);
    for (int s = 0; s < n; s++) h->count[len[s]]++;
    if (h->count[0] == n) { memset(h->fast, 0, sizeof h->fast); return 0; }
    int left = 1;
    for (int l = 1; l < 16; l++) { left <<= 1; left -= h->count[l]; if (left < 0) return -1; }
    offs[1] = 0;
    for (int l = 1; l < 15; l++) offs[l + 1] = offs[l] + h->count[l];
    for (int s = 0; s < n; s++) if (len[s]) h->symbol[offs[len[s]]++] = (short)s;
    /* first-level table for codes up to FASTBITS long (codes are sent LSB first) */
    memset(h->fast, 0, sizeof h->fast);
    int code = 0, idx = 0;
    for (int l = 1; l <= FASTBITS; l++) {
        for (int k = 0; k < h->count[l]; k++, code++, idx++) {
            int rev = 0;
            for (int b = 0; b < l; b++) if (code & (1 << b)) rev |= 1 << (l - 1 - b);
            for (int i = rev; i < (1 << FASTBITS); i += 1 << l) h->fast[i] = (unsigned short)(h->symbol[idx] << 4 | l);
        }
        code <<= 1;
    }
    return left;
}

static int decode(const struct huff *h)
{
    need(FASTBITS);
    unsigned e = h->fast[bitbuf & ((1 << FASTBITS) - 1)];
    if (e) { bitbuf >>= e & 15; bitcnt -= e & 15; return e >> 4; }
    int code = 0, first = 0, index = 0;
    for (int l = 1; l < 16; l++) {
        code |= (int)bits(1);
        int count = h->count[l];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count; first += count;
        first <<= 1; code <<= 1;
    }
    return -1;
}

static const unsigned short lbase[29] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
static const unsigned char lext[29] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
static const unsigned short dbase[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
static const unsigned char dext[30] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };

static struct huff lcode, dcode, fixl, fixd;
static int fixed_built;

static int codes(const struct huff *lc, const struct huff *dc)
{
    for (;;) {
        int sym = decode(lc);
        if (sym < 0) return -1;
        if (sym < 256) { PUT(sym); continue; }
        if (sym == 256) return 0;
        sym -= 257;
        if (sym >= 29) return -1;
        unsigned len = lbase[sym] + bits(lext[sym]);
        int ds = decode(dc);
        if (ds < 0 || ds >= 30) return -1;
        unsigned dist = dbase[ds] + bits(dext[ds]);
        if (dist > out_total + wpos) return -1;          /* before the start of the output */
        unsigned from = (wpos - dist) & (WSIZE - 1);
        while (len--) { unsigned char c = win[from]; from = (from + 1) & (WSIZE - 1); PUT(c); }
        if (overrun > 4) return -1;
    }
}

static int inflate_data(void)
{
    static const unsigned char order[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
    unsigned char lens[320];
    bitbuf = 0; bitcnt = 0;
    int last;
    do {
        last = (int)bits(1);
        int type = (int)bits(2);
        if (type == 0) {
            bitbuf >>= bitcnt & 7; bitcnt -= bitcnt & 7;
            unsigned len = bits(16), nlen = bits(16);
            if ((len ^ 0xFFFF) != nlen) return -1;
            while (len--) PUT(bits(8));
        } else if (type == 1) {
            if (!fixed_built) {
                int s = 0;
                for (; s < 144; s++) lens[s] = 8;
                for (; s < 256; s++) lens[s] = 9;
                for (; s < 280; s++) lens[s] = 7;
                for (; s < 288; s++) lens[s] = 8;
                build(&fixl, lens, 288);
                for (s = 0; s < 30; s++) lens[s] = 5;
                build(&fixd, lens, 30);
                fixed_built = 1;
            }
            if (codes(&fixl, &fixd)) return -1;
        } else if (type == 2) {
            int nlen = (int)bits(5) + 257, ndist = (int)bits(5) + 1, ncode = (int)bits(4) + 4;
            if (nlen > 286 || ndist > 30) return -1;
            memset(lens, 0, 19);
            for (int i = 0; i < ncode; i++) lens[order[i]] = (unsigned char)bits(3);
            if (build(&lcode, lens, 19) != 0) return -1;
            int i = 0;
            while (i < nlen + ndist) {
                int sym = decode(&lcode);
                if (sym < 0) return -1;
                if (sym < 16) { lens[i++] = (unsigned char)sym; continue; }
                int l = 0, rep;
                if (sym == 16) { if (!i) return -1; l = lens[i - 1]; rep = 3 + (int)bits(2); }
                else if (sym == 17) rep = 3 + (int)bits(3);
                else rep = 11 + (int)bits(7);
                if (i + rep > nlen + ndist) return -1;
                while (rep--) lens[i++] = (unsigned char)l;
            }
            if (lens[256] == 0) return -1;
            int e = build(&lcode, lens, nlen);
            if (e < 0 || (e > 0 && nlen - lcode.count[0] != 1)) return -1;
            e = build(&dcode, lens + nlen, ndist);
            if (e < 0 || (e > 0 && ndist - dcode.count[0] != 1)) return -1;
            if (codes(&lcode, &dcode)) return -1;
        } else return -1;
        if (overrun > 4) return -1;
    } while (!last);
    return 0;
}

/* ------------------------------------------------------------ names */
static int wildmatch(const char *p, const char *s)
{
    for (; *p; p++, s++) {
        if (*p == '*') {
            while (p[1] == '*') p++;
            if (!p[1]) return 1;
            for (; *s; s++) if (wildmatch(p + 1, s)) return 1;
            return 0;
        }
        if (!*s) return 0;
        if (*p != '?' && toupper((unsigned char)*p) != toupper((unsigned char)*s)) return 0;
    }
    return !*s;
}

static const char *basename_of(const char *n)
{
    const char *b = n;
    for (const char *p = n; *p; p++) if (*p == '/' || *p == '\\') b = p + 1;
    return b;
}

static int spec_match(char **specs, int nspecs, const char *name)
{
    if (!nspecs) return 1;
    const char *b = basename_of(name);
    for (int i = 0; i < nspecs; i++) {
        const char *s = specs[i];
        if (!strcmp(s, "*.*") || !strcmp(s, "*")) return 1;
        int haspath = strchr(s, '\\') || strchr(s, '/');
        if (!haspath) { if (wildmatch(s, b)) return 1; }
        else {
            char t[128]; snprintf(t, sizeof t, "%s", s);
            for (char *q = t; *q; q++) if (*q == '\\') *q = '/';
            if (wildmatch(t, name)) return 1;
        }
    }
    return 0;
}

/* one path component -> a DOS 8.3 name, upper case */
static void dosname(const char *in, int n, char *out)
{
    char base[9], ext[4];
    int bl = 0, el = 0, dot = -1;
    for (int i = 0; i < n; i++) if (in[i] == '.') dot = i;
    if (dot == 0) dot = -1;
    for (int i = 0; i < (dot >= 0 ? dot : n) && bl < 8; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c == '.' || c == ' ') continue;
        if (strchr("\"*+,/:;<=>?[]|", c) || c < 32) c = '_';
        base[bl++] = (char)toupper(c);
    }
    if (dot >= 0) for (int i = dot + 1; i < n && el < 3; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c == '.' || c == ' ') continue;
        if (strchr("\"*+,/:;<=>?[]|", c) || c < 32) c = '_';
        ext[el++] = (char)toupper(c);
    }
    if (!bl) base[bl++] = '_';
    base[bl] = 0; ext[el] = 0;
    sprintf(out, el ? "%s.%s" : "%s", base, ext);
}

/* zip name -> relative DOS path (with or without its directories) */
static void local_name(const char *zname, char *out, int keepdirs)
{
    out[0] = 0;
    const char *p = keepdirs ? zname : basename_of(zname);
    while (*p == '/' || *p == '\\') p++;
    while (*p) {
        const char *e = p;
        while (*e && *e != '/' && *e != '\\') e++;
        if (e > p && !(e - p == 1 && p[0] == '.') && !(e - p == 2 && p[0] == '.' && p[1] == '.')) {
            char comp[16];
            dosname(p, (int)(e - p), comp);
            if (out[0]) strcat(out, "\\");
            strcat(out, comp);
        }
        p = e;
        while (*p == '/' || *p == '\\') p++;
    }
}

static void make_dirs(const char *path)          /* every directory in path (not the last component) */
{
    char t[160];
    snprintf(t, sizeof t, "%s", path);
    for (char *p = t; *p; p++) {
        if (*p == '\\' && p > t && p[-1] != ':') {
            *p = 0;
            struct stat st;
            if (stat(t, &st) != 0) mkdir(t);
            *p = '\\';
        }
    }
}

/* ------------------------------------------------------------ members */
struct member {
    unsigned method, flags, dtime, ddate, attr;
    unsigned long crc, csize, usize, loff;
    char name[128];
};

static const char *method_name(const struct member *m)
{
    static const char *const n[] = { "Stored", "Shrunk", "Reduced1", "Reduced2", "Reduced3", "Reduced4", "Imploded", "Tokenzd" };
    if (m->method == 8) { static const char *const d[] = { "DeflatN", "DeflatX", "DeflatF", "DeflatS" }; return d[(m->flags >> 1) & 3]; }
    if (m->method < 8) return n[m->method];
    if (m->method == 9) return "Defl64";
    return "Unknown";
}

static int ratio(unsigned long len, unsigned long size)
{
    if (!len || size >= len) return 0;
    return (int)(((unsigned long long)(len - size) * 1000 / len + 5) / 10);
}

static void put_date(char *b, unsigned d, unsigned t)
{
    sprintf(b, "%02u-%02u-%02u  %02u:%02u", (d >> 5) & 15, d & 31, ((d >> 9) + 80) % 100, t >> 11, (t >> 5) & 63);
}

static char outdir[100];
static unsigned long tot_len, tot_size;
static int nfiles;

static int ask_overwrite(const char *name, char *path, int pathmax)
{
    if (all_yes) return 1;
    for (;;) {
        printf("WARNING: %s already exists.  Overwrite (y/n/a/r)? ", name);
        fflush(stdout);
        int c = toupper(getch());
        if (c == 3 || c == 27) c = 'N';
        if (c == 'Y' || c == 'N' || c == 'A' || c == 'R') {
            printf("%c\n", c);
            if (c == 'Y') return 1;
            if (c == 'N') return 0;
            if (c == 'A') { all_yes = 1; return 1; }
            printf("Enter new name: ");
            fflush(stdout);
            char nn[80];
            if (!fgets(nn, sizeof nn, stdin)) return 0;
            char *e = strpbrk(nn, "\r\n"); if (e) *e = 0;
            if (!nn[0]) return 0;
            char *slash = strrchr(path, '\\');
            char *colon = strrchr(path, ':');
            char *cut = slash > colon ? slash : colon;
            if (cut) cut[1] = 0; else path[0] = 0;
            strncat(path, nn, pathmax - strlen(path) - 1);
            if (access(path, 0) != 0) return 1;
            name = path;
        }
    }
}

static int is_newer(const char *path, const struct member *m)
{
    int fd = open(path, O_RDONLY | O_BINARY);
    if (fd < 0) return 1;
    unsigned d = 0, t = 0;
    _dos_getftime(fd, &d, &t);
    close(fd);
    unsigned long have = (unsigned long)d << 16 | t, zip = (unsigned long)m->ddate << 16 | m->dtime;
    return zip > have;
}

static void process(struct member *m, char **specs, int nspecs)
{
    int isdir = m->name[0] && (m->name[strlen(m->name) - 1] == '/' || m->name[strlen(m->name) - 1] == '\\');
    if (!spec_match(specs, nspecs, m->name)) return;
    if (opt_v) {
        char dt[24];
        char at[5] = "----";
        if (m->attr & 0x20) at[0] = 'a';
        if (m->attr & 0x02) at[1] = 'h';
        if (!(m->attr & 0x01)) at[2] = 'w';
        if (m->attr & 0x04) at[3] = 's';
        put_date(dt, m->ddate, m->dtime);
        printf("%7lu  %-7s %7lu %4d%%  %s  %08lx %s  %s\n", m->usize, method_name(m), m->csize, ratio(m->usize, m->csize),
               dt, m->crc, at, m->name);
        tot_len += m->usize; tot_size += m->csize; nfiles++;
        return;
    }
    char rel[140], path[240];
    local_name(m->name, rel, opt_d);
    if (isdir) {
        nfiles++;
        if (opt_d && !opt_t && !opt_c && rel[0]) { snprintf(path, sizeof path, "%s%s\\", outdir, rel); make_dirs(path); }
        return;
    }
    if (!rel[0]) return;
    nfiles++;
    snprintf(path, sizeof path, "%s%s", outdir, rel);
    int stored = m->method == 0;
    if (m->flags & 1) { fflush(stdout); fprintf(stderr, "UNZIP: Warning! %s is encrypted - skipped\n", rel); seterr(E_WARN); return; }
    if (m->method != 0 && m->method != 8) {
        fflush(stdout);
        fprintf(stderr, "UNZIP: Warning! %s: unsupported compression method (%s) - skipped\n", rel, method_name(m));
        seterr(E_WARN);
        return;
    }
    out_fd = -1;
    if (opt_t) { printf("    Testing: %-14s", rel); fflush(stdout); }
    else if (opt_c) { printf("%s\n", rel); fflush(stdout); out_fd = 1; setmode(1, O_BINARY); }
    else {
        if (access(path, 0) == 0) {
            if (opt_n && !is_newer(path, m)) return;
            if (!opt_o && !opt_n && !ask_overwrite(rel, path, sizeof path)) return;
        }
        if (opt_d) make_dirs(path);
        out_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0644);
        if (out_fd < 0) { fflush(stdout); fprintf(stderr, "UNZIP: Can't create %s\n", path); seterr(E_WARN); return; }
        printf("  %s: %s\n", stored ? "Extracting" : " Inflating", path);
        fflush(stdout);
    }
    /* local header */
    unsigned char lh[30];
    int bad = 0, eof = 0;
    if (zseek(m->loff) || zread(lh, 30) || g32(lh) != 0x04034b50UL) bad = 1;
    else if (lseek(zfd, (long)(m->loff + 30 + g16(lh + 26) + g16(lh + 28)), SEEK_SET) < 0) bad = 1;
    crc = 0xFFFFFFFFUL; wpos = 0; out_total = 0; write_fail = 0; overrun = 0; short_read = 0;
    in_left = m->csize;
    if (!bad) {
        if (stored) { unsigned long n = m->usize; while (n--) PUT(inbyte()); if (overrun) bad = 1; }
        else if (inflate_data()) bad = 1;
        flush_win();
        if (short_read) eof = 1;
    }
    crc ^= 0xFFFFFFFFUL;
    if (out_fd > 1) {
        _dos_setftime(out_fd, m->ddate, m->dtime);
        close(out_fd);
        if (m->attr & 0x07) _dos_setfileattr(path, m->attr & 0x07);
    }
    if (opt_c) setmode(1, O_TEXT);
    out_fd = -1;
    if (write_fail) { fflush(stdout); fprintf(stderr, "UNZIP: Disk full writing %s\n", path); seterr(E_FULL); return; }
    if (eof) { if (opt_t) printf("\n"); fflush(stdout); fprintf(stderr, "UNZIP: Unexpected end of zip file in %s\n", rel); seterr(E_EOF); return; }
    if (bad || out_total != m->usize || crc != m->crc) {
        if (opt_t) printf(" FAILED\n");
        fflush(stdout);
        fprintf(stderr, "UNZIP: Warning! %s: %s\n", rel, bad ? "bad compressed data" : "CRC error");
        seterr(E_DATA);
        return;
    }
    if (opt_t) printf(" OK\n");
}

/* ------------------------------------------------------------ main */
static void banner(void)
{
    printf("UNZIP  Extract Utility  Version 1.0  ARM-DOS\n"
           "(C) 1989 Europa Micro Systems.  All Rights Reserved.\n\n");
}

static void usage(void)
{
    printf("Usage:  UNZIP [options] zipfile[.ZIP] [d:\\outdir\\] [file...]\n\n"
           "  -o  overwrite existing files without asking\n"
           "  -n  extract only files newer than the ones on disk\n"
           "  -d  restore the directory structure stored in the zip\n"
           "  -v  view: list the files in the zip\n"
           "  -t  test the integrity of the zip\n"
           "  -c  extract to the console (screen)\n\n"
           "Wildcards (* and ?) select files; the default is all files.\n");
}

int main(int argc, char **argv)
{
    char *zipname = NULL, *specs[32];
    int nspecs = 0;
    crc_init();
    banner();
    for (int i = 1; i < argc; i++) {
        char *a = argv[i];
        if (a[0] == '-' || a[0] == '/') {
            for (char *p = a + 1; *p; p++) {
                switch (toupper((unsigned char)*p)) {
                case 'O': opt_o = 1; break;
                case 'N': opt_n = 1; break;
                case 'D': opt_d = 1; break;
                case 'V': opt_v = 1; break;
                case 'T': opt_t = 1; break;
                case 'C': opt_c = 1; break;
                default:
                    fprintf(stderr, "UNZIP: Invalid option: %c%c\n", a[0], *p);
                    usage();
                    return E_OPTION;
                }
            }
            continue;
        }
        if (!zipname) { zipname = a; continue; }
        size_t l = strlen(a);
        struct stat st;
        if (!outdir[0] && !nspecs && (a[l - 1] == '\\' || a[l - 1] == '/' || a[l - 1] == ':' ||
                                      (!strpbrk(a, "*?") && stat(a, &st) == 0 && S_ISDIR(st.st_mode)))) {
            snprintf(outdir, sizeof outdir - 1, "%s", a);
            for (char *q = outdir; *q; q++) if (*q == '/') *q = '\\';
            l = strlen(outdir);
            if (outdir[l - 1] != '\\' && outdir[l - 1] != ':') strcat(outdir, "\\");
            continue;
        }
        if (nspecs < 32) specs[nspecs++] = a;
    }
    if (!zipname) { usage(); return E_OK; }

    char zpath[100];
    snprintf(zpath, sizeof zpath - 4, "%s", zipname);
    zfd = open(zpath, O_RDONLY | O_BINARY);
    if (zfd < 0 && !strchr(basename_of(zpath), '.')) { strcat(zpath, ".ZIP"); zfd = open(zpath, O_RDONLY | O_BINARY); }
    if (zfd < 0) { fprintf(stderr, "UNZIP: Zip file not found: %s\n", zpath); return E_NOZIP; }
    for (char *q = zpath; *q; q++) *q = (char)toupper((unsigned char)*q);

    if (outdir[0] && !opt_v && !opt_t && !opt_c) {
        char t[110]; snprintf(t, sizeof t, "%sX", outdir); make_dirs(t);
    }

    printf(opt_v ? "Searching ZIP: %s\n\n" : "Searching: %s\n", zpath);
    if (opt_v) printf(" Length  Method     Size Ratio    Date    Time    CRC-32  Attr  Name\n"
                      " ------  ------     ---- -----    ----    ----    ------  ----  ----\n");

    /* the end of central directory record */
    long zsize = lseek(zfd, 0, SEEK_END);
    static unsigned char tail[1024 + 22];
    long found = -1;
    unsigned long cdoff = 0, cdcount = 0;
    for (long back = 0; found < 0 && back < 65536 + 22 && back < zsize; back += 1024) {
        long start = zsize - back - (long)sizeof tail;
        if (start < 0) start = 0;
        int n = (int)(zsize - back - start);
        if (n < 22) break;
        if (zseek((unsigned long)start) || zread(tail, n)) break;
        for (int i = n - 22; i >= 0; i--)
            if (tail[i] == 'P' && tail[i + 1] == 'K' && tail[i + 2] == 5 && tail[i + 3] == 6) {
                found = start + i;
                cdcount = g16(tail + i + 10);
                cdoff = g32(tail + i + 16);
                break;
            }
        if (start == 0) break;
    }

    struct member m;
    if (found >= 0) {
        unsigned long pos = cdoff;
        for (unsigned long k = 0; k < cdcount; k++) {
            unsigned char h[46];
            if (zseek(pos) || zread(h, 46) || g32(h) != 0x02014b50UL) {
                fflush(stdout); fprintf(stderr, "UNZIP: Unexpected end of zip file (central directory)\n"); seterr(E_EOF); break;
            }
            memset(&m, 0, sizeof m);
            m.flags = g16(h + 8); m.method = g16(h + 10); m.dtime = g16(h + 12); m.ddate = g16(h + 14);
            m.crc = g32(h + 16); m.csize = g32(h + 20); m.usize = g32(h + 24);
            unsigned nl = g16(h + 28), xl = g16(h + 30), cl = g16(h + 32);
            m.attr = h[38];
            m.loff = g32(h + 42);
            unsigned take = nl < sizeof m.name - 1 ? nl : sizeof m.name - 1;
            if (zread(m.name, (int)take)) { seterr(E_EOF); break; }
            m.name[take] = 0;
            pos += 46 + nl + xl + cl;
            process(&m, specs, nspecs);
        }
    } else {
        /* no central directory: walk the local headers */
        unsigned long pos = 0;
        for (;;) {
            unsigned char h[30];
            if (zseek(pos) || zread(h, 30) || g32(h) != 0x04034b50UL) break;
            memset(&m, 0, sizeof m);
            m.flags = g16(h + 6); m.method = g16(h + 8); m.dtime = g16(h + 10); m.ddate = g16(h + 12);
            m.crc = g32(h + 14); m.csize = g32(h + 18); m.usize = g32(h + 22);
            unsigned nl = g16(h + 26), xl = g16(h + 28);
            unsigned take = nl < sizeof m.name - 1 ? nl : sizeof m.name - 1;
            if (zread(m.name, (int)take)) break;
            m.name[take] = 0;
            m.loff = pos;
            if (m.flags & 8) { fflush(stdout); fprintf(stderr, "UNZIP: Zip file has no central directory\n"); seterr(E_EOF); break; }
            pos += 30 + nl + xl + m.csize;
            process(&m, specs, nspecs);
        }
        if (!nfiles && errlevel < E_EOF) { fflush(stdout); fprintf(stderr, "UNZIP: %s is not a zip file\n", zpath); seterr(E_EOF); }
    }
    close(zfd);

    if (opt_v && nfiles)
        printf(" ------           ------  ---                                   -------\n"
               "%7lu          %7lu %4d%%                                  %d\n", tot_len, tot_size, ratio(tot_len, tot_size), nfiles);
    if (!nfiles && !errlevel) { fflush(stdout); fprintf(stderr, "UNZIP: No file(s) found.\n"); return E_NOFILES; }
    return errlevel;
}
