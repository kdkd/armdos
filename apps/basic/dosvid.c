/*
 * dosvid.c - the PC hardware side of BASIC.EXE on ARM-DOS: screen, colours,
 * graphics, keyboard, speaker, Ctrl-Break, PEEK/POKE/INP/OUT, FILES.
 *
 * Kept apart from the interpreter (bwx_dos.c is the glue) so that the DOS
 * headers' macros never meet bwBASIC's identifiers.
 *
 * Screen output: when standard output is the console, PRINT goes through
 * our own writer (INT 10h + text memory at B800:0000, with the COLOR
 * attribute), the way GW-BASIC drew its screen; when it is redirected to a
 * file, output goes through DOS unchanged. Input lines always come from
 * DOS (buffered input, so the DOS editing keys work).
 *
 * Copyright (c) 2026 the ARM-DOS project. Part of the ARM-DOS port of
 * Bywater BASIC; distributed under the GNU GPL version 2 like bwBASIC.
 */
#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <dos.h>
#include <conio.h>
#include <io.h>
#include <direct.h>
#include <malloc.h>
#include <process.h>
#include <armdos.h>
#include "dosvid.h"

/* ------------------------------------------------------------------ state */
static int tty_out, tty_in;
static int bmode;               /* BASIC SCREEN mode: 0 text, 1, 2, 13 */
static int cols = 80, rows = 25;
static int text_cols = 80;     /* WIDTH for SCREEN 0 */
static int fg = 7, bg = 0, border;
static int gfg = 15;            /* default drawing colour */
static int wrap_pending;
static int cursor_on = 1;
static double lpx, lpy;         /* graphics "last point" */
static unsigned defseg;         /* DEF SEG (0 = flat) */
static volatile int brk;        /* Ctrl-Break / Ctrl-C seen */
static armdos_vect_t old1b, old23;
static FILE *con;               /* our console stream (NULL if redirected) */

/* mode 7 = the Hercules/MDA card (text at B0000h, no BASIC graphics: SCREEN 1/2/13
   give "Illegal function call" as on an MDA) - see README */
#define MONO   (BDA8(0x49) == 7)
#define VRAM   ((volatile uint16_t *)(MONO ? 0xB0000 : 0xB8000))
#define CGA    ((volatile uint8_t *)0xB8000)
#define VGA    ((volatile uint8_t *)0xA0000)
#define BDA8(o)  (*(volatile uint8_t *)(0x400 + (o)))
#define BDA16(o) (*(volatile uint16_t *)(0x400 + (o)))

static void int10(unsigned ax, unsigned bx, unsigned cx, unsigned dx)
{
    struct armregs r;
    memset(&r, 0, sizeof r);
    r.r0 = ax; r.r1 = bx; r.r2 = cx; r.r3 = dx;
    _armdos_int10(&r);
}

/* ------------------------------------------------------------- cursor -- */
static int cur_row(void) { return BDA16(0x50) >> 8; }
static int cur_col(void) { return BDA16(0x50) & 0xFF; }
static void set_cur(int row, int col)       /* 0-based, updates the hardware */
{
    int10(0x0200, 0, 0, (row << 8) | col);
}

static int text_attr(void)
{
    return ((bg & 7) << 4) | (fg & 15) | ((fg & 16) ? 0x80 : 0);
}

static void scroll_up(void)
{
    int fill = bmode == 0 ? text_attr() : (bmode == 13 ? bg : 0);
    int10(0x0601, fill << 8, 0, ((rows - 1) << 8) | (cols - 1));
}

/* ------------------------------------------------------------- writer -- */
static void put_raw(int ch, int *row, int *col)
{
    if (wrap_pending) {
        wrap_pending = 0;
        *col = 0;
        if (++*row >= rows) { scroll_up(); *row = rows - 1; }
    }
    if (bmode == 0) {
        VRAM[*row * cols + *col] = (uint16_t) ((text_attr() << 8) | (uint8_t) ch);
    } else {
        BDA16(0x50) = (uint16_t) ((*row << 8) | *col);
        int10(0x0900 | (uint8_t) ch, bmode == 13 ? (fg & 0xFF) : (bmode == 1 ? (fg & 3) : (fg ? 1 : 0)), 1, 0);
    }
    if (++*col >= cols) {
        *col = cols - 1;
        wrap_pending = 1;
    }
}

static void newline(int *row, int *col)
{
    wrap_pending = 0;
    *col = 0;
    if (++*row >= rows) { scroll_up(); *row = rows - 1; }
}

static void con_putc(int ch, int *row, int *col)
{
    switch (ch) {
    case '\n': newline(row, col); break;
    case '\r': wrap_pending = 0; *col = 0; break;
    case '\a': dv_beep(); break;
    case '\b': wrap_pending = 0; if (*col > 0) --*col; break;
    case '\t': do put_raw(' ', row, col); while (*col % 8 && !wrap_pending); break;
    case '\f': dv_cls(); *row = cur_row(); *col = cur_col(); break;
    default:   put_raw(ch, row, col); break;
    }
}

static ssize_t con_write(void *cookie, const char *buf, size_t n)
{
    int row = cur_row(), col = cur_col();
    size_t i;
    (void) cookie;
    if (wrap_pending && col != cols - 1) wrap_pending = 0;   /* someone moved it */
    for (i = 0; i < n; i++)
        con_putc((unsigned char) buf[i], &row, &col);
    set_cur(row, col);
    return (ssize_t) n;
}

FILE *dv_console(void) { return con; }

/* anything else (DOS echoing typed input) moved the cursor: forget */
void dv_sync(void) { wrap_pending = 0; }

/* ---------------------------------------------------------- break keys -- */
static void h1b(struct armregs *f) { (void) f; brk = 1; }
static void h23(struct armregs *f) { brk = 1; f->cpsr &= ~ARM_CPSR_C; }  /* continue */

int dv_break(void)
{
    static unsigned n;
    if (!brk && tty_in && (++n & 63) == 0) {
        struct armregs r;
        memset(&r, 0, sizeof r);
        r.r0 = 0x0100;
        _armdos_int16(&r);
        if (!(r.cpsr & ARM_CPSR_Z) && (r.r0 & 0xFF) == 3) {    /* Ctrl-C waiting */
            r.r0 = 0;
            _armdos_int16(&r);
            brk = 1;
        }
    }
    if (brk) {
        brk = 0;
        dv_sound_off();
        return 1;
    }
    return 0;
}

void dv_clear_break(void) { brk = 0; }

/* --------------------------------------------------------------- setup -- */
int dv_is_tty(void) { return tty_out; }

void dv_init(void)
{
    static const cookie_io_functions_t fns = { NULL, con_write, NULL, NULL };
    tty_out = isatty(1);
    tty_in = isatty(0);
    old1b = armdos_getvect(0x1B);
    old23 = armdos_getvect(0x23);
    armdos_setvect(0x1B, h1b);
    armdos_setvect(0x23, h23);
    if (tty_out) {
        int m = BDA8(0x49);
        cols = BDA16(0x4A) ? BDA16(0x4A) : 80;
        text_cols = cols == 40 ? 40 : 80;
        rows = BDA8(0x84) ? BDA8(0x84) + 1 : 25;
        if (m != 3 && m != 2 && m != 1 && m != 0 && m != 7) {    /* not in text mode: go there */
            int10(0x0003, 0, 0, 0);
            cols = 80; rows = 25;
        }
        con = fopencookie(NULL, "w", fns);
        if (con) setvbuf(con, NULL, _IOFBF, 512);
    }
}

void dv_exit(void)
{
    dv_sound_off();
    if (con) fflush(con);
    if (tty_out && bmode != 0) {
        int10(0x0003, 0, 0, 0);
        bmode = 0;
    }
    armdos_setvect(0x1B, old1b);
    armdos_setvect(0x23, old23);
}

int dv_cols(void) { return cols; }

/* ----------------------------------------------------- CLS LOCATE COLOR -- */
void dv_cls(void)
{
    if (!tty_out) return;
    if (con) fflush(con);
    wrap_pending = 0;
    if (bmode == 0) {
        uint16_t blank = (uint16_t) ((text_attr() << 8) | ' ');
        int i;
        for (i = 0; i < rows * cols; i++) VRAM[i] = blank;
    } else if (bmode == 13) {
        memset((void *) VGA, bg, 64000);
    } else {
        memset((void *) CGA, 0, 16384);
    }
    set_cur(0, 0);
    lpx = bmode == 2 ? 320 : 160;
    lpy = 100;
}

int dv_locate(int row, int col, int cursor)
{
    if (con) fflush(con);
    if (row > rows || col > cols) return -1;
    if (tty_out && (row > 0 || col > 0)) {
        int r = row > 0 ? row - 1 : cur_row();
        int c = col > 0 ? col - 1 : cur_col();
        wrap_pending = 0;
        set_cur(r, c);
    }
    if (cursor >= 0 && tty_out && bmode == 0) {
        cursor_on = cursor != 0;
        int10(0x0100, 0, cursor_on ? (MONO ? 0x0B0C : 0x0D0E) : 0x2000, 0);
    }
    return 0;
}

int dv_csrlin(void) { return tty_out ? cur_row() + 1 : 1; }
int dv_pos(void) { return tty_out ? cur_col() + 1 : 1; }

/* n = number of arguments given (a, b, c); an omitted one is < 0 */
int dv_color(int a, int b, int c)
{
    if (con) fflush(con);
    switch (bmode) {
    case 0:
        if (a > 31 || b > 15 || c > 15) return -1;
        if (a >= 0) fg = a;
        if (b >= 0) bg = b;
        if (c >= 0) border = c;
        return 0;
    case 1:             /* COLOR background, palette */
        if (a >= 0) { if (a > 15) return -1; bg = a; int10(0x0B00, a, 0, 0); }
        if (b >= 0) int10(0x0B00, 0x0100 | (b & 1), 0, 0);
        return 0;
    case 13:            /* COLOR foreground[, background] */
        if (a > 255 || b > 255) return -1;
        if (a >= 0) { fg = a; gfg = a; }
        if (b >= 0) bg = b;
        return 0;
    default:
        return -1;      /* SCREEN 2: no COLOR */
    }
}

/* PALETTE attribute, colour (text/CGA: attribute controller; 13: DAC as
   &Hbbggrr, 6 bits each, like QBasic) */
int dv_palette(long attr, long color)
{
    if (!tty_out) return 0;
    if (bmode == 13) {
        if (attr < 0 || attr > 255) return -1;
        armdos_outb(0x3C8, (uint8_t) attr);
        armdos_outb(0x3C9, color & 63);
        armdos_outb(0x3C9, (color >> 8) & 63);
        armdos_outb(0x3C9, (color >> 16) & 63);
        return 0;
    }
    if (attr < 0 || attr > 15 || color < 0 || color > 63) return -1;
    int10(0x1000, (color << 8) | attr, 0, 0);
    return 0;
}

int dv_screen(int mode)
{
    int m;
    if (con) fflush(con);
    switch (mode) {
    case 0:  m = text_cols == 40 ? 1 : 3; break;
    case 1:  m = 4; break;
    case 2:  m = 6; break;
    case 13: m = 0x13; break;
    default: return -1;
    }
    if (MONO && mode != 0) return -1;
    if (!tty_out) { bmode = mode; return 0; }
    if (mode == bmode && mode != 0) return 0;
    int10(m, 0, 0, 0);
    bmode = mode;
    cols = (m == 1 || m == 4 || m == 0x13) ? 40 : 80;
    rows = 25;
    wrap_pending = 0;
    fg = mode == 0 ? 7 : (mode == 1 ? 3 : (mode == 2 ? 1 : 15));
    gfg = fg;
    bg = 0;
    lpx = mode == 2 ? 320 : 160;
    lpy = 100;
    return 0;
}

int dv_width(int w)
{
    if (w != 40 && w != 80) return -1;
    if (MONO && w == 40) return -1;
    if (bmode == 0) text_cols = w;
    if (!tty_out || bmode != 0 || w == cols) { if (!tty_out) cols = w; return 0; }
    if (con) fflush(con);
    int10(w == 40 ? 0x0001 : 0x0003, 0, 0, 0);
    cols = w;
    wrap_pending = 0;
    return 0;
}

int dv_mode(void) { return bmode; }

/* ------------------------------------------------------------ graphics -- */
static int gw(void) { return bmode == 2 ? 640 : 320; }

static void pset(int x, int y, int c)
{
    if (x < 0 || y < 0 || x >= gw() || y >= 200) return;
    if (bmode == 13) {
        VGA[y * 320 + x] = (uint8_t) c;
    } else if (bmode == 1) {
        volatile uint8_t *p = CGA + (y & 1) * 0x2000 + (y >> 1) * 80 + (x >> 2);
        int sh = (3 - (x & 3)) * 2;
        *p = (uint8_t) ((*p & ~(3 << sh)) | ((c & 3) << sh));
    } else if (bmode == 2) {
        volatile uint8_t *p = CGA + (y & 1) * 0x2000 + (y >> 1) * 80 + (x >> 3);
        int sh = 7 - (x & 7);
        *p = (uint8_t) ((*p & ~(1 << sh)) | ((c & 1) << sh));
    }
}

static int point(int x, int y)
{
    if (x < 0 || y < 0 || x >= gw() || y >= 200) return -1;
    if (bmode == 13) return VGA[y * 320 + x];
    if (bmode == 1) return (CGA[(y & 1) * 0x2000 + (y >> 1) * 80 + (x >> 2)] >> ((3 - (x & 3)) * 2)) & 3;
    return (CGA[(y & 1) * 0x2000 + (y >> 1) * 80 + (x >> 3)] >> (7 - (x & 7))) & 1;
}

static int maxcolor(void) { return bmode == 13 ? 255 : (bmode == 1 ? 3 : 1); }

static int gcheck(int *c)
{
    if (bmode == 0) return -1;              /* Illegal function call */
    if (*c < 0) *c = gfg;
    else if (*c > maxcolor()) *c &= maxcolor();
    return 0;
}

static void hline(int x1, int x2, int y, int c)
{
    int x;
    if (x1 > x2) { x = x1; x1 = x2; x2 = x; }
    for (x = x1; x <= x2; x++) pset(x, y, c);
}

static void line(int x1, int y1, int x2, int y2, int c)
{
    int dx = abs(x2 - x1), sx = x1 < x2 ? 1 : -1;
    int dy = -abs(y2 - y1), sy = y1 < y2 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        pset(x1, y1, c);
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
}

static int ri(double v) { return (int) floor(v + 0.5); }

void dv_lastpoint(double *x, double *y) { *x = lpx; *y = lpy; }

int dv_pset(double x, double y, int c, int preset)
{
    if (preset && c < 0) c = bmode == 13 ? bg : 0;
    if (gcheck(&c)) return -1;
    pset(ri(x), ri(y), c);
    lpx = x; lpy = y;
    return 0;
}

int dv_point(double x, double y) { return bmode == 0 ? -1 : point(ri(x), ri(y)); }

/* box: 0 line, 1 B, 2 BF */
int dv_line(double x1, double y1, double x2, double y2, int c, int box)
{
    int a = ri(x1), b = ri(y1), e = ri(x2), f = ri(y2);
    if (gcheck(&c)) return -1;
    if (box == 2) {
        int y, lo = b < f ? b : f, hi = b < f ? f : b;
        for (y = lo; y <= hi; y++) hline(a, e, y, c);
    } else if (box == 1) {
        hline(a, e, b, c); hline(a, e, f, c);
        line(a, b, a, f, c); line(e, b, e, f, c);
    } else {
        line(a, b, e, f, c);
    }
    lpx = x2; lpy = y2;
    return 0;
}

/* CIRCLE: start/end in radians (NAN = full circle), aspect < 0 = default */
int dv_circle(double cx, double cy, double r, int c, double start, double end, double aspect)
{
    double rx, ry, t, step, t0 = 0, t1 = 2 * M_PI;
    int px = 0, py = 0, first = 1;
    if (gcheck(&c)) return -1;
    if (aspect <= 0) aspect = bmode == 2 ? 5.0 / 12.0 : 5.0 / 6.0;
    if (aspect < 1) { rx = r; ry = r * aspect; } else { ry = r; rx = r / aspect; }
    if (!isnan(start)) t0 = fabs(start);
    if (!isnan(end)) t1 = fabs(end);
    if (t1 < t0) t1 += 2 * M_PI;
    step = 1.0 / (rx > ry ? (rx > 1 ? rx : 1) : (ry > 1 ? ry : 1));
    for (t = t0; ; t += step) {
        if (t > t1) t = t1;
        int x = ri(cx + rx * cos(t)), y = ri(cy - ry * sin(t));
        if (first) pset(x, y, c); else line(px, py, x, y, c);
        px = x; py = y; first = 0;
        if (t >= t1) break;
    }
    if (!isnan(start) && start < 0) line(ri(cx), ri(cy), ri(cx + rx * cos(t0)), ri(cy - ry * sin(t0)), c);
    if (!isnan(end) && end < 0) line(ri(cx), ri(cy), ri(cx + rx * cos(t1)), ri(cy - ry * sin(t1)), c);
    lpx = cx; lpy = cy;
    return 0;
}

/* PAINT: scanline flood fill up to the border colour */
int dv_paint(double x, double y, int c, int border)
{
    int w = gw(), n = 0, cap = 1024;
    int *st;
    if (gcheck(&c)) return -1;
    if (border < 0) border = c;
    int sx = ri(x), sy = ri(y);
    lpx = x; lpy = y;
    if (point(sx, sy) < 0 || point(sx, sy) == border) return 0;
    st = (int *) malloc(cap * 2 * sizeof (int));
    if (!st) return -2;
    st[n++] = sx; st[n++] = sy;
    while (n) {
        int py = st[--n], px = st[--n], l, r, i;
        if (point(px, py) == border || point(px, py) == c) continue;
        for (l = px; l > 0 && point(l - 1, py) != border && point(l - 1, py) != c; l--) ;
        for (r = px; r < w - 1 && point(r + 1, py) != border && point(r + 1, py) != c; r++) ;
        hline(l, r, py, c);
        for (i = -1; i <= 1; i += 2) {
            int ny = py + i, xx, in = 0;
            if (ny < 0 || ny >= 200) continue;
            for (xx = l; xx <= r; xx++) {
                int p = point(xx, ny);
                int ok = p != border && p != c;
                if (ok && !in) {
                    if (n + 2 > cap * 2) {
                        int *ns = (int *) realloc(st, cap * 4 * sizeof (int));
                        if (!ns) { free(st); return -2; }
                        st = ns; cap *= 2;
                    }
                    st[n++] = xx; st[n++] = ny;
                }
                in = ok;
            }
        }
    }
    free(st);
    return 0;
}

/* DRAW: the graphics macro language (U D L R E F G H M B N C S A TA P) */
static const char *dnum(const char *p, double *v, int *have)
{
    char *e;
    while (*p == ' ') p++;
    *v = strtod(p, &e);
    *have = e != p;
    return e;
}

int dv_draw(const char *s)
{
    static int scale = 4, angle = 0, ta = 0;
    const char *p = s;
    int c = -1;
    if (gcheck(&c)) return -1;
    c = gfg;
    while (*p) {
        int cmd = toupper((unsigned char) *p++), nodraw = 0, noupd = 0, have;
        double v;
        if (cmd == ' ' || cmd == ';') continue;
        while (cmd == 'B' || cmd == 'N') {
            if (cmd == 'B') nodraw = 1; else noupd = 1;
            while (*p == ' ') p++;
            cmd = toupper((unsigned char) *p++);
        }
        if (strchr("UDLREFGH", cmd)) {
            double dx = 0, dy = 0, nx, ny, a, ca, sa;
            p = dnum(p, &v, &have);
            if (!have) v = 1;
            switch (cmd) {
            case 'U': dy = -v; break;           case 'D': dy = v; break;
            case 'L': dx = -v; break;           case 'R': dx = v; break;
            case 'E': dx = v; dy = -v; break;   case 'F': dx = v; dy = v; break;
            case 'G': dx = -v; dy = v; break;   case 'H': dx = -v; dy = -v; break;
            }
            dx = dx * scale / 4; dy = dy * scale / 4;
            a = -(angle * 90 + ta) * M_PI / 180;
            ca = cos(a); sa = sin(a);
            nx = lpx + dx * ca - dy * sa;
            ny = lpy + dx * sa + dy * ca;
            if (!nodraw) line(ri(lpx), ri(lpy), ri(nx), ri(ny), c);
            if (!noupd) { lpx = nx; lpy = ny; }
        } else if (cmd == 'M') {
            double x, y, nx, ny;
            int rel;
            while (*p == ' ') p++;
            rel = *p == '+' || *p == '-';
            p = dnum(p, &x, &have); if (!have) return -1;
            while (*p == ' ') p++;
            if (*p++ != ',') return -1;
            p = dnum(p, &y, &have); if (!have) return -1;
            if (rel) { nx = lpx + x * scale / 4; ny = lpy + y * scale / 4; } else { nx = x; ny = y; }
            if (!nodraw) line(ri(lpx), ri(lpy), ri(nx), ri(ny), c);
            if (!noupd) { lpx = nx; lpy = ny; }
        } else if (cmd == 'C') {
            p = dnum(p, &v, &have); if (!have) return -1;
            c = (int) v & maxcolor();
        } else if (cmd == 'S') {
            p = dnum(p, &v, &have); if (!have) return -1;
            scale = (int) v; if (scale < 1) scale = 4;
        } else if (cmd == 'A') {
            p = dnum(p, &v, &have); if (!have) return -1;
            angle = (int) v & 3;
        } else if (cmd == 'T') {
            if (toupper((unsigned char) *p) != 'A') return -1;
            p = dnum(p + 1, &v, &have); if (!have) return -1;
            ta = (int) v;
        } else if (cmd == 'P') {
            double pc, bc;
            p = dnum(p, &pc, &have); if (!have) return -1;
            while (*p == ' ') p++;
            if (*p++ != ',') return -1;
            p = dnum(p, &bc, &have); if (!have) return -1;
            dv_paint(lpx, lpy, (int) pc, (int) bc);
        } else {
            return -1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------- keyboard -- */
int dv_inkey(char *out)
{
    struct armregs r;
    if (con) fflush(con);
    if (!tty_in) {
        int c = getchar();
        if (c == EOF) return 0;
        out[0] = (char) c;
        return 1;
    }
    memset(&r, 0, sizeof r);
    r.r0 = 0x0100;
    _armdos_int16(&r);
    if (r.cpsr & ARM_CPSR_Z) return 0;
    r.r0 = 0;
    _armdos_int16(&r);
    if ((r.r0 & 0xFF) == 3) { brk = 1; return 0; }
    if ((r.r0 & 0xFF) == 0 || (r.r0 & 0xFF) == 0xE0) {
        out[0] = 0;
        out[1] = (char) ((r.r0 >> 8) & 0xFF);
        return 2;
    }
    out[0] = (char) (r.r0 & 0xFF);
    return 1;
}

int dv_getkey(void)
{
    if (con) fflush(con);
    return tty_in ? getch() : getchar();
}

/* --------------------------------------------------------------- sound -- */
static void spk_on(double hz)
{
    unsigned d = (unsigned) (1193182.0 / hz);
    if (d < 1) d = 1;
    if (d > 65535) d = 65535;
    armdos_outb(0x43, 0xB6);
    armdos_outb(0x42, d & 0xFF);
    armdos_outb(0x42, (d >> 8) & 0xFF);
    armdos_outb(0x61, armdos_inb(0x61) | 3);
}

void dv_sound_off(void)
{
    armdos_outb(0x61, armdos_inb(0x61) & ~3);
}

/* wait, in slices so that Ctrl-Break works; 1 = interrupted */
static int wait_ms(double ms)
{
    while (ms > 0.5) {
        double slice = ms > 50 ? 50 : ms;
        unsigned long us = (unsigned long) (slice * 1000);
        struct armregs r;
        memset(&r, 0, sizeof r);
        r.r0 = 0x8600;
        r.r2 = us >> 16;
        r.r3 = us & 0xFFFF;
        _armdos_intr(0x15, &r);
        ms -= slice;
        if (brk) return 1;
    }
    return 0;
}

int dv_sound(double hz, double ticks)
{
    if (con) fflush(con);
    if (ticks < 0 || ticks > 65535 || hz < 0 || hz > 32767) return -1;
    if (ticks == 0) { dv_sound_off(); return 0; }
    if (hz < 37) return -1;
    if (hz < 20000) spk_on(hz); else dv_sound_off();
    int r = wait_ms(ticks * 1000.0 / 18.2065);
    dv_sound_off();
    return r ? 1 : 0;
}

int dv_beep(void)
{
    if (con) fflush(con);
    spk_on(800);
    int r = wait_ms(250);
    dv_sound_off();
    return r;
}

/* PLAY: the music macro language */
static int play_oct = 4, play_len = 4, play_tempo = 120, play_mode = 0; /* 0 MN 1 ML 2 MS */

static const char *pnum(const char *p, int *v)
{
    int have = 0, n = 0;
    while (*p == ' ') p++;
    while (isdigit((unsigned char) *p)) { n = n * 10 + (*p++ - '0'); have = 1; }
    *v = have ? n : -1;
    return p;
}

static int play_tone(double hz, int len, int dots)
{
    double ms = 60000.0 / play_tempo * 4.0 / len, on;
    int i;
    for (i = 0; i < dots; i++) ms *= 1.5;
    on = play_mode == 1 ? ms : (play_mode == 2 ? ms * 3 / 4 : ms * 7 / 8);
    if (hz > 0) {
        spk_on(hz);
        if (wait_ms(on)) { dv_sound_off(); return 1; }
        dv_sound_off();
        return wait_ms(ms - on);
    }
    return wait_ms(ms);
}

int dv_play(const char *s)
{
    static const int semis[7] = { 9, 11, 0, 2, 4, 5, 7 };  /* A B C D E F G */
    const char *p = s;
    if (con) fflush(con);
    while (*p) {
        int c = toupper((unsigned char) *p++), v, len, dots = 0;
        if (c == ' ' || c == ';') continue;
        if (c >= 'A' && c <= 'G') {
            int semi = semis[c - 'A'];
            if (*p == '#' || *p == '+') { semi++; p++; }
            else if (*p == '-') { semi--; p++; }
            p = pnum(p, &v);
            len = v > 0 ? v : play_len;
            if (len > 64) return -1;
            while (*p == '.') { dots++; p++; }
            if (play_tone(32.7032 * pow(2.0, (play_oct * 12 + semi) / 12.0), len, dots)) return 1;
        } else if (c == 'N') {
            p = pnum(p, &v);
            if (v < 0 || v > 84) return -1;
            while (*p == '.') { dots++; p++; }
            if (play_tone(v ? 32.7032 * pow(2.0, (v - 1) / 12.0) : 0, play_len, dots)) return 1;
        } else if (c == 'P' || c == 'R') {
            p = pnum(p, &v);
            if (v < 1 || v > 64) return -1;
            while (*p == '.') { dots++; p++; }
            if (play_tone(0, v, dots)) return 1;
        } else if (c == 'O') {
            p = pnum(p, &v); if (v < 0 || v > 6) return -1; play_oct = v;
        } else if (c == 'L') {
            p = pnum(p, &v); if (v < 1 || v > 64) return -1; play_len = v;
        } else if (c == 'T') {
            p = pnum(p, &v); if (v < 32 || v > 255) return -1; play_tempo = v;
        } else if (c == '>') {
            if (play_oct < 6) play_oct++;
        } else if (c == '<') {
            if (play_oct > 0) play_oct--;
        } else if (c == 'M') {
            c = toupper((unsigned char) *p++);
            if (c == 'N') play_mode = 0;
            else if (c == 'L') play_mode = 1;
            else if (c == 'S') play_mode = 2;
            else if (c != 'F' && c != 'B') return -1;
        } else {
            return -1;
        }
    }
    return 0;
}

/* ------------------------------------------------- memory, ports, time -- */
void dv_defseg(long seg) { defseg = seg < 0 ? 0 : (unsigned) seg & 0xFFFF; }

static int addr_ok(unsigned long a) { return a < 0x01000000ul; }

int dv_peek(long off, int *val)
{
    unsigned long a = ((unsigned long) defseg << 4) + (unsigned long) off;
    if (off < 0 || !addr_ok(a)) return -1;
    *val = *(volatile uint8_t *) a;
    return 0;
}

int dv_poke(long off, int val)
{
    unsigned long a = ((unsigned long) defseg << 4) + (unsigned long) off;
    if (off < 0 || !addr_ok(a) || val < 0 || val > 255) return -1;
    if (con) fflush(con);
    *(volatile uint8_t *) a = (uint8_t) val;
    return 0;
}

int dv_inp(long port) { return armdos_inb((unsigned) port & 0xFFFF); }
void dv_out(long port, int val)
{
    if (con) fflush(con);
    armdos_outb((unsigned) port & 0xFFFF, (uint8_t) val);
}

double dv_timer(void)
{
    return ARMDOS_BIOS_TICKS / 18.2065;
}

/* ---------------------------------------------------------------- FILES -- */
int dv_files(FILE *out, const char *spec)
{
    struct find_t f;
    char pat[80], dir[80];
    struct diskfree_t df;
    int n = 0, drive;
    unsigned long freeb = 0;

    if (!spec || !*spec) spec = "*.*";
    strncpy(pat, spec, sizeof pat - 5);
    pat[sizeof pat - 5] = 0;
    if (pat[strlen(pat) - 1] == '\\' || pat[strlen(pat) - 1] == ':') strcat(pat, "*.*");
    if (getcwd(dir, sizeof dir)) fprintf(out, "%s\n", dir);
    if (_dos_findfirst(pat, _A_NORMAL | _A_SUBDIR | _A_RDONLY, &f) != 0)
        return -1;          /* File not found */
    do {
        char name[9], ext[4], *dot;
        dot = strchr(f.name, '.');
        if (dot && dot != f.name) {
            snprintf(name, sizeof name, "%.*s", (int) (dot - f.name), f.name);
            snprintf(ext, sizeof ext, "%s", dot + 1);
        } else {                    /* no extension, or "." / ".." */
            snprintf(name, sizeof name, "%s", f.name);
            ext[0] = 0;
        }
        fprintf(out, "%-8s.%-3s%s", name, ext, (f.attrib & _A_SUBDIR) ? "<DIR>" : "     ");
        if (++n % 4 == 0) fputc('\n', out); else fputs("  ", out);
    } while (_dos_findnext(&f) == 0);
    if (n % 4) fputc('\n', out);
    drive = (pat[1] == ':') ? toupper((unsigned char) pat[0]) - 'A' + 1 : 0;
    if (_dos_getdiskfree(drive, &df) == 0)
        freeb = (unsigned long) df.avail_clusters * df.sectors_per_cluster * df.bytes_per_sector;
    fprintf(out, " %lu Bytes free\n", freeb);
    return 0;
}

unsigned long dv_memfree(void)
{
    return _memavl();
}

/* SHELL with no command: an interactive COMMAND.COM (EXIT comes back) */
int dv_shell(void)
{
    const char *cs = getenv("COMSPEC");
    int r;
    if (!cs || !*cs) cs = "\\COMMAND.COM";
    if (con) fflush(con);
    fflush(stdout);
    r = spawnl(P_WAIT, cs, cs, (char *) NULL);
    return r;
}
