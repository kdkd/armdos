/* apps/paint/gfx.c - ARM Paint: drawing primitives on 320x200 byte buffers. */
#include <stdlib.h>
#include "paint.h"

static unsigned seed = 0x1988u;
unsigned rnd(void) { seed = seed * 1103515245u + 12345u; return (seed >> 8) & 0xFFFFFF; }

void pset(uint8_t *b, int x, int y, int c)
{
    if ((unsigned)x < W && (unsigned)y < H) b[y * W + x] = (uint8_t)c;
}

void hline(uint8_t *b, int x0, int x1, int y, int c)
{
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if ((unsigned)y >= H || x1 < 0 || x0 >= W) return;
    if (x0 < 0) x0 = 0;
    if (x1 >= W) x1 = W - 1;
    uint8_t *p = b + y * W;
    for (int x = x0; x <= x1; x++) p[x] = (uint8_t)c;
}

void vline(uint8_t *b, int x, int y0, int y1, int c)
{
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    for (int y = y0; y <= y1; y++) pset(b, x, y, c);
}

void fillrect(uint8_t *b, int x0, int y0, int x1, int y1, int c)
{
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    for (int y = y0; y <= y1; y++) hline(b, x0, x1, y, c);
}

void frame(uint8_t *b, int x0, int y0, int x1, int y1, int c)
{
    hline(b, x0, x1, y0, c); hline(b, x0, x1, y1, c);
    vline(b, x0, y0, y1, c); vline(b, x1, y0, y1, c);
}

/* a round brush: the pixels of a size x size box whose centres lie in the circle */
void stamp(uint8_t *b, int x, int y, int size, int c)
{
    if (size <= 1) { pset(b, x, y, c); return; }
    int o = size / 2;
    for (int j = 0; j < size; j++) {
        int dy = 2 * j - size + 1;
        for (int i = 0; i < size; i++) {
            int dx = 2 * i - size + 1;
            if (dx * dx + dy * dy <= size * size + (size > 2 ? size : 0)) pset(b, x - o + i, y - o + j, c);
        }
    }
}

void line(uint8_t *b, int x0, int y0, int x1, int y1, int c, int size)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        stamp(b, x0, y0, size, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void rectangle(uint8_t *b, int x0, int y0, int x1, int y1, int c, int filled, int size)
{
    if (filled) { fillrect(b, x0, y0, x1, y1, c); return; }
    if (size <= 1) { frame(b, x0, y0, x1, y1, c); return; }
    line(b, x0, y0, x1, y0, c, size); line(b, x1, y0, x1, y1, c, size);
    line(b, x1, y1, x0, y1, c, size); line(b, x0, y1, x0, y0, c, size);
}

/* the ellipse inscribed in the rectangle (Zingl's algorithm) */
void ellipse(uint8_t *b, int x0, int y0, int x1, int y1, int c, int filled, int size)
{
    long long a = abs(x1 - x0), bb = abs(y1 - y0), b1 = bb & 1;
    long long dx = 4 * (1 - a) * bb * bb, dy = 4 * (b1 + 1) * a * a;
    long long err = dx + dy + b1 * a * a, e2;
    if (x0 > x1) { x0 = x1; x1 += (int)a; }
    if (y0 > y1) y0 = y1;
    y0 += (int)((bb + 1) / 2); y1 = y0 - (int)b1;
    a *= 8 * a; b1 = 8 * bb * bb;
#define EP(px, py) (filled ? (void)0 : stamp(b, px, py, size, c))
    do {
        if (filled) { hline(b, x0, x1, y0, c); hline(b, x0, x1, y1, c); }
        EP(x1, y0); EP(x0, y0); EP(x0, y1); EP(x1, y1);
        e2 = 2 * err;
        if (e2 <= dy) { y0++; y1--; err += dy += a; }
        if (e2 >= dx || 2 * err > dy) { x0++; x1--; err += dx += b1; }
    } while (x0 <= x1);
    while (y0 - y1 < bb) {
        if (filled) { hline(b, x0 - 1, x1 + 1, y0, c); hline(b, x0 - 1, x1 + 1, y1, c); }
        EP(x0 - 1, y0); EP(x1 + 1, y0); y0++;
        EP(x0 - 1, y1); EP(x1 + 1, y1); y1--;
    }
#undef EP
}

/* scan-line flood fill of the 4-connected region of one colour */
void flood(uint8_t *b, int x, int y, int c)
{
    if ((unsigned)x >= W || (unsigned)y >= H) return;
    int old = b[y * W + x];
    if (old == c) return;
    int cap = 4096, n = 0;
    int16_t *st = malloc(cap * 2 * sizeof(int16_t));
    if (!st) return;
    st[n * 2] = (int16_t)x; st[n * 2 + 1] = (int16_t)y; n++;
    while (n) {
        n--;
        int sx = st[n * 2], sy = st[n * 2 + 1];
        uint8_t *row = b + sy * W;
        if (row[sx] != old) continue;
        int l = sx, r = sx;
        while (l > 0 && row[l - 1] == old) l--;
        while (r < W - 1 && row[r + 1] == old) r++;
        for (int i = l; i <= r; i++) row[i] = (uint8_t)c;
        for (int d = -1; d <= 1; d += 2) {
            int ny = sy + d;
            if ((unsigned)ny >= H) continue;
            uint8_t *nr = b + ny * W;
            int in = 0;
            for (int i = l; i <= r; i++) {
                if (nr[i] == old) {
                    if (!in) {
                        if (n >= cap) {
                            int16_t *ns = realloc(st, cap * 4 * sizeof(int16_t));
                            if (!ns) { free(st); return; }
                            st = ns; cap *= 2;
                        }
                        st[n * 2] = (int16_t)i; st[n * 2 + 1] = (int16_t)ny; n++;
                        in = 1;
                    }
                } else in = 0;
            }
        }
    }
    free(st);
}

void spray(uint8_t *b, int x, int y, int radius, int n, int c)
{
    while (n-- > 0) {
        int dx = (int)(rnd() % (2 * radius + 1)) - radius;
        int dy = (int)(rnd() % (2 * radius + 1)) - radius;
        if (dx * dx + dy * dy <= radius * radius) pset(b, x + dx, y + dy, c);
    }
}

void glyph8(uint8_t *b, int x, int y, int ch, int c)
{
    const uint8_t *g = font8 + (ch & 0xFF) * 8;
    for (int r = 0; r < 8; r++) {
        uint8_t bits = g[r];
        for (int i = 0; i < 8; i++) if (bits & (0x80 >> i)) pset(b, x + i, y + r, c);
    }
}

int text8(uint8_t *b, int x, int y, const char *s, int c)
{
    for (; *s; s++, x += 8) glyph8(b, x, y, (uint8_t)*s, c);
    return x;
}

void text8bg(uint8_t *b, int x, int y, const char *s, int c, int bg)
{
    for (; *s; s++, x += 8) { fillrect(b, x, y, x + 7, y + 7, bg); glyph8(b, x, y, (uint8_t)*s, c); }
}
