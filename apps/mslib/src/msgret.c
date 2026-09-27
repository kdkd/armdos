/*
 * msgret.c - the MS-DOS 4.0 message retriever (INC/MSGSERV.ASM: SYSLOADMSG,
 * SYSDISPMSG, SYSGETMSG) re-implemented in C for ARM-DOS, for Microsoft's
 * MIT-licensed C utilities (MEM, ATTRIB, FDISK, SUBST, JOIN).
 *
 * Behaviour follows MSGSERV.ASM:
 *  - messages are looked up by class (DH): 0FFh utility (the program's own
 *    table, generated from its .SKL by tools/msgtab.py), 1 extended errors,
 *    2 parse errors (DH bit 1; resident tables COMMAND.COM supplies through
 *    INT 2Fh AX=122Eh in DOS 4 - linked into each program here).  An unknown
 *    class 1/2 number prints "Extended Error n" / "Parse Error n".
 *  - %1..%9 are replaced by the sublist whose id matches (not by position);
 *    "%%" is not collapsed; a sublist with id 0 at the end of a message is
 *    the special case: " - " and the parameter are appended (before the final
 *    CR LF of a utility message).
 *  - replaceable types: char / ASCIIZ, unsigned / signed decimal and hex of
 *    byte/word/dword, date and time in the country format; min/max width,
 *    left/right alignment and pad character; a ',' pad inserts the country
 *    thousands separator.
 *  - class 1/2 messages get CR LF appended unless DH bit 7 is set.
 *  - output goes to the handle in BX with INT 21h AH=40h (so redirection and
 *    pipes work) or, for BX = FFFFh, through AH=02h.
 *  - DL requests keyboard input afterwards (01h/07h/08h/0Ah, 0Cxh = flush
 *    then function x); the character comes back in AX.
 *
 * Sublist (11 bytes, packed; the 4-byte value is a flat pointer on ARM-DOS):
 *   +0 size (= stride to the next sublist)  +1 reserved  +2 value
 *   +6 id  +7 flags  +8 max width  +9 min width  +10 pad char
 * For date/time types the value field holds the data itself (year word,
 * month, day / hour, minutes, seconds, hundredths), as in the original.
 *
 * Portions: interface and behaviour (c) Microsoft Corp. (MS-DOS 4.0,
 * INC/MSGSERV.ASM), MIT License - see apps/mslib/LICENSE.
 */
#include <string.h>
#include <dos.h>
#include "mslib_msg.h"

#define UTIL_CLASS  0xFF
#define EXT_CLASS   0x01
#define PARSE_CLASS 0x02
#define NO_CRLF     0x80
#define NO_HANDLE   0xFFFF

/* country information (INT 21h AH=38h), read once */
static int have_cty;
static unsigned char cty[34];

static void get_country(void)
{
    union REGS r;
    if (have_cty) return;
    memset(cty, 0, sizeof cty);
    r.x.ax = 0x3800;
    r.x.dx = (unsigned)cty;
    intdos(&r, &r);
    if (r.x.cflag) {                    /* the retriever's defaults */
        cty[0] = 0; cty[7] = ','; cty[9] = '.'; cty[0x0B] = '-'; cty[0x0D] = ':';
    }
    have_cty = 1;
}

static const struct msg_entry *find_in(const struct msg_entry *t, unsigned cls, unsigned num)
{
    if (!t) return 0;
    for (; t->text; t++)
        if (t->num == num && (t->cls == cls || (cls == UTIL_CLASS && t->cls == UTIL_CLASS)))
            return t;
    return 0;
}

/* returns the entry; *special is set when the class 1/2 fallback is used */
static const struct msg_entry *find_msg(unsigned num, unsigned dh, int *special)
{
    const struct msg_entry *e;
    *special = 0;
    if (dh == UTIL_CLASS) return find_in(_msg_util_table, UTIL_CLASS, num);
    if (dh & PARSE_CLASS) {
        if ((e = find_in(_msg_util_table, PARSE_CLASS, num))) return e;
        if ((e = find_in(_msg_parse_table, PARSE_CLASS, num)) && e->len) return e;
        *special = 1;
        return find_in(_msg_parse_table, PARSE_CLASS, 0xFFFF);
    }
    if ((e = find_in(_msg_util_table, EXT_CLASS, num))) return e;
    if ((e = find_in(_msg_extend_table, EXT_CLASS, num)) && e->len) return e;
    *special = 1;
    return find_in(_msg_extend_table, EXT_CLASS, 0xFFFF);
}

/* ------------------------------------------------------------ output */

static unsigned out_handle;
static int out_error;

static void out_bytes(const char *p, unsigned n)
{
    union REGS r;
    if (!n || out_error) return;
    if (out_handle == NO_HANDLE) {
        while (n--) {
            r.h.ah = 0x02;
            r.h.dl = (unsigned char)*p++;
            intdos(&r, &r);
        }
        return;
    }
    r.x.ax = 0x4000;
    r.x.bx = out_handle;
    r.x.cx = n;
    r.x.dx = (unsigned)p;
    intdos(&r, &r);
    if (r.x.cflag || (r.x.ax & 0xFFFF) != n) out_error = 1;
}

/* -------------------------------------------------- the replacements */

struct sub {                            /* a decoded sublist */
    unsigned char size, id, flags, maxw, minw, pad;
    unsigned char raw[4];               /* the value field (pointer or data) */
    const unsigned char *ptr;
};

static void read_sub(const unsigned char *s, struct sub *o)
{
    uint32_t v;
    o->size = s[0];
    memcpy(o->raw, s + 2, 4);
    memcpy(&v, s + 2, 4);
    o->ptr = (const unsigned char *)v;
    o->id = s[6];
    o->flags = s[7];
    o->maxw = s[8];
    o->minw = s[9];
    o->pad = s[10];
}

/* digits of v (base 10 or 16), most significant first; thousands
   separators when the pad character is ',' ($M_CONVERT2ASC) */
static int conv(uint32_t v, unsigned base, int thou, char *out)
{
    char tmp[24];
    int n = 0;
    for (;;) {
        unsigned d = v % base;
        v /= base;
        tmp[n++] = d > 9 ? d + 55 : d + '0';
        if (!v) break;
        if (thou && (n == 3 || n == 7 || n == 11)) tmp[n++] = cty[7] ? cty[7] : ',';
    }
    for (int i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
    return n;
}

static int conv2(unsigned v, char *out)  /* $M_CONVERTDATE / TIME: at least 2 digits */
{
    int n = conv(v, 10, 0, out);
    if (n == 1) { out[1] = out[0]; out[0] = '0'; n = 2; }
    return n;
}

static void replace(const struct sub *s, int special)
{
    char buf[160];
    const char *src = buf;
    int len = 0;
    unsigned type = s->flags & 0x0F, size = s->flags & 0x30;

    if (special) out_bytes(" - ", 3);
    if (type == 0) {                                    /* character */
        if (size == 0x10) {
            src = (const char *)s->ptr;
            len = src ? strlen(src) : 0;
        } else {
            buf[0] = s->ptr ? (char)*s->ptr : 0;
            len = 1;
        }
    } else if (type == 1 || type == 2 || type == 3) {   /* numbers */
        uint32_t v = 0;
        int neg = 0;
        if (s->ptr) {
            if (size == 0x00 || size == 0x10) {
                v = s->ptr[0];
                if (type == 2 && (v & 0x80)) { neg = 1; v &= 0x7F; }
            } else if (size == 0x20) {
                v = s->ptr[0] | (s->ptr[1] << 8);
                if (type == 2 && (v & 0x8000)) { neg = 1; v &= 0x7FFF; }
            } else {
                memcpy(&v, s->ptr, 4);
                if (type == 2 && (v & 0x80000000u)) { neg = 1; v &= 0x7FFFFFFFu; }
            }
        }
        if (neg) buf[len++] = '-';
        get_country();
        len += conv(v, type == 3 ? 16 : 10, s->pad == ',', buf + len);
    } else if (type == 4) {                             /* date: year word, month, day */
        unsigned y = s->raw[0] | (s->raw[1] << 8), m = s->raw[2], d = s->raw[3];
        char sep;
        get_country();
        sep = cty[0x0B] ? cty[0x0B] : '-';
        /* $M_YEAR: without the 4-digit bit the year is capped at 99 (the
           callers pass year - 1900; ATTRIB shows 2026 as 99) */
        if (!(s->flags & 0x10) && y > 99) y = 99;
        unsigned f = cty[0] | (cty[1] << 8);
        unsigned a = m, b = d, c = y;
        if (f == 1) { a = d; b = m; }
        else if (f == 2) { a = y; b = m; c = d; }
        len += conv2(a, buf + len); buf[len++] = sep;
        len += conv2(b, buf + len); buf[len++] = sep;
        len += conv2(c, buf + len);
    } else {                                            /* time: hour, min, sec, hundredths */
        unsigned h = s->raw[0], mi = s->raw[1], se = s->raw[2], hu = s->raw[3];
        int twelve;
        get_country();
        twelve = type == 5 && !(cty[0x11] & 1);
        char tsep = cty[0x0D] ? cty[0x0D] : ':', dsep = cty[9] ? cty[9] : '.';
        unsigned hh = h;
        if (twelve) { if (hh >= 13) hh -= 12; if (hh == 0) hh = 12; }
        len += conv(hh, 10, 0, buf + len);
        buf[len++] = tsep; len += conv2(mi, buf + len);
        if (size == 0x10 || size == 0x20) { buf[len++] = tsep; len += conv2(se, buf + len); }
        if (size == 0x20) { buf[len++] = dsep; len += conv2(hu, buf + len); }
        if (twelve) buf[len++] = (h < 12 || h > 23) ? 'a' : 'p';
    }

    int pad = s->minw > len ? s->minw - len : 0;
    if (s->maxw && len > s->maxw) len = s->maxw;
    char pc = (char)s->pad;
    if (pad && (s->flags & 0x80)) for (int i = 0; i < pad; i++) out_bytes(&pc, 1);
    out_bytes(src, len);
    if (pad && !(s->flags & 0x80)) for (int i = 0; i < pad; i++) out_bytes(&pc, 1);
}

/* $M_DISPLAY_MESSAGE */
static void display(const struct msg_entry *e, unsigned dh, unsigned nsub,
                    const unsigned char *sublists, int fallback, unsigned fbnum)
{
    const char *p = e->text;
    int remain = e->len;

    if (fallback) {                     /* "Extended Error %1" + the number */
        char num[12];
        const char *pc = strchr(p, '%');
        out_bytes(p, pc ? (unsigned)(pc - p) : (unsigned)remain);
        out_bytes(num, conv(fbnum, 10, 0, num));
        return;
    }
    if (!nsub || !sublists) { out_bytes(p, remain); return; }
    for (;;) {
        int n = 0;
        char prev = 0;
        while (n < remain) {
            if (p[n] == '%' && n + 1 < remain && p[n + 1] != '%' && prev != '%') break;
            prev = p[n++];
        }
        int rest = remain - n;
        struct sub s;
        int found = 0, special = 0;
        if (nsub) {
            nsub--;
            const unsigned char *sl = sublists;
            for (unsigned i = 0; i < 16; i++) {
                read_sub(sl, &s);
                if (rest && s.id + '0' == (unsigned char)p[n + 1]) { found = 1; break; }
                if (!rest && s.id == 0) { special = 1; break; }
                if (!s.size) break;
                sl += s.size;
            }
        }
        if (special) {
            int hold = (dh == UTIL_CLASS && n >= 2) ? 2 : 0;
            out_bytes(p, n - hold);
            replace(&s, 1);
            out_bytes(p + n - hold, hold);
            return;
        }
        out_bytes(p, n);
        if (!rest) return;
        if (found) replace(&s, 0);
        else out_bytes(p + n, 2);       /* no sublist for it: print as is */
        p += n + 2;
        remain = rest - 2;
        if (remain <= 0) return;
    }
}

/* ------------------------------------------------------ the services */

void sysloadmsg(union REGS *in, union REGS *out)
{
    union REGS r;
    (void)in;
    r.x.ax = 0x3000;
    intdos(&r, &r);
    get_country();
    if ((r.x.ax & 0xFFFF) != (MSLIB_EXPECTED_MAJOR | (MSLIB_EXPECTED_MINOR << 8))) {
        out->x.ax = 1;                  /* "Incorrect DOS version" */
        out->x.bx = 2;                  /* STDERR */
        out->x.cx = 0;
        out->h.dl = 0;
        out->h.dh = UTIL_CLASS;
        out->x.cflag = 1;
        return;
    }
    out->x.cflag = 0;
}

void sysgetmsg(union REGS *in, struct SREGS *segs, union REGS *out)
{
    unsigned num = in->x.ax & 0xFFFF, dh = in->h.dh;
    int special;
    const struct msg_entry *e = find_msg(num, dh, &special);
    if (segs) segs->ds = segs->es = 0;
    if (!e) { out->x.cflag = 1; return; }
    out->x.ax = num;
    out->x.si = (unsigned)e->text;
    out->x.cx = e->len;
    out->x.cflag = 0;
}

void sysdispmsg(union REGS *in, union REGS *out)
{
    unsigned num = in->x.ax & 0xFFFF, handle = in->x.bx & 0xFFFF;
    unsigned nsub = in->x.cx & 0xFFFF, dl = in->h.dl, dh = in->h.dh;
    const unsigned char *sub = (const unsigned char *)in->x.si;
    unsigned di = in->x.di;
    int special;
    union REGS save = *in;

    const struct msg_entry *e = find_msg(num, dh, &special);
    *out = save;
    if (!e) { out->x.cflag = 1; return; }
    out_handle = handle;
    out_error = 0;
    display(e, dh, nsub, sub, special, num);
    if (dh != UTIL_CLASS && !(dh & NO_CRLF)) out_bytes("\r\n", 2);
    if (out_error) {
        out->x.ax = 39;                 /* insufficient disk space ($M_GET_EXT_ERR_39) */
        out->x.cflag = 1;
        return;
    }
    if (dl) {                           /* $M_WAIT_FOR_INPUT */
        union REGS r;
        if (dl > 0xC0) { r.h.ah = 0x0C; r.h.al = dl & 0x0F; }
        else r.h.ah = dl;
        r.x.dx = di;
        intdos(&r, &r);
        if (dl != 0x0A) r.h.ah = 0;
        out->x.ax = r.x.ax & 0xFFFF;
    }
    out->x.cflag = 0;
}
