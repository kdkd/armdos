/*
 * parse.c - command-line scanning: DELIM/SCANOFF (TENV2.ASM), PRESCAN
 * (TMISC1.ASM: redirection and pipes), CPARSE (CPARSE.ASM) and parseline
 * (PARSE2.ASM), and a reduced SYSPARSE for the internal commands' parse
 * blocks (TDATA.ASM PARSE_*; error substitution as SETUP_PARSE_ERROR_MSG).
 */
#include "cmd.h"

struct arg_unit arg;
char err_sub[COMBUFLEN];


int is_delim(int c)
{
    return c == ' ' || c == '=' || c == ',' || c == ';' || c == '\t' || c == '\n';
}

const char *scanoff(const char *s)
{
    while (is_delim((uint8_t)*s)) s++;
    return s;
}

/* UPCONV: a-z, and the country's file upper-case table for 80h-FFh (we use
 * code page 437's: the kernel's table maps the accented letters) */
int upconv(int c)
{
    c &= 0xFF;
    if (c >= 'a' && c <= 'z') return c - 32;
    if (c >= 0x80) {
        static const uint8_t up437[128] = {
            0x80,0x9A,0x45,0x41,0x8E,0x41,0x8F,0x80,0x45,0x45,0x45,0x49,0x49,0x49,0x8E,0x8F,
            0x90,0x92,0x92,0x4F,0x99,0x4F,0x55,0x55,0x59,0x99,0x9A,0x9B,0x9C,0x9D,0x9E,0x9F,
            0x41,0x49,0x4F,0x55,0xA5,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0xAD,0xAE,0xAF,
            0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF,
            0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,
            0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF,
            0xE0,0xE1,0xE2,0xE3,0xE4,0xE5,0xE6,0xE7,0xE8,0xE9,0xEA,0xEB,0xEC,0xED,0xEE,0xEF,
            0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0xFA,0xFB,0xFC,0xFD,0xFE,0xFF,
        };
        return up437[c - 0x80];
    }
    return c;
}

/* PATHCHRCMP: '\' always; '/' too unless '/' is the switch character */
int pathchr(int c)
{
    if (c == '\\') return 1;
    if (c == '/' && switchar != '/') return 1;
    return 0;
}

/* --------------------------------------------------------- PRESCAN -- */

__attribute__((noreturn)) static void pipeerrsyn(void)
{
    pipedel();
    cerror_msg(M_SYNTMES);
}

/* copy a redirection file name (SETREOUTSTR .. GOTRESTR); returns the
 * terminating character, *psi advanced */
static int get_redir_name(const char **psi, char *dst)
{
    const char *si = *psi;
    char *d = dst;
    int al;
    for (;;) {
        al = (uint8_t)*si++;
        if (al == '\r') break;
        if (is_delim(al)) break;
        if (al == switchar) break;
        if (al == '"') pipeerrsyn();
        if (al == '<' || al == '>') {
            si--;
            al = ' ';
            break;
        }
        if (d < dst + 62) *d++ = al;
    }
    /* trailing ':' allowed on devices, unless the name is only ':' */
    if (d - dst > 1 && d[-1] == ':') d--;
    *d = 0;
    *psi = si;
    return al;
}

int prescan(void)
{
    const char *si = (const char *)combuf + 2;
    char *di = (char *)combuf + 2;
    int quotes = 0, cl = 0;

    for (const char *p = si; *p != '\r'; p++)
        if (*p == '"') quotes++;

    for (;;) {
        int al = (uint8_t)*si++;
        int ah;
        if (al == '"') {
            quotes--;
            if (quotes != 0) {
                do {
                    *di++ = al;
                    cl++;
                    al = (uint8_t)*si++;
                } while (al != '"');
                quotes--;
            }
        }
        if (al == '>') {
            if (*si == '>') {
                si++;
                re_out_app++;
            }
            si = scanoff(si);
            al = (uint8_t)*si;
            if (al == '<' || al == '\r') {
                *di = '\r';
                re_outstr[0] = 9;
                re_outstr[1] = 0;
                goto end;
            }
            ah = get_redir_name(&si, re_outstr);
        } else if (al == '<') {
            si = scanoff(si);
            al = (uint8_t)*si;
            if (al == '>' || al == '\r') {
                *di = '\r';
                re_instr[0] = 9;
                re_instr[1] = 0;
                goto end;
            }
            ah = get_redir_name(&si, re_instr);
        } else if (al == '|') {
            ah = al;
            if (!pipeflag) echoflag <<= 1;
            pipeflag++;
            si = scanoff(si);
            if (*si == '\r' || *si == '|') pipeerrsyn();
        } else {
            ah = al;
        }
        *di++ = ah;
        if (ah == '\r') break;
        cl++;
    }
end:
    if (pipeflag) {
        const char *s = scanoff((const char *)combuf + 2);
        char *d = pipestr;
        while ((*d++ = *s++) != '\r') ;
        pipeptr = pipestr;
    }
    combuf[1] = cl;
    return pipeflag != 0;
}

/* ------------------------------------------------------------ CPARSE -- */

static const char switch_list[] = "VBAPW";
int cparse_comma;                   /* "+,," seen (the comma flag) */

/*
 * CPARSE: one token from *ps into dst.  bl = special delimiter; cpyflag as
 * COMMAND's (1 = from COPY: upper-case, add the drive to rooted paths; 2 =
 * not the first token of parseline: ':' is part of the token).  Returns
 * NULL on CR (token not altered); else dst.  *flags: 1 switch, 2 wildcard,
 * 4 path separator, 80h special delimiter skipped.  *swbits ORed with the
 * switch bits.  *startel: start of the last path element in dst.
 */
char *cparse(const char **ps, char *dst, int bl, int cpyflag, int *flags, uint16_t *swbits,
             char **startel, int *count, int expand_star)
{
    const char *si = *ps;
    char *di = dst;
    int bh = 0, skpdel = 0, elpos = 0, elcnt = 0, cx = 0;
    int al;

    *startel = di;
    cparse_comma = 0;
#define MOVE(c) do { *di++ = (c); cx++; elcnt++; } while (0)
moredelim:
    for (;;) {
        al = (uint8_t)*si++;
        if (!is_delim(al)) break;
        if (al == ' ' || al == '\t') continue;
        int t = skpdel;
        skpdel = al;
        if (!t) continue;
        if (bh & 0x80) cparse_comma = 1;
        goto x_done;                        /* a null argument */
    }
    if (al == bl) {
        bh |= 0x80;
        goto moredelim;
    }
    if (al == '\r') {
        *ps = si - 1;
        *flags = bh;
        *count = 0;
        return NULL;
    }
    if (al == switchar) {
        bh |= 1;
        *swbits |= SW_ANY;
        si = scanoff(si);
        al = (uint8_t)*si++;
        if (al == '\r') {
            *di = 0;
            *swbits |= SW_BAD;
            *ps = si - 1;
            *flags = bh;
            *count = cx;
            return NULL;
        }
        MOVE(al);
        int u = upconv(al);
        const char *f = strchr(switch_list, u);
        if (f && u) *swbits |= 1u << (4 - (f - switch_list));
        else *swbits |= SW_BAD;
        goto out_token;
    }
    if (*si == ':') {
        if (cpyflag == 1) al = upconv(al);
        MOVE(al);
        al = (uint8_t)*si++;
        MOVE(al);
        *startel = di;
        elcnt = 0;
        goto anum_test;
    }
    *startel = di;
    elcnt = 0;
    if (cpyflag == 1 && pathchr(al)) {
        MOVE(curdrv + 'A');
        MOVE(':');
        *startel = di;
        elcnt = 0;
    }
    for (;;) {
        /* anum_char */
        if (cpyflag == 1) al = upconv(al);
        if (al == '.') {
            elpos++;
            elcnt = -1;
        }
        if (al == '?') bh |= 2;
        if (al == '*') {
            bh |= 2;
            if (expand_star) {
                int n = (elpos ? 2 : 7) - elcnt;
                if (n < 0) cerror_msg(M_BADCPMES);
                while (n-- > 0) MOVE('?');
                /* then the '?' replacing the '*' itself */
                al = '?';
            }
        }
        if (pathchr(al)) {
            bh |= 4;
            if (expand_star && (bh & 2)) cerror_msg(M_BADCD);
            *startel = di + 1;
            elcnt = -1;
            elpos = 0;
        }
        MOVE(al);
    anum_test:
        al = (uint8_t)*si++;
        if (is_delim(al) || al == '\r' || al == switchar || al == bl) goto x_done;
        if (al != ':') continue;
        if (cpyflag == 2) {
            MOVE(':');
            goto anum_test;
        }
        si++;
        goto x_done;
    }
x_done:
    si--;
out_token:
    *di = 0;
    *ps = si;
    *flags = bh;
    *count = cx;
#undef MOVE
    return dst;
}

/* ---------------------------------------------------------- parseline -- */

int parseline(void)
{
    const char *si;
    char tpbuf[COMBUFLEN + 16];
    char *argbufptr;
    int last_arg = -1;
    int cpyflag = 0;

    memset(&arg, 0, sizeof arg);
    argbufptr = arg.argbuf;
    si = (const char *)combuf + 2;
    if (!forflag) {
        memcpy(arg.argforcombuf, combuf + 2, combuf[1] + 1);
        si = arg.argforcombuf;
    }
    for (;;) {
        const char *comptr = si;
        uint16_t bp = 0;
        int flags, cx;
        char *startel;
        si = scanoff(si);
        char *t = cparse(&si, tpbuf, ' ', cpyflag, &flags, &bp, &startel, &cx, 0);
        if (!t) {
            if (!bp) break;
            /* a trailing switch character: goes in as an argument */
        }
        cpyflag = 2;
        /* newarg */
        if (flags & AF_SWITCH) {
            if (last_arg != -1) {
                arg.argv[last_arg].argsw_word |= bp;
                arg.argswinfo |= bp;
            }
        } else {
            last_arg = arg.argvcnt;
        }
        if (arg.argvcnt >= ARGMAX) return -1;
        struct argv_ele *a = &arg.argv[arg.argvcnt++];
        a->arglen = cx;
        a->argflags = flags;
        a->argsw_word = 0;
        if (argbufptr + cx >= arg.argbuf + ARGBLEN - 1) return -1;
        a->argpointer = argbufptr;
        a->argstartel = argbufptr + (startel - tpbuf);
        a->arg_ocomptr = (char *)comptr;
        memcpy(argbufptr, tpbuf, cx);
        argbufptr[cx] = 0;
        argbufptr += cx + 1;
        if (!t) break;
    }
    return 0;
}

/* ----------------------------------------------------------- SYSPARSE -- */

static int p_delim(int c)
{
    return c == ' ' || c == '\t' || c == ',';
}

static int p_eol(int c)
{
    return c == '\r' || c == 0;
}

/* item text ends at a delimiter, the switch character or the end of line */
static const char *item_end(const char *s, int is_switch)
{
    if (is_switch) s++;
    while (!p_eol((uint8_t)*s) && !p_delim((uint8_t)*s) && *s != (char)switchar) {
        if (is_switch && *s == ':') break;
        s++;
    }
    return s;
}

static int getnum(const char **ps, unsigned *v)
{
    const char *s = *ps;
    unsigned long n = 0;
    if (*s < '0' || *s > '9') return -1;
    while (*s >= '0' && *s <= '9') {
        n = n * 10 + (*s - '0');
        if (n > 0xFFFF) return -1;
        s++;
    }
    *v = n;
    *ps = s;
    return 0;
}

/* $P_Date_Format: up to three numbers separated by - / . in country order */
static int parse_date(const char *s, const char *e, struct pres *r)
{
    unsigned v[3] = {0, 0, 0};
    int n = 0;
    while (s < e && n < 3) {
        if (getnum(&s, &v[n])) return -1;
        n++;
        if (s == e) break;
        if (*s != '-' && *s != '/' && *s != '.') return -1;
        s++;
        if (s == e) break;              /* trailing separator: next is 0 */
    }
    if (s != e) return -1;
    int fmt = country[0] | (country[1] << 8);
    unsigned y, m, d;
    if (fmt == 2) {
        y = v[0]; m = v[1]; d = v[2];
    } else {
        m = v[0]; d = v[1]; y = v[2];
        if (fmt == 1) { unsigned t = m; m = d; d = t; }
    }
    if (m > 255 || d > 255) return -1;
    if (y < 100) y += 1900;
    r->y = y;
    r->m = m;
    r->d = d;
    return 0;
}

/* $P_Time_Format: h[:m[:s[.hh]]] with ':' or '.' between h/m/s, '.' or ','
 * before hundredths, optional a/p (am/pm) in 12-hour countries */
static int parse_time(const char *s, const char *e, struct pres *r)
{
    unsigned v[4] = {0, 0, 0, 0};
    int n = 0, am = 0, pm = 0;
    /* a/p suffix */
    if (!(country[0x11] & 1)) {
        const char *t = e;
        if (t > s && (t[-1] == 'm' || t[-1] == 'M') && t - 1 > s &&
            (upconv(t[-2]) == 'A' || upconv(t[-2]) == 'P'))
            t--;
        if (t > s && (upconv(t[-1]) == 'A' || upconv(t[-1]) == 'P')) {
            if (upconv(t[-1]) == 'A') am = 1; else pm = 1;
            e = t - 1;
        }
    }
    while (s < e && n < 4) {
        if (getnum(&s, &v[n])) return -1;
        n++;
        if (s == e) break;
        if (n < 3) {
            if (*s != ':' && *s != '.') return -1;
        } else {
            if (*s != '.' && *s != ',') return -1;
        }
        s++;
        if (s == e) break;
    }
    if (s != e) return -1;
    unsigned h = v[0];
    if (h > 255 || v[1] > 255 || v[2] > 255 || v[3] > 255) return -1;
    if (am) {
        if (h > 12) return -1;
        if (h == 12) h = 0;
    }
    if (pm && h != 12) {
        h += 12;
        if (h > 24) return -1;
    }
    r->h = h;
    r->mi = v[1];
    r->s = v[2];
    r->hs = v[3];
    return 0;
}

static int streqi(const char *a, const char *b, int n)
{
    for (int i = 0; i < n; i++)
        if (upconv(a[i]) != upconv(b[i])) return 0;
    return b[n] == 0;
}

int sysparse(const char **ps, const struct pblock *pb, int *npos, struct pres *res)
{
    const char *s = *ps;
    const char *start = s;
    int err = 0;

    memset(res, 0, sizeof *res);
    while (p_delim((uint8_t)*s)) s++;
    if (p_eol((uint8_t)*s)) {
        if (*npos < pb->minp) {
            err = P_MISSING;
            goto error;
        }
        *ps = s;
        res->type = PT_EOL;
        return PT_EOL;
    }
    const char *item = s;
    int sw = *s == (char)switchar;
    const char *e = item_end(s, sw);
    int len = e - item;
    if (sw) {
        int found = -1;
        if (pb->switches)
            for (int i = 0; pb->switches[i]; i++)
                if (streqi(item, pb->switches[i], len)) { found = i; break; }
        if (found < 0) {
            err = P_BADSWITCH;
            s = e;
            goto error;
        }
        res->type = PT_SWITCH;
        res->sw = found;
        memcpy(res->text, item, len);
        res->text[len] = 0;
    } else {
        if (*npos >= pb->maxp) {
            err = P_TOOMANY;
            s = e;
            goto error;
        }
        if (len > COMBUFLEN - 1) len = COMBUFLEN - 1;
        for (int i = 0; i < len; i++)
            res->text[i] = pb->capfile ? upconv(item[i]) : item[i];
        res->text[len] = 0;
        switch (pb->kind) {
        case K_FILE_OR_DRIVE:
        case K_DRIVE:
            if (len == 2 && item[1] == ':' && upconv(item[0]) >= 'A' && upconv(item[0]) <= 'Z') {
                res->type = PT_DRIVE;
                res->drive = upconv(item[0]) - 'A' + 1;
                break;
            }
            if (pb->kind == K_DRIVE) {
                err = P_BADPARM;
                s = e;
                goto error;
            }
            res->type = PT_FILE;
            break;
        case K_FILE:
            res->type = PT_FILE;
            break;
        case K_STRING:
            res->type = PT_STRING;
            if (len > 1 && res->text[len - 1] == ':') res->text[len - 1] = 0;
            break;
        case K_ONOFF:
            if (streqi(item, "ON", len)) res->num = 0;
            else if (streqi(item, "OFF", len)) res->num = 'f';
            else {
                err = P_VALUE2;
                s = e;
                goto error;
            }
            res->type = PT_STRING;
            break;
        case K_NUM: {
            const char *t = item;
            unsigned v;
            if (getnum(&t, &v) || t != e) {
                err = P_VALUE;
                s = e;
                goto error;
            }
            if (v < pb->lo || v > pb->hi) {
                err = P_RANGE;
                s = e;
                goto error;
            }
            res->type = PT_NUM;
            res->num = v;
            break;
        }
        case K_DATE:
            if (parse_date(item, e, res)) {
                err = P_FORMAT;
                s = e;
                goto error;
            }
            res->type = PT_DATE;
            break;
        case K_TIME:
            if (parse_time(item, e, res)) {
                err = P_FORMAT;
                s = e;
                goto error;
            }
            res->type = PT_TIME;
            break;
        }
        (*npos)++;
    }
    s = e;
    {
        int n = s - start;
        if (n > COMBUFLEN - 1) n = COMBUFLEN - 1;
        memcpy(err_sub, start, n);
        err_sub[n] = 0;
    }
    while (p_delim((uint8_t)*s)) s++;
    *ps = s;
    return 0;
error:
    /* SETUP_PARSE_ERROR_MSG: the text from where this call started to where
     * the parser stopped */
    {
        int n = s - start;
        if (n > COMBUFLEN - 1) n = COMBUFLEN - 1;
        memcpy(err_sub, start, n);
        err_sub[n] = 0;
        char *cr = strchr(err_sub, '\r');
        if (cr) *cr = 0;
    }
    *ps = s;
    return err;
}

/* parse_with_msg: parse; on an error, CERROR with the parse message */
int parse_with_msg(const char **ps, const struct pblock *pb, int *npos, struct pres *res)
{
    int r = sysparse(ps, pb, npos, res);
    if (r > 0) cerror_parse(r, r == P_MISSING ? NULL : err_sub);
    return r;
}

/* parse_check_eol: anything more on the line is "Too many parameters" */
int parse_check_eol(const char **ps, const struct pblock *pb, int *npos)
{
    struct pres r;
    int e = sysparse(ps, pb, npos, &r);
    if (e == PT_EOL) return 0;
    if (e == 0) e = P_TOOMANY;       /* substitution: the item just parsed */
    cerror_parse(e, e == P_MISSING ? NULL : err_sub);
}
