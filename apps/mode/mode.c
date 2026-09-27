/*
 * mode.c - MODE.COM for ARM-DOS 4.00: a re-creation of MS-DOS 4.00's MODE
 * (CMD/MODE, Microsoft, MIT licence) with its messages and output formats
 * (verified against the real MODE 4.00 under DOSBox-X):
 *
 *   MODE                              status of LPT1-LPT3 and CON
 *   MODE [device] /STATUS             status of one device
 *   MODE [display][,shift[,T]]        40 80 BW40 BW80 CO40 CO80 MONO; R|L
 *   MODE [display],rows               25 43 50 (with ANSI.SYS)
 *   MODE CON[:] [COLS=c] [LINES=n]    (4.0) columns / lines, lines need ANSI.SYS
 *   MODE CON[:] RATE=r DELAY=d        (4.0) keyboard typematic rate
 *   MODE COMn[:] baud[,parity[,data[,stop[,retry]]]]
 *   MODE COMn BAUD=b [PARITY=p] [DATA=d] [STOP=s] [RETRY=r]
 *   MODE LPTn[:] [cols][,[lpi][,P]]   or COLS= LINES= RETRY=
 *   MODE LPTn[:]=COMm[:]              redirect a printer to a serial port
 *   MODE CON CP PREPARE=((cp,...) file) | SELECT=cp | REFRESH | /STATUS
 *                                     code pages (with DISPLAY.SYS)
 *
 * The resident portion (printer redirection, retries) is mdres.c.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include "mode.h"

/* ------------------------------------------------------------ output */

static void out(const char *s)
{
    struct armregs r = { 0 };
    r.r0 = 0x4000; r.r1 = 1; r.r2 = strlen(s); r.r3 = (uint32_t)s;
    _armdos_int21(&r);
}

static void outf(const char *fmt, ...)
{
    char b[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    out(b);
}

static int errors;
static void err_sub(const char *msg, const char *sub)
{
    outf("\r\n%s - %s\r\n", msg, sub);
    errors++;
}
static void err(const char *msg)
{
    outf("\r\n%s\r\n", msg);
    errors++;
}

#define CP_NOT_SUPPORTED "Code page operation not supported on this device\r\n"

/* ------------------------------------------------------------ the lexer */

/* The command tail as items. Positional operands are separated by blanks
   or commas (two commas in a row = an omitted operand), KEY=VALUE are
   keywords, /X switches. `text` is the operand as typed, as the parse
   error messages show it (the last one with the blanks that end the line). */
enum { T_WORD, T_EMPTY, T_KEY, T_SWITCH, T_REROUTE };
struct item {
    int kind;
    char up[80];                /* upper case; for T_KEY "KEY" */
    char val[80];               /* T_KEY: the value (upper case) */
    char text[90];
    int comma;                  /* followed by a comma */
};
static struct item it[24];
static int nit;
static char tail[130];

static int isdelim(int c) { return c == ' ' || c == '\t' || c == ';'; }

static void lex(void)
{
    const char *p = tail;
    int prev_comma = 1;         /* a leading comma means an omitted first operand */
    nit = 0;
    while (nit < 24) {
        while (isdelim(*p)) p++;
        if (!*p) break;
        struct item *t = &it[nit];
        memset(t, 0, sizeof *t);
        const char *s = p;
        if (*p == ',') {
            if (prev_comma) {
                t->kind = T_EMPTY;
                t->comma = 1;
                strcpy(t->text, " ");
                nit++;
            } else if (nit) it[nit - 1].comma = 1;
            prev_comma = 1;
            p++;
            continue;
        }
        prev_comma = 0;
        int colon = 0;
        if (*p == '=') {                        /* LPTn[:]=COMm */
            p++;
            while (isdelim(*p)) p++;
            s = p;
            t->kind = T_REROUTE;
            while (*p && !isdelim(*p) && *p != ',') p++;
        } else if (*p == '/') {
            t->kind = T_SWITCH;
            p++;
            while (*p && !isdelim(*p) && *p != ',' && *p != '/' && *p != '=') p++;
        } else {
            t->kind = T_WORD;
            int depth = 0;
            while (*p && (depth || (!isdelim(*p) && *p != ',' && *p != '/' && *p != ':' && *p != '='))) {
                if (*p == '(') depth++;
                if (*p == ')') depth--;
                p++;
            }
            if (*p == '=' ) {
                /* KEY=VALUE, or DEVICE=COMm (a reroute written without the colon) */
                const char *k = p++;
                while (isdelim(*p)) p++;
                const char *v = p;
                depth = 0;
                while (*p && (depth || (!isdelim(*p) && *p != ','))) {
                    if (*p == '(') depth++;
                    if (*p == ')') depth--;
                    p++;
                }
                t->kind = T_KEY;
                int n = k - s < 79 ? k - s : 79;
                memcpy(t->up, s, n);
                n = p - v < 79 ? p - v : 79;
                memcpy(t->val, v, n);
            } else if (*p == ':') {
                p++;                            /* COM1: - the colon ends the device name */
                colon = 1;
            }
        }
        if (t->kind != T_KEY) {
            int n = p - s < 79 ? p - s : 79;
            memcpy(t->up, s, n);
            if (t->kind == T_WORD && n && t->up[n - 1] == ':') t->up[n - 1] = 0;
        }
        /* the text for error messages: to the end of the line for the last
           item; a device name without its colon; KEY= alone as typed */
        const char *e = p;
        const char *q = p;
        while (isdelim(*q)) q++;
        if (!*q) e = q;
        if (colon) e = p - 1;
        if (t->kind == T_KEY && !t->val[0]) e = strchr(s, '=') + 1;
        prev_comma = colon;                     /* "LPT1:,,P": the colon separates too */
        int n = e - s < 89 ? e - s : 89;
        memcpy(t->text, s, n);
        t->text[n] = 0;
        for (char *c = t->up; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
        for (char *c = t->val; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
        nit++;
    }
}

/* the text of the item after the device name: for "COM1:96" the parser
   sees "96" separated by the colon */
static int is_num(const char *s) { if (!*s) return 0; for (; *s; s++) if (*s < '0' || *s > '9') return 0; return 1; }

/* ------------------------------------------------------------ hardware */

static int dos(struct armregs *r) { return _armdos_int21(r); }

static int ansi_installed(void)
{
    struct armregs r = { 0 };
    r.r0 = 0x1A00;
    _armdos_int2f(&r);
    return (r.r0 & 0xFF) == 0xFF;
}

static int com_ports(void) { return (*(volatile uint16_t *)0x410 >> 9) & 7; }
static int lpt_ports(void) { return (*(volatile uint16_t *)0x410 >> 14) & 3; }
static int com_exists(int n)
{
    return ((volatile uint16_t *)0x400)[n] != 0 || n < com_ports();
}
static int lpt_exists(int n)
{
    return ((volatile uint16_t *)0x408)[n] != 0 || n < lpt_ports();
}

/* the IOCTL data block of 440Ch CX=037Fh/035Fh */
struct dispinfo {
    uint8_t level, res1;
    uint16_t len, flags;
    uint8_t mode, res2;
    uint16_t colors, width, length, cols, rows;
} __attribute__((packed));

static int con_ioctl(int minor, struct dispinfo *d)
{
    struct armregs r = { 0 };
    r.r0 = 0x3D02; r.r3 = (uint32_t)"CON";
    if (dos(&r)) return -1;
    int h = r.r0 & 0xFFFF;
    memset(&r, 0, sizeof r);
    r.r0 = 0x440C; r.r1 = h; r.r2 = 0x0300 | minor; r.r3 = (uint32_t)d;
    int cf = dos(&r);
    int e = cf ? (int)(r.r0 & 0xFFFF) : 0;
    memset(&r, 0, sizeof r);
    r.r0 = 0x3E00; r.r1 = h;
    dos(&r);
    return e;
}

static void int10(int ax, int bx, int cx, int dx)
{
    struct armregs r = { 0 };
    r.r0 = ax; r.r1 = bx; r.r2 = cx; r.r3 = dx;
    _armdos_int10(&r);
}

/* GET_VIDEO_INFO (MODEVID.ASM): which adapters are there. INT 10h AH=1Bh (VGA
   BIOS) gives the display combination codes; without it, an EGA answers the
   AH=12h BL=10h call; otherwise the buffers at B000h (mono) and B800h (colour)
   are probed by writing to them (CHECK_BUFF). Returns VC_COLOR | VC_MONO. */
#define VC_COLOR 1
#define VC_MONO  2
static int check_buff(uint32_t a)
{
    volatile uint8_t *p = (volatile uint8_t *)a;
    uint8_t old = *p;
    *p = 0x55;
    int ok = *p == 0x55;
    *p = old;
    return ok;
}
static int dcc_caps(int c)
{
    if (c == 0x0B || c == 6 || c == 8 || c == 0x0A || c == 0x0C || c == 2 || c == 4) return VC_COLOR;
    if (c == 1 || c == 5 || c == 7) return VC_MONO;
    return 0;
}
static int video_caps(void)
{
    static uint8_t info[64];
    struct armregs r = { 0 };
    r.r0 = 0x1B00; r.r1 = 0; r.r5 = (uint32_t)info;
    _armdos_int10(&r);
    if ((r.r0 & 0xFF) == 0x1B) {
        if (info[0x26] == 0 && (info[0x2D] & 1)) return VC_COLOR | VC_MONO;     /* all modes on all monitors */
        return dcc_caps(info[0x25]) | dcc_caps(info[0x26]);
    }
    if (*(volatile uint16_t *)0xC0000 == 0xAA55) {
        memset(&r, 0, sizeof r);
        r.r0 = 0x1200; r.r1 = 0x10;
        _armdos_int10(&r);
        if ((r.r1 & 0xFF) != 0x10)
            return ((r.r1 >> 8) & 0xFF) == 0 ? (VC_COLOR | (check_buff(0xB0000) ? VC_MONO : 0))
                                             : (VC_MONO | (check_buff(0xB8000) ? VC_COLOR : 0));
    }
    return (check_buff(0xB0000) ? VC_MONO : 0) | (check_buff(0xB8000) ? VC_COLOR : 0);
}

static int cur_mode(void)
{
    struct armregs r = { 0 };
    r.r0 = 0x0F00;
    _armdos_int10(&r);
    return r.r0 & 0x7F;
}

/* ------------------------------------------------------ resident portion */

static struct moderes *res;             /* the resident data, if loaded */
static int res_new;                     /* we are going resident at the end */

static struct moderes *find_resident(void)
{
    struct armregs r = { 0 };
    r.r0 = 0xDD00;
    _armdos_intr(0x17, &r);
    if ((r.r0 & 0xFFFF) == 0x4D4F && !memcmp((void *)r.r1, "MODE", 4)) return (struct moderes *)r.r1;
    return 0;
}

static struct moderes *need_resident(void)
{
    if (res) return res;
    mode_old14 = armdos_getvect(0x14);
    mode_old17 = armdos_getvect(0x17);
    armdos_setvect(0x14, mode_int14);
    armdos_setvect(0x17, mode_int17);
    res = &mode_res;
    res_new = 1;
    out("\r\nResident portion of MODE loaded\r\n");
    return res;
}

static void go_resident(void)
{
    struct armregs r = { 0 };
    /* close the standard handles: a resident program keeps nothing open
       (and a redirected output file is complete when the shell closes it) */
    for (int h = 0; h < 5; h++) {
        r.r0 = 0x3E00; r.r1 = h;
        _armdos_int21(&r);
        memset(&r, 0, sizeof r);
    }
    r.r0 = 0x4900; r.r8 = _armdos_psp->envseg;
    dos(&r);
    _armdos_psp->envseg = 0;
    memset(&r, 0, sizeof r);
    r.r0 = 0x3100;
    r.r3 = ((uint32_t)mode_res_end - (uint32_t)_armdos_psp + 15) >> 4;
    dos(&r);
}

/* ------------------------------------------------------------ status */

static void status_header(const char *dev)
{
    outf("\r\nStatus for device %s:\r\n------------------%s", dev, strlen(dev) == 3 ? "----\r\n" : "-----\r\n");
}

static void reroute_status(int n)
{
    if (res && res->reroute[n]) outf("LPT%d: rerouted to COM%d:\r\n", n + 1, res->reroute[n]);
    else outf("LPT%d: not rerouted\r\n", n + 1);
}

static const char *retry_name(int c)
{
    switch (c) {
    case 'E': return "E";
    case 'B': return "B";
    case 'R': return "R";
    default: return "NONE";
    }
}

static void con_codepage(const char *dev, int first);
static void status_con(void)
{
    status_header("CON");
    if (ansi_installed()) {
        struct dispinfo d;
        memset(&d, 0, sizeof d);
        d.len = 14;
        int e = con_ioctl(0x7F, &d);
        if (!e && d.mode == 1) {
            outf("COLUMNS=%s\r\n", d.cols == 80 ? "80" : "40");
            outf("LINES=%u\r\n", d.rows);
        } else {
            out("COLUMNS=NONE\r\nLINES=NONE\r\n");
        }
    }
    con_codepage("CON", nit);           /* the code page status, as MODE CON CP /STATUS */
}

static void status_lpt(int n)
{
    char dev[6];
    snprintf(dev, sizeof dev, "LPT%d", n + 1);
    status_header(dev);
    reroute_status(n);
    if (lpt_exists(n)) {
        outf("RETRY=%s\r\n", retry_name(res ? res->lptretry[n] : 0));
        out(CP_NOT_SUPPORTED);
    }
}

static void status_com(int n)
{
    char dev[6];
    snprintf(dev, sizeof dev, "COM%d", n + 1);
    if (!com_exists(n)) { err_sub("Illegal device name", dev); return; }
    status_header(dev);
    outf("RETRY=%s\r\n", retry_name(res ? res->comretry[n] : 0));
}

static void status_all(void)
{
    for (int i = 0; i < 3; i++) status_lpt(i);
    status_con();
}

/* ------------------------------------------------------- the display */

static void set_video(int equip, int mode)
{
    volatile uint16_t *eq = (volatile uint16_t *)0x410;
    *eq = (*eq & ~0x30) | equip;
    int10(mode, 0, 0, 0);
    int10(0x0200, 0, 0, 0);
    /* MODE 4.00 then sets the CGA cursor shape 0607h, which VGA BIOSes
       scale to the character cell; the ARM-PC BIOS does not scale, and its
       mode set already gives the right cursor, so it is left alone */
}

static void shift_screen(const char *dir)
{
    outf("\r\n      Unable to shift screen %s\r\n", dir);
    errors++;
}

static int valid_rows(const char *s) { return !strcmp(s, "25") || !strcmp(s, "43") || !strcmp(s, "50"); }

static int set_display_info(int cols, int rows, const char *what)
{
    struct dispinfo d;
    memset(&d, 0, sizeof d);
    d.len = 14;
    if (con_ioctl(0x7F, &d)) return -1;
    d.mode = 1;
    d.len = 14;
    if (cols) d.cols = cols;
    if (rows) d.rows = rows;
    if (d.mode == 1) { d.width = 0xFFFF; d.length = 0xFFFF; d.colors = 16; }
    int e = con_ioctl(0x5F, &d);
    if (e) {
        /* MODE 4.00 shows these for extended errors 29 and 31 (the driver's
           codes 10 and 12); this kernel reports other codes for driver errors */
        if (e == 31) err("Required font not loaded");
        else err_sub("Function not supported on this computer", what);
        return -1;
    }
    return 0;
}

static void display(void)
{
    const char *d = it[0].kind == T_EMPTY ? "" : it[0].up;
    int m = cur_mode();
    int ansi = ansi_installed();
    int color_now = !(m == 0 || m == 2 || m == 5 || m == 6 || m == 7);
    int caps = (*d && strcmp(d, "R") && strcmp(d, "L")) ? video_caps() : VC_COLOR;
    if (!strcmp(d, "MONO")) {
        if (!(caps & VC_MONO)) { err_sub("Function not supported on this computer", it[0].up); return; }
        set_video(0x30, 7);
        int10(0x0100, 0, 0x0B0C, 0);          /* SET_CURSOR_TYPE: B,C for mono */
        return;
    }
    if (*d && !(caps & VC_COLOR) && (!strcmp(d, "BW40") || !strcmp(d, "BW80") || !strcmp(d, "CO40") ||
                                     !strcmp(d, "CO80") || !strcmp(d, "40") || !strcmp(d, "80"))) {
        err_sub("Function not supported on this computer", it[0].up);
        return;
    }
    if (!strcmp(d, "BW40")) set_video(0x10, 0);
    else if (!strcmp(d, "BW80")) set_video(0x20, 2);
    else if (!strcmp(d, "CO40")) set_video(0x10, 1);
    else if (!strcmp(d, "CO80")) set_video(0x20, 3);
    else if (!strcmp(d, "40") || !strcmp(d, "80")) {
        int cols = atoi(d);
        if (ansi) {
            if (set_display_info(cols, 0, it[0].text)) return;
        } else if (cols == 40) set_video(0x10, color_now ? 1 : 0);
        else set_video(0x20, color_now ? 3 : 2);
    }
    if (errors || nit < 2) return;
    /* second operand: shift direction or lines */
    const struct item *t = &it[1];
    if (t->kind == T_EMPTY) return;
    if (!strcmp(t->up, "R")) shift_screen("right");
    else if (!strcmp(t->up, "L")) shift_screen("left");
    else if (valid_rows(t->up)) {
        if (!ansi) err("ANSI.SYS must be installed to perform requested function");
        else set_display_info(0, atoi(t->up), t->text);
    } else err_sub("Invalid parameter", t->text);
}

/* ------------------------------------------------------------ CON */

static void con_features(int first)
{
    int cols = 0, lines = 0, rate = 0, delay = 0;
    for (int i = first; i < nit; i++) {
        struct item *t = &it[i];
        if (t->kind == T_SWITCH) {
            if (strcmp(t->up, "/STATUS") && strcmp(t->up, "/STA") && strcmp(t->up, "/STAT")) {
                err_sub("Invalid switch", t->text);
                return;
            }
            continue;
        }
        if (t->kind != T_KEY) { err_sub("Invalid parameter", t->text); return; }
        if (!t->val[0]) { err_sub("Parameter format not correct", t->text); return; }
        int v = is_num(t->val) ? atoi(t->val) : -1;
        if (!strcmp(t->up, "COLS") || !strcmp(t->up, "COLUMNS")) {
            if (v != 40 && v != 80) { err_sub("Invalid parameter", t->text); return; }
            cols = v;
        } else if (!strcmp(t->up, "LINES")) {
            if (v != 25 && v != 43 && v != 50) { err_sub("Invalid parameter", t->text); return; }
            lines = v;
        } else if (!strcmp(t->up, "RATE")) {
            if (v < 1 || v > 32) { err_sub("Invalid parameter", t->text); return; }
            rate = v;
        } else if (!strcmp(t->up, "DELAY") || !strcmp(t->up, "DEL")) {
            if (v < 1 || v > 4) { err_sub("Invalid parameter", t->text); return; }
            delay = v;
        } else { err_sub("Invalid parameter", t->text); return; }
    }
    if ((rate != 0) != (delay != 0)) { err("RATE and DELAY must be specified together"); return; }
    if (cols || lines) {
        if (ansi_installed()) {
            char what[40];
            snprintf(what, sizeof what, lines ? "LINES=%d" : "COLS=%d", lines ? lines : cols);
            if (set_display_info(cols, lines, what)) return;
        } else if (lines) {
            err("ANSI.SYS must be installed to perform requested function");
            return;
        } else {
            int m = cur_mode();
            int color_now = !(m == 0 || m == 2 || m == 5 || m == 6 || m == 7);
            if (cols == 40) set_video(0x10, color_now ? 1 : 0);
            else set_video(0x20, color_now ? 3 : 2);
        }
    }
    if (rate) {
        struct armregs r = { 0 };
        r.r0 = 0x0305;
        r.r1 = ((delay - 1) << 8) | (32 - rate);
        _armdos_int16(&r);
    }
}

/* ------------------------------------------------------------ COMn */

static const char *const bauds[] = { "110", "150", "300", "600", "1200", "2400", "4800", "9600", "19200" };
static const char *const baud_short[] = { "11", "15", "30", "60", "12", "24", "48", "96", "19" };

static int find_baud(const char *s)
{
    for (int i = 0; i < 9; i++)
        if (!strcmp(s, bauds[i]) || !strcmp(s, baud_short[i])) return i;
    if (!strcmp(s, "19.2") || !strcmp(s, "19.2K")) return 8;
    return -1;
}

static int retry_letter(const char *s)
{
    if (!strcmp(s, "P") || !strcmp(s, "B")) return 'B';
    if (!strcmp(s, "E")) return 'E';
    if (!strcmp(s, "R")) return 'R';
    if (!strcmp(s, "NONE") || !strcmp(s, "N")) return 0;
    return -1;
}

static void com_port(int n, int first)
{
    const struct item *baud = 0, *par = 0, *data = 0, *stop = 0, *retry = 0;
    int keyword = first < nit && it[first].kind == T_KEY;
    if (keyword) {
        for (int i = first; i < nit; i++) {
            const struct item *t = &it[i];
            if (t->kind != T_KEY) { err_sub("Invalid parameter", t->text); return; }
            if (!t->val[0]) { err_sub("Parameter format not correct", t->text); return; }
            if (!strcmp(t->up, "BAUD")) baud = t;
            else if (!strcmp(t->up, "PARITY")) par = t;
            else if (!strcmp(t->up, "DATA")) data = t;
            else if (!strcmp(t->up, "STOP")) stop = t;
            else if (!strcmp(t->up, "RETRY")) retry = t;
            else { err_sub("Invalid parameter", t->text); return; }
        }
        if (!baud) { err("Baud rate required"); return; }
    } else {
        const struct item **slot[5] = { &baud, &par, &data, &stop, &retry };
        int k = 0;
        for (int i = first; i < nit; i++) {
            const struct item *t = &it[i];
            if (t->kind == T_KEY || t->kind == T_SWITCH || t->kind == T_REROUTE) { err_sub("Invalid parameter", t->text); return; }
            if (k >= 5) { err("Invalid number of parameters"); return; }
            if (t->kind != T_EMPTY) *slot[k] = t;
            else if (k == 0) { err_sub("Invalid parameter", t->text); return; }
            k++;
        }
    }
    const char *v;
    v = baud->kind == T_KEY ? baud->val : baud->up;
    int b = find_baud(v);
    if (b < 0) { err_sub("Invalid parameter", baud->text); return; }
    char pc = 'e';
    if (par) {
        v = par->kind == T_KEY ? par->val : par->up;
        if (!strcmp(v, "N") || !strcmp(v, "NONE")) pc = 'n';
        else if (!strcmp(v, "O") || !strcmp(v, "ODD")) pc = 'o';
        else if (!strcmp(v, "E") || !strcmp(v, "EVEN")) pc = 'e';
        else if (!strcmp(v, "M") || !strcmp(v, "MARK") || !strcmp(v, "S") || !strcmp(v, "SPACE")) {
            err_sub("Function not supported on this computer", par->text); return;
        } else { err_sub("Invalid parameter", par->text); return; }
    }
    char dc = '7';
    if (data) {
        v = data->kind == T_KEY ? data->val : data->up;
        if (!strcmp(v, "7") || !strcmp(v, "8")) dc = v[0];
        else if (!strcmp(v, "5") || !strcmp(v, "6")) { err_sub("Function not supported on this computer", data->text); return; }
        else { err_sub("Invalid parameter", data->text); return; }
    }
    const char *sc = b == 0 ? "2" : "1";
    if (stop) {
        v = stop->kind == T_KEY ? stop->val : stop->up;
        if (!strcmp(v, "1")) sc = "1";
        else if (!strcmp(v, "2")) sc = "2";
        else if (!strcmp(v, "1.5")) { err_sub("Function not supported on this computer", stop->text); return; }
        else { err_sub("Invalid parameter", stop->text); return; }
    }
    int rl = -2;                                /* not given */
    if (retry) {
        v = retry->kind == T_KEY ? retry->val : retry->up;
        rl = retry_letter(v);
        if (rl < 0) { err_sub("Invalid parameter", retry->text); return; }
    }
    if (b == 8) { err_sub("Function not supported on this computer", baud->text); return; }
    char dev[6];
    snprintf(dev, sizeof dev, "COM%d", n + 1);
    if (!com_exists(n)) { err_sub("Illegal device name", dev); return; }

    /* INT 14h AH=00h: baud << 5 | parity << 3 | stop << 2 | data - 5 */
    int al = (b << 5) | (pc == 'n' ? 0 : pc == 'o' ? 0x08 : 0x18) | (sc[0] == '2' ? 4 : 0) | (dc == '8' ? 3 : 2);
    struct armregs r = { 0 };
    r.r0 = al; r.r3 = n;
    _armdos_intr(0x14, &r);

    /* the retry setting (MODECOM.ASM SETTO) */
    char shown = '-';
    if (rl > 0) {
        need_resident()->comretry[n] = rl;
        const char *rv = retry->kind == T_KEY ? retry->val : retry->up;
        shown = rv[0] == 'P' ? 'p' : rl + 32;
    } else if (res) {
        if (!keyword || rl == 0) res->comretry[n] = 0;
        else if (res->comretry[n]) shown = res->comretry[n] + 32;
    }
    outf("\r\nCOM%d: %s,%c,%c,%s,%c\r\n", n + 1, bauds[b], pc, dc, sc, shown);
}

/* ------------------------------------------------------------ LPTn */

static int print_char(int n, int c)
{
    struct armregs r = { 0 };
    r.r0 = c & 0xFF; r.r3 = n;
    _armdos_intr(0x17, &r);
    return (r.r0 >> 8) & 0x29;                  /* time-out, I/O error, paper out */
}

static void lpt_port(int n, int first)
{
    const struct item *cols = 0, *lpi = 0, *retry = 0;
    int keyword = first < nit && it[first].kind == T_KEY;
    if (keyword) {
        for (int i = first; i < nit; i++) {
            const struct item *t = &it[i];
            if (t->kind != T_KEY) { err_sub("Invalid parameter", t->text); return; }
            if (!t->val[0]) { err_sub("Parameter format not correct", t->text); return; }
            if (!strcmp(t->up, "COLS")) cols = t;
            else if (!strcmp(t->up, "LINES")) lpi = t;
            else if (!strcmp(t->up, "RETRY")) retry = t;
            else { err_sub("Invalid parameter", t->text); return; }
        }
    } else {
        const struct item **slot[3] = { &cols, &lpi, &retry };
        int k = 0;
        for (int i = first; i < nit; i++) {
            const struct item *t = &it[i];
            if (t->kind == T_KEY || t->kind == T_SWITCH) { err_sub("Invalid parameter", t->text); return; }
            if (k >= 3) { err("Invalid number of parameters"); return; }
            if (t->kind != T_EMPTY) *slot[k] = t;
            k++;
        }
    }
    int c = 0, l = 0, rl = 0;
    if (cols) {
        const char *v = cols->kind == T_KEY ? cols->val : cols->up;
        if (strcmp(v, "80") && strcmp(v, "132")) { err_sub("Invalid parameter", cols->text); return; }
        c = atoi(v);
    }
    if (lpi) {
        const char *v = lpi->kind == T_KEY ? lpi->val : lpi->up;
        if (strcmp(v, "6") && strcmp(v, "8")) { err_sub("Invalid parameter", lpi->text); return; }
        l = atoi(v);
    }
    if (retry) {
        const char *v = retry->kind == T_KEY ? retry->val : retry->up;
        rl = keyword ? retry_letter(v) : (!strcmp(v, "P") ? 'B' : -1);
        if (rl < 0) { err_sub("Invalid parameter", retry->text); return; }
    }
    /* setting a printer ends its redirection */
    if (res) res->reroute[n] = 0;
    outf("\r\nLPT%d: not rerouted\r\n", n + 1);
    if (!lpt_exists(n)) return;
    if (rl) need_resident();
    if (c || l) {
        int e = 0;
        if (c) e |= print_char(n, c == 132 ? 0x0F : 0x12);
        if (l) { e |= print_char(n, 0x1B); e |= print_char(n, l == 8 ? '0' : '2'); }
        if (e) out("\r\nPrinter error\r\n");
        else {
            if (c) outf("\r\nLPT%d: set for %d\r\n", n + 1, c);
            if (l) out("\r\nPrinter lines per inch set\r\n");
        }
    }
    if (res) res->lptretry[n] = rl;
    outf("\r\n%s retry on parallel printer time-out\r\n", rl ? "Infinite" : "No");
}

static void reroute(int n, const struct item *t)
{
    const char *v = t->up;
    char com[8];
    strncpy(com, v, 7);
    com[7] = 0;
    int l = strlen(com);
    if (l && com[l - 1] == ':') com[--l] = 0;
    if (l != 4 || strncmp(com, "COM", 3) || com[3] < '1' || com[3] > '4') { err_sub("Invalid parameter", t->text); return; }
    int m = com[3] - '1';
    if (!com_exists(m)) { err_sub("Illegal device name", com); return; }
    need_resident()->reroute[n] = m + 1;
    outf("\r\nLPT%d: rerouted to COM%d:\r\n", n + 1, m + 1);
}

/* ------------------------------------------------------------ code pages */

/* MODE device CP ... (MODECP.ASM): through the device's generic IOCTL
   (category 3: 4Ch/4Dh prepare with the font file written in between by
   IOCTL write, 4Ah select, 6Ah query selected, 6Bh query the lists), as
   DISPLAY.SYS provides for CON. The device's error code comes back as the
   extended error (driver status + 13h): 16h/1 = the device has no code page
   functions, 1Ah (7) = code page not prepared, 1Bh (8) = the keyboard (KEYB)
   refused it, 1Dh (0Ah) = device error. */
#define E_NOCP(e)   ((e) == 0x16 || (e) == 1)
#define E_NOTPREP   0x1A
#define E_KEYB      0x1B
#define E_DEVERR    0x1D
#define E_GENFAIL   0x1F

static int cp_noerror;

static int dev_generic(int h, int minor, void *data)
{
    struct armregs r = { 0 };
    r.r0 = 0x440C; r.r1 = h; r.r2 = 0x0300 | minor; r.r3 = (uint32_t)data;
    return dos(&r) ? (int)(r.r0 & 0xFFFF) : 0;
}
static void cp_msg(const char *m) { cp_noerror = 0; out(m); }

static void cp_query_list(int h, const char *dev);
static void cp_status(int h, const char *dev)
{
    uint16_t pk[2] = { 2, 0 };
    int e = dev_generic(h, 0x6A, pk);
    if (!e) {
        outf("\r\nActive code page for device %s is %u\r\n", dev, pk[1]);
        cp_query_list(h, dev);
    } else if (e == E_NOTPREP) {
        cp_msg("No code page has been selected\r\n");
        cp_noerror = 1;
        cp_query_list(h, dev);
    } else if (E_NOCP(e)) cp_msg(CP_NOT_SUPPORTED);
    else cp_msg("Device error during status\r\n");
}

static void cp_query_list(int h, const char *dev)
{
    uint16_t pk[2 + 2 * 12 + 2];
    memset(pk, 0xFF, sizeof pk);
    pk[0] = sizeof pk - 2;
    int e = dev_generic(h, 0x6B, pk);
    if (e) {
        if (E_NOCP(e)) cp_msg(CP_NOT_SUPPORTED);
        else cp_msg("Device error during status\r\n");
        return;
    }
    int preped = 0;
    const uint16_t *p = pk + 1;
    for (int list = 0; list < 2; list++) {
        unsigned n = *p++;
        if (n > 12) n = 12;
        if (!n) continue;
        outf("%s code pages:\r\n", list ? "Prepared" : "Hardware");
        for (unsigned i = 0; i < n; i++, p++) {
            if ((*p & 0xFF) == 0xFF) out("  code page not prepared\r\n");
            else { preped++; outf("  code page %u\r\n", *p); }
        }
    }
    if (!preped) outf("\r\nDevice %s not prepared\r\n", dev);
    if (cp_noerror) out("\r\nMODE status code page function completed\r\n");
}

static void cp_select(int h, unsigned cp)
{
    uint16_t pk[2] = { 2, cp };
    int e = dev_generic(h, 0x4A, pk);
    if (!e) { if (cp_noerror) out("\r\nMODE select code page function completed\r\n"); return; }
    if (e == E_NOTPREP) cp_msg("Code page not prepared\r\n");
    else if (e == E_KEYB) cp_msg("\r\nCurrent keyboard does not support this code page\r\n");
    else cp_msg("Device error during select\r\n");
}

/* DES_START: the prepare packet (none: refresh) */
static int cp_des_start(int h, const uint16_t *cps, int n, const char *what)
{
    uint16_t pk[2 + 12];
    pk[0] = 2 + 2 * n; pk[1] = n;
    for (int i = 0; i < n; i++) pk[2 + i] = cps[i];
    int e = dev_generic(h, 0x4C, pk);
    if (!e) return 0;
    if (e == E_DEVERR) outf("Device error during %s\r\n", what);
    else if (n == 0 && e == E_GENFAIL) out("\r\nUnable to perform refresh operation\r\n");
    else if (E_NOCP(e)) out(CP_NOT_SUPPORTED);
    else out("\r\nCurrent keyboard does not support this code page\r\n");
    cp_noerror = 0;
    return -1;
}

static void cp_binary(int h)
{
    struct armregs r = { 0 };
    r.r0 = 0x4400; r.r1 = h;
    if (dos(&r)) return;
    int info = r.r3 & 0xFF;
    memset(&r, 0, sizeof r);
    r.r0 = 0x4401; r.r1 = h; r.r3 = info | 0x20;
    dos(&r);
}

static void cp_des_end(int h, const char *what)
{
    uint16_t pk[2] = { 2, 0 };
    if (dev_generic(h, 0x4D, pk)) { cp_msg(what[0] == 'p' ? "Device error during prepare\r\n" : "Device error during refresh\r\n"); return; }
    if (cp_noerror) outf("\r\nMODE %s code page function completed\r\n", what);
}

static void cp_prepare(int h, const uint16_t *cps, int n, const char *file)
{
    struct armregs r = { 0 };
    r.r0 = 0x3D00; r.r3 = (uint32_t)file;
    if (dos(&r)) { cp_msg("\r\nFailure to access code page font file\r\n"); return; }
    int fh = r.r0 & 0xFFFF;
    if (!cp_des_start(h, cps, n, "prepare")) {
        cp_binary(h);
        static uint8_t buf[4096];
        for (;;) {
            memset(&r, 0, sizeof r);
            r.r0 = 0x3F00; r.r1 = fh; r.r2 = sizeof buf; r.r3 = (uint32_t)buf;
            if (dos(&r)) { cp_msg("\r\nError during read of font file\r\n"); break; }
            unsigned got = r.r0 & 0xFFFF;
            if (!got) break;
            memset(&r, 0, sizeof r);
            r.r0 = 0x4403; r.r1 = h; r.r2 = got; r.r3 = (uint32_t)buf;
            if (dos(&r)) {
                int e = r.r0 & 0xFFFF;
                if (e == 0x08 + 0x13) cp_msg("\r\nDevice or code page missing from font file\r\n");
                else if (e == E_DEVERR) cp_msg("Device error during write of font file to device\r\n");
                else cp_msg("\r\nFont file contents invalid\r\n");
                break;
            }
        }
        cp_des_end(h, "prepare");
    }
    memset(&r, 0, sizeof r);
    r.r0 = 0x3E00; r.r1 = fh;
    dos(&r);
}

static void cp_refresh(int h)
{
    if (cp_des_start(h, 0, 0, "refresh")) return;
    cp_binary(h);
    cp_des_end(h, "refresh");
}

/* PREPARE=((cp,cp,...) [d:][path]file): -1 for an omitted code page */
static int parse_prepare(const char *v, uint16_t *cps, int *n, char *file)
{
    const char *p = v;
    *n = 0;
    if (*p++ != '(') return -1;
    while (*p == ' ') p++;
    if (*p++ != '(') return -1;
    for (;;) {
        while (*p == ' ') p++;
        if (*n >= 12) return -1;
        if (*p >= '0' && *p <= '9') {
            unsigned c = 0;
            while (*p >= '0' && *p <= '9') c = c * 10 + (*p++ - '0');
            cps[(*n)++] = c;
        } else cps[(*n)++] = 0xFFFF;
        while (*p == ' ') p++;
        if (*p == ',') { p++; continue; }
        if (*p == ')') { p++; break; }
        return -1;
    }
    while (*p == ' ') p++;
    int k = 0;
    while (*p && *p != ')' && *p != ' ' && k < 79) file[k++] = *p++;
    file[k] = 0;
    while (*p == ' ') p++;
    if (*p++ != ')' || *p || !k) return -1;
    return 0;
}

static void con_codepage(const char *dev, int first)
{
    /* what is asked for */
    int fn = 's';                               /* status */
    const struct item *arg = 0;
    for (int i = first; i < nit; i++) {
        const struct item *t = &it[i];
        if (t->kind == T_SWITCH && (!strcmp(t->up, "/STATUS") || !strcmp(t->up, "/STA") || !strcmp(t->up, "/STAT"))) continue;
        if (t->kind == T_KEY && (!strcmp(t->up, "SEL") || !strcmp(t->up, "SELECT"))) { fn = 'S'; arg = t; continue; }
        if (t->kind == T_KEY && (!strcmp(t->up, "PREP") || !strcmp(t->up, "PREPARE"))) { fn = 'P'; arg = t; continue; }
        if (t->kind == T_WORD && (!strcmp(t->up, "REF") || !strcmp(t->up, "REFRESH"))) { fn = 'R'; continue; }
        err_sub("Invalid parameter", t->text);
        return;
    }
    uint16_t cps[12];
    int n = 0;
    char file[80];
    unsigned sel = 0;
    if (fn == 'S') {
        if (!is_num(arg->val)) { err_sub("Invalid parameter", arg->text); return; }
        sel = atoi(arg->val);
    }
    if (fn == 'P' && parse_prepare(arg->val, cps, &n, file)) { err_sub("Invalid parameter", arg->text); return; }

    struct armregs r = { 0 };
    r.r0 = 0x3D02; r.r3 = (uint32_t)dev;
    if (dos(&r)) { outf("\r\nFailure to access device: %s\r\n", dev); return; }
    int h = r.r0 & 0xFFFF;
    memset(&r, 0, sizeof r);
    r.r0 = 0x4400; r.r1 = h;
    cp_noerror = 1;
    if (dos(&r) || !(r.r3 & 0x80)) out(CP_NOT_SUPPORTED);
    else if (fn == 'S') cp_select(h, sel);
    else if (fn == 'P') cp_prepare(h, cps, n, file);
    else if (fn == 'R') cp_refresh(h);
    else cp_status(h, dev);
    memset(&r, 0, sizeof r);
    r.r0 = 0x3E00; r.r1 = h;
    dos(&r);
}

static void codepage(const char *dev, int first)
{
    int is_con = !strcmp(dev, "CON");
    if (is_con) { con_codepage(dev, first); return; }
    for (int i = first; i < nit; i++) {
        const struct item *t = &it[i];
        if (t->kind == T_KEY) {
            const char *op = !strcmp(t->up, "SEL") || !strcmp(t->up, "SELECT") ? "select" :
                             !strcmp(t->up, "PREP") || !strcmp(t->up, "PREPARE") ? "prepare" : 0;
            if (op) { outf("Device error during %s\r\n", op); return; }
        }
        if (t->kind == T_WORD && (!strcmp(t->up, "REF") || !strcmp(t->up, "REFRESH"))) {
            out("Device error during refresh\r\n");
            return;
        }
    }
    out(CP_NOT_SUPPORTED);
}

/* ------------------------------------------------------------ main */

static int devnum(const char *s, const char *pre)
{
    size_t n = strlen(pre);
    if (strncmp(s, pre, n) || !s[n] || s[n + 1]) return -1;
    return s[n] >= '1' && s[n] <= '9' ? s[n] - '1' : -1;
}

static int only_status(int first)
{
    for (int i = first; i < nit; i++)
        if (!(it[i].kind == T_SWITCH && (!strcmp(it[i].up, "/STATUS") || !strcmp(it[i].up, "/STA") ||
                                         !strcmp(it[i].up, "/STAT")))) return 0;
    return 1;
}

int main(void)
{
    /* the command tail, as typed */
    const uint8_t *t = _armdos_psp->cmdtail;
    int n = t[0] < 127 ? t[0] : 127;
    memcpy(tail, t + 1, n);
    tail[n] = 0;
    char *cr = strchr(tail, '\r');
    if (cr) *cr = 0;
    lex();
    res = find_resident();

    if (nit == 0) { status_all(); goto done; }
    struct item *d = &it[0];
    if (d->kind == T_SWITCH) {
        if (only_status(0)) status_all();
        else err_sub("Invalid switch", d->text);
        goto done;
    }
    int second_cp = nit > 1 && it[1].kind == T_WORD && (!strcmp(it[1].up, "CP") || !strcmp(it[1].up, "CODEPAGE"));
    int k;
    if (d->kind == T_WORD && !strcmp(d->up, "CON")) {
        if (second_cp) codepage("CON", 2);
        else if (only_status(1)) status_con();
        else con_features(1);
    } else if (d->kind == T_WORD && (k = devnum(d->up, "COM")) >= 0) {
        if (k > 3) { err_sub("Invalid parameter", d->text); goto done; }
        if (only_status(1)) status_com(k);
        else com_port(k, 1);
    } else if ((d->kind == T_WORD && ((k = devnum(d->up, "LPT")) >= 0)) || (d->kind == T_WORD && !strcmp(d->up, "PRN"))) {
        int prn = !strcmp(d->up, "PRN");
        if (prn) k = 0;
        if (k > 2) { err_sub("Invalid parameter", d->text); goto done; }
        if (second_cp) codepage(d->up, 2);
        else if (prn && nit == 1) out(CP_NOT_SUPPORTED);
        else if (nit > 1 && only_status(1)) status_lpt(k);
        else if (nit > 1 && it[1].kind == T_REROUTE) {
            if (nit > 2) err("Invalid number of parameters");
            else reroute(k, &it[1]);
        } else lpt_port(k, 1);
    } else if (d->kind == T_KEY && (k = devnum(d->up, "LPT")) >= 0 && k <= 2) {
        struct item r2 = *d;                    /* LPT1=COM1 */
        strcpy(r2.up, d->val);
        reroute(k, &r2);
    } else if ((d->kind == T_WORD && (!strcmp(d->up, "40") || !strcmp(d->up, "80") || !strcmp(d->up, "BW40") ||
                                      !strcmp(d->up, "BW80") || !strcmp(d->up, "CO40") || !strcmp(d->up, "CO80") ||
                                      !strcmp(d->up, "MONO"))) || d->kind == T_EMPTY) {
        if (nit > 3) err("Invalid number of parameters");
        else display();
    } else {
        err_sub("Invalid parameter", d->text);
    }
done:
    if (res_new) go_resident();
    return errors ? 1 : 0;
}
