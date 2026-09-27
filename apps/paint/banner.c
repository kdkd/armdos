/* apps/paint/banner.c - ARM Banner: huge letters sideways along continuous
 * tractor-feed paper on an Epson FX-80 compatible printer (LPT1).
 *
 *   BANNER [message] [/F:n] [/B:n]
 *
 *   /F:n  font    1 Block (the ROM 8x8 font), 2 Smooth (the VGA 8x16 font,
 *                 smoothed), 3 Outline, 4 Shadow
 *   /B:n  border  0 none, 1 line, 2 double line, 3 stars, 4 hearts
 *
 * Without a message it asks for everything. The banner is printed as ESC K
 * (60 dpi) bit images across the full 8" line, one 8-pin band (8/72") at a
 * time with ESC 3 24 line spacing and no skip over the perforation: the
 * letters' tops point to the right-hand edge of the paper, so the banner reads
 * left to right when the first sheet is on the left.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <conio.h>
#include <dos.h>
#include <armdos.h>

#define COLS   480                  /* 8" at 60 dpi */
#define ESC    0x1B

static const uint8_t *font8, *font16;
static int font = 2, border = 3;
static char msg[81];

/* the geometry of one letter on the paper */
static int gh;                      /* glyph rows (8 or 16) */
static const uint8_t *gfont;
static int x_top, x_bottom;         /* columns of the glyph's top and bottom edges */
static int rows_per_col;            /* paper rows (1/72") per glyph column */

static int lpt_err;
static void out(int b)
{
    if (lpt_err) return;
    union REGS r;
    r.x.ax = (unsigned)(b & 0xFF); r.x.dx = 0;
    int86(0x17, &r, &r);
    if (r.h.ah & 0x29) lpt_err = r.h.ah;
}

static int bit(int ch, int c, int r)
{
    if (c < 0 || c > 7 || r < 0 || r >= gh) return 0;
    return (gfont[ch * gh + r] >> (7 - c)) & 1;
}

/* is the glyph inked at glyph coordinates (u across, v down), in 1/256ths? */
static int inked(int ch, int u, int v)
{
    if (font == 1) return bit(ch, u >> 8, v >> 8);
    /* smooth: bilinear interpolation of the bitmap, thresholded at a half */
    int uu = u - 128, vv = v - 128;
    int c0 = uu >> 8, r0 = vv >> 8, fu = uu & 255, fv = vv & 255;
    int a = bit(ch, c0, r0) * (256 - fu) + bit(ch, c0 + 1, r0) * fu;
    int b = bit(ch, c0, r0 + 1) * (256 - fu) + bit(ch, c0 + 1, r0 + 1) * fu;
    return a * (256 - fv) + b * fv >= 128 * 256;
}

/* ink at paper row y (0 = the letter's first row) and column x */
static int letter_ink(int ch, int y, int x)
{
    int span = x_top - x_bottom;
    int v = (int)((long)(x_top - x) * gh * 256 / span);            /* down the glyph */
    int u = (int)((long)y * 256 / rows_per_col);                   /* along it */
    if (v < 0 || v >= gh * 256 || u < 0 || u >= 8 * 256) {
        if (font != 4) return 0;
    }
    switch (font) {
    case 3: {                                  /* outline: inside, but near the edge */
        if (!inked(ch, u, v)) return 0;
        const int d = 150;                     /* ~0.6 of a glyph pixel */
        return !inked(ch, u - d, v) || !inked(ch, u + d, v) || !inked(ch, u, v - d) || !inked(ch, u, v + d);
    }
    case 4: {                                  /* shadow: solid letter, a grey shadow down and right */
        if (v >= 0 && v < gh * 256 && u >= 0 && u < 8 * 256 && inked(ch, u, v)) return 1;
        int su = u - 90, sv = v - 90;
        if (sv >= 0 && sv < gh * 256 && su >= 0 && su < 8 * 256 && inked(ch, su, sv)) return ((x + y) & 1);
        return 0;
    }
    default: return inked(ch, u, v);
    }
}

/* the border along both edges: x measured from the edge (0..35), y along the paper */
static int border_ink(int x, int y)
{
    switch (border) {
    case 1: return x >= 12 && x < 18;
    case 2: return (x >= 8 && x < 14) || (x >= 20 && x < 23);
    case 3: case 4: {
        /* a motif every half inch, upright as the banner reads: its "up" is the
           right-hand edge of the paper (higher x), its "right" further along */
        int cy = y % 36;
        float px = (cy - 18.0f) / 15.0f, py = -(x - 17.5f) / 15.0f;
        if (border == 4) {                             /* a heart: (x^2+y^2-1)^3 - x^2 y^3 <= 0 */
            float hx = px * 1.3f, hy = -py * 1.3f + 0.25f;
            float a = hx * hx + hy * hy - 1;
            return a * a * a - hx * hx * hy * hy * hy <= 0;
        }
        /* a five-pointed star: the polar radius against a star-shaped bound */
        float r2 = px * px + py * py;
        if (r2 > 1.0f) return 0;
        /* angle via atan2-free octant trick: use the 5-fold symmetry numerically */
        static float cosk[5], sink[5];
        static int init;
        if (!init) {
            /* cos/sin of 90 + 72k degrees, precomputed */
            const float c[5] = { 0.0f, -0.9510565f, -0.5877853f, 0.5877853f, 0.9510565f };
            const float s[5] = { 1.0f, 0.3090170f, -0.8090170f, -0.8090170f, 0.3090170f };
            for (int k = 0; k < 5; k++) { cosk[k] = c[k]; sink[k] = s[k]; }
            init = 1;
        }
        /* inside if inside any of the 5 triangles (point k, and the inner pentagon) */
        float qx = px, qy = -py;
        for (int k = 0; k < 5; k++) {
            /* rotate the point so the k-th tip points up */
            float rx = qx * sink[k] - qy * cosk[k], ry = qx * cosk[k] + qy * sink[k];
            /* the tip's triangle: apex (0,1), base at y = 0.38 * ... half width 0.36 at y=0.31 */
            if (ry <= 1.0f && ry >= -0.4f) {
                float w = (1.0f - ry) * 0.3633f;
                if (rx >= -w && rx <= w) return 1;
            }
        }
        return 0;
    }
    }
    return 0;
}

static int glyph_len(int ch) { return ch == ' ' ? 4 * rows_per_col : 8 * rows_per_col; }

static int progress(int letter, int n)
{
    printf("\rPrinting letter %d of %d ", letter, n);
    fflush(stdout);
    while (kbhit()) if (getch() == 27) return 1;
    return 0;
}

static int print_banner(void)
{
    int n = (int)strlen(msg);
    int edge = border ? 36 : 0;
    x_bottom = edge + 16;
    x_top = COLS - 1 - edge - 16;
    gh = font == 1 ? 8 : 16;
    gfont = font == 1 ? font8 : font16;
    int glyph_px = (x_top - x_bottom) / gh;                        /* columns per glyph row */
    /* 60 dpi across, 72 along: keep the font's look (8x16 glyphs are drawn 9x16 on a VGA, 4:3) */
    rows_per_col = font == 1 ? glyph_px * 72 / 60 : glyph_px * 72 * 3 / (60 * 2);
    const int gap = 18, lead = 72;
    /* where each letter starts along the paper */
    int *start = malloc((n + 1) * sizeof(int));
    if (!start) return -2;
    int y = lead;
    for (int i = 0; i < n; i++) { start[i] = y; y += glyph_len((uint8_t)msg[i]) + gap; }
    start[n] = y;
    int total = y + lead;
    uint8_t *band = malloc(COLS);
    if (!band) { free(start); return -2; }
    lpt_err = 0;
    out(ESC); out('@');
    out(ESC); out('O');
    out(ESC); out('3'); out(24);
    int cur = 0, cancelled = 0;
    for (int by = 0; by < total && !lpt_err; by += 8) {
        while (cur < n && by >= start[cur + 1]) cur++;
        if (!(by & 63) && progress(cur < n ? cur + 1 : n, n)) { cancelled = 1; break; }
        memset(band, 0, COLS);
        for (int p = 0; p < 8; p++) {
            int yy = by + p;
            uint8_t m = (uint8_t)(0x80 >> p);
            if (border)
                for (int x = 0; x < edge; x++)
                    {
                        if (border_ink(x, yy)) band[x] |= m;
                        if (border_ink(border <= 2 ? edge - 1 - x : x, yy)) band[COLS - edge + x] |= m;
                    }
            /* the letter(s) under this row (the shadow may reach into the gap) */
            for (int k = cur > 0 ? cur - 1 : 0; k <= cur && k < n; k++) {
                int ch = (uint8_t)msg[k];
                if (ch == ' ') continue;
                int ly = yy - start[k];
                if (ly < 0 || ly >= glyph_len(ch) + (font == 4 ? gap : 0)) continue;
                for (int x = x_bottom - 12; x <= x_top; x++)
                    if (x >= 0 && letter_ink(ch, ly, x)) band[x] |= m;
            }
        }
        int w = COLS;
        while (w > 0 && !band[w - 1]) w--;
        if (w) {
            out(ESC); out('K'); out(w & 0xFF); out(w >> 8);
            for (int x = 0; x < w; x++) out(band[x]);
        }
        out('\r'); out('\n');
    }
    out(ESC); out('2');
    if (cancelled) out(ESC), out('@');
    free(band); free(start);
    printf("\r%-40s\r", "");
    if (lpt_err) return -1;
    return cancelled;
}

static int get_font(int bh, const uint8_t **p)
{
    struct armregs r;
    memset(&r, 0, sizeof r);
    r.r0 = 0x1130; r.r1 = (unsigned)bh << 8;
    _armdos_int10(&r);
    *p = (const uint8_t *)r.r6;
    return *p != 0;
}

static void ask(const char *prompt, char *buf, int max)
{
    printf("%s", prompt);
    fflush(stdout);
    if (!fgets(buf, max, stdin)) buf[0] = 0;
    buf[strcspn(buf, "\r\n")] = 0;
}

int main(int argc, char **argv)
{
    int have_msg = 0;
    msg[0] = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] == '/' && (a[1] == '?' || toupper(a[1]) == 'H')) {
            printf("Prints a banner on the printer.\n\n"
                   "BANNER [message] [/F:n] [/B:n]\n\n"
                   "  /F:n  font: 1 Block, 2 Smooth, 3 Outline, 4 Shadow\n"
                   "  /B:n  border: 0 None, 1 Line, 2 Double, 3 Stars, 4 Hearts\n");
            return 0;
        }
        if (a[0] == '/' && (toupper(a[1]) == 'F' || toupper(a[1]) == 'B') && a[2] == ':') {
            int v = atoi(a + 3);
            if (toupper(a[1]) == 'F') { if (v < 1 || v > 4) { printf("Invalid font - %s\n", a); return 1; } font = v; }
            else { if (v < 0 || v > 4) { printf("Invalid border - %s\n", a); return 1; } border = v; }
            continue;
        }
        if (a[0] == '/') { printf("Invalid switch - %s\n", a); return 1; }
        if (strlen(msg) + strlen(a) + 2 < sizeof msg) { if (msg[0]) strcat(msg, " "); strcat(msg, a); }
        have_msg = 1;
    }
    if (!get_font(3, &font8) || !get_font(6, &font16)) { printf("Cannot find the ROM fonts\n"); return 1; }
    if (!have_msg) {
        char b[8];
        printf("ARM Banner  Version 1.00\n"
               "Copyright (C) Europa Micro Systems 1988.  All rights reserved.\n\n");
        ask("Message: ", msg, sizeof msg);
        if (!msg[0]) return 0;
        printf("\nFonts:   1 Block   2 Smooth   3 Outline   4 Shadow\n");
        ask("Font (1-4) [2]: ", b, sizeof b);
        if (b[0] >= '1' && b[0] <= '4') font = b[0] - '0';
        printf("\nBorders: 0 None   1 Line   2 Double   3 Stars   4 Hearts\n");
        ask("Border (0-4) [3]: ", b, sizeof b);
        if (b[0] >= '0' && b[0] <= '4') border = b[0] - '0';
        printf("\n");
    }
    for (char *c = msg; *c; c++) *c = (char)toupper((uint8_t)*c);
    union REGS r;
    r.x.ax = 0x0200; r.x.dx = 0;
    int86(0x17, &r, &r);
    if ((r.h.ah & 0x29) || !(r.h.ah & 0x10)) { printf("Printer not ready\n"); return 2; }
    printf("Printing \"%s\" - press Esc to stop\n", msg);
    int res = print_banner();
    if (res < 0) { printf(res == -2 ? "Not enough memory\n" : "Printer error\n"); return 2; }
    printf(res ? "Banner cancelled\n" : "Banner printed\n");
    return 0;
}
