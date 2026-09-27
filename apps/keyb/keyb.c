/*
 * keyb.c - KEYB.COM for ARM-DOS 4.00: the transient part (the command line,
 * KEYBOARD.SYS, the messages, installing and updating the resident part).
 *
 *   KEYB [xx[,[yyy],[[d:][path]KEYBOARD.SYS]]] [/ID:nnn]
 *
 * A re-creation in C of MS-DOS 4.00's KEYB (CMD/KEYB/KEYBCMD.ASM, PARSER.ASM,
 * KEYBTBBL.ASM; Microsoft, MIT licence), checked against the genuine KEYB 4.00
 * under DOSBox-X: the same parameter rules and error messages (including the
 * way the parser quotes the offending parameter), the query output, how the
 * code page is chosen (the one given, else the one active on CON (DISPLAY.SYS,
 * INT 2Fh AD02h/AD03h), else the layout's first), the tables loaded for every
 * code page prepared on CON, the warnings, the exit codes. The resident part
 * is kbres.c.
 */
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <dos.h>
#include "keyb.h"

/* exit codes (KEYBCMD.ASM) */
#define INVALID_PARMS      1
#define BAD_KEYB_DEF_FILE  2
#define MEMORY_OVERFLOW    3
#define CONSOLE_ERROR      4
#define CP_NOT_DESIGNATED  5
#define KEYB_TABLE_NOT_LOAD 6


/* ------------------------------------------------------------- output */

static void out(const char *s) { union REGS r = { 0 }; r.x.ax = 0x4000; r.x.bx = 1; r.x.cx = strlen(s); r.x.dx = (unsigned)s; intdos(&r, &r); }

static void outn(unsigned n)
{
    char b[8]; int i = 7;
    b[i] = 0;
    do { b[--i] = '0' + n % 10; n /= 10; } while (n);
    out(b + i);
}

static int int2f(union REGS *r) { int86(0x2F, r, r); return r->x.cflag != 0; }

/* --------------------------------------------------------- the parser */

/* RET_CODE_1: the first parameter */
enum { LANG_VALID, LANG_INVALID, NO_LANG, NO_IDLANG };

static struct {
    int rc1, rc2, syntax, idvalid;      /* rc2: 0 valid code page, 1 invalid, 2 none */
    int err;                            /* parse message number */
    const char *old_ptr, *cur_ptr;      /* the text the parse error quotes */
    char lang[3];
    unsigned cp, id;
    int id_first;                       /* the ID came as the first parameter */
    char path[128];
} P;

static const char *PARSE_MSG[] = {
    0, "Too many parameters", "Required parameter missing", "Invalid switch", "Invalid keyword", 0,
    "Parameter value not in allowed range", "Parameter value not allowed", "Parameter value not allowed",
    "Parameter format not correct", "Invalid parameter", "Invalid parameter combination",
};

static int is_delim(int c) { return c == ' ' || c == '\t' || c == ',' || c == ';' || c == '='; }
static int is_end(int c) { return c == '\r' || c == 0; }

/* one item of the command line: [p, e) is its text, *next where the parser goes on
   (after one following delimiter) */
static const char *item(const char *p, const char **start, const char **end, int *comma)
{
    while (*p == ' ' || *p == '\t') p++;
    *start = p;
    *comma = 0;
    if (*p == ',' || *p == ';') { *end = p; *comma = 1; return p + 1; }   /* an empty positional */
    while (!is_end(*p) && !is_delim(*p) && !(*p == '/' && p != *start)) p++;
    *end = p;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == ',' || *p == ';') { *comma = 1; p++; }
    return p;
}

static int all_digits(const char *s, const char *e) { if (s == e) return 0; for (; s < e; s++) if (*s < '0' || *s > '9') return 0; return 1; }
static unsigned number(const char *s, const char *e) { unsigned long v = 0; for (; s < e; s++) { v = v * 10 + (*s - '0'); if (v > 65535) v = 65535; } return v; }
static int upc(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }

static void parse_error(int msg, const char *next)
{
    P.syntax = 1;
    P.err = msg;
    P.old_ptr = P.cur_ptr;
    P.cur_ptr = next;
}

static void parse(const char *tail)
{
    memset(&P, 0, sizeof P);
    P.rc1 = NO_IDLANG; P.rc2 = 2;
    P.cur_ptr = tail;
    const char *p = tail;
    int count = 0;
    for (;;) {
        const char *s, *e;
        int comma;
        while (*p == ' ' || *p == '\t') p++;
        if (is_end(*p)) break;
        const char *next = item(p, &s, &e, &comma);
        if (*s == '/') {                                    /* the only switch: /ID:nnn */
            if (e - s >= 3 && upc(s[1]) == 'I' && upc(s[2]) == 'D' && (s[3] == ':' || s[3] == '=')) {
                const char *v = s + 4;
                if (all_digits(v, e)) {
                    unsigned n = number(v, e);
                    if (n > 999) { P.rc1 = LANG_INVALID; parse_error(0, next); return; }
                    P.idvalid = 1; P.id = n;
                    p = next;
                    continue;
                }
            }
            parse_error(3, next);
            return;
        }
        count++;
        if (count == 1) {
            if (s == e) { P.rc1 = LANG_INVALID; parse_error(10, next); return; }
            if (all_digits(s, e)) {
                unsigned n = number(s, e);
                if (n > 999) { P.rc1 = LANG_INVALID; parse_error(0, next); return; }
                P.rc1 = LANG_VALID; P.idvalid = 1; P.id = n; P.id_first = 1;
            } else if (e - s == 2) {
                P.lang[0] = upc(s[0]); P.lang[1] = upc(s[1]);
                P.rc1 = LANG_VALID;
            } else { P.rc1 = LANG_INVALID; parse_error(10, next); return; }
        } else if (count == 2) {
            if (s == e) P.rc2 = 2;
            else if (all_digits(s, e)) {
                unsigned n = number(s, e);
                if (n > 999) { P.rc2 = 1; parse_error(0, next); return; }
                P.rc2 = 0; P.cp = n;
            } else { P.rc2 = 1; parse_error(10, next); return; }
        } else if (count == 3) {
            unsigned n = e - s;
            if (n >= sizeof P.path) n = sizeof P.path - 1;
            memcpy(P.path, s, n);
            P.path[n] = 0;
        } else { parse_error(1, next); return; }
        P.old_ptr = P.cur_ptr;
        P.cur_ptr = next;
        p = next;
    }
    if (P.idvalid && !P.id_first && P.rc1 != LANG_VALID) {
        const char *e = p;
        P.syntax = 1; P.err = 8;
        P.old_ptr = P.cur_ptr; P.cur_ptr = e;
    }
}

/* -------------------------------------------------------- KEYBOARD.SYS */

static int fh = -1;
static uint8_t hdr[0x1C];
static unsigned num_id, num_lang;

static int dos_open(const char *name)
{
    union REGS r = { 0 }; r.x.ax = 0x3D00; r.x.dx = (unsigned)name;
    intdos(&r, &r);
    return r.x.cflag ? -(int)(r.x.ax & 0xFFFF) : (int)(r.x.ax & 0xFFFF);
}
static int dos_read_at(uint32_t off, void *buf, unsigned n)
{
    union REGS r = { 0 };
    r.x.ax = 0x4200; r.x.bx = fh; r.x.cx = off >> 16; r.x.dx = off & 0xFFFF;
    intdos(&r, &r);
    if (r.x.cflag) return -1;
    r.x.ax = 0x3F00; r.x.bx = fh; r.x.cx = n; r.x.dx = (unsigned)buf;
    intdos(&r, &r);
    if (r.x.cflag || (r.x.ax & 0xFFFF) != n) return -1;
    return 0;
}
static void dos_close(void) { if (fh >= 0) { union REGS r = { 0 }; r.x.ax = 0x3E00; r.x.bx = fh; intdos(&r, &r); fh = -1; } }

static char fname[160];

/* BUILD_PATH: the path given, else KEYBOARD.SYS in the current directory, in
   KEYB.COM's own directory, in the root */
static int build_path(void)
{
    if (P.path[0]) { strcpy(fname, P.path); return dos_open(fname); }
    strcpy(fname, "KEYBOARD.SYS");
    int h = dos_open(fname);
    if (h >= 0 || (h != -2 && h != -3)) return h;
    /* argv0: after the environment's double NUL and the word count */
    const uint8_t *psp = (const uint8_t *)_armdos_psp;
    const char *env = (const char *)(((uint32_t)(psp[0x2C] | (psp[0x2D] << 8))) << 4);
    if (env) {
        const char *q = env;
        while (q[0] || q[1]) q++;
        q += 4;
        strncpy(fname, q, sizeof fname - 16);
        fname[sizeof fname - 16] = 0;
        char *bs = strrchr(fname, '\\');
        strcpy(bs ? bs + 1 : fname, "KEYBOARD.SYS");
        h = dos_open(fname);
        if (h >= 0 || (h != -2 && h != -3)) return h;
    }
    strcpy(fname, "\\KEYBOARD.SYS");
    return dos_open(fname);
}

static uint16_t rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }

/* the language entry: 10 bytes + 6 per code page */
static uint8_t lent[10 + 6 * 16];
static uint32_t lent_off;

static int read_lang_entry(uint32_t off)
{
    lent_off = off;
    if (dos_read_at(off, lent, 10)) return -1;
    if (lent[9] > 16 || dos_read_at(off + 10, lent + 10, 6 * lent[9])) return -1;
    return 0;
}

/* the offset of the code page's translate section, 0 if the layout has none */
static uint32_t cp_table(unsigned cp)
{
    for (unsigned i = 0; i < lent[9]; i++)
        if (rd16(lent + 10 + 6 * i) == cp) return rd16(lent + 12 + 6 * i) | ((uint32_t)rd16(lent + 14 + 6 * i) << 16);
    return 0;
}

/* ------------------------------------------------------- building the tables */

static uint8_t *tb;             /* the tables, as they will be in resident memory */
static uint32_t tb_used, tb_size;
static uint8_t *tb_dest;        /* where they will be */

/* copy a translate section from the file, keeping only the states for our
   keyboard type (STATE_BUILD) */
static int load_section(uint32_t off, unsigned max)
{
    uint8_t *buf = malloc(max + 16);
    if (!buf) return -1;
    if (dos_read_at(off, buf, 4)) { free(buf); return -1; }
    unsigned len = rd16(buf);
    if (len < 6 || len > max || dos_read_at(off, buf, len)) { free(buf); return -1; }
    if (tb_used + len + 4 > tb_size) { free(buf); return -2; }
    uint8_t *d = tb + tb_used, *o = d;
    memcpy(o, buf, 4);
    o += 4;
    const uint8_t *p = buf + 4;
    while (p + 2 <= buf + len) {
        unsigned sl = rd16(p);
        if (!sl) break;
        if (p + sl > buf + len) break;
        if (rd16(p + 3) & G_KB) { memcpy(o, p, sl); o += sl; }
        p += sl;
    }
    o[0] = o[1] = 0;
    o += 2;
    unsigned nl = o - d;
    d[0] = nl; d[1] = nl >> 8;
    tb_used += (nl + 3) & ~3u;
    free(buf);
    return (int)(d - tb);
}

/* the layout ids published on port F6h (ARCH.md 4.6) */
struct layout_id { char code[2]; uint16_t id; uint8_t n; };
static const struct layout_id LAYOUT_IDS[] = {
    { "GR", 0, 1 }, { "SP", 0, 2 }, { "PO", 0, 3 }, { "FR", 189, 4 }, { "DK", 0, 5 }, { "SG", 0, 6 },
    { "IT", 141, 7 }, { "UK", 166, 8 }, { "SF", 0, 9 }, { "BE", 0, 10 }, { "NL", 0, 11 }, { "NO", 0, 12 },
    { "CF", 0, 13 }, { "SV", 0, 14 }, { "SU", 0, 15 }, { "LA", 0, 16 }, { "DV", 0, 17 }, { "DL", 0, 18 },
    { "DR", 0, 19 }, { "FR", 120, 20 }, { "IT", 142, 21 }, { "UK", 168, 22 },
};
static uint8_t layout_number(const uint8_t *e)
{
    for (unsigned i = 0; i < sizeof LAYOUT_IDS / sizeof LAYOUT_IDS[0]; i++) {
        const struct layout_id *l = &LAYOUT_IDS[i];
        if (l->code[0] == e[0] && l->code[1] == e[1] && (!l->id || l->id == rd16(e + 2))) return l->n;
    }
    return 0;           /* US */
}

/* ----------------------------------------------------------- main */

static struct keybsd *res;      /* the resident KEYB, if installed */
static int con;                 /* DISPLAY.SYS (INT 2Fh AD00h) */
static int exit_code;
static int query_call;

static void finish(void) __attribute__((noreturn));
static void finish(void)
{
    dos_close();
    if (res) res->table_ok = 1;
    exit(exit_code);
}

static void msg_exit(const char *m, int code) { out(m); exit_code = code; finish(); }

int main(void)
{
    const uint8_t *psp = (const uint8_t *)_armdos_psp;
    union REGS r = { 0 };
    r.x.ax = 0x3000; intdos(&r, &r);
    if ((r.x.ax & 0xFFFF) != 4) { out("Incorrect DOS version\r\n"); return 1; }

    r.x.ax = 0xAD80; r.x.cflag = 0;
    int2f(&r);
    if ((r.x.ax & 0xFF) == 0xFF && r.x.di && !memcmp((void *)r.x.di, "KEYB", 4)) {
        res = (struct keybsd *)r.x.di;
        res->table_ok = 0;              /* no processing while we build */
    }
    memset(&r, 0, sizeof r);
    r.x.ax = 0xAD00;
    int2f(&r);
    con = (r.x.ax & 0xFF) == 0xFF;

    char tail[130];
    unsigned tl = psp[0x80];
    if (tl > 127) tl = 127;
    memcpy(tail, psp + 0x81, tl);
    tail[tl] = '\r';
    parse(tail);

    if (P.syntax) {
        if (P.rc1 == LANG_INVALID && !P.err) msg_exit("Invalid keyboard ID specified\r\n", INVALID_PARMS);
        if (P.rc2 == 1 && !P.err) msg_exit("Invalid code page specified\r\n", INVALID_PARMS);
        out(PARSE_MSG[P.err] ? PARSE_MSG[P.err] : "Invalid parameter");
        out(" - ");
        char q[130];
        unsigned n = P.cur_ptr - P.old_ptr;
        if (n > 128) n = 128;
        memcpy(q, P.old_ptr, n);
        q[n] = 0;
        char *cr = strchr(q, '\r');
        if (cr) *cr = 0;
        out(q);
        out("\r\n");
        exit_code = INVALID_PARMS;
        finish();
    }

    if (P.rc1 == NO_IDLANG) {           /* query */
        query_call = 1;
        int id_shown = 0;
        if (res) {
            if (res->language[0]) {
                out("Current keyboard code: ");
                char l[3] = { res->language[0], res->language[1], 0 };
                out(l);
            } else {
                out("Current keyboard ID: ");
                outn(res->invoked_id);
                id_shown = 1;
            }
            out("  code page: ");
            outn(res->invoked_cp);
            out("\r\n");
            if (!id_shown && res->invoked_id) {
                out("Current keyboard ID: ");
                outn(res->invoked_id);
                out("\n\r");
            }
        } else out("KEYB has not been installed\r\n");
        if (con) {
            memset(&r, 0, sizeof r);
            r.x.ax = 0xAD02;
            if (int2f(&r)) msg_exit("Active code page not available from CON device\r\n", CONSOLE_ERROR);
            out("Current CON code page: ");
            outn(r.x.bx & 0xFFFF);
            out("\r\n");
        } else out("Active code page not available from CON device\r\n");
        exit_code = 0;
        finish();
    }

    /* ---- install / change the layout ---- */
    fh = build_path();
    if (fh < 0) { fh = -1; msg_exit("Bad or missing Keyboard Definition File\r\n", BAD_KEYB_DEF_FILE); }
    if (dos_read_at(0, hdr, sizeof hdr) || hdr[0] != 0xFF || memcmp(hdr + 1, "KEYB   ", 7))
        msg_exit("Bad or missing Keyboard Definition File\r\n", BAD_KEYB_DEF_FILE);
    num_id = rd16(hdr + 0x18); num_lang = rd16(hdr + 0x1A);
    unsigned max_com = rd16(hdr + 0x10), max_spec = rd16(hdr + 0x12), max_logic = rd16(hdr + 0x14);
    uint8_t *tabs = malloc(6 * (num_lang + num_id));
    if (!tabs || dos_read_at(0x1C, tabs, 6 * (num_lang + num_id)))
        msg_exit("Bad or missing Keyboard Definition File\r\n", BAD_KEYB_DEF_FILE);
    const uint8_t *idtab = tabs + 6 * num_lang;
    uint32_t entry = 0;

    if (P.idvalid) {
        /* an ID: alone it names the layout; with a code, the two must agree */
        int found = 0, match = 0;
        for (unsigned i = 0; i < num_id; i++) {
            if (rd16(idtab + 6 * i) != P.id) continue;
            found = 1;
            uint32_t off = rd16(idtab + 6 * i + 2) | ((uint32_t)rd16(idtab + 6 * i + 4) << 16);
            if (P.id_first) { entry = off; match = 1; break; }
            uint8_t e[4];
            if (dos_read_at(off, e, 4)) msg_exit("Bad or missing Keyboard Definition File\r\n", BAD_KEYB_DEF_FILE);
            if (e[0] == P.lang[0] && e[1] == P.lang[1]) { entry = off; match = 1; break; }
        }
        if (!found) msg_exit("Invalid keyboard ID specified\r\n", INVALID_PARMS);
        if (!match) {
            /* the language must still exist for the combination message */
            msg_exit("Keyboard ID specified is inconsistent with the selected keyboard layout\r\n", INVALID_PARMS);
        }
    } else {
        for (unsigned i = 0; i < num_lang; i++)
            if (tabs[6 * i] == P.lang[0] && tabs[6 * i + 1] == P.lang[1]) {
                entry = rd16(tabs + 6 * i + 2) | ((uint32_t)rd16(tabs + 6 * i + 4) << 16);
                break;
            }
        if (!entry) msg_exit("Invalid keyboard code specified\r\n", INVALID_PARMS);
    }
    if (read_lang_entry(entry)) msg_exit("Bad or missing Keyboard Definition File\r\n", BAD_KEYB_DEF_FILE);
    uint32_t logic_off = rd16(lent + 4) | ((uint32_t)rd16(lent + 6) << 16);

    unsigned cp = 0;
    if (P.rc2 == 0) {                   /* a code page was given: the layout must have it */
        if (!cp_table(P.cp)) msg_exit("Invalid code page specified\r\n", INVALID_PARMS);
        cp = P.cp;
    }

    /* the designated code pages: CON's (hardware + prepared), else just one */
    uint16_t desig[16];
    unsigned ndesig = 0;
    unsigned sys_cp = 0;
    if (!con) {
        if (P.rc2 == 0) desig[ndesig++] = P.cp;
        else { cp = rd16(lent + 10); sys_cp = cp; desig[ndesig++] = cp; }   /* FIND_FIRST_CP */
    } else {
        uint16_t buf[3 + 16];
        memset(&r, 0, sizeof r);
        r.x.ax = 0xAD03; r.x.cx = sizeof buf; r.x.di = (unsigned)buf;
        if (int2f(&r)) msg_exit("Active code page not available from CON device\r\n", CONSOLE_ERROR);
        unsigned n = buf[0] + buf[2];
        if (n > 16) n = 16;
        for (unsigned i = 0; i < n; i++) desig[ndesig++] = buf[3 + i];
        memset(&r, 0, sizeof r);
        r.x.ax = 0xAD02;
        int none = int2f(&r);
        unsigned invoked = r.x.bx & 0xFFFF;
        if (P.rc2 != 0) {
            if (none) { cp = rd16(lent + 10); sys_cp = cp; }
            else cp = invoked;
        }
        unsigned i;
        for (i = 0; i < ndesig; i++) if (desig[i] == cp) break;
        if (i == ndesig) msg_exit("Code page specified has not been prepared\r\n", CP_NOT_DESIGNATED);
        if (!sys_cp && !none && cp != invoked) out("Code page specified is inconsistent with the selected code page\r\n");
    }

    /* TABLE_BUILD */
    if (!cp_table(cp)) {
        out("Code page requested ("); outn(cp); out(") is not valid for given keyboard code\r\n");
        exit_code = KEYB_TABLE_NOT_LOAD;
        finish();
    }
    unsigned nsect_max = ndesig ? ndesig : 1;
    uint32_t capacity = res ? res->capacity : (uint32_t)(max_logic + 4 + max_com + 4 + (max_spec + 4) * nsect_max + 16);
    tb_size = capacity;
    tb = malloc(tb_size + 16);
    if (!tb) msg_exit("Unable to create KEYB table in resident memory\r\n", MEMORY_OVERFLOW);
    tb_dest = res ? res->tables : (uint8_t *)keyb_res_end;

    struct keybsd sd;
    memset(&sd, 0, sizeof sd);
    /* the state logic */
    uint8_t lh[4];
    if (dos_read_at(logic_off, lh, 4)) msg_exit("Bad or missing Keyboard Definition File\r\n", BAD_KEYB_DEF_FILE);
    unsigned llen = rd16(lh);
    if (llen > tb_size || dos_read_at(logic_off, tb, llen)) msg_exit("Bad or missing Keyboard Definition File\r\n", BAD_KEYB_DEF_FILE);
    tb_used = (llen + 3) & ~3u;
    sd.special = rd16(lh + 2);
    sd.logic = tb_dest;
    int o = load_section(logic_off + llen, max_com);
    if (o == -2) msg_exit("Unable to create KEYB table in resident memory\r\n", MEMORY_OVERFLOW);
    if (o < 0) msg_exit("Bad or missing Keyboard Definition File\r\n", BAD_KEYB_DEF_FILE);
    sd.common = tb_dest + o;
    int invalid = 0;
    for (unsigned i = 0; i < ndesig; i++) {
        if (desig[i] == 0xFFFF) continue;
        uint32_t t = cp_table(desig[i]);
        if (!t) { invalid = 1; continue; }
        int dup = 0;
        for (unsigned k = 0; k < sd.nsect; k++) if (rd16(tb + (sd.sect[k] - tb_dest) + 2) == desig[i]) dup = 1;
        if (dup || sd.nsect >= 8) continue;
        o = load_section(t, max_spec);
        if (o == -2) msg_exit("Unable to create KEYB table in resident memory\r\n", MEMORY_OVERFLOW);
        if (o < 0) msg_exit("Bad or missing Keyboard Definition File\r\n", BAD_KEYB_DEF_FILE);
        sd.sect[sd.nsect++] = tb_dest + o;
        if (desig[i] == cp) sd.active = tb_dest + o;
    }
    if (invalid) out("One or more CON code pages invalid for given keyboard code\r\n");
    dos_close();

    sd.language[0] = P.id_first ? 0 : P.lang[0];     /* as typed (KEYB US: "US", its entry says XX) */
    sd.language[1] = P.id_first ? 0 : P.lang[1];
    sd.invoked_cp = cp;
    sd.invoked_id = P.idvalid ? P.id : 0;
    sd.hot_on = 59; sd.hot_off = 60;               /* F1, F2 */
    sd.layout_id = layout_number(lent);
    sd.used = tb_used;

    if (res) {
        /* KEYB is resident: replace its tables (COPY_INTO_SDA) */
        res->table_ok = 0;
        memcpy(res->tables, tb, tb_used);
        memcpy((uint8_t *)res + SD_COPY_START, (uint8_t *)&sd + SD_COPY_START, SD_COPY_END - SD_COPY_START);
        res->nls1 = res->nls2 = 0;
        res->country = 0xFF;                        /* AD82h BL=FFh: activate the language */
        res->table_ok = 1;
        {
            union REGS a = { 0 }; a.x.ax = 0xAD82; a.x.bx = 0xFF; int2f(&a);
        }
        exit(0);
    }

    /* install: the resident part is keyb_sd + kbres.c's code; the tables go behind it */
    memcpy((uint8_t *)&keyb_sd + SD_COPY_START, (uint8_t *)&sd + SD_COPY_START, SD_COPY_END - SD_COPY_START);
    keyb_sd.keyb_type = G_KB;
    keyb_sd.system_flag = 0x4000 | 0x8000;          /* PC_AT, extended INT 16h */
    keyb_sd.capacity = capacity;
    keyb_sd.tables = (uint8_t *)keyb_res_end;
    keyb_sd.country = 0xFF;
    keyb_sd.table_ok = 1;
    keyb_sd.old15 = (armdos_vect_t)((volatile uint32_t *)0)[0x15];
    keyb_sd.old2f = (armdos_vect_t)((volatile uint32_t *)0)[0x2F];

    /* close the standard handles: a resident program keeps nothing open */
    for (int h = 0; h < 5; h++) { union REGS c = { 0 }; c.x.ax = 0x3E00; c.x.bx = h; intdos(&c, &c); }
    /* free the environment, as DOS 4's KEYB does not - it keeps it; so do we */

    uint32_t end = (uint32_t)keyb_res_end + capacity;
    uint32_t paras = (end - (uint32_t)_armdos_psp + 15) >> 4;
    /* the table copy must not run over the stack we are using */
    uint32_t sp;
    __asm__ volatile("mov %0, sp" : "=r"(sp));
    if ((uint32_t)keyb_res_end + tb_used > sp - 2048) msg_exit("Unable to create KEYB table in resident memory\r\n", MEMORY_OVERFLOW);

    armdos_disable();
    ((volatile uint32_t *)0)[0x15] = (uint32_t)keyb_int15;
    ((volatile uint32_t *)0)[0x2F] = (uint32_t)keyb_int2f;
    armdos_enable();
    keyb_publish();
    keyb_stay(tb, tb_used, paras);
}
