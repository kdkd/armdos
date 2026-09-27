/*
 * parse.c - SysParse, the MS-DOS 4.0 command line parser (INC/PARSE.ASM,
 * INC/PSDATA.INC), re-implemented in C for ARM-DOS.  Called from C as
 *
 *     parse(&in, &out);   in:  SI = command line (ends with CR), DI = &p_parms,
 *                              CX = ordinal (0 first, then out.x.cx)
 *                         out: AX = return code (0 ok, 1 too many, 2 missing,
 *                              3 bad switch, 4 bad keyword, 6 range, 7 value,
 *                              8 string, 9 syntax, FFFFh end of line), BL =
 *                              terminator, CX = next ordinal, SI = past the
 *                              operand, DX = the result buffer filled
 *
 * The routine follows PARSE.ASM step by step (packing an operand into the
 * 128-byte $P_STRING_BUF, switch / keyword / positional managers, match
 * flags in the order complex, date, time, number, signed number, drive,
 * file spec, quoted string, simple string, CAPS through INT 21h AH=65h,
 * "=" whitespace compression, the comma "missing positional" rule ...).
 *
 * Control blocks: the same structures as Microsoft's PARSE.H, laid out with
 * ARM natural alignment, every pointer and WORD pointer field 32 bits:
 *
 *   PARMS    +0 PARMSX*  +4 num_extra  +5 len_extra_delim  +6 delimiters
 *            [+6+len: EOL count, EOL characters]           (num_extra = 2)
 *   PARMSX   +0 minp  +1 maxp  [align 4: maxp CONTROL*]  maxs  [align 4:
 *            maxs CONTROL*]  maxk  [align 4: maxk CONTROL*]
 *            (an array is present only when its count is non-zero)
 *   CONTROL  +0 match_flags (u32)  +4 function_flags (u32)  +8 RESULT*
 *            +12 value list*  +16 nid  +17 synonyms, ASCIIZ each, upper case
 *   RESULT   +0 type  +1 item tag  +4 synonym*  +8 value (u32: number, flat
 *            pointer to string, drive, date = year word + month + day,
 *            time = h m s hh)
 *   VALUES   packed bytes: nval, [nrng, {tag, lo32, hi32} x nrng,
 *            [nnval, {tag, val32} x nnval, [nstr, {tag, char* (32)} x nstr]]]
 *
 * Portions: algorithm (c) Microsoft Corp. (MS-DOS 4.0 INC/PARSE.ASM), MIT
 * License - see apps/mslib/LICENSE.
 */
#include <string.h>
#include <dos.h>
#include "mslib_msg.h"
#include "mslib.h"

/* assembly-time feature switches of PSDATA.INC (KeySW etc.); a program may
   define its own _mslib_parse_features to match its _PARSE.ASM */
__attribute__((weak)) const unsigned _mslib_parse_features = MSLIB_PARSE_ALL;
#define FEAT(x) (_mslib_parse_features & (x))

/* match flags */
#define M_NUM    0x8000
#define M_SNUM   0x4000
#define M_SIMPLE 0x2000
#define M_DATE   0x1000
#define M_TIME   0x0800
#define M_CMPX   0x0400
#define M_FILE   0x0200
#define M_DRV    0x0100
#define M_QUOTE  0x0080
#define M_IGCOL  0x0010
#define M_REPEAT 0x0002
#define M_OPT    0x0001
/* function flags */
#define F_CAPFILE 0x0001
#define F_CAPCHAR 0x0002
#define F_RMCOLON 0x0010
#define F_COLONOPT 0x0020
/* result types */
#define T_NUMBER 1
#define T_LIST   2
#define T_STRING 3
#define T_CMPX   4
#define T_FILE   5
#define T_DRIVE  6
#define T_DATE   7
#define T_TIME   8
#define T_QUOTED 9
#define NO_TAG   0xFF
/* return codes */
#define RC_OK      0
#define RC_TOOMANY 1
#define RC_MISSING 2
#define RC_NOTSW   3
#define RC_NOTKEY  4
#define RC_RANGE   6
#define RC_NOTVAL  7
#define RC_NOTSTR  8
#define RC_SYNTAX  9
#define RC_EOL     0xFFFF
/* $P_Flags2 */
#define FL_EQU    0x01
#define FL_NEG    0x02
#define FL_TIME12 0x04
#define FL_KEYCMP 0x08
#define FL_SWCMP  0x10
#define FL_EXTRA  0x20
#define FL_SW     0x40
#define FL_SIGNED 0x80
/* $P_Flags1 */
#define FL1_AM    0x01

typedef unsigned char u8;

static u8 string_buf[128 + 4];          /* $P_STRING_BUF */
static struct {
    unsigned flags2, flags1;
    unsigned ordinal, rc;
    const u8 *si_save;                  /* next operand on the command line */
    u8 *dx;                             /* result buffer filled */
    u8 terminator;
    u8 *savesi_cmpx;                    /* operand start on the command line */
    const u8 *found_synonym;
    u8 *save_eob;
    u8 *keyorsw_ptr;
    int err_filespec;
    const u8 *parms;                    /* ES:DI */
} P;

static u8 *tbl_file, *tbl_char;         /* upper case tables (INT 21h AH=65h) */
static const u8 *dbcs_ev;
static int dbcs_read;
static u8 cdi[34];                      /* country information */
static int cdi_read;

static uint32_t rd32(const u8 *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static void wr32(u8 *p, uint32_t v) { memcpy(p, &v, 4); }
static const u8 *rdptr(const u8 *p) { return (const u8 *)rd32(p); }

/* ------------------------------------------------ control block access */

static unsigned ctl_match(const u8 *c) { return rd32(c) & 0xFFFF; }
static unsigned ctl_func(const u8 *c) { return rd32(c + 4) & 0xFFFF; }
static u8 *ctl_result(const u8 *c) { return (u8 *)rd32(c + 8); }
static const u8 *ctl_values(const u8 *c) { return rdptr(c + 12); }

static unsigned align4(unsigned o) { return (o + 3) & ~3u; }

/* the CONTROL* arrays of PARMSX: which 0 positional, 1 switch, 2 keyword */
static const u8 *parmsx_array(const u8 *px, int which, unsigned *count)
{
    unsigned o = 2, n = px[1];
    const u8 *arr = 0;
    for (int w = 0; ; w++) {
        if (n) { o = align4(o); arr = px + o; o += 4 * n; }
        else arr = px + o;
        if (w == which) { *count = n; return arr; }
        n = px[o];
        o++;
    }
}

/* ------------------------------------------------------ DBCS, EOL, delimiters */

static int chk_dbcs(u8 c)
{
    if (!dbcs_read) {
        union REGS r;
        r.x.ax = 0x6300;
        r.x.si = 0;
        intdos(&r, &r);
        dbcs_ev = (const u8 *)r.x.si;
        dbcs_read = 1;
    }
    if (!dbcs_ev) return 0;
    for (const u8 *p = dbcs_ev; p[0] || p[1]; p += 2)
        if (c >= p[0] && c <= p[1]) return 1;
    return 0;
}

static int chk_eol(u8 c)
{
    if (c == '\r' || c == 0 || c == '\n') return 1;
    if (P.parms[4] < 2) return 0;
    const u8 *e = P.parms + 6 + P.parms[5];
    for (unsigned i = 0; i < e[0]; i++)
        if (c == e[1 + i]) return 1;
    return 0;
}

/* *sip points at the character after c (DBCS blank check) */
static int chk_delim(u8 c, const u8 **sip)
{
    P.terminator = ' ';
    P.flags2 &= ~FL_EXTRA;
    if (c == ' ' || c == '\t') return 1;
    if (c == ',') goto extra;
    if (c == 0x81 && **sip == 0x40) { (*sip)++; return 1; }
    if (P.parms[4] >= 1) {
        for (unsigned i = 0; i < P.parms[5]; i++)
            if (c == P.parms[6 + i]) goto extra;
    }
    return 0;
extra:
    P.terminator = c;
    if (!(P.flags2 & FL_EQU)) P.flags2 |= FL_EXTRA;
    return 1;
}

/* returns 1 at the end of line; *sip is left at the first non-delimiter */
static int skip_delim(const u8 **sip)
{
    const u8 *si = *sip;
    for (;;) {
        u8 c = *si++;
        if (chk_eol(c)) { *sip = si - 1; return 1; }
        if (!chk_delim(c, &si)) { *sip = si - 1; return 0; }
        if (P.flags2 & FL_EXTRA) {
            if (P.flags2 & (FL_SW | FL_EQU)) si--;
            *sip = si;
            return 0;
        }
    }
}

static int is_digit(u8 c) { return c >= '0' && c <= '9'; }

/* $P_Chk_Switch: 1 if c is a '/' that ends the operand */
static int chk_switch(u8 c, const u8 *bx, const u8 *si)
{
    if (bx != string_buf) {
        if (c != '/') return 0;
        if (is_digit(bx[-1]) && is_digit(*si)) return 0;    /* DateSW: may be a date */
        return 1;
    }
    if (c == '/') P.flags2 |= FL_SW;
    return 0;
}

/* ------------------------------------------------------------ CAPS */

static u8 *get_table(int id)
{
    u8 b[5];
    union REGS r;
    r.x.ax = 0x6500 | id;
    r.x.bx = 0xFFFF;
    r.x.cx = 5;
    r.x.dx = 0xFFFF;
    r.x.di = (unsigned)b;
    intdos(&r, &r);
    if (r.x.cflag) return 0;
    return (u8 *)rd32(b + 1);
}

static u8 caps_char(u8 c, int id)
{
    if (c < 0x80) return (c >= 'a' && c <= 'z') ? c & 0xDF : c;
    u8 **t = (id == 4 && FEAT(MSLIB_PARSE_CAPS)) ? &tbl_file : &tbl_char;
    if (!*t) *t = get_table(t == &tbl_file ? 4 : 2);
    if (!*t) return c;
    return (*t)[2 + (c - 0x80)];
}

static void caps_string(u8 *s, int id)
{
    for (; *s; s++) {
        if (chk_dbcs(*s)) { s++; if (!*s) break; continue; }
        *s = caps_char(*s, id);
    }
}

static void remove_colon(u8 *s)
{
    for (; *s; s++) {
        if (*s == ':' && s[1] == 0) { *s = 0; return; }
        if (chk_dbcs(*s)) { s++; if (!*s) return; }
    }
}

/* ------------------------------------------------------------ results */

/* $P_Fill_Result.  v = number / list index / drive / date / time value */
static void fill_result(const u8 *ctl, u8 type, u8 tag, uint32_t v, u8 *str)
{
    u8 *r = ctl_result(ctl);
    P.dx = r;
    if (!r) return;
    r[0] = type;
    r[1] = tag;
    wr32(r + 4, (uint32_t)P.found_synonym);
    switch (type) {
    case T_NUMBER: case T_DATE: case T_TIME:
        wr32(r + 8, v);
        break;
    case T_LIST:
        wr32(r + 8, v);
        break;
    case T_DRIVE:
        r[8] = (u8)v;
        break;
    case T_CMPX:
        wr32(r + 8, (uint32_t)(P.savesi_cmpx + 1));
        break;
    default:                            /* 3 string, 5 file spec, 9 quoted */
        wr32(r + 8, (uint32_t)str);
        if (ctl_func(ctl) & F_CAPFILE) caps_string(str, 4);
        else if (ctl_func(ctl) & F_CAPCHAR) caps_string(str, 2);
        if (ctl_func(ctl) & F_RMCOLON) remove_colon(str);
        break;
    }
}

/* ------------------------------------------------------ string compare */

/* $P_String_Comp: si = operand (string buffer), syn = synonym (upper case) */
static int string_comp(u8 *si, const u8 *syn, const u8 *ctl)
{
    for (;;) {
        u8 c = *si;
        if (chk_dbcs(c)) {
            if (c != *syn) return 0;
            si++; syn++;
            if (*si != *syn) return 0;
            si++; syn++;
            continue;
        }
        c = caps_char(c, 2);
        if (P.flags2 & FL_KEYCMP) {
            if (c == '=') {
                if (syn[1] != 0) return 0;
                si++;
                goto same;
            }
        } else if (P.flags2 & FL_SWCMP) {
            if (c == ':') {
                if (*syn != 0) return 0;
                si++;
                goto same;
            }
        }
        if (c != *syn) {
            if ((P.flags2 & FL_SW) && (ctl_func(ctl) & F_COLONOPT) && *syn == 0) goto same;
            if (!(ctl_match(ctl) & M_IGCOL)) return 0;
            if (c == ':') { if (*syn == 0) goto same; return 0; }
            if (c == 0 && *syn == ':') goto same;
            return 0;
        }
        if (!c) goto same;
        si++; syn++;
    }
same:
    P.keyorsw_ptr = si;
    return 1;
}

static int search_keyorsw(u8 *si, const u8 *ctl)
{
    unsigned n = ctl[16];
    const u8 *syn = ctl + 17;
    for (; n; n--) {
        if (string_comp(si, syn, ctl)) { P.found_synonym = syn; return 1; }
        while (*syn) syn++;
        syn++;
    }
    return 0;
}

/* --------------------------------------------------------- the formats */

static void value(u8 *si, const u8 *ctl)
{
    uint32_t v = 0;
    u8 type = T_NUMBER, tag = NO_TAG;
    for (; *si; si++) {
        if (!is_digit(*si)) goto err;
        uint64_t t = (uint64_t)v * 10 + (*si - '0');
        if (P.flags2 & FL_NEG) { if (t > 0x80000000u) goto err; }
        else if (t > 0xFFFFFFFFu) goto err;
        v = (uint32_t)t;
    }
    if (P.flags2 & FL_NEG) v = -v;
    const u8 *vl = ctl_values(ctl);
    if (!vl || vl[0] == 0) goto done;
    {
        const u8 *p = vl + 1;
        unsigned nrng = *p;
        if (FEAT(MSLIB_PARSE_VAL1) && nrng) {
            const u8 *e = p + 1;
            for (unsigned i = 0; i < nrng; i++, e += 9) {
                uint32_t lo = rd32(e + 1), hi = rd32(e + 5);
                int in = (P.flags2 & FL_SIGNED) ? ((int32_t)v >= (int32_t)lo && (int32_t)v <= (int32_t)hi)
                                                : (v >= lo && v <= hi);
                if (in) { tag = e[0]; goto done; }
            }
            P.rc = RC_RANGE;
            goto done;
        }
        if (FEAT(MSLIB_PARSE_VAL2)) {
            const u8 *q = p + nrng * 9 + 1;
            unsigned nn = *q++;
            for (unsigned i = 0; i < nn; i++, q += 5)
                if (rd32(q + 1) == v) { tag = q[0]; goto done; }
            P.rc = RC_NOTVAL;
            goto done;
        }
    }
err:
    P.rc = RC_SYNTAX;
    type = T_STRING;
    tag = NO_TAG;
    fill_result(ctl, type, tag, 0, string_buf);
    return;
done:
    fill_result(ctl, type, tag, v, 0);
}

static void svalue(u8 *si, const u8 *ctl)
{
    P.flags2 |= FL_SIGNED;
    P.flags2 &= ~FL_NEG;
    if (*si == '+') si++;
    else if (*si == '-') { P.flags2 |= FL_NEG; si++; }
    value(si, ctl);
}

static void simple_string(u8 *si, const u8 *ctl)
{
    const u8 *vl = ctl_values(ctl);
    if (!vl || vl[0] == 0) { fill_result(ctl, T_STRING, NO_TAG, 0, si); return; }
    if (vl[0] == 3 && FEAT(MSLIB_PARSE_VAL3 | MSLIB_PARSE_KEY)) {
        const u8 *p = vl + 1;
        p += p[0] * 9 + 1;              /* ranges */
        p += p[0] * 5 + 1;              /* values */
        unsigned n = *p++;
        for (unsigned i = 0; i < n; i++, p += 5) {
            const u8 *s = rdptr(p + 1);
            if (string_comp(si, s, ctl)) {
                fill_result(ctl, T_LIST, p[0], (uint32_t)s, 0);
                return;
            }
        }
        P.rc = RC_NOTSTR;
        fill_result(ctl, T_STRING, NO_TAG, 0, si);
        return;
    }
    P.rc = RC_SYNTAX;
    fill_result(ctl, T_STRING, NO_TAG, 0, si);
}

static void set_cdi(void)
{
    if (!cdi_read) {
        union REGS r;
        r.x.ax = 0x3800;
        r.x.dx = (unsigned)cdi;
        intdos(&r, &r);
        cdi_read = 1;
    }
}

/* $P_Get_DecNum: a number up to a separator; *term = separator or 0 */
static int get_decnum(u8 **sip, u8 *term, unsigned *out, int time)
{
    u8 *si = *sip;
    unsigned v = 0;
    int n = 0;
    for (;;) {
        u8 c = *si;
        if (c == 0) { *term = 0; break; }
        if (is_digit(c)) { v = v * 10 + (c - '0'); if (v > 0xFFFF) return 0; n++; si++; continue; }
        if (time ? (c == ':' || c == '.' || c == ',') : (c == '-' || c == '/' || c == '.')) {
            *term = c; si++; break;
        }
        return 0;
    }
    if (!n) return 0;
    *sip = si;
    *out = v;
    return 1;
}

static void date_format(u8 *si, const u8 *ctl)
{
    unsigned v[3] = { 0, 0, 0 };
    u8 term = 0;
    set_cdi();
    int i;
    for (i = 0; i < 3; i++) {
        if (!get_decnum(&si, &term, &v[i], 0)) goto err;
        if (!term) break;
    }
    if (term) goto err;
    unsigned fmt = cdi[0] | (cdi[1] << 8), y, m, d;
    if (fmt == 2) { y = v[0]; m = v[1]; d = v[2]; }
    else { m = v[0]; d = v[1]; y = v[2]; if (fmt == 1) { unsigned t = m; m = d; d = t; } }
    if (m > 255 || d > 255) goto err;
    if (y < 100) y += 1900;
    fill_result(ctl, T_DATE, NO_TAG, y | (m << 16) | (d << 24), 0);
    return;
err:
    fill_result(ctl, T_STRING, NO_TAG, 0, string_buf);
    P.rc = RC_SYNTAX;
}

static void time_format(u8 *si, const u8 *ctl)
{
    unsigned v[4] = { 0, 0, 0, 0 };
    u8 term = 0;
    /* $P_Time_2412: strip a / p / am / pm */
    u8 *e = si + strlen((char *)si);
    int pm = 0, am = 0;
    if (e > si) {
        u8 c = e[-1] | 0x20;
        if (c == 'm' && e - si >= 2) { c = e[-2] | 0x20; if (c == 'p' || c == 'a') { e[-2] = 0; pm = c == 'p'; am = c == 'a'; } }
        else if (c == 'p' || c == 'a') { e[-1] = 0; pm = c == 'p'; am = c == 'a'; }
    }
    int i;
    for (i = 0; i < 4; i++) {
        if (!get_decnum(&si, &term, &v[i], 1)) goto err;
        if (!term) break;
    }
    if (term) goto err;
    if (pm) { if (v[0] > 12 || v[0] == 0) goto err; if (v[0] < 12) v[0] += 12; }
    if (am) { if (v[0] > 12 || v[0] == 0) goto err; if (v[0] == 12) v[0] = 0; }
    if (v[0] > 23 || v[1] > 59 || v[2] > 59 || v[3] > 99) goto err;
    fill_result(ctl, T_TIME, NO_TAG, v[0] | (v[1] << 8) | (v[2] << 16) | (v[3] << 24), 0);
    return;
err:
    fill_result(ctl, T_STRING, NO_TAG, 0, string_buf);
    P.rc = RC_SYNTAX;
}

/* $P_Quoted_Str: copy from the command line (bx) to si until the closing quote */
static int quoted_str(u8 **sip, u8 **bxp)
{
    u8 *si = *sip, *bx = *bxp;
    for (;;) {
        u8 c = bx[0];
        if (chk_eol(c)) { *sip = si; *bxp = bx; return 0; }
        if (c == '"') {
            if (bx[1] == '"') { *si++ = '"'; bx += 2; continue; }
            *si = 0;
            *sip = si; *bxp = bx;
            return 1;
        }
        if (chk_dbcs(c)) { *si++ = c; bx++; c = *bx; }
        *si++ = c;
        bx++;
    }
}

static void complex_format(u8 *si, const u8 *ctl)
{
    u8 *bx = P.savesi_cmpx;
    u8 *s0 = si;
    if (*bx != '(') { P.rc = RC_SYNTAX; goto out; }
    unsigned depth = 0;
    for (;;) {
        u8 c = *bx;
        if (chk_eol(c)) goto eof;
        if (c == '(') depth++;
        if (c == ')') {
            if (!depth) goto eof;
            if (--depth == 0) {
                si[0] = c; si[1] = 0;
                *bx = 0;
                const u8 *n = bx + 1;
                skip_delim(&n);
                P.si_save = n;
                goto out;
            }
        }
        if (c == '"') {
            *si++ = c; bx++;
            if (!quoted_str(&si, &bx)) goto eof;
        } else {
            if (chk_dbcs(c)) { *si++ = c; bx++; c = *bx; }
            *si = c;
        }
        si++; bx++;
    }
eof:
    *P.save_eob = 0;
    P.rc = RC_SYNTAX;
out:
    (void)s0;
    fill_result(ctl, T_CMPX, NO_TAG, 0, 0);
}

static void quoted_format(u8 *si, const u8 *ctl)
{
    u8 *bx = P.savesi_cmpx;
    if (*bx != '"') { P.rc = RC_SYNTAX; goto out; }
    bx++;
    u8 *s = si;
    if (!quoted_str(&s, &bx)) { *P.save_eob = 0; P.rc = RC_SYNTAX; goto out; }
    s[1] = 0;
    {
        const u8 *n = bx + 1;
        skip_delim(&n);
        P.si_save = n;
    }
out:
    fill_result(ctl, T_QUOTED, NO_TAG, 0, si);
}

static int filesp_chk(u8 c) { return c && strchr("[]|<>+=;\"", c) != 0; }

/* $P_File_Format */
static void file_format(u8 *si, const u8 *ctl, unsigned match)
{
    u8 *di = P.savesi_cmpx;
    u8 *start = si;
    if (!*si) {
        *start = 0;
        if (!(ctl_match(ctl) & M_OPT)) P.rc = RC_MISSING;
        goto res;
    }
    if (filesp_chk(*si)) {
        P.err_filespec = 1;
        *start = 0;
        goto res;
    }
    for (;;) {
        u8 c = *si;
        if (!c) break;
        if (filesp_chk(c)) {
            P.terminator = c;
            *si = 0;
            di++;
            P.si_save = di;
            break;
        }
        if (chk_dbcs(c)) { di++; si++; }
        di++; si++;
    }
res:
    if (match & M_FILE) fill_result(ctl, T_FILE, NO_TAG, 0, start);
}

static void drive_format(u8 *si, const u8 *ctl)
{
    u8 c = si[0];
    if (!c) return;
    if (chk_dbcs(c)) goto err;
    if (!(si[1] == ':' && si[2] == 0)) {
        if (!(ctl_match(ctl) & M_IGCOL) || si[1] != 0) goto err;
    }
    c |= 0x20;
    if (c < 'a' || c > 'z') goto err;
    fill_result(ctl, T_DRIVE, NO_TAG, c - 'a' + 1, 0);
    return;
err:
    P.rc = RC_SYNTAX;
}

/* $P_Check_Match_Flags */
static void check_match_flags(u8 *si, const u8 *ctl)
{
    unsigned m = ctl_match(ctl);
    P.err_filespec = 0;
    if (!m) {
        P.rc = RC_SYNTAX;
        fill_result(ctl, T_STRING, NO_TAG, 0, si);
        goto exit;
    }
    if ((m & M_CMPX) && FEAT(MSLIB_PARSE_CMPX)) {
        P.rc = RC_OK; complex_format(si, ctl);
        if (P.rc != RC_SYNTAX) goto exit;
    }
    if ((m & M_DATE) && FEAT(MSLIB_PARSE_DATE)) {
        P.rc = RC_OK; date_format(si, ctl);
        if (P.rc != RC_SYNTAX) goto exit;
    }
    if ((m & M_TIME) && FEAT(MSLIB_PARSE_TIME)) {
        P.rc = RC_OK; time_format(si, ctl);
        if (P.rc != RC_SYNTAX) goto exit;
    }
    if ((m & M_NUM) && FEAT(MSLIB_PARSE_NUM)) {
        P.rc = RC_OK; P.flags2 &= ~(FL_NEG | FL_SIGNED); value(si, ctl);
        if (P.rc != RC_SYNTAX) goto exit;
    }
    if ((m & M_SNUM) && FEAT(MSLIB_PARSE_NUM)) {
        P.rc = RC_OK; svalue(si, ctl);
        if (P.rc != RC_SYNTAX) goto exit;
    }
    if ((m & M_DRV) && FEAT(MSLIB_PARSE_DRV)) {
        P.rc = RC_OK;
        file_format(si, ctl, m);
        drive_format(si, ctl);
        if (P.rc != RC_SYNTAX) goto exit;
    }
    if ((m & M_FILE) && FEAT(MSLIB_PARSE_FILE)) {
        P.rc = RC_OK; file_format(si, ctl, m);
        if (P.rc != RC_SYNTAX) goto exit;
    }
    if ((m & M_QUOTE) && FEAT(MSLIB_PARSE_QUOTE)) {
        P.rc = RC_OK; quoted_format(si, ctl);
        if (P.rc != RC_SYNTAX) goto exit;
    }
    if (m & M_SIMPLE) {
        P.rc = RC_OK; simple_string(si, ctl);
    }
exit:
    if (P.err_filespec && P.rc == RC_OK) P.rc = RC_SYNTAX;
}

/* ----------------------------------------------------------- managers */

static void chk_pos_control(u8 *si, const u8 *ctl)
{
    unsigned m = ctl_match(ctl);
    if (!(m & M_REPEAT)) P.ordinal++;
    if (*si == 0) {
        if (!(m & M_OPT)) { P.rc = RC_MISSING; return; }
        fill_result(ctl, T_STRING, NO_TAG, 0, si);
        return;
    }
    check_match_flags(si, ctl);
}

/* returns 0 if the control block does not know the switch/keyword */
static int chk_sw_control(u8 *si, const u8 *ctl)
{
    P.flags2 |= FL_SWCMP;
    if (!search_keyorsw(si, ctl)) return 0;
    P.flags2 &= ~FL_SWCMP;
    P.savesi_cmpx += P.keyorsw_ptr - si;
    si = P.keyorsw_ptr;
    if (*si) { check_match_flags(si, ctl); return 1; }
    if (si[-1] == ':') P.rc = RC_SYNTAX;
    else if (ctl_match(ctl) != 0 && !(ctl_match(ctl) & M_OPT)) P.rc = RC_MISSING;
    fill_result(ctl, T_STRING, NO_TAG, 0, si);
    return 1;
}

static int chk_key_control(u8 *si, const u8 *ctl)
{
    if (!FEAT(MSLIB_PARSE_KEY)) return 0;
    P.flags2 |= FL_KEYCMP;
    if (!search_keyorsw(si, ctl)) return 0;
    P.flags2 &= ~FL_KEYCMP;
    P.savesi_cmpx += P.keyorsw_ptr - si;
    si = P.keyorsw_ptr;
    if (*si == 0) {
        P.rc = RC_SYNTAX;
        fill_result(ctl, T_STRING, NO_TAG, 0, si);
        return 1;
    }
    check_match_flags(si, ctl);
    return 1;
}

/* "=" with blanks or tabs around it becomes a plain "=" (outside quotes) */
static void compress_equals(u8 *line)
{
    unsigned count = 0;
    while (!chk_eol(line[count])) count++;
    for (;;) {
        int changed = 0, dq = 0;
        u8 *s = line;
        int cx = count;
        while (cx > 0) {
            if (*s == '"') dq = !dq;
            if (!dq) {
                u8 a = s[0], b = s[1];
                if ((a == ' ' && b == '=') || (a == '=' && b == ' ') ||
                    (a == '=' && b == '\t') || (a == '\t' && b == '=')) {
                    s[0] = '=';
                    memmove(s + 1, s + 2, cx - 1);
                    s--;
                    changed = 1;
                    count--;
                    cx--;
                }
            }
            s++;
            cx--;
        }
        if (!changed) break;
    }
}

void parse(union REGS *in, union REGS *out)
{
    const u8 *si = (const u8 *)in->x.si;
    P.parms = (const u8 *)in->x.di;
    unsigned cx = in->x.cx & 0xFFFF;
    if (out != in) *out = *in;          /* the other registers are preserved */

    P.flags1 = P.flags2 = 0;
    P.ordinal = cx;
    P.rc = RC_OK;
    P.found_synonym = 0;
    P.dx = 0;
    if (FEAT(MSLIB_PARSE_KEY)) compress_equals((u8 *)si);

    const u8 *px = rdptr(P.parms);
    if (skip_delim(&si)) {
        out->x.ax = (cx < px[0]) ? RC_MISSING : RC_EOL;
        out->x.cx = cx;
        out->x.si = (unsigned)si;
        out->x.cflag = 0;
        return;
    }
    P.savesi_cmpx = (u8 *)si;
    u8 *bx = string_buf;
    if (!(P.flags2 & FL_EXTRA)) {
        for (;;) {
            u8 c = *si++;
            if (chk_switch(c, bx, si) || chk_eol(c)) { si--; break; }
            if (chk_delim(c, &si)) {
                if (P.flags2 & FL_EXTRA) {
                    if (P.flags2 & (FL_SW | FL_EQU)) si--;
                    break;
                }
                skip_delim(&si);
                break;
            }
            if (bx < string_buf + 128) *bx++ = c;
            if (c == '=') P.flags2 |= FL_EQU;
            if (chk_dbcs(c)) { c = *si++; if (bx < string_buf + 128) *bx++ = c; }
        }
    }
    P.si_save = si;
    *bx = 0;
    P.save_eob = bx;

    u8 *s = string_buf;
    unsigned n;
    const u8 *arr;
    if (*s == '/') {                                    /* switch manager */
        arr = parmsx_array(px, 1, &n);
        int done = 0;
        if (FEAT(MSLIB_PARSE_SW))
            for (unsigned i = 0; i < n && !done; i++) done = chk_sw_control(s, rdptr(arr + 4 * i));
        if (!done) P.rc = RC_NOTSW;
    } else if (P.flags2 & FL_EQU) {                     /* keyword manager */
        arr = parmsx_array(px, 2, &n);
        int done = 0;
        for (unsigned i = 0; i < n && !done; i++) done = chk_key_control(s, rdptr(arr + 4 * i));
        if (!done) P.rc = RC_NOTKEY;
    } else {                                            /* positional */
        arr = parmsx_array(px, 0, &n);
        if (P.ordinal >= n) P.rc = RC_TOOMANY;
        else chk_pos_control(s, rdptr(arr + 4 * P.ordinal));
    }
    out->x.cx = P.ordinal;
    out->x.ax = P.rc;
    out->x.si = (unsigned)P.si_save;
    out->x.dx = (unsigned)P.dx;
    out->h.bl = P.terminator;
    out->x.cflag = 0;
}
