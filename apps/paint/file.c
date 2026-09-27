/* apps/paint/file.c - ARM Paint: ZSoft PCX (version 5, 8-bit, RLE) and Windows BMP. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <io.h>
#include "paint.h"

static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, unsigned v) { put16(p, v & 0xFFFF); put16(p + 2, v >> 16); }
static unsigned get16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static unsigned get32(const uint8_t *p) { return get16(p) | (get16(p + 2) << 16); }
static uint8_t to8(uint8_t v6) { return (uint8_t)((v6 << 2) | (v6 >> 4)); }

static int ext_is(const char *path, const char *ext)
{
    const char *d = strrchr(path, '.');
    if (!d || strchr(d, '\\')) return 0;
    for (d++; *d && *ext; d++, ext++) if ((*d & ~0x20) != *ext) return 0;
    return !*d && !*ext;
}

/* One scan line, RLE: a count byte 0xC1..0xFF precedes a repeated byte;
 * single bytes below 0xC0 stand for themselves. */
int pcx_encode_line(const uint8_t *src, int n, uint8_t *out)
{
    int o = 0;
    for (int i = 0; i < n;) {
        int run = 1;
        while (i + run < n && run < 63 && src[i + run] == src[i]) run++;
        if (run > 1 || src[i] >= 0xC0) out[o++] = (uint8_t)(0xC0 | run);
        out[o++] = src[i];
        i += run;
    }
    return o;
}

static int write_all(int fd, const void *p, unsigned n)
{
    return write(fd, p, n) == (int)n ? 0 : -1;
}

static int save_pcx(int fd)
{
    uint8_t h[128];
    memset(h, 0, sizeof h);
    h[0] = 0x0A; h[1] = 5; h[2] = 1; h[3] = 8;
    put16(h + 8, W - 1); put16(h + 10, H - 1);
    put16(h + 12, 320); put16(h + 14, 200);          /* "DPI": PC Paintbrush wrote the screen size */
    for (int i = 0; i < 48; i++) h[16 + i] = to8(pal[i]);
    h[65] = 1; put16(h + 66, W); put16(h + 68, 1);
    put16(h + 70, 320); put16(h + 72, 200);
    if (write_all(fd, h, 128)) return -1;
    static uint8_t buf[W * 2 * 8];
    int o = 0;
    for (int y = 0; y < H; y++) {
        o += pcx_encode_line(img + y * W, W, buf + o);
        if (o > (int)sizeof buf - W * 2 || y == H - 1) { if (write_all(fd, buf, o)) return -1; o = 0; }
    }
    uint8_t p[769];
    p[0] = 0x0C;
    for (int i = 0; i < 768; i++) p[1 + i] = to8(pal[i]);
    return write_all(fd, p, 769);
}

static int save_bmp(int fd)
{
    uint8_t h[54 + 1024];
    memset(h, 0, sizeof h);
    unsigned off = 54 + 1024, size = off + W * H;
    h[0] = 'B'; h[1] = 'M'; put32(h + 2, size); put32(h + 10, off);
    put32(h + 14, 40); put32(h + 18, W); put32(h + 22, H); put16(h + 26, 1); put16(h + 28, 8);
    put32(h + 34, W * H); put32(h + 38, 2835); put32(h + 42, 2835); put32(h + 46, 256); put32(h + 50, 256);
    for (int i = 0; i < 256; i++) {
        h[54 + i * 4] = to8(pal[i * 3 + 2]); h[55 + i * 4] = to8(pal[i * 3 + 1]); h[56 + i * 4] = to8(pal[i * 3]);
    }
    if (write_all(fd, h, sizeof h)) return -1;
    for (int y = H - 1; y >= 0; y--) if (write_all(fd, img + y * W, W)) return -1;
    return 0;
}

int save_picture(const char *path)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0666);
    if (fd < 0) return -1;
    int r = ext_is(path, "BMP") ? save_bmp(fd) : save_pcx(fd);
    if (close(fd) < 0) r = -1;
    return r;
}

/* ------------------------------------------------------------------ loading */
static void place(int w, int h, char *msg)
{
    if (w > W || h > H) sprintf(msg, "%dx%d, cropped", w, h);
    else if (w < W || h < H) sprintf(msg, "Picture %dx%d", w, h);
}

static int load_pcx(const uint8_t *f, unsigned n, char *msg)
{
    if (n < 128 || f[0] != 0x0A || f[2] != 1) return -2;
    int w = (int)get16(f + 8) - (int)get16(f + 4) + 1, h = (int)get16(f + 10) - (int)get16(f + 6) + 1;
    int bpp = f[3], planes = f[65], bpl = (int)get16(f + 66);
    if (w <= 0 || h <= 0 || bpl <= 0) return -2;
    int mono = 0;
    if (bpp == 8 && planes == 1) {
        if (n < 128 + 769 || f[n - 769] != 0x0C) return -3;
        for (int i = 0; i < 768; i++) pal[i] = f[n - 768 + i] >> 2;
    } else if (bpp == 1 && planes >= 1 && planes <= 4) {
        memcpy(pal, defpal, 768);
        if (planes == 1) { mono = 1; memset(pal, 0, 3); memset(pal + 3, 63, 3); }
        else for (int i = 0; i < 48; i++) pal[i] = f[16 + i] >> 2;
    } else return -3;
    int lw = bpl * planes;
    uint8_t *line = malloc(lw);
    if (!line) return -4;
    memset(img, 0, W * H);
    unsigned p = 128, end = bpp == 8 ? n - 769 : n;
    for (int y = 0; y < h; y++) {
        for (int i = 0; i < lw;) {
            if (p >= end) { line[i++] = 0; continue; }
            uint8_t b = f[p++];
            int cnt = 1;
            if ((b & 0xC0) == 0xC0) { cnt = b & 0x3F; b = p < end ? f[p++] : 0; }
            while (cnt-- > 0 && i < lw) line[i++] = b;
        }
        if (y >= H) continue;
        for (int x = 0; x < w && x < W; x++) {
            int v;
            if (bpp == 8) v = line[x];
            else {
                v = 0;
                for (int k = 0; k < planes; k++) if (line[k * bpl + (x >> 3)] & (0x80 >> (x & 7))) v |= 1 << k;
            }
            img[y * W + x] = (uint8_t)v;
        }
    }
    free(line);
    (void)mono;
    place(w, h, msg);
    return 0;
}

static int load_bmp(const uint8_t *f, unsigned n, char *msg)
{
    if (n < 54 || f[0] != 'B' || f[1] != 'M') return -2;
    unsigned off = get32(f + 10), hs = get32(f + 14);
    if (hs < 40) return -3;
    int w = (int)get32(f + 18), h = (int)get32(f + 22);
    int bpp = get16(f + 28), comp = (int)get32(f + 30);
    unsigned used = get32(f + 46);
    int topdown = h < 0;
    if (topdown) h = -h;
    if (w <= 0 || h <= 0 || !(bpp == 8 || bpp == 4) || !(comp == 0 || (comp == 1 && bpp == 8))) return -3;
    if (!used || used > (1u << bpp)) used = 1u << bpp;
    memcpy(pal, defpal, 768);
    for (unsigned i = 0; i < used; i++) {
        const uint8_t *q = f + 14 + hs + i * 4;
        if (q + 3 > f + n) break;
        pal[i * 3] = q[2] >> 2; pal[i * 3 + 1] = q[1] >> 2; pal[i * 3 + 2] = q[0] >> 2;
    }
    memset(img, 0, W * H);
    if (off >= n) return -3;
#define PUT(x, y, v) do { int yy_ = topdown ? (y) : h - 1 - (y); \
        if ((x) < W && yy_ < H && (x) >= 0 && yy_ >= 0) img[yy_ * W + (x)] = (uint8_t)(v); } while (0)
    if (comp == 0) {
        unsigned stride = ((unsigned)(w * bpp + 31) / 32) * 4;
        for (int y = 0; y < h; y++) {
            const uint8_t *row = f + off + (unsigned)y * stride;
            if (row + stride > f + n) break;
            for (int x = 0; x < w && x < W; x++)
                PUT(x, y, bpp == 8 ? row[x] : (x & 1 ? row[x >> 1] & 15 : row[x >> 1] >> 4));
        }
    } else {                                                      /* RLE8 */
        unsigned p = off;
        int x = 0, y = 0;
        while (p + 1 < n && y < h) {
            int a = f[p++], b = f[p++];
            if (a) { while (a--) { PUT(x, y, b); x++; } continue; }
            if (b == 0) { x = 0; y++; }
            else if (b == 1) break;
            else if (b == 2) { if (p + 1 >= n) break; x += f[p]; y += f[p + 1]; p += 2; }
            else { for (int i = 0; i < b && p < n; i++) { PUT(x, y, f[p]); x++; p++; } if (b & 1) p++; }
        }
    }
#undef PUT
    place(w, h, msg);
    return 0;
}

/* 0 ok, -1 cannot open, -2 not a picture, -3 unsupported kind, -4 memory */
int load_picture(const char *path, char *msg)
{
    msg[0] = 0;
    int fd = open(path, O_RDONLY | O_BINARY);
    if (fd < 0) return -1;
    long n = filelength(fd);
    if (n <= 0 || n > 4L * 1024 * 1024) { close(fd); return -2; }
    uint8_t *f = malloc((size_t)n);
    if (!f) { close(fd); return -4; }
    long got = 0;
    while (got < n) {
        int r = read(fd, f + got, (unsigned)(n - got > 32768 ? 32768 : n - got));
        if (r <= 0) break;
        got += r;
    }
    close(fd);
    uint8_t savepal[768];
    memcpy(savepal, pal, 768);
    int r = -2;
    if (got >= 2 && f[0] == 'B' && f[1] == 'M') r = load_bmp(f, (unsigned)got, msg);
    else if (got >= 1 && f[0] == 0x0A) r = load_pcx(f, (unsigned)got, msg);
    if (r) memcpy(pal, savepal, 768);
    free(f);
    return r;
}
