/*
 * HERCULES.EXE - a Hercules Graphics Card demo for the ARM/AT (C:\DEMO).
 *
 * Needs the machine's Hercules card + monochrome monitor option. Programs the
 * card directly, as every Hercules program did (there is no BIOS graphics
 * mode): configuration switch 3BFh = 3 ("full": graphics allowed, second page
 * at B8000h), mode control 3B8h, the 6845 at 3B4h/3B5h with the 720x348 table.
 * Pixels: offset = 2000h*(y&3) + 90*(y>>2) + x/8, bit 7 = leftmost.
 *
 *   scene 1: a wireframe ARM chip (a 48-pin flat pack) spinning over a
 *            scrolling perspective grid, drawn page-flipped on the card's two
 *            32 KB pages, flipped in the vertical retrace (3BAh bit 7 = 0)
 *   scene 2: a Lotus 1-2-3 style bar chart with hatched bars
 *
 * Keys: Space/Enter = next scene, Esc = back to DOS. Back in text mode via
 * INT 10h AX=0007h, after the configuration switch is set to 0 again.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <conio.h>
#include <armdos.h>

#define W 720
#define H 348
#define BDA8(o)  (*(volatile uint8_t *)(0x400 + (o)))
#define BDA16(o) (*(volatile uint16_t *)(0x400 + (o)))
#define TICKS    (*(volatile uint32_t *)0x46C)

static uint8_t *const page[2] = { (uint8_t *)0xB0000, (uint8_t *)0xB8000 };
static uint8_t *dst;                    /* the page being drawn */
static const uint8_t *font;             /* the BIOS 8x8 font (INT 43h vector) */

/* ------------------------------------------------------------ the card */

/* a Hercules: a mono card whose status bit 7 (vertical retrace) toggles */
static int hercules_present(void)
{
    if ((BDA16(0x10) & 0x30) != 0x30 && BDA8(0x49) != 7) return 0;
    uint8_t first = armdos_inb(0x3BA) & 0x80;
    uint32_t t0 = TICKS;
    while (TICKS - t0 < 4)
        if ((armdos_inb(0x3BA) & 0x80) != first) return 1;
    return 0;
}

static void crtc_table(const uint8_t *t)
{
    for (int i = 0; i < 12; i++) { armdos_outb(0x3B4, i); armdos_outb(0x3B5, t[i]); }
}

static void graphics_mode(void)
{
    static const uint8_t gtab[12] = { 0x35, 0x2D, 0x2E, 0x07, 0x5B, 0x02, 0x57, 0x57, 0x02, 0x03, 0x00, 0x00 };
    armdos_outb(0x3BF, 0x03);           /* "full": graphics + page 1 */
    armdos_outb(0x3B8, 0x02);           /* graphics, video off while reprogramming */
    crtc_table(gtab);
    memset(page[0], 0, 0x8000);
    memset(page[1], 0, 0x8000);
    armdos_outb(0x3B8, 0x0A);           /* graphics, video on, page 0 */
}

static void text_mode(void)
{
    armdos_outb(0x3B8, 0x20);           /* text, video off */
    armdos_outb(0x3BF, 0x00);           /* back to "diag": no graphics, no page 1 */
    struct armregs r = { 0 };
    r.r0 = 0x0007;
    _armdos_int10(&r);
}

static void show(int p) { armdos_outb(0x3B8, 0x0A | (p ? 0x80 : 0)); }

static void vsync(void)
{
    while (!(armdos_inb(0x3BA) & 0x80)) ;       /* in a retrace: let it end */
    while (armdos_inb(0x3BA) & 0x80) ;          /* the next one starts */
}

/* ------------------------------------------------------------- drawing */

static inline void pset(int x, int y)
{
    if ((unsigned)x < W && (unsigned)y < H)
        dst[((y & 3) << 13) + 90 * (y >> 2) + (x >> 3)] |= 0x80 >> (x & 7);
}
static inline void pclr(int x, int y)
{
    if ((unsigned)x < W && (unsigned)y < H)
        dst[((y & 3) << 13) + 90 * (y >> 2) + (x >> 3)] &= ~(0x80 >> (x & 7));
}

static void line(int x0, int y0, int x1, int y1)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    if (dx > 4 * W || -dy > 4 * H) return;          /* way off screen */
    for (;;) {
        pset(x0, y0);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void hline(int x0, int x1, int y) { for (int x = x0; x <= x1; x++) pset(x, y); }
static void vline(int x, int y0, int y1) { for (int y = y0; y <= y1; y++) pset(x, y); }
static void rect(int x0, int y0, int x1, int y1) { hline(x0, x1, y0); hline(x0, x1, y1); vline(x0, y0, y1); vline(x1, y0, y1); }

/* 8x8 text, scaled sx by sy; inverse = clear the pixels (on a filled area) */
static void text(int x, int y, const char *s, int sx, int sy, int inverse)
{
    for (; *s; s++, x += 8 * sx) {
        const uint8_t *g = font + (uint8_t)*s * 8;
        for (int r = 0; r < 8; r++)
            for (int c = 0; c < 8; c++)
                if (g[r] & (0x80 >> c))
                    for (int j = 0; j < sy; j++)
                        for (int i = 0; i < sx; i++) {
                            if (inverse) pclr(x + c * sx + i, y + r * sy + j);
                            else pset(x + c * sx + i, y + r * sy + j);
                        }
    }
}
static void ctext(int y, const char *s, int sx, int sy) { text((W - (int)strlen(s) * 8 * sx) / 2, y, s, sx, sy, 0); }

static void fill(int x0, int y0, int x1, int y1, int pat)
{
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            int on;
            switch (pat) {
            case 0: on = 1; break;                                  /* solid */
            case 1: on = ((x + y) % 6) < 2; break;                  /* diagonal hatch */
            case 2: on = (x % 6 == 0) || (y % 4 == 0); break;       /* cross hatch */
            default: on = ((x ^ y) & 1) && (y & 1); break;          /* light dots */
            }
            if (on) pset(x, y);
        }
}

/* ------------------------------------------------------- scene 1: the chip */

struct v3 { float x, y, z; };
#define MAXL 400
static struct v3 la[MAXL], lb[MAXL];
static int nl;
static void seg(float x0, float y0, float z0, float x1, float y1, float z1)
{
    if (nl >= MAXL) return;
    la[nl] = (struct v3){ x0, y0, z0 };
    lb[nl] = (struct v3){ x1, y1, z1 };
    nl++;
}

/* letter strokes on the top face, in a 4x6 cell (u right, v up) */
static const signed char glyph_A[] = { 0,0, 2,6,  2,6, 4,0,  1,3, 3,3, -1 };
static const signed char glyph_R[] = { 0,0, 0,6,  0,6, 3,6,  3,6, 4,5,  4,5, 4,4,  4,4, 3,3,  3,3, 0,3,  2,3, 4,0, -1 };
static const signed char glyph_M[] = { 0,0, 0,6,  0,6, 2,3,  2,3, 4,6,  4,6, 4,0, -1 };

static void build_chip(void)
{
    const float B = 20, T = 3;                  /* half width, half thickness */
    float c[8][3];
    for (int i = 0; i < 8; i++) { c[i][0] = (i & 1) ? B : -B; c[i][1] = (i & 2) ? T : -T; c[i][2] = (i & 4) ? B : -B; }
    static const int e[12][2] = { {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7} };
    for (int i = 0; i < 12; i++) seg(c[e[i][0]][0], c[e[i][0]][1], c[e[i][0]][2], c[e[i][1]][0], c[e[i][1]][1], c[e[i][1]][2]);
    /* the pin-1 dot and a bevel line on the top */
    for (int k = 0; k < 8; k++) {
        float a0 = k * 6.2832f / 8, a1 = (k + 1) * 6.2832f / 8;
        seg(15 + 1.5f * cosf(a0), T, 14 + 1.5f * sinf(a0), 15 + 1.5f * cosf(a1), T, 14 + 1.5f * sinf(a1));
    }
    seg(-B + 2, T, B - 2, B - 2, T, B - 2);
    /* 12 gull-wing pins per side */
    for (int side = 0; side < 4; side++)
        for (int p = 0; p < 12; p++) {
            float t = -16.5f + p * 3.0f, w = 0.6f;
            for (int h = -1; h <= 1; h += 2) {
                float q = t + h * w;
                float pts[4][2] = { { B, 0 }, { B + 2, 0 }, { B + 3, -5 }, { B + 5, -5 } };   /* (out, y) */
                for (int s = 0; s < 3; s++) {
                    float o0 = pts[s][0], y0 = pts[s][1], o1 = pts[s + 1][0], y1 = pts[s + 1][1];
                    float X0, Z0, X1, Z1;
                    switch (side) {
                    case 0: X0 = o0; Z0 = q; X1 = o1; Z1 = q; break;
                    case 1: X0 = -o0; Z0 = q; X1 = -o1; Z1 = q; break;
                    case 2: X0 = q; Z0 = o0; X1 = q; Z1 = o1; break;
                    default: X0 = q; Z0 = -o0; X1 = q; Z1 = -o1; break;
                    }
                    seg(X0, y0, Z0, X1, y1, Z1);
                }
            }
            /* the pin's tip */
            float o = B + 5;
            switch (side) {
            case 0: seg(o, -5, t - w, o, -5, t + w); break;
            case 1: seg(-o, -5, t - w, -o, -5, t + w); break;
            case 2: seg(t - w, -5, o, t + w, -5, o); break;
            default: seg(t - w, -5, -o, t + w, -5, -o); break;
            }
        }
    /* "ARM" on the top, 2.2 units per cell step */
    const signed char *gl[3] = { glyph_A, glyph_R, glyph_M };
    for (int l = 0; l < 3; l++) {
        const signed char *g = gl[l];
        float u0 = -13 + l * 9.5f, sc = 1.7f;     /* read right way up from the front */
        for (int k = 0; g[k] >= 0; k += 4)
            seg(-(u0 + g[k] * sc), T, g[k + 1] * sc - 7, -(u0 + g[k + 2] * sc), T, g[k + 3] * sc - 7);
    }
    /* the die outline under "ARM" */
    seg(-15, T, -10, 15, T, -10);
}

static int proj(struct v3 p, float ca, float sa, float cb, float sb, int *sx, int *sy)
{
    float x = p.x * ca - p.z * sa, z = p.x * sa + p.z * ca;       /* spin (Y axis) */
    float y = p.y * cb - z * sb;  z = p.y * sb + z * cb;           /* tilt (X axis) */
    z += 95;
    if (z < 5) return 0;
    /* Hercules pixels are ~1.5x taller than wide: stretch x to keep it square */
    *sx = W / 2 + (int)(x * 400 * 1.45f / z);
    *sy = H / 2 - 22 - (int)(y * 400 / z);
    return 1;
}

static void grid(int frame)
{
    /* an 80s perspective floor below the chip, scrolling towards the viewer */
    const int horizon = 226;
    for (int k = 0; k < 8; k++) {
        float z = 1.0f + (float)(k * 16 + 16 - (frame % 16)) / 16.0f;      /* 1..9 */
        int y = horizon + (int)(106.0f / z);
        if (y < H - 14) hline(0, W - 1, y);
    }
    for (int k = -12; k <= 12; k++) {
        int xb = W / 2 + k * 60 * 3, xt = W / 2 + k * 14;
        line(xt, horizon + 12, xb, H - 15);
    }
}

static int scene_chip(void)
{
    build_chip();
    int p = 1, frame = 0;
    for (;;) {
        dst = page[p];
        memset(dst, 0, 0x8000);
        float a = frame * 0.035f, b = 0.45f + 0.25f * sinf(frame * 0.021f);
        float ca = cosf(a), sa = sinf(a), cb = cosf(b), sb = sinf(b);
        grid(frame);
        for (int i = 0; i < nl; i++) {
            int x0, y0, x1, y1;
            if (proj(la[i], ca, sa, cb, sb, &x0, &y0) && proj(lb[i], ca, sa, cb, sb, &x1, &y1)) line(x0, y0, x1, y1);
        }
        /* title bar (reverse) and footer */
        fill(0, 0, W - 1, 19, 0);
        text(12, 2, "HERCULES GRAPHICS CARD", 2, 2, 1);
        text(W - 12 - 14 * 8, 6, "720 x 348 MONO", 1, 1, 1);
        hline(0, W - 1, H - 14);
        text(8, H - 10, "EUROPA ARM/AT  -  ARM926 @ 100 MHz", 1, 1, 0);
        text(W - 8 - 29 * 8, H - 10, "SPACE: next scene   ESC: DOS", 1, 1, 0);
        show(p);
        vsync();
        p ^= 1;
        frame++;
        if (kbhit()) {
            int k = getch();
            if (k == 0) { getch(); continue; }
            if (k == 27) return 0;
            if (k == ' ' || k == '\r') return 1;
        }
    }
}

/* -------------------------------------------------- scene 2: the chart */

static void scene_chart(void)
{
    static const char *const series[3] = { "ARM/AT", "ARM/XT", "Portable" };
    static const int val[3][4] = { { 120, 210, 340, 460 }, { 90, 110, 130, 120 }, { 0, 40, 95, 180 } };
    static const char *const q[4] = { "Q1 '88", "Q2 '88", "Q3 '88", "Q4 '88" };
    dst = page[0];
    memset(page[0], 0, 0x8000);
    show(0);
    ctext(6, "EUROPA MICRO SYSTEMS", 2, 2);
    ctext(26, "Units shipped by quarter, 1988", 1, 1);
    const int x0 = 96, x1 = 690, y0 = 50, y1 = 280;          /* plot area */
    /* y axis: 0..500 */
    vline(x0, y0, y1);
    hline(x0, x1, y1);
    for (int v = 0; v <= 500; v += 100) {
        int y = y1 - (y1 - y0) * v / 500;
        char b[8];
        snprintf(b, sizeof b, "%3d", v);
        text(x0 - 34, y - 3, b, 1, 1, 0);
        hline(x0 - 4, x0, y);
        if (v) for (int x = x0 + 4; x < x1; x += 6) pset(x, y);   /* dotted grid line */
    }
    text(8, y0 + 60, "Thousands", 1, 1, 0);
    /* bars */
    const int groupw = (x1 - x0) / 4, barw = 34;
    for (int g = 0; g < 4; g++) {
        int gx = x0 + g * groupw + (groupw - 3 * barw - 2 * 6) / 2;
        for (int s = 0; s < 3; s++) {
            int bx = gx + s * (barw + 6);
            int top = y1 - (y1 - y0) * val[s][g] / 500;
            if (val[s][g]) { fill(bx, top, bx + barw, y1 - 1, s); rect(bx, top, bx + barw, y1); }
        }
        text(x0 + g * groupw + groupw / 2 - 24, y1 + 6, q[g], 1, 1, 0);
        vline(x0 + (g + 1) * groupw, y1, y1 + 3);
    }
    /* legend */
    int lx = 150;
    for (int s = 0; s < 3; s++) {
        fill(lx, 306, lx + 22, 318, s);
        rect(lx, 306, lx + 22, 318);
        text(lx + 30, 309, series[s], 1, 1, 0);
        lx += 160;
    }
    rect(0, 0, W - 1, H - 1);
    text(W - 16 - 20 * 8, H - 14, "any key: back to DOS", 1, 1, 0);
    /* wait for a key (a keypress while the chip spun has been read already) */
    while (!kbhit()) armdos_halt();
    if (getch() == 0) getch();
}

int main(void)
{
    if (!hercules_present()) {
        printf("This program requires a Hercules Graphics Card.\n"
               "(On the ARM/AT page: Monitor = Hercules Mono, then power-cycle.)\n");
        return 1;
    }
    font = (const uint8_t *)(*(volatile uint32_t *)(0x43 * 4));
    graphics_mode();
    if (scene_chip()) scene_chart();
    text_mode();
    printf("Hercules Graphics Card demo - Europa Micro Systems 1988\n");
    return 0;
}
