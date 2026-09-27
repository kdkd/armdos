/*
 * u4.c - shared helpers for the ARM-DOS versions of the DOS 4.00 external
 * commands.  See u4.h.
 */
#include "u4.h"

int u4_err;

int u4_int21(struct armregs *r) { return _armdos_int21(r); }

int u4_write(int h, const void *p, unsigned n)
{
    struct armregs r;
    if (!n) return 0;
    u4_clr(&r);
    r.r0 = 0x4000; r.r1 = h; r.r2 = n; r.r3 = (uint32_t)p;
    if (u4_int21(&r)) { u4_err = r.r0 & 0xFFFF; return -1; }
    return r.r0 & 0xFFFF;
}

void u4_puts(int h, const char *s) { u4_write(h, s, strlen(s)); }

int u4_read(int h, void *p, unsigned n)
{
    struct armregs r;
    u4_clr(&r);
    r.r0 = 0x3F00; r.r1 = h; r.r2 = n; r.r3 = (uint32_t)p;
    if (u4_int21(&r)) { u4_err = r.r0 & 0xFFFF; return -1; }
    return r.r0 & 0xFFFF;
}

int u4_open(const char *name, int mode)
{
    struct armregs r;
    u4_clr(&r);
    r.r0 = 0x3D00 | (mode & 0xFF); r.r3 = (uint32_t)name;
    if (u4_int21(&r)) { u4_err = r.r0 & 0xFFFF; return -1; }
    return r.r0 & 0xFFFF;
}

int u4_creat(const char *name, int attr)
{
    struct armregs r;
    u4_clr(&r);
    r.r0 = 0x3C00; r.r2 = attr; r.r3 = (uint32_t)name;
    if (u4_int21(&r)) { u4_err = r.r0 & 0xFFFF; return -1; }
    return r.r0 & 0xFFFF;
}

int u4_close(int h)
{
    struct armregs r;
    u4_clr(&r);
    r.r0 = 0x3E00; r.r1 = h;
    if (u4_int21(&r)) { u4_err = r.r0 & 0xFFFF; return -1; }
    return 0;
}

long u4_lseek(int h, long off, int whence)
{
    struct armregs r;
    u4_clr(&r);
    r.r0 = 0x4200 | (whence & 0xFF); r.r1 = h;
    r.r2 = ((uint32_t)off >> 16) & 0xFFFF; r.r3 = (uint32_t)off & 0xFFFF;
    if (u4_int21(&r)) { u4_err = r.r0 & 0xFFFF; return -1; }
    return (long)(((r.r3 & 0xFFFF) << 16) | (r.r0 & 0xFFFF));
}

void u4_exit(int code)
{
    struct armregs r;
    u4_clr(&r);
    r.r0 = 0x4C00 | (code & 0xFF);
    u4_int21(&r);
    for (;;) ;
}

int u4_getkey(int fn)
{
    struct armregs r;
    u4_clr(&r);
    r.r0 = 0x0C00 | (fn & 0xFF);
    u4_int21(&r);
    return r.r0 & 0xFF;
}

int u4_curdrive(void)
{
    struct armregs r;
    u4_clr(&r);
    r.r0 = 0x1900;
    u4_int21(&r);
    return r.r0 & 0xFF;
}

int u4_version_ok(void)
{
    struct armregs r;
    u4_clr(&r);
    r.r0 = 0x3000;
    u4_int21(&r);
    return (r.r0 & 0xFFFF) == 0x0004;
}

int u4_check_version(void)
{
    if (u4_version_ok()) return 1;
    u4_puts(STDERR, "Incorrect DOS version\r\n");
    return 0;
}

char *u4_utoa(uint32_t v, char *buf)
{
    char t[12];
    int n = 0;
    do { t[n++] = '0' + v % 10; v /= 10; } while (v);
    for (int i = 0; i < n; i++) buf[i] = t[n - 1 - i];
    buf[n] = 0;
    return buf;
}

char *u4_hex(uint32_t v, int width, char *buf)
{
    char t[9];
    int n = 0;
    do { t[n++] = "0123456789ABCDEF"[v & 15]; v >>= 4; } while (v);
    int i = 0;
    while (n < width) { buf[i++] = '0'; width--; }
    while (n) buf[i++] = t[--n];
    buf[i] = 0;
    return buf;
}

char *u4_pad(const char *s, int width, char *buf)
{
    int n = strlen(s), i = 0;
    while (n + i < width) { buf[i] = ' '; i++; }
    strcpy(buf + i, s);
    return buf;
}

/* the US (code page 437) upper-case table for 80h-FFh, as COUNTRY.SYS */
static const unsigned char up437[128] = {
    0x80,0x9A,0x45,0x41,0x8E,0x41,0x8F,0x80,0x45,0x45,0x45,0x49,0x49,0x49,0x8E,0x8F,
    0x90,0x92,0x92,0x4F,0x99,0x4F,0x55,0x55,0x59,0x99,0x9A,0x9B,0x9C,0x9D,0x9E,0x9F,
    0x41,0x49,0x4F,0x55,0xA5,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0xAD,0xAE,0xAF,
    0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF,
    0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,
    0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF,
    0xE0,0xE1,0xE2,0xE3,0xE4,0xE5,0xE6,0xE7,0xE8,0xE9,0xEA,0xEB,0xEC,0xED,0xEE,0xEF,
    0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0xFA,0xFB,0xFC,0xFD,0xFE,0xFF,
};

unsigned char u4_upcase(unsigned char c)
{
    if (c >= 'a' && c <= 'z') return c - 32;
    if (c >= 0x80) return up437[c - 0x80];
    return c;
}

void u4_strupr(char *s) { for (; *s; s++) *s = u4_upcase(*s); }

char *u4_cmdline(void)
{
    static char line[130];
    const uint8_t *t = _armdos_psp->cmdtail;
    int n = t[0];
    if (n > 127) n = 127;
    memcpy(line, t + 1, n);
    line[n] = '\r';
    line[n + 1] = 0;
    /* a tail without its CR (should not happen) still ends at the length */
    for (int i = 0; i < n; i++) if (line[i] == '\r') { line[i + 1] = 0; break; }
    return line;
}

/* ---------------------------------------------------------- messages */

void u4_msg(int h, const char *text, const char *const *args)
{
    const char *p = text, *run = text;
    while (*p) {
        if (p[0] == '%' && p[1] >= '1' && p[1] <= '9') {
            u4_write(h, run, p - run);
            const char *a = args ? args[p[1] - '1'] : 0;
            if (a) u4_puts(h, a);
            p += 2;
            run = p;
        } else p++;
    }
    u4_write(h, run, p - run);
}

const char *u4_exterr_text(int code)
{
    switch (code) {
    case 1: return "Invalid function";
    case 2: return "File not found";
    case 3: return "Path not found";
    case 4: return "Too many open files";
    case 5: return "Access denied ";
    case 6: return "Invalid handle";
    case 7: return "Memory control blocks destroyed";
    case 8: return "Insufficient memory";
    case 9: return "Invalid memory block address";
    case 10: return "Invalid Environment";
    case 11: return "Invalid format";
    case 12: return "Invalid function parameter";
    case 13: return "Invalid data";
    case 15: return "Invalid drive specification";
    case 16: return "Attempt to remove current directory";
    case 17: return "Not same device";
    case 18: return "No more files";
    case 19: return "Write protect error";
    case 20: return "Invalid unit";
    case 21: return "Not ready";
    case 22: return "Invalid device request";
    case 23: return "Data error";
    case 24: return "Invalid device request parameters";
    case 25: return "Seek error";
    case 26: return "Invalid media type";
    case 27: return "Sector not found";
    case 28: return "Printer out of paper error";
    case 29: return "Write fault error";
    case 30: return "Read fault error";
    case 31: return "General failure";
    case 32: return "Sharing violation";
    case 33: return "Lock violation";
    case 34: return "Invalid disk change";
    case 35: return "FCB unavailable";
    case 36: return "System resource exhausted";
    case 37: return "Code page mismatch";
    case 38: return "Out of input";
    case 39: return "Insufficient disk space";
    case 80: return "File exists";
    case 82: return "Cannot make directory entry";
    case 83: return "Fail on INT 24";
    case 84: return "Too many redirections";
    case 85: return "Duplicate redirection";
    case 86: return "Invalid password";
    case 87: return "Invalid parameter";
    case 88: return "Network data fault";
    default: return 0;
    }
}

const char *u4_parse_text(int rc)
{
    switch (rc) {
    case 1: return "Too many parameters";
    case 2: return "Required parameter missing";
    case 3: return "Invalid switch";
    case 4: return "Invalid keyword";
    case 6: return "Parameter value not in allowed range";
    case 7: case 8: return "Parameter value not allowed";
    case 9: return "Parameter format not correct";
    case 10: return "Invalid parameter";
    case 11: return "Invalid parameter combination";
    default: return 0;
    }
}

void u4_class_msg(int h, const char *text, const char *param)
{
    u4_puts(h, text);
    if (param) { u4_puts(h, " - "); u4_puts(h, param); }
    u4_puts(h, "\r\n");
}

void u4_exterr(int h, int code, const char *param)
{
    const char *t = u4_exterr_text(code);
    char buf[24];
    if (!t) { strcpy(buf, "Extended Error "); u4_utoa(code, buf + 15); t = buf; }
    u4_class_msg(h, t, param);
}

void u4_parse_err(int h, int rc, const char *param)
{
    const char *t = u4_parse_text(rc);
    char buf[24];
    if (!t) { strcpy(buf, "Parse Error "); u4_utoa(rc, buf + 12); t = buf; }
    u4_class_msg(h, t, param);
}

/* ---------------------------------------------------------- the parser */
/*
 * A re-implementation of SysParse (INC/PARSE.ASM, DOS 4.00).  Per call:
 * skip white space; an operand is copied up to a delimiter (blank, tab,
 * comma, the caller's extra delimiters), an end of line (CR, LF, NUL, the
 * caller's) or a "/" that is not its first character.  An operand starting
 * with "/" is a switch, one containing "=" a keyword, anything else the next
 * positional.  The type checks run in PARSE.ASM's order (date, number,
 * drive, file spec, quoted string, simple string); a type that does not fit
 * gives way to the next one, the last failure is the error.
 */

#define FL_EQU   0x01
#define FL_EXTRA 0x20
#define FL_SW    0x40

struct pctx {
    const struct u4_parms *p;
    struct u4_pstate *st;
    struct u4_result *r;
    const char *save_cmpx;      /* the operand in the command line */
    const char *si_save;        /* where the next call starts */
    char *eob;                  /* end of the copied operand */
    int flags, rc, err_file;
    const char *kw_ptr;         /* after the switch name / "=" in buf */
};

static int is_eol(struct pctx *c, int ch)
{
    if (ch == '\r' || ch == 0 || ch == '\n') return 1;
    return c->p->eols && ch && strchr(c->p->eols, ch);
}

/* sets FL_EXTRA for comma / extra delimiters (unless in a keyword) */
static int is_delim(struct pctx *c, int ch)
{
    c->st->terminator = ' ';
    c->flags &= ~FL_EXTRA;
    if (ch == ' ' || ch == '\t') return 1;
    if (ch == ',' || (ch && c->p->delims && strchr(c->p->delims, ch))) {
        c->st->terminator = ch;
        if (!(c->flags & FL_EQU)) c->flags |= FL_EXTRA;
        return 1;
    }
    return 0;
}

/* returns 1 at end of line (si left on the EOL char) */
static int skip_delim(struct pctx *c, const char **psi)
{
    const char *si = *psi;
    for (;;) {
        int ch = (unsigned char)*si++;
        if (is_eol(c, ch)) { *psi = si - 1; return 1; }
        if (!is_delim(c, ch)) { *psi = si - 1; return 0; }
        if (c->flags & FL_EXTRA) {
            if (c->flags & (FL_SW | FL_EQU)) si--;
            *psi = si;
            return 0;
        }
    }
}

static void fill(struct pctx *c, const struct u4_ctl *ctl, int type, char *s)
{
    c->r->type = type;
    c->r->ctl = ctl;
    if (type == R_STRING || type == R_FILE || type == R_QUOTED) {
        c->r->str = s;
        if (ctl && s) {
            if (ctl->func & P_CAP_FILE) u4_strupr(s);
            else if (ctl->func & P_CAP_CHAR) u4_strupr(s);
            if (ctl->func & P_RM_COLON) {
                int n = strlen(s);
                if (n && s[n - 1] == ':') s[n - 1] = 0;
            }
        }
    }
}

static const char filesp[] = "[]|<>+=;\"";

static void file_format(struct pctx *c, const struct u4_ctl *ctl, char *s, int fillit)
{
    const char *di = c->save_cmpx;
    if (!*s) {
        if (!(ctl->match & P_OPTIONAL)) c->rc = P_MISSING;
    } else if (strchr(filesp, *s)) {
        c->err_file = 1;
        *s = 0;
    } else {
        char *q = s;
        while (*q && !strchr(filesp, *q)) { q++; di++; }
        if (*q) {
            c->st->terminator = *q;
            *q = 0;
            di++;
            c->si_save = di;
        }
    }
    if (fillit) fill(c, ctl, R_FILE, s);
}

static int decnum(const char **ps, unsigned *v, int *end)
{
    const char *s = *ps;
    unsigned n = 0;
    *end = 0;
    for (;;) {
        char ch = *s;
        if (!ch) { *end = 1; break; }
        if (ch == '-' || ch == '/' || ch == '.') { s++; break; }
        if (ch < '0' || ch > '9') { *ps = s; *v = n; return -1; }
        n = n * 10 + (ch - '0');
        if (n > 0xFFFF) { *ps = s; return -1; }
        s++;
    }
    *ps = s;
    *v = n;
    return 0;
}

static void date_format(struct pctx *c, const struct u4_ctl *ctl, char *s)
{
    unsigned v[3] = { 0, 0, 0 };
    const char *q = s;
    int end, i;
    for (i = 0; i < 3; i++) {
        if (decnum(&q, &v[i], &end)) goto bad;
        if (end) break;
    }
    if (i == 3) goto bad;            /* a 4th field */
    if (v[0] > 255 || v[1] > 255) goto bad;
    c->r->month = v[0]; c->r->day = v[1];
    c->r->year = v[2] < 100 ? v[2] + 1900 : v[2];
    fill(c, ctl, R_DATE, s);
    return;
bad:
    fill(c, ctl, R_STRING, s);
    c->rc = P_SYNTAX;
}

static void value(struct pctx *c, const struct u4_ctl *ctl, char *s)
{
    uint32_t v = 0;
    const char *q = s;
    for (; *q; q++) {
        if (*q < '0' || *q > '9') goto bad;
        uint64_t n = (uint64_t)v * 10 + (*q - '0');
        if (n > 0xFFFFFFFFu) goto bad;
        v = (uint32_t)n;
    }
    c->r->value = v;
    if (ctl->nrange) {
        int ok = 0;
        for (int i = 0; i < ctl->nrange; i++)
            if (v >= ctl->range[i].lo && v <= ctl->range[i].hi) ok = 1;
        if (!ok) c->rc = P_RANGE;
    }
    fill(c, ctl, R_NUMBER, s);
    return;
bad:
    c->rc = P_SYNTAX;
    fill(c, ctl, R_STRING, s);
}

static void quoted_format(struct pctx *c, const struct u4_ctl *ctl, char *s)
{
    const char *bx = c->save_cmpx;
    char *out = s;
    if (*bx != '"') goto bad;
    bx++;
    for (;;) {
        if (is_eol(c, (unsigned char)*bx)) { *c->eob = 0; goto bad; }
        if (*bx == '"') {
            if (bx[1] == '"') { *out++ = '"'; bx += 2; continue; }
            break;
        }
        *out++ = *bx++;
    }
    *out = 0;
    {
        const char *si = bx + 1;
        int f = c->flags;
        skip_delim(c, &si);
        c->flags = f;
        c->si_save = si;
    }
    fill(c, ctl, R_QUOTED, s);
    return;
bad:
    c->rc = P_SYNTAX;
    fill(c, ctl, R_QUOTED, s);
}

static void drive_format(struct pctx *c, const struct u4_ctl *ctl, char *s)
{
    if (!*s) return;
    int ok = (s[1] == ':' && s[2] == 0) || ((ctl->match & P_IGCOLON) && s[1] == 0);
    int d = s[0] | 0x20;
    if (!ok || d < 'a' || d > 'z') { c->rc = P_SYNTAX; return; }
    c->r->value = d - 'a' + 1;
    fill(c, ctl, R_DRIVE, s);
}

static void check_match(struct pctx *c, const struct u4_ctl *ctl, char *s)
{
    uint16_t m = ctl->match;
    c->err_file = 0;
    if (!m) { c->rc = P_SYNTAX; fill(c, ctl, R_STRING, s); return; }
    if (m & P_DATE) {
        c->rc = P_OK; date_format(c, ctl, s);
        if (c->rc != P_SYNTAX) goto done;
    }
    if (m & P_NUM) {
        c->rc = P_OK; value(c, ctl, s);
        if (c->rc != P_SYNTAX) goto done;
    }
    if (m & P_DRIVE) {
        c->rc = P_OK;
        file_format(c, ctl, s, 0);
        drive_format(c, ctl, s);
        if (c->rc != P_SYNTAX) goto done;
    }
    if (m & P_FILE) {
        c->rc = P_OK; file_format(c, ctl, s, 1);
        if (c->rc != P_SYNTAX) goto done;
    }
    if (m & P_QUOTED) {
        c->rc = P_OK; quoted_format(c, ctl, s);
        if (c->rc != P_SYNTAX) goto done;
    }
    if (m & P_SIMPLE) {
        c->rc = P_OK; fill(c, ctl, R_STRING, s);
    }
done:
    if (c->err_file && c->rc == P_OK) c->rc = P_SYNTAX;
}

/* compare the operand in buf against a synonym; sets kw_ptr on a match */
static int string_comp(struct pctx *c, const struct u4_ctl *ctl, const char *s, const char *syn, int is_key)
{
    for (;;) {
        unsigned char ch = u4_upcase((unsigned char)*s);
        if (is_key && ch == '=') {
            if (syn[1] == 0) { c->kw_ptr = s + 1; return 1; }
            return 0;
        }
        if (!is_key && ch == ':') {
            if (*syn == 0) { c->kw_ptr = s + 1; return 1; }
            return 0;
        }
        if (ch != (unsigned char)*syn) {
            if (!is_key && (ctl->func & P_COLON_NN) && *syn == 0) { c->kw_ptr = s; return 1; }
            if (ctl->match & P_IGCOLON) {
                if (ch == ':' && *syn == 0) { c->kw_ptr = s; return 1; }
                if (ch == 0 && *syn == ':') { c->kw_ptr = s; return 1; }
            }
            return 0;
        }
        if (!ch) { c->kw_ptr = s; return 1; }
        s++; syn++;
    }
}

static const char *search(struct pctx *c, const struct u4_ctl *ctl, const char *s, int is_key)
{
    const char *n = ctl->names;
    while (n && *n) {
        if (string_comp(c, ctl, s, n, is_key)) return n;
        n += strlen(n) + 1;
    }
    return 0;
}

int u4_parse(const struct u4_parms *p, struct u4_pstate *st, struct u4_result *r)
{
    struct pctx c;
    memset(&c, 0, sizeof c);
    memset(r, 0, sizeof *r);
    c.p = p; c.st = st; c.r = r;
    const char *si = st->si;

    if (skip_delim(&c, &si)) {
        st->si = si;
        r->type = R_EOL;
        if (st->ordinal < p->minp) return P_MISSING;
        return P_RC_EOL;
    }
    c.save_cmpx = si;
    char *bx = st->buf;
    if (!(c.flags & FL_EXTRA)) {
        for (;;) {
            int ch = (unsigned char)*si++;
            if (ch == '/') {
                if (bx != st->buf) { si--; break; }
                c.flags |= FL_SW;
            }
            if (is_eol(&c, ch)) { si--; break; }
            if (is_delim(&c, ch)) {
                if (c.flags & FL_EXTRA) {
                    if (c.flags & (FL_SW | FL_EQU)) si--;
                    break;
                }
                skip_delim(&c, &si);
                break;
            }
            if (bx < st->buf + sizeof st->buf - 1) *bx++ = ch;
            if (ch == '=') c.flags |= FL_EQU;
        }
    }
    c.si_save = si;
    *bx = 0;
    c.eob = bx;
    c.rc = P_OK;
    char *s = st->buf;

    if (s[0] == '/') {
        int i;
        for (i = 0; i < p->nsw; i++) {
            const struct u4_ctl *ctl = p->sw[i];
            const char *syn = search(&c, ctl, s, 0);
            if (!syn) continue;
            r->synonym = syn;
            char *v = (char *)c.kw_ptr;
            c.save_cmpx += v - s;
            if (*v) check_match(&c, ctl, v);
            else {
                if (v > s && v[-1] == ':') c.rc = P_SYNTAX;
                else if (ctl->match && !(ctl->match & P_OPTIONAL)) c.rc = P_MISSING;
                fill(&c, ctl, R_STRING, v);
            }
            break;
        }
        if (i == p->nsw) c.rc = P_BAD_SWITCH;
    } else if (c.flags & FL_EQU) {
        int i;
        for (i = 0; i < p->nkey; i++) {
            const struct u4_ctl *ctl = p->key[i];
            const char *syn = search(&c, ctl, s, 1);
            if (!syn) continue;
            r->synonym = syn;
            char *v = (char *)c.kw_ptr;
            c.save_cmpx += v - s;
            if (!*v) { c.rc = P_SYNTAX; fill(&c, ctl, R_STRING, v); }
            else check_match(&c, ctl, v);
            break;
        }
        if (i == p->nkey) c.rc = P_BAD_KEYWORD;
    } else {
        if (st->ordinal >= p->maxp) c.rc = P_TOO_MANY;
        else {
            const struct u4_ctl *ctl = p->pos[st->ordinal];
            if (!(ctl->match & P_REPEAT)) st->ordinal++;
            if (!*s) {
                if (!(ctl->match & P_OPTIONAL)) c.rc = P_MISSING;
                else fill(&c, ctl, R_STRING, s);
            } else check_match(&c, ctl, s);
        }
    }
    st->si = c.si_save;
    if (!r->str) r->str = s;
    return c.rc;
}
