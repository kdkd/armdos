/* viewer.c - the picture viewer: VGA mode 13h, a GIF decoder fed byte by byte as the
 * picture comes down the line, interlaced passes drawn coarse first (each row of a pass
 * fills the rows below it until a later pass arrives), a status line with bytes and CPS.
 * Like the CompuServe viewers of the day, you watch the picture arrive. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include <fcntl.h>
#include <io.h>
#include <direct.h>
#include "online.h"

#define VGA ((volatile uint8_t *)0xA0000)
#define PIC_H 184               /* rows 184-199: the status line */
enum { C_BLACK = 240, C_DGREY, C_GREY, C_WHITE, C_BLUE, C_LBLUE, C_CYAN, C_YELLOW, C_RED, C_GREEN };
static const uint8_t ui_pal[16][3] = {
    {0,0,0},{85,85,85},{170,170,170},{255,255,255},{0,0,170},{85,85,255},{0,170,170},{255,255,85},
    {170,0,0},{0,170,0},{255,85,85},{85,255,85},{170,85,0},{170,0,170},{85,255,255},{255,255,255} };

int viewer_pending;
static struct {
    int active, done, failed;
    char name[16], caption[80];
    int w, h;                   /* announced size */
    uint32_t size, got;
    uint8_t *data;              /* the whole GIF, for saving */
    uint32_t t0;
    /* GIF parser */
    int st, cnt, blk, sub;
    uint8_t hdr[16];
    int gct, gctn, palpos;
    int iw, ih, ix, iy, inter, lct, lctn;
    /* LZW */
    int minsz, size_, clear, eoi, next, prev, first;
    uint32_t acc; int nbits;
    uint16_t prefix[4096];
    uint8_t suffix[4096], stack[4097];
    /* output position */
    int x, y, pass, rowsdone, ox, oy;
} V;

static const uint8_t *font8;

static void vmode(int m)
{
    union REGS r;
    r.x.ax = (unsigned)m;
    int86(0x10, &r, &r);
}

static void dac(int i, int r, int g, int b)
{
    armdos_outb(0x3C8, (uint8_t)i);
    armdos_outb(0x3C9, (uint8_t)(r >> 2));
    armdos_outb(0x3C9, (uint8_t)(g >> 2));
    armdos_outb(0x3C9, (uint8_t)(b >> 2));
}

static void get_font(void)
{
    /* INT 10h AX=1130h BH=3: the 8x8 font; ARM-DOS returns ES:BP as a flat pointer in BP (r6) */
    struct armregs a;
    memset(&a, 0, sizeof a);
    a.r0 = 0x1130; a.r1 = 0x0300;
    _armdos_int10(&a);
    font8 = (const uint8_t *)a.r6;
}

static void gtext(int x, int y, int fg, int bg, const char *s)
{
    for (; *s && x <= 312; s++, x += 8) {
        const uint8_t *g = font8 ? font8 + (uint8_t)*s * 8 : NULL;
        for (int r = 0; r < 8; r++) {
            uint8_t bits = g ? g[r] : 0;
            volatile uint8_t *p = VGA + (y + r) * 320 + x;
            for (int c = 0; c < 8; c++) p[c] = (bits & (0x80 >> c)) ? (uint8_t)fg : (uint8_t)bg;
        }
    }
}

static void gfill(int x, int y, int w, int h, int c)
{
    for (int j = 0; j < h; j++) memset((void *)(VGA + (y + j) * 320 + x), c, w);
}

static void status(void)
{
    char a[48], b[48];
    uint32_t el = TICKS() - V.t0;
    uint32_t cps = el > 9 ? V.got * 182u / (el * 10u) : 0;
    gfill(0, PIC_H, 320, 16, C_BLUE);
    if (V.failed) snprintf(a, sizeof a, "%s: bad picture data", V.name);
    else if (V.done) snprintf(a, sizeof a, "%.12s %lu bytes, %lu cps", V.name, (unsigned long)V.got, (unsigned long)cps);
    else snprintf(a, sizeof a, "%.12s %lu/%lu %lu cps", V.name, (unsigned long)V.got, (unsigned long)V.size, (unsigned long)cps);
    gtext(0, PIC_H, C_WHITE, C_BLUE, a);
    if (V.done) snprintf(b, sizeof b, "Complete.  S=Save  any key=Return");
    else snprintf(b, sizeof b, "Pass %d of 4 %3lu%%  Esc=Stop", V.inter ? V.pass + 1 : 1, V.size ? (unsigned long)(V.got * 100u / V.size) : 0UL);
    gtext(0, PIC_H + 8, V.done ? C_YELLOW : C_GREY, C_BLUE, b);
    /* a progress bar under the text */
    int w = V.size ? (int)((uint64_t)V.got * 320 / V.size) : 0;
    if (w > 320) w = 320;
    gfill(0, 199, w, 1, C_YELLOW);
}

/* ------------------------------------------------------------ GIF decoding */
static void put_row_fill(void)
{
    /* the row just finished stands in for the rows below it until their pass comes */
    static const int fill[4] = { 8, 4, 2, 1 };
    int f = V.inter ? fill[V.pass] : 1;
    int sy = V.oy + V.y;
    for (int k = 1; k < f && V.y + k < V.ih; k++)
        if (sy + k < PIC_H) memcpy((void *)(VGA + (sy + k) * 320 + V.ox), (const void *)(VGA + sy * 320 + V.ox), V.iw);
}

static void next_row(void)
{
    static const int start[4] = { 0, 4, 2, 1 }, step[4] = { 8, 8, 4, 2 };
    put_row_fill();
    V.rowsdone++;
    if (!V.inter) { V.y++; return; }
    V.y += step[V.pass];
    while (V.y >= V.ih && V.pass < 3) { V.pass++; V.y = start[V.pass]; }
}

static void pixel(uint8_t c)
{
    if (V.y >= V.ih) return;
    int sx = V.ox + V.x, sy = V.oy + V.y;
    if (sx >= 0 && sx < 320 && sy >= 0 && sy < PIC_H) VGA[sy * 320 + sx] = c;
    if (++V.x >= V.iw) { V.x = 0; next_row(); }
}

static void lzw_reset(void)
{
    V.size_ = V.minsz + 1;
    V.next = V.eoi + 1;
    V.prev = -1;
}

static void lzw_code(int code)
{
    if (code == V.clear) { lzw_reset(); return; }
    if (code == V.eoi) { V.st = 99; return; }
    int sp = 0, c = code;
    if (V.prev < 0) {
        if (code >= V.clear) { V.failed = 1; V.st = 99; return; }
        pixel((uint8_t)code); V.first = code; V.prev = code; return;
    }
    if (code > V.next || code >= 4096) { V.failed = 1; V.st = 99; return; }
    if (code == V.next) { V.stack[sp++] = (uint8_t)V.first; c = V.prev; }
    while (c > V.eoi) { V.stack[sp++] = V.suffix[c]; c = V.prefix[c]; if (sp >= 4096) { V.failed = 1; V.st = 99; return; } }
    V.stack[sp++] = (uint8_t)c;
    V.first = c;
    while (sp) pixel(V.stack[--sp]);
    if (V.next < 4096) {
        V.prefix[V.next] = (uint16_t)V.prev;
        V.suffix[V.next] = (uint8_t)V.first;
        V.next++;
        if (V.next == (1 << V.size_) && V.size_ < 12) V.size_++;
    }
    V.prev = code;
}

static void set_palette_entry(int i, uint8_t r, uint8_t g, uint8_t b)
{
    if (i < 240) dac(i, r, g, b);
}

/* states: 0 header(13) 1 GCT 2 block type 3 ext label 4 ext sub-len 5 ext data
 *         6 image descriptor(9) 7 LCT 8 LZW min size 9 sub-len 10 sub data 99 done */
static void gif_byte(uint8_t b)
{
    switch (V.st) {
    case 0:
        V.hdr[V.cnt++] = b;
        if (V.cnt == 13) {
            if (memcmp(V.hdr, "GIF", 3)) { V.failed = 1; V.st = 99; break; }
            V.gct = V.hdr[10] & 0x80; V.gctn = 2 << (V.hdr[10] & 7);
            V.cnt = 0; V.palpos = 0;
            V.st = V.gct ? 1 : 2;
        }
        break;
    case 1: case 7:
        V.hdr[V.cnt++] = b;
        if (V.cnt == 3) { set_palette_entry(V.palpos, V.hdr[0], V.hdr[1], V.hdr[2]); V.palpos++; V.cnt = 0; }
        if (V.palpos == (V.st == 1 ? V.gctn : V.lctn)) V.st = V.st == 1 ? 2 : 8;
        break;
    case 2:
        if (b == 0x21) V.st = 3;
        else if (b == 0x2C) { V.st = 6; V.cnt = 0; }
        else V.st = 99;
        break;
    case 3: V.st = 4; break;
    case 4: if (b == 0) V.st = 2; else { V.sub = b; V.st = 5; } break;
    case 5: if (--V.sub == 0) V.st = 4; break;
    case 6:
        V.hdr[V.cnt++] = b;
        if (V.cnt == 9) {
            V.ix = V.hdr[0] | (V.hdr[1] << 8); V.iy = V.hdr[2] | (V.hdr[3] << 8);
            V.iw = V.hdr[4] | (V.hdr[5] << 8); V.ih = V.hdr[6] | (V.hdr[7] << 8);
            V.inter = (V.hdr[8] & 0x40) != 0;
            V.lct = V.hdr[8] & 0x80; V.lctn = 2 << (V.hdr[8] & 7);
            /* centre the picture in the 320x184 area */
            V.ox = (320 - V.iw) / 2; V.oy = (PIC_H - V.ih) / 2;
            if (V.ox < 0) V.ox = 0;
            if (V.oy < 0) V.oy = 0;
            V.x = V.y = V.pass = 0;
            V.cnt = 0; V.palpos = 0;
            V.st = V.lct ? 7 : 8;
        }
        break;
    case 8:
        V.minsz = b < 2 || b > 11 ? 8 : b;
        V.clear = 1 << V.minsz; V.eoi = V.clear + 1;
        lzw_reset(); V.acc = 0; V.nbits = 0;
        V.st = 9;
        break;
    case 9: if (b == 0) V.st = 2; else { V.sub = b; V.st = 10; } break;
    case 10:
        V.acc |= (uint32_t)b << V.nbits; V.nbits += 8;
        while (V.st == 10 && V.nbits >= V.size_) {
            int code = (int)(V.acc & ((1u << V.size_) - 1));
            V.acc >>= V.size_; V.nbits -= V.size_;
            lzw_code(code);
        }
        if (V.st == 10 && --V.sub == 0) V.st = 9;
        else if (V.st == 99) { /* EOI inside the block: skip the rest */ }
        break;
    default: break;
    }
}

/* ------------------------------------------------------------ the frames */
void viewer_begin(const uint8_t *p, int n)
{
    free(V.data);
    memset(&V, 0, sizeof V);
    int k = 0;
    while (k < n && p[k]) k++;
    snprintf(V.name, sizeof V.name, "%.*s", k, (const char *)p);
    int c0 = k + 1, c1 = c0;
    while (c1 < n && p[c1]) c1++;
    snprintf(V.caption, sizeof V.caption, "%.*s", c1 - c0, (const char *)p + c0);
    const uint8_t *q = p + c1 + 1;
    if (q + 8 <= p + n) {
        V.w = q[0] | (q[1] << 8); V.h = q[2] | (q[3] << 8);
        V.size = (uint32_t)q[4] | ((uint32_t)q[5] << 8) | ((uint32_t)q[6] << 16) | ((uint32_t)q[7] << 24);
    }
    if (V.size && V.size < 512u * 1024u) V.data = malloc(V.size);
    V.active = 1;
    viewer_pending = 1;
    V.t0 = TICKS();
    /* into mode 13h at once: the picture is drawn as it arrives */
    vmode(0x13);
    if (!font8) get_font();
    for (int i = 0; i < 16; i++) dac(240 + i, ui_pal[i][0], ui_pal[i][1], ui_pal[i][2]);
    for (int i = 0; i < 240; i++) dac(i, 0, 0, 0);
    gfill(0, 0, 320, PIC_H, C_BLACK);
    status();
}

void viewer_byte(uint8_t b)
{
    if (!V.active) return;
    if (V.data && V.got < V.size) V.data[V.got] = b;
    V.got++;
    if (V.st != 99) gif_byte(b);
}

void viewer_end(void)
{
    if (!V.active) return;
    V.done = 1;
}

static int save_gif(void)
{
    char path[80];
    if (!V.data || V.got < V.size) return 0;
    mkdir("C:\\ONLINE\\PICTURES");
    snprintf(path, sizeof path, "C:\\ONLINE\\PICTURES\\%s", V.name);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0644);
    if (fd < 0) return 0;
    int ok = write(fd, V.data, V.size) == (int)V.size;
    close(fd);
    return ok;
}

void viewer_run(void)
{
    uint32_t last = 0;
    int saved = 0;
    viewer_pending = 0;
    for (;;) {
        net_poll();
        if (TICKS() - last >= 4 || (V.done && last != 0xFFFFFFFFu)) {
            status();
            if (saved) gtext(0, PIC_H + 8, C_GREEN, C_BLUE, saved > 0 ? "Saved in C:\\ONLINE\\PICTURES       " : "Could not save the picture.       ");
            last = V.done ? 0xFFFFFFFFu : TICKS();
        }
        if (!net.carrier) break;
        if (key_ready()) {
            int k = key_get();
            if (V.done && (k == 's' || k == 'S') && !saved) { saved = save_gif() ? 1 : -1; last = 0; continue; }
            if (!V.done) net_cancel();
            break;
        }
        idle();
    }
    V.active = 0;
    free(V.data); V.data = NULL;
    vmode(0x03);
    scr_cursor_on(0);
}
