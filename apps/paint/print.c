/* apps/paint/print.c - ARM Paint: the picture to an Epson FX-80 compatible
 * 9-pin printer on LPT1, as ESC * bit-image graphics.
 *
 * The 320x200 picture (4:3 on the screen) comes out 6.4" x 4.8": the grey of
 * every printer dot is sampled (bilinear) from the picture's luminance, then
 * dithered to black and white (Floyd-Steinberg, serpentine, or an 8x8 Bayer
 * matrix). Draft = ESC * 0 (60 x 72 dpi, 384 dots a line), letter quality =
 * ESC * 1 (120 x 72 dpi, 768 dots). Line spacing ESC 3 24 (24/216" = the 8
 * pins' height) makes the bands touch; each band is centred with 8 spaces.
 */
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include "paint.h"

#define ROWS 346                    /* 4.8" at 72 dpi */
#define ESC 0x1B

static int lpt_err;

int printer_status(void)
{
    union REGS r;
    r.x.ax = 0x0200; r.x.dx = 0;
    int86(0x17, &r, &r);
    return r.h.ah;
}

static void out(int b)
{
    if (lpt_err) return;
    union REGS r;
    r.x.ax = (unsigned)(b & 0xFF); r.x.dx = 0;       /* AH=00h: print AL on LPT1 */
    int86(0x17, &r, &r);
    if (r.h.ah & 0x29) lpt_err = r.h.ah;             /* time-out, I/O error, out of paper */
}
static void outs(const char *s) { while (*s) out((uint8_t)*s++); }

static const uint8_t bayer[8][8] = {
    { 0, 32,  8, 40,  2, 34, 10, 42}, {48, 16, 56, 24, 50, 18, 58, 26},
    {12, 44,  4, 36, 14, 46,  6, 38}, {60, 28, 52, 20, 62, 30, 54, 22},
    { 3, 35, 11, 43,  1, 33,  9, 41}, {51, 19, 59, 27, 49, 17, 57, 25},
    {15, 47,  7, 39, 13, 45,  5, 37}, {63, 31, 55, 23, 61, 29, 53, 21},
};

int print_picture(int quality, int dither, const char *title, int (*progress)(int band, int bands))
{
    const int cols = quality == PQ_LETTER ? 768 : 384;
    const int bands = (ROWS + 7) / 8;
    uint8_t lum[256];
    for (int i = 0; i < 256; i++)
        lum[i] = (uint8_t)((pal[i * 3] * 30 + pal[i * 3 + 1] * 59 + pal[i * 3 + 2] * 11) * 255 / (63 * 100));
    int16_t *e0 = calloc(cols + 2, sizeof(int16_t)), *e1 = calloc(cols + 2, sizeof(int16_t));
    uint8_t *band = malloc(cols);
    uint8_t *grey = malloc(cols);
    if (!e0 || !e1 || !band || !grey) { free(e0); free(e1); free(band); free(grey); return -2; }
    /* source x of every output column, 8.8 fixed point */
    lpt_err = 0;
    out(ESC); out('@');
    if (title && *title) { out(ESC); out('E'); outs(title); out(ESC); out('F'); }
    outs("\r\n\r\n");
    out(ESC); out('O');                               /* no skip over the perforation */
    out(ESC); out('3'); out(24);
    int cancelled = 0;
    for (int b = 0; b < bands && !lpt_err; b++) {
        if (progress && progress(b, bands)) { cancelled = 1; break; }
        memset(band, 0, cols);
        for (int k = 0; k < 8; k++) {
            int r = b * 8 + k;
            if (r >= ROWS) break;
            int sy = (int)(((2 * r + 1) * H * 256L) / (2 * ROWS)) - 128;
            if (sy < 0) sy = 0;
            int y0 = sy >> 8, fy = sy & 255, y1 = y0 + 1 < H ? y0 + 1 : y0;
            for (int x = 0; x < cols; x++) {
                int sx = (int)(((2 * x + 1) * W * 256L) / (2 * cols)) - 128;
                if (sx < 0) sx = 0;
                int x0 = sx >> 8, fx = sx & 255, x1 = x0 + 1 < W ? x0 + 1 : x0;
                int a = lum[img[y0 * W + x0]] * (256 - fx) + lum[img[y0 * W + x1]] * fx;
                int c = lum[img[y1 * W + x0]] * (256 - fx) + lum[img[y1 * W + x1]] * fx;
                grey[x] = (uint8_t)(((a >> 8) * (256 - fy) + (c >> 8) * fy) >> 8);
            }
            uint8_t bit = (uint8_t)(0x80 >> k);        /* bit 7 = the top pin */
            if (dither == DITHER_ORDERED) {
                for (int x = 0; x < cols; x++)
                    if (grey[x] * 64 < bayer[r & 7][x & 7] * 256 + 128) band[x] |= bit;
            } else {
                memset(e1, 0, (cols + 2) * sizeof(int16_t));
                int dir = (r & 1) ? -1 : 1;
                for (int i = 0; i < cols; i++) {
                    int x = dir > 0 ? i : cols - 1 - i;
                    int v = grey[x] + e0[x + 1];
                    int ink = v < 128;
                    int err = v - (ink ? 0 : 255);
                    if (ink) band[x] |= bit;
                    e0[x + 1 + dir] += (int16_t)(err * 7 / 16);
                    e1[x + 1 - dir] += (int16_t)(err * 3 / 16);
                    e1[x + 1] += (int16_t)(err * 5 / 16);
                    e1[x + 1 + dir] += (int16_t)(err / 16);
                }
                int16_t *t = e0; e0 = e1; e1 = t;
            }
        }
        int n = cols;
        while (n > 0 && !band[n - 1]) n--;             /* no need to carry the head over white */
        if (n) {
            outs("        ");                           /* 0.8" in: centred on the 8" line */
            out(ESC); out('*'); out(quality == PQ_LETTER ? 1 : 0); out(n & 0xFF); out(n >> 8);
            for (int x = 0; x < n; x++) out(band[x]);
        }
        out('\r'); out('\n');
    }
    out(ESC); out('2');
    outs("\r\n");
    if (cancelled) { out(ESC); out('@'); }
    free(e0); free(e1); free(band); free(grey);
    if (lpt_err) return -1;
    return cancelled ? 1 : 0;
}
