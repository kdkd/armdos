/*
 * REPLACE - ARM-DOS re-creation of the MS-DOS 4.00 REPLACE command.
 *
 *   REPLACE [d:][path]filename [d:][path] [/A] [/P] [/R] [/S] [/U] [/W]
 *
 * Behaviour follows CMD/REPLACE/REPLACE.C of the MS-DOS 4.0 source (MIT
 * licence, (C) Microsoft Corp.):
 *  - the command line is the program's argv joined with single blanks (as
 *    the MS C start-up gives it), parsed by SysParse: one required and one
 *    optional file spec, switches /A /P /R /S /U /W (each at most once)
 *  - the source is made fully qualified (drive, current directory) and its
 *    normal files are listed; none: "No files found - d:\path\name"
 *  - the target defaults to the current directory; ".." works
 *  - replace mode walks the target directory (and with /S its subdirectories)
 *    and copies every source file that exists there (with /U only if the
 *    source is newer); /A copies the source files that do not exist in the
 *    target; /P asks "Replace x? (Y/N)" / "Add x? (Y/N)" (no echo)
 *  - a read-only target is only overwritten with /R and keeps its
 *    attributes (plus archive); the copy gets the source's date and time
 *  - "Replacing x" / "Adding x", then "n file(s) replaced|added" or "No
 *    files replaced|added"; errors end the walk: "Path not found - x",
 *    "Access denied  - x", "Invalid drive specification - x"
 * Extended attributes and APPEND /X suspension are left out.
 */
#include "u4.h"

#define MAXFILES 256

struct filedata {
    uint8_t  attr;
    uint16_t time, date;
    uint32_t size;
    char     name[15];
};

static struct filedata files[MAXFILES];
static int  nfiles;
static int  add, prompt, readonly, descending, update, wait_;
static unsigned counted;
static char source[140], target[140], errfname[140];
static char p_sfilespec[130], p_path[130];
static uint8_t *buf;
static unsigned buflen;

/* ---------------------------------------------------------- DOS */

static void setdta(void *p)
{
    struct armregs r; u4_clr(&r);
    r.r0 = 0x1A00; r.r3 = (uint32_t)p;
    u4_int21(&r);
}

static int search(const char *spec, int attr, uint8_t *dta, struct filedata *f, int first)
{
    struct armregs r; u4_clr(&r);
    setdta(dta);
    r.r0 = first ? 0x4E00 : 0x4F00; r.r2 = attr; r.r3 = (uint32_t)spec;
    if (u4_int21(&r)) return r.r0 & 0xFFFF;
    f->attr = dta[21];
    f->time = dta[22] | (dta[23] << 8);
    f->date = dta[24] | (dta[25] << 8);
    f->size = dta[26] | (dta[27] << 8) | (dta[28] << 16) | ((uint32_t)dta[29] << 24);
    memcpy(f->name, dta + 30, 13);
    f->name[13] = 0;
    return 0;
}

static int curdir(int drive, char *out)
{
    struct armregs r; u4_clr(&r);
    r.r0 = 0x4700; r.r3 = drive; r.r4 = (uint32_t)out;
    if (u4_int21(&r)) return r.r0 & 0xFFFF;
    return 0;
}

static int chmod_(const char *n, int set, unsigned *attr)
{
    struct armregs r; u4_clr(&r);
    r.r0 = 0x4300 | set; r.r2 = *attr; r.r3 = (uint32_t)n;
    if (u4_int21(&r)) return r.r0 & 0xFFFF;
    *attr = r.r2 & 0xFFFF;
    return 0;
}

static int xopen(const char *n, int mode, int flags, int *h)
{
    struct armregs r; u4_clr(&r);
    r.r0 = 0x6C00; r.r1 = mode; r.r2 = 0; r.r3 = flags; r.r4 = (uint32_t)n; r.r5 = 0xFFFF;
    if (u4_int21(&r)) return r.r0 & 0xFFFF;
    *h = r.r0 & 0xFFFF;
    return 0;
}

/* ---------------------------------------------------------- messages */

static void out(const char *s) { u4_puts(STDOUT, s); }

static __attribute__((noreturn)) void dexit(int code) { u4_exit(code); }

/* utility message with a "%0" name: text (without its CR LF), " - ", name */
static void msg0(int h, const char *text, const char *name) { u4_class_msg(h, text, name); }

static int yes_no(void)
{
    for (;;) {
        int c = u4_getkey(8);
        struct armregs r; u4_clr(&r);
        r.r0 = 0x6523; r.r3 = c;
        int a;
        if (!u4_int21(&r)) a = r.r0 & 0xFFFF;
        else a = (c == 'y' || c == 'Y') ? 1 : (c == 'n' || c == 'N') ? 0 : 2;
        if (a <= 1) return a;
        return -1;
    }
}

/* ---------------------------------------------------------- copying */

static int docopy(const char *sdir, const char *tdir, struct filedata *file, uint16_t time, uint16_t date)
{
    char s[140], t[140];
    int sh, th, st;
    strcpy(s, sdir); strcat(s, file->name);
    strcpy(t, tdir); strcat(t, file->name);
    if (!strcmp(s, t)) {
        msg0(STDERR, "File cannot be copied onto itself", s);
        return 0;
    }
    if (prompt) {
        int a;
        do {
            u4_puts(STDERR, add ? "\r\nAdd " : "\r\nReplace ");
            u4_puts(STDERR, t);
            u4_puts(STDERR, "? (Y/N)");
            a = yes_no();
        } while (a < 0);
        if (a == 0) return 0;
    }
    out(add ? "\r\nAdding " : "\r\nReplacing ");
    out(t);
    out("\r\n");
    /* REPLACE.C opens with DX = 0101h; DOS 4 ignores DH (so does ARM-DOS's
     * kernel); DH is left 0 here */
    st = xopen(s, 0x2080, 0x0001, &sh);
    if (st) { strcpy(errfname, s); return st; }
    if (!add) {
        unsigned a = 0;
        st = chmod_(t, 0, &a);
        if (st) { strcpy(errfname, t); u4_close(sh); return st; }
        file->attr = a;
        if (readonly) a &= 0xFFFE;
        if (file->attr != a) {
            st = chmod_(t, 1, &a);
            if (st) { strcpy(errfname, t); u4_close(sh); return st; }
        }
    }
    st = xopen(t, 0x2081, 0x12, &th);
    if (st) { strcpy(errfname, t); u4_close(sh); return st; }
    for (;;) {
        int n = u4_read(sh, buf, buflen);
        if (n < 0) { st = u4_err; break; }
        int w = u4_write(th, buf, n);
        if (w < 0) { st = u4_err; break; }
        if (w != n) {                           /* disk full */
            u4_close(th);
            struct armregs r; u4_clr(&r);
            r.r0 = 0x4100; r.r3 = (uint32_t)t; u4_int21(&r);
            msg0(STDERR, "Insufficient disk space", t);
            u4_close(sh);
            return 0;
        }
        if ((unsigned)n != buflen) break;
    }
    if (!st) {
        struct armregs r; u4_clr(&r);
        r.r0 = 0x5701; r.r1 = th; r.r2 = time; r.r3 = date;
        if (u4_int21(&r)) st = r.r0 & 0xFFFF;
    }
    if (!st) {
        if (u4_close(th)) { st = u4_err; strcpy(errfname, t); }
        else {
            unsigned a = file->attr | 0x20;
            file->attr = a;
            if (!add) st = chmod_(t, 1, &a);
            if (st) strcpy(errfname, t);
            counted++;
        }
    }
    u4_close(sh);
    return st;
}

static int findfile(const char *name)
{
    for (int i = 0; i < nfiles; i++) if (!strcmp(files[i].name, name)) return i;
    return -1;
}

static int dodir(const char *src, const char *tgt)
{
    char sub[160];
    uint8_t dta[64];
    struct filedata f;
    strcpy(sub, tgt); strcat(sub, "*.*");
    int st = search(sub, 0x10, dta, &f, 1);
    while (!st) {
        if ((f.attr & 0x10) && descending && f.name[0] != '.') {
            strcpy(sub, tgt); strcat(sub, f.name); strcat(sub, "\\");
            st = dodir(src, sub);
        } else {
            int i = findfile(f.name);
            if (i >= 0 && !(f.attr & 0x10)) {
                if (update) {
                    if (files[i].date < f.date || (files[i].date == f.date && files[i].time <= f.time)) ;
                    else st = docopy(src, tgt, &f, files[i].time, files[i].date);
                } else st = docopy(src, tgt, &f, files[i].time, files[i].date);
            }
        }
        if (!st) st = search(0, 0, dta, &f, 0);
    }
    return st == 18 ? 0 : st;
}

static int doadd(const char *src, const char *tgt)
{
    char path[160];
    uint8_t dta[64];
    struct filedata dummy;
    int st = 0;
    for (int i = 0; i < nfiles && !st; i++) {
        strcpy(path, tgt); strcat(path, files[i].name);
        st = search(path, 0x10, dta, &dummy, 1);
        if (st == 18 && files[i].name[0]) st = docopy(src, tgt, &files[i], files[i].time, files[i].date);
        else st = 0;
    }
    return st;
}

/* ---------------------------------------------------------- parsing */

static const struct u4_ctl con1 = { P_FILE, P_CAP_FILE, 0, 0, 0 };
static const struct u4_ctl con2 = { P_FILE | P_OPTIONAL, P_CAP_FILE, 0, 0, 0 };
static const struct u4_ctl swit = { P_OPTIONAL, 0, "/A\0/P\0/R\0/S\0/U\0/W\0", 0, 0 };
static const struct u4_ctl *const pos_tab[] = { &con1, &con2 };
static const struct u4_ctl *const sw_tab[] = { &swit };
static const struct u4_parms parms = { 1, 2, pos_tab, 1, sw_tab, 0, 0, 0, 0 };

static __attribute__((noreturn)) void parse_exit(int rc, const char *p)
{
    u4_parse_err(STDERR, rc, p);
    dexit(11);
}

static void parse_line(void)
{
    /* argv[1..] joined with blanks, each followed by one */
    static char line[260];
    const char *t = u4_cmdline();
    char *d = line;
    for (;;) {
        while (*t == ' ' || *t == '\t') t++;
        if (*t == '\r' || !*t) break;
        while (*t && *t != ' ' && *t != '\t' && *t != '\r') *d++ = *t++;
        *d++ = ' ';
    }
    *d++ = '\r';
    *d = 0;

    struct u4_pstate st;
    struct u4_result r;
    int have_source = 0;
    memset(&st, 0, sizeof st);
    st.si = line;
    for (;;) {
        const char *from = st.si;
        int rc = u4_parse(&parms, &st, &r);
        char txt[130];
        int n = st.si - from;
        if (n > 128) n = 128;
        memcpy(txt, from, n);
        txt[n] = 0;
        if (rc == P_RC_EOL) break;
        if (rc == P_OK) {
            if (r.type == R_FILE && !have_source) { strcpy(p_sfilespec, r.str); have_source = 1; }
            else if (r.type == R_FILE) strcpy(p_path, r.str);
            else {
                int *sw = 0;
                switch (r.synonym ? r.synonym[1] : 0) {
                case 'A': sw = &add; break;
                case 'P': sw = &prompt; break;
                case 'R': sw = &readonly; break;
                case 'S': sw = &descending; break;
                case 'U': sw = &update; break;
                case 'W': sw = &wait_; break;
                }
                if (!sw || *sw) parse_exit(P_BAD_SWITCH, txt);
                *sw = 1;
            }
            continue;
        }
        switch (rc) {
        case P_TOO_MANY: parse_exit(P_TOO_MANY, txt);
        case P_SYNTAX: parse_exit(10, txt);
        case P_BAD_SWITCH: parse_exit(P_BAD_SWITCH, txt);
        case P_MISSING:
            u4_puts(STDERR, "Source path required\r\n");
            dexit(11);
        }
    }
}

/* "d:\curdir" + "\" when needed + rest */
static int qualify(char *s)
{
    char save[140];
    int drv;
    if (s[1] != ':') {
        strcpy(save, s);
        s[0] = 'A' + u4_curdrive(); s[1] = ':'; s[2] = 0;
        strcat(s, save);
    }
    drv = s[0] - 'A' + 1;
    if (s[2] != '\\') {
        strcpy(save, s + 2);
        s[2] = '\\';
        int st = curdir(drv, s + 3);
        if (st) return st;
        if (s[strlen(s) - 1] != '\\') strcat(s, "\\");
        strcat(s, save);
    }
    return 0;
}

int main(void)
{
    if (!u4_check_version()) dexit(1);
    parse_line();
    if ((add && descending) || (add && update)) {
        u4_parse_err(STDERR, 11, 0);
        dexit(11);
    }
    /* the copy buffer: 64 KB less 16, as the 0x1000 paragraphs DOS gives */
    {
        struct armregs r; u4_clr(&r);
        r.r0 = 0x4800; r.r1 = 0x1000;
        if (u4_int21(&r)) {
            unsigned max = r.r1 & 0xFFFF;
            u4_clr(&r); r.r0 = 0x4800; r.r1 = max;
            if (!max || u4_int21(&r)) { u4_exterr(STDERR, 8, 0); dexit(8); }
            buflen = max << 4;
        } else buflen = 0xFFFF;
        buf = (uint8_t *)((r.r0 & 0xFFFF) << 4);
    }
    if (wait_) {
        u4_puts(STDERR, "Press any key to continue . . .\r\n");
        if (u4_getkey(8) == 0) u4_getkey(1);
    }

    /* the source, fully qualified */
    strcpy(source, p_sfilespec);
    int status = qualify(source);
    strcpy(errfname, source);

    /* the source files */
    {
        uint8_t dta[64];
        int st = status ? status : search(source, 0, dta, &files[0], 1);
        while (!st && nfiles < MAXFILES) {
            nfiles++;
            if (nfiles < MAXFILES) st = search(0, 0, dta, &files[nfiles], 0);
        }
        if (st == 18) st = 0;
        status = st;
        if (!status && nfiles == 0) {
            out("\r\nNo files found - "); out(source); out("\r\n");
            dexit(2);
        }
    }
    if (!status) {
        /* the source directory: up to the last "\" (or "d:") */
        int i = strlen(source) - 1;
        while (i > 0 && source[i] != '\\' && source[i] != ':') i--;
        source[i + 1] = 0;
        /* the target */
        strcpy(target, p_path);
        if (!target[0]) { target[0] = 'A' + u4_curdrive(); target[1] = ':'; target[2] = 0; }
        strcpy(errfname, target);
        if (strlen(target) == 2 && target[1] == ':') {
            target[2] = '\\';
            status = curdir(target[0] - 'A' + 1, target + 3);
        }
        if (target[1] != ':') {
            char save[140];
            strcpy(save, target);
            target[0] = 'A' + u4_curdrive(); target[1] = ':'; target[2] = 0;
            strcat(target, save);
        }
        strcpy(errfname, target);
        if (!status && target[2] != '\\') {
            char save[140];
            strcpy(save, target + 2);
            target[2] = '\\';
            status = curdir(target[0] - 'A' + 1, target + 3);
            if (!status) {
                if (target[strlen(target) - 1] != '\\') strcat(target, "\\");
                if (save[0] != '.') strcat(target, save);
                else if (save[1] == '.') {
                    target[strlen(target) - 1] = 0;
                    *(strrchr(target, '\\') + 1) = 0;
                }
            }
        }
        strcpy(errfname, target);
        if (!status && target[strlen(target) - 1] != '\\') strcat(target, "\\");
    }
    if (!status) status = add ? doadd(source, target) : dodir(source, target);

    switch (status) {
    case 0: break;
    case 2: msg0(STDOUT, "File not found", errfname); break;
    case 3: msg0(STDOUT, "Path not found", errfname); break;
    case 5: msg0(STDOUT, "Access denied ", errfname); break;
    case 15: msg0(STDERR, "Invalid drive specification", errfname); break;
    default: dexit(1);
    }
    char num[12];
    if (add) {
        if (!counted) out("\r\nNo files added\r\n");
        else { out("\r\n"); out(u4_utoa(counted, num)); out(" file(s) added\r\n"); }
    } else {
        if (!counted) out("\r\nNo files replaced\r\n");
        else { out("\r\n"); out(u4_utoa(counted, num)); out(" file(s) replaced\r\n"); }
    }
    dexit(status);
}
