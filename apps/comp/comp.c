/*
 * COMP - ARM-DOS re-creation of the MS-DOS 4.00 COMP command.
 *
 *   COMP [d:][path][filename1] [d:][path][filename2]
 *
 * Behaviour follows CMD/COMP/COMP2.ASM and COMPPAR.ASM of the MS-DOS 4.0
 * source (MIT licence, (C) Microsoft Corp.): names that are missing are
 * asked for ("Enter primary filename" / "Enter 2nd filename or drive id",
 * buffered input on STDERR); a path that is a directory means all its files,
 * a name that is not a directory is split into directory + file name, and
 * COMP changes into those directories as the original does (and changes
 * back).  Wildcards in the first name select the files, "?" in the second
 * takes the character of the first.  For each pair: CR LF CR LF, "%1 and
 * %2", CR LF, then either "Files are different sizes", up to ten "Compare
 * error at OFFSET x / File 1 = x / File 2 = x" (hex, no padding) ending in
 * "10 Mismatches - ending compare", or "EOF mark not found" and/or "Files
 * compare OK".  Finally "Compare more files (Y/N) ?" (STDERR, INT 21h
 * AH=0Ch AL=01h).  The original searches and reads with FCBs; this version
 * uses handle calls with the same patterns and attributes.
 */
#include "u4.h"

#define LBUF 61440u             /* COMP's large buffer on a machine with memory */

static char path1[132], path2[132];
static int  last1, last2;       /* index of the last "\" (-1 none) = findfs */
static uint8_t fcb[40], fcb2[40], infcb[40], outfcb[40];
static char name1[140], name2[140];
static char oldp1[70], oldp2[70];
static int  curdrv;             /* 1 = A: */
static int  swt16;              /* secondary directory already found */
static int  clear;
static uint8_t dta1[64], dta2[64];
static uint8_t buf1[4096], buf2[4096];

/* ------------------------------------------------------------ DOS */

static void setdta(void *p)
{
    struct armregs r; u4_clr(&r);
    r.r0 = 0x1A00; r.r3 = (uint32_t)p;
    u4_int21(&r);
}

static int chdir_(const char *p)
{
    /* DOS 4 fails CHDIR "d:" (a drive with no path); ARM-DOS's kernel
     * accepts it, which would make COMP show "C:\\NAME" for "C:NAME" */
    if (p[0] && p[1] == ':' && !p[2]) return -1;
    struct armregs r; u4_clr(&r);
    r.r0 = 0x3B00; r.r3 = (uint32_t)p;
    return u4_int21(&r) ? -1 : 0;
}

/* INT 21h AH=29h; returns AL */
static int parse_fcb(const char *s, uint8_t *f, int al, const char **rest)
{
    struct armregs r; u4_clr(&r);
    r.r0 = 0x2900 | al; r.r4 = (uint32_t)s; r.r5 = (uint32_t)f;
    u4_int21(&r);
    if (rest) *rest = (const char *)r.r4;
    return r.r0 & 0xFF;
}

static int find(const char *pat, int first)
{
    struct armregs r; u4_clr(&r);
    r.r0 = first ? 0x4E00 : 0x4F00; r.r2 = 0; r.r3 = (uint32_t)pat;
    return u4_int21(&r) ? -1 : 0;
}

/* ------------------------------------------------------------ messages */

static void out(const char *s) { u4_puts(STDOUT, s); }
static void err(const char *s) { u4_puts(STDERR, s); }
static void crlf(int h) { u4_puts(h, "\r\n"); }

/* a "%0" message: text " - " name CR LF */
static void msg0(int h, const char *text, const char *name)
{
    u4_class_msg(h, text, name);
}

/* ------------------------------------------------------------ parser */

static const struct u4_ctl pos = { P_FILE | P_OPTIONAL, P_CAP_FILE, 0, 0, 0 };
static const struct u4_ctl *const pos_tab[] = { &pos, &pos };
static const struct u4_parms parms = { 0, 2, pos_tab, 0, 0, 0, 0, 0, 0 };
static int parm_count;

/* COMPPAR.ASM PARSER: every file name goes to path1 when the ordinal
 * after it equals parm_count, else to path2; errors end COMP */
static void parser(const char *line)
{
    struct u4_pstate st;
    struct u4_result r;
    memset(&st, 0, sizeof st);
    st.si = line;
    for (;;) {
        const char *cur = st.si;
        int rc = u4_parse(&parms, &st, &r);
        if (rc == P_RC_EOL) return;
        if (rc != P_OK) {
            char p[130];
            const char *b = cur;
            int n = 0;
            while (b < st.si && *b == ' ') b++;
            while (b + n < st.si && b[n] != ' ' && n < 128) n++;
            memcpy(p, b, n);
            p[n] = 0;
            u4_parse_err(STDERR, rc, p);
            u4_exit(1);
        }
        strcpy(st.ordinal == parm_count ? path1 : path2, r.str);
    }
}

/* GETNAM: prompt on STDERR, buffered input, parse */
static void getnam(const char *prompt, int count)
{
    static uint8_t ib[130];
    err(prompt);
    ib[0] = 127;
    struct armregs r; u4_clr(&r);
    r.r0 = 0x0A00; r.r3 = (uint32_t)ib;
    u4_int21(&r);
    ib[2 + ib[1]] = '\r';
    parm_count = count;
    parser((const char *)ib + 2);
}

/* ------------------------------------------------------------ paths */

static int findfs(const char *p)
{
    int last = -1;
    for (int i = 0; p[i]; i++) if (p[i] == '\\') last = i;
    return last;
}

static void setq(uint8_t *f) { memset(f + 1, '?', 11); }

/* FINDPATH: 0 = directory found (and made current), -1 = not */
static int findpath(char *path, int *last, uint8_t *f, char *oldp)
{
    int drv = f[0];
    oldp[0] = '@' + drv; oldp[1] = ':'; oldp[2] = '\\';
    struct armregs r; u4_clr(&r);
    r.r0 = 0x4700; r.r3 = drv; r.r4 = (uint32_t)(oldp + 3);
    if (u4_int21(&r)) oldp[3] = 0;
    if (!path[0]) goto fp0;
    if (!chdir_(path)) { setq(f); return 0; }
    if (*last < 0) { path[0] = 0; goto fp0; }
    {   /* BACKOFF */
        int s = path[1] == ':' ? 2 : 0;
        int cut = *last == s ? *last + 1 : *last;
        const char *nm = path + *last + 1;
        if (!*nm) setq(f);
        else {
            parse_fcb(nm, f, 2, 0);
            if (f[1] == ' ') { setq(f); return -1; }
        }
        path[cut] = 0;
        return chdir_(path) ? -1 : 0;
    }
fp0:
    if (f[1] == ' ') setq(f);
    return 0;
}

/* FILL_N: d: + path (without its drive) + "\" + name.ext */
static void fill_n(char *dst, const uint8_t *f, const char *path)
{
    char *d = dst;
    *d++ = '@' + f[0];
    *d++ = ':';
    if (path[0]) {
        const char *s = path[1] == ':' ? path + 2 : path;
        while (*s) *d++ = *s++;
        if (d[-1] != '\\') *d++ = '\\';
    }
    int i;
    for (i = 0; i < 8 && f[1 + i] != ' '; i++) *d++ = f[1 + i];
    if (f[9] != ' ') {
        *d++ = '.';
        for (i = 0; i < 3 && f[9 + i] != ' '; i++) *d++ = f[9 + i];
    }
    *d = 0;
}

/* "D:NAME.EXT" from an FCB (MakePathFromFcb, without its lone ".") */
static void fcbname(char *dst, const uint8_t *f)
{
    char *d = dst;
    *d++ = '@' + f[0];
    *d++ = ':';
    int i;
    for (i = 0; i < 8 && f[1 + i] != ' '; i++) *d++ = f[1 + i];
    if (f[9] != ' ') {
        *d++ = '.';
        for (i = 0; i < 3 && f[9 + i] != ' '; i++) *d++ = f[9 + i];
    }
    *d = 0;
}

/* a found directory entry (DTA +1Eh) back into FCB form */
static void dta2fcb(uint8_t *f, const uint8_t *dta, int drive)
{
    memset(f, 0, 40);
    f[0] = drive;
    memset(f + 1, ' ', 11);
    const char *n = (const char *)dta + 0x1E;
    int i = 0;
    while (*n && *n != '.' && i < 8) f[1 + i++] = *n++;
    if (*n == '.') {
        n++;
        for (i = 0; *n && i < 3; i++) f[9 + i] = *n++;
    }
}

static uint32_t dta_size(const uint8_t *dta)
{
    return dta[0x1A] | (dta[0x1B] << 8) | (dta[0x1C] << 16) | ((uint32_t)dta[0x1D] << 24);
}

/* ------------------------------------------------------------ compare */

static void hexmsg(const char *text, uint32_t v)
{
    char h[10];
    out(text);
    out(u4_hex(v, 1, h));
    out("\r\n");
}

/* returns 0 normally */
static void compare(int h1, int h2, uint32_t size)
{
    uint32_t off = 0;
    int cnt = 0, errs = 0;
    uint8_t last1 = 0, last2 = 0;
    while (off < size) {
        int n1 = u4_read(h1, buf1, sizeof buf1);
        int n2 = u4_read(h2, buf2, sizeof buf2);
        int n = n1 < n2 ? n1 : n2;
        if (n <= 0) break;
        for (int i = 0; i < n; i++) {
            if (buf1[i] != buf2[i]) {
                errs = 1;
                hexmsg("Compare error at OFFSET ", off + i);
                hexmsg("File 1 = ", buf1[i]);
                hexmsg("File 2 = ", buf2[i]);
                if (++cnt == 10) {
                    out("10 Mismatches - ending compare\r\n");
                    return;
                }
            }
        }
        last1 = buf1[n - 1]; last2 = buf2[n - 1];
        off += n;
    }
    /* the EOF mark is looked for in the file that filled COMP's large
     * buffer last: the first file for (size / 60K) even, else the second */
    uint8_t lastb = ((size / LBUF) & 1) ? last2 : last1;
    if (size == 0 || lastb != 0x1A) out("EOF mark not found\r\n");
    if (!errs) out("Files compare OK\r\n");
}

/* ------------------------------------------------------------ main */

static int yesno(int c)
{
    struct armregs r; u4_clr(&r);
    r.r0 = 0x6523; r.r3 = c & 0xFF;
    if (!u4_int21(&r)) return r.r0 & 0xFFFF;
    if (c == 'y' || c == 'Y') return 1;
    if (c == 'n' || c == 'N') return 0;
    return 2;
}

static void do_main(void)
{
    char pat[140];
    clear = 0;
    for (;;) {
        while (!path1[0]) {
            path2[0] = 0;
rpt:
            getnam("\r\n\r\nEnter primary filename\r\n", 1);
            if (parse_fcb(path1, fcb, 1, 0) == 0xFF) {
                err("Invalid drive specification\r\n");
                path1[0] = 0;
            }
        }
        if (clear) { path2[0] = 0; clear = 0; }
        while (!path2[0]) {
            getnam("\r\n\r\nEnter 2nd filename or drive id\r\n", 2);
            if (parse_fcb(path2, fcb2, 1, 0) == 0xFF) {
                err("Invalid drive specification\r\n");
                path2[0] = 0;
            }
        }
        last1 = findfs(path1);
        last2 = findfs(path2);
        if (!fcb[0]) fcb[0] = curdrv;
        if (!fcb2[0]) fcb2[0] = curdrv;

        if (findpath(path1, &last1, fcb, oldp1)) {
            fill_n(name1, fcb, path1);
            err("\r\n\r\n");
            msg0(STDERR, "Invalid path", name1);
            goto quit4;
        }
        setdta(dta1);
        fcbname(pat, fcb);
        if (find(pat, 1)) {
            fill_n(name1, fcb, path1);
            err("\r\n\r\n");
            msg0(STDERR, "File not found", name1);
            goto quit4;
        }
        for (;;) {                                      /* f1ok */
            int h1, h2 = -1, st2;
            dta2fcb(infcb, dta1, fcb[0]);
            uint32_t size1 = dta_size(dta1);
            fcbname(pat, infcb);
            h1 = u4_open(pat, 0);
            if (h1 < 0) {
                fill_n(name1, infcb, path1);
                err("\r\n\r\n");
                msg0(STDERR, "Access denied ", name1);
                goto quit4;
            }
            if (path1[0]) chdir_(oldp1);
            out("\r\n\r\n");
            fill_n(name1, infcb, path1);
            memset(outfcb, 0, sizeof outfcb);
            outfcb[0] = fcb2[0];
            if (!swt16) st2 = findpath(path2, &last2, fcb2, oldp2);
            else { st2 = 0; if (path2[0]) chdir_(path2); }
            for (int i = 1; i <= 11; i++) outfcb[i] = fcb2[i] == '?' ? infcb[i] : fcb2[i];
            fill_n(name2, outfcb, path2);
            out(name1); out(" and "); out(name2); out("\r\n");
            out("\r\n");
            if (st2) {
                msg0(STDERR, "Invalid path", name2);
                chdir_(oldp2);
                u4_close(h1);
                goto quit5;
            }
            setdta(dta2);
            fcbname(pat, outfcb);
            if (find(pat, 1)) {
                err("\r\n");
                msg0(STDERR, "File not found", name2);
                chdir_(oldp2);
                goto quit3;
            }
            uint32_t size2 = dta_size(dta2);
            h2 = u4_open(pat, 0);
            if (path2[0]) chdir_(oldp2);
            if (h2 < 0) {
                err("\r\n");
                chdir_(oldp2);
                msg0(STDERR, "Access denied ", name2);
                goto quit3;
            }
            if (size1 != size2) out("Files are different sizes\r\n");
            else compare(h1, h2, size1);
quit3:
            if (h1 >= 0) u4_close(h1);
            if (h2 >= 0) u4_close(h2);
            swt16 = 1;
            if (path1[0]) chdir_(path1);
            setdta(dta1);
            if (find(pat, 0)) break;
        }
quit4:
        chdir_(oldp1);
quit5:
        path2[0] = 0;
        swt16 = 0;
        int a;
        for (;;) {
            err("Compare more files (Y/N) ?");
            int c = u4_getkey(1);
            crlf(STDERR);
            crlf(STDOUT);
            a = yesno(c);
            if (a == 1 || a == 0) break;
        }
        if (a != 1) return;
        memset(path1, 0, sizeof path1);
        memset(path2, 0, sizeof path2);
        clear = 1;
        goto rpt;
    }
}

int main(void)
{
    if (!u4_check_version()) return 1;
    /* DOS's FCB parse of the first two operands: AL/AH = FFh for a bad drive */
    {
        const char *t = u4_cmdline(), *rest;
        uint8_t f[40];
        int a = parse_fcb(t, f, 1, &rest);
        int b = parse_fcb(rest, f, 1, 0);
        if (a == 0xFF || b == 0xFF) {
            err("Invalid drive specification\r\n");
            return 0;
        }
    }
    curdrv = u4_curdrive() + 1;
    parm_count = 1;
    parser(u4_cmdline());
    memset(fcb, 0, sizeof fcb);
    memset(fcb2, 0, sizeof fcb2);
    parse_fcb(path1, fcb, 1, 0);     /* the PSP's FCB 1 */
    parse_fcb(path2, fcb2, 1, 0);    /* more_init parses path2 even if empty */
    do_main();
    return 0;
}
