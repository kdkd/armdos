/*
 * DEMO.EXE - "ARM-DOS 4.00: the demo". A 1990-style intro in VGA mode 13h:
 * starfield, plasma, rotozoomer, tunnel, copper bars, a sine scroller and a
 * PC speaker tune played from INT 08h. Hot loops in ARM assembly (fx.S).
 *
 *   DEMO [/Q] [/P n] [/T s]
 *     /Q     quiet (no music)       /P n   start with part n (0-4)
 *     /T s   quit by itself after s seconds
 *   Esc quits, Space skips to the next part.
 *
 * Like the intros it imitates it reprograms the PIT (1120 Hz: the music runs
 * at 70 Hz, the BIOS clock still gets its 18.2 Hz), waits for the vertical
 * retrace on port 3DAh, then moves the frame to A0000h with LDM/STM, and
 * restores everything on the way out.
 *
 * Copyright (C) 1990 Europa Micro Systems (ARM-DOS project).
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "armdos.h"
#include "demo.h"

#define W 320
#define H 200
#define VGA ((uint8_t *)0xA0000)
#define TICK_HZ 1120
#define PIT_DIV 1065                    /* 1193182 / 1065 = 1120.4 Hz */

static uint8_t back[W * H] __attribute__((aligned(32)));
static int16_t sn[1024];                /* sine, Q14 */
#define SIN(a) sn[(a) & 1023]
#define COS(a) sn[((a) + 256) & 1023]
static uint8_t font[256 * 16];
static uint32_t colbits[256][8];        /* glyph columns, row 0 in bit 31 */
static uint8_t pal[768];                /* the current palette, 6 bits per gun */
static uint8_t *tex;                    /* 256 x 256, values 0-63 */
static uint32_t *ttab;                  /* tunnel: shade<<24 | dist<<8 | angle */
#define TW 400
#define TH 250
static uint8_t s1[576], s2[576], rowtab[256];

/* ================================================================ timer */

static volatile uint32_t ticks;
static uint32_t chain_acc, last_pit;
static armdos_vect_t old08;

static void int08(struct armregs *f)
{
    ticks++;
    if (!(ticks & 15)) music_tick();    /* 70 Hz */
    chain_acc += PIT_DIV;
    if (chain_acc >= 65536) { chain_acc -= 65536; old08(f); }     /* BIOS clock, EOI */
    else armdos_outb(0x20, 0x20);
}

static void timer_start(void)
{
    old08 = armdos_getvect(0x08);
    armdos_disable();
    armdos_setvect(0x08, int08);
    armdos_outb(0x43, 0x34);
    armdos_outb(0x40, PIT_DIV & 0xFF);
    armdos_outb(0x40, PIT_DIV >> 8);
    armdos_enable();
}

static void timer_stop(void)
{
    armdos_disable();
    armdos_outb(0x43, 0x36);
    armdos_outb(0x40, 0);
    armdos_outb(0x40, 0);
    armdos_setvect(0x08, old08);
    armdos_enable();
}

/* PIT clocks since timer_start */
static uint32_t pit_now(void)
{
    uint32_t t1, t2, c;
    do {
        t1 = ticks;
        armdos_outb(0x43, 0x00);
        c = armdos_inb(0x40);
        c |= armdos_inb(0x40) << 8;
        t2 = ticks;
    } while (t1 != t2);
    if (c > PIT_DIV) c = PIT_DIV;
    uint32_t t = t1 * PIT_DIV + (PIT_DIV - c);
    if (t < last_pit) t += PIT_DIV;
    return last_pit = t;
}

static void wait_retrace(void)
{
    while (armdos_inb(0x3DA) & 8) armdos_halt();        /* let the current one end */
    while (!(armdos_inb(0x3DA) & 8)) armdos_halt();     /* wait for the next one */
}

/* ============================================================== tables */

static uint32_t seed = 0x19900601;
static uint32_t rnd(void) { seed = seed * 1103515245u + 12345u; return seed >> 8; }

static void make_sine(void)
{
    /* s(n+1) = 2cos(w) s(n) - s(n-1), Q30, w = 2 pi / 1024 */
    int64_t a = 0, b = 6588356, c2 = 2147443222LL;
    sn[0] = 0;
    for (int n = 1; n < 1024; n++) {
        sn[n] = (int16_t)(b >> 16);
        int64_t nb = ((c2 * b) >> 30) - a;
        a = b; b = nb;
    }
}

static const uint16_t atan_tab[15] = { 8192, 4836, 2555, 1297, 651, 326, 163, 81, 41, 20, 10, 5, 3, 1, 1 };

/* CORDIC: the angle of (x, y), 65536 = full circle */
static unsigned atan2_16(int y, int x)
{
    int ang = 0;
    if (x < 0) { x = -x; y = -y; ang = 32768; }
    x <<= 10; y <<= 10;
    for (int i = 0; i < 15; i++) {
        int nx;
        if (y > 0) { nx = x + (y >> i); y -= x >> i; ang += atan_tab[i]; }
        else { nx = x - (y >> i); y += x >> i; ang -= atan_tab[i]; }
        x = nx;
    }
    return (unsigned)ang & 0xFFFF;
}

static unsigned isqrt(unsigned v)
{
    unsigned r = 0, bit = 1u << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else r >>= 1;
        bit >>= 2;
    }
    return r;
}

static void draw_glyph_tex(int ch, int x0, int y0, int scale, uint8_t v)
{
    for (int r = 0; r < 16; r++)
        for (int c = 0; c < 8; c++)
            if (font[ch * 16 + r] & (0x80 >> c))
                for (int j = 0; j < scale; j++)
                    for (int i = 0; i < scale; i++)
                        tex[((y0 + r * scale + j) & 255) * 256 + ((x0 + c * scale + i) & 255)] = v;
}

static int make_tables(void)
{
    make_sine();
    /* font from the VGA character generator (loaded by the BIOS in text mode) */
    const volatile uint8_t *cg = (const volatile uint8_t *)0x11000000;
    for (int ch = 0; ch < 256; ch++)
        for (int r = 0; r < 16; r++) font[ch * 16 + r] = cg[ch * 32 + r];
    for (int ch = 0; ch < 256; ch++)
        for (int c = 0; c < 8; c++) {
            uint32_t b = 0;
            for (int r = 0; r < 16; r++) if (font[ch * 16 + r] & (0x80 >> c)) b |= 0x80000000u >> r;
            colbits[ch][c] = b;
        }
    /* plasma rows */
    for (int i = 0; i < 576; i++) {
        s1[i] = 32 + (SIN(i * 4) * 31 >> 14);
        s2[i] = 32 + (SIN(i * 1024 / 180) * 31 >> 14);
    }
    for (int i = 0; i < 256; i++) rowtab[i] = 32 + (SIN(i * 8) * 31 >> 14);

    /* texture: an XOR pattern with "ARM" on it */
    tex = malloc(65536);
    ttab = malloc(TW * TH * 4);
    if (!tex || !ttab) return -1;
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 256; x++) tex[y * 256 + x] = ((x ^ y) & 255) >> 3 >> 1 << 1 | (((x >> 4) ^ (y >> 4)) & 1);
    static const char logo[] = "ARM";
    for (int dy = -2; dy <= 2; dy += 2)
        for (int dx = -2; dx <= 2; dx += 2)
            for (int i = 0; i < 3; i++) draw_glyph_tex(logo[i], 128 - 60 + i * 40 + dx, 128 - 40 + dy, 5, 0);
    for (int i = 0; i < 3; i++) draw_glyph_tex(logo[i], 128 - 60 + i * 40, 128 - 40, 5, 63);
    for (int y = 0; y < 256; y++)       /* a gradient on the letters */
        for (int x = 0; x < 256; x++)
            if (tex[y * 256 + x] == 63) tex[y * 256 + x] = 36 + ((y - 88) & 127) * 27 / 80;

    /* tunnel */
    for (int y = 0; y < TH; y++)
        for (int x = 0; x < TW; x++) {
            int dx = x - TW / 2, dy = y - TH / 2;
            unsigned r = isqrt(dx * dx + dy * dy);
            if (r < 1) r = 1;
            unsigned d = (12288 / r) & 255;
            unsigned a = atan2_16(dy, dx) >> 8;
            int lvl = (int)(r * 4 + ((x & 1) * 2 + (y & 1)) * 40) / 150;
            if (lvl > 2) lvl = 2;
            unsigned band = 2 - lvl;
            ttab[y * TW + x] = (band * 64) << 24 | d << 8 | a;
        }
    return 0;
}

/* ============================================================ palettes */

static int bright = 0;                  /* 0-64 */
static int cycle;                       /* plasma colour cycling */
static int cycling;

static void set_rgb(int i, int r, int g, int b) { pal[i * 3] = r; pal[i * 3 + 1] = g; pal[i * 3 + 2] = b; }
static int clamp63(int v) { return v < 0 ? 0 : v > 63 ? 63 : v; }

static void common_palette(void)
{
    for (int k = 0; k < 8; k++) {           /* copper bars: 4 hues x 8 levels */
        int in = 12 + k * 7;
        set_rgb(192 + k, clamp63(in), clamp63(in / 3), 0);
        set_rgb(200 + k, in / 4, clamp63(in * 3 / 4), clamp63(in));
        set_rgb(208 + k, in / 5, clamp63(in), in / 3);
        set_rgb(216 + k, clamp63(in), in / 5, clamp63(in));
    }
    for (int k = 0; k < 16; k++) set_rgb(224 + k, 63, clamp63(63 - k * 3), clamp63(k * 4));    /* scroller */
    for (int k = 0; k < 8; k++) set_rgb(240 + k, 14 + k * 7, 14 + k * 7, 18 + k * 6);
}

static void plasma_palette(void)
{
    for (int i = 0; i < 192; i++) {
        int a = i * 1024 / 192;
        set_rgb(i, 32 + (SIN(a) * 31 >> 14), 32 + (SIN(a + 341) * 31 >> 14), 32 + (SIN(a + 683) * 31 >> 14));
    }
}

static void texture_palette(void)
{
    for (int i = 0; i < 32; i++)            /* the pattern: deep blue to cyan */
        set_rgb(i, i < 20 ? 0 : (i - 20) * 2, i * 3 / 2, 16 + i * 3 / 2);
    for (int i = 32; i < 64; i++) {         /* the letters: gold */
        int k = i - 32;
        set_rgb(i, clamp63(40 + k), clamp63(24 + k * 3 / 2), clamp63(k > 20 ? (k - 20) * 5 : 0));
    }
}

static void tunnel_palette(void)
{
    static const int pct[3] = { 100, 55, 22 };
    for (int band = 0; band < 3; band++)
        for (int i = 0; i < 64; i++) {
            int r = i < 32 ? 12 + i * 3 / 2 : 50 + (i - 32) / 3, g = i < 32 ? i / 2 : 30 + (i - 32);
            int b = i < 32 ? i / 4 : (i - 32) * 2;
            set_rgb(band * 64 + i, clamp63(r) * pct[band] / 100, clamp63(g) * pct[band] / 100,
                    clamp63(b) * pct[band] / 100);
        }
}

static void upload_palette(void)
{
    armdos_outb(0x3C8, 0);
    for (int i = 0; i < 256; i++) {
        int src = (cycling && i < 192) ? (i + cycle) % 192 : i;
        for (int k = 0; k < 3; k++) armdos_outb(0x3C9, pal[src * 3 + k] * bright >> 6);
    }
}

/* ============================================================ drawing */

/* colour: palette index; grad: add 0-7 from top to bottom of the glyphs */
static void textg(const char *s, int cx, int y, int scale, uint8_t colour, int grad)
{
    int n = strlen(s), x0 = cx - n * 8 * scale / 2;
    for (int i = 0; i < n; i++)
        for (int r = 0; r < 16; r++) {
            uint8_t bits = font[(uint8_t)s[i] * 16 + r];
            for (int c = 0; c < 8; c++)
                if (bits & (0x80 >> c))
                    for (int j = 0; j < scale; j++) {
                        int yy = y + r * scale + j, xx = x0 + (i * 8 + c) * scale;
                        if (yy < 0 || yy >= H) continue;
                        uint8_t cc = colour + (grad ? (r * scale + j) * 8 / (16 * scale) : 0);
                        for (int k = 0; k < scale; k++)
                            if ((unsigned)(xx + k) < W) back[yy * W + xx + k] = cc;
                    }
        }
}

static void text(const char *s, int cx, int y, int scale, uint8_t colour) { textg(s, cx, y, scale, colour, 0); }

#define NSTARS 300
static int32_t stx[NSTARS], sty[NSTARS], stz[NSTARS];
static uint32_t rz[1024];

static void stars_init(void)
{
    for (int z = 1; z < 1024; z++) rz[z] = 4194304 / z;
    for (int i = 0; i < NSTARS; i++) {
        stx[i] = (int)(rnd() % 5120) - 2560;
        sty[i] = (int)(rnd() % 3200) - 1600;
        stz[i] = 64 + rnd() % 960;
    }
}

static void stars(int speed)
{
    for (int i = 0; i < NSTARS; i++) {
        stz[i] -= speed;
        if (stz[i] < 64) { stz[i] += 960; stx[i] = (int)(rnd() % 5120) - 2560; sty[i] = (int)(rnd() % 3200) - 1600; }
        int z = stz[i];
        int px = 160 + (stx[i] * (int)rz[z] >> 16), py = 100 + (sty[i] * (int)rz[z] >> 16);
        if ((unsigned)px >= W - 1 || (unsigned)py >= H) continue;
        uint8_t c = 240 + 7 - (z >> 7);
        back[py * W + px] = c;
        if (z < 500) back[py * W + px + 1] = c;
        if (z < 200 && py + 1 < H) { back[(py + 1) * W + px] = c; back[(py + 1) * W + px + 1] = c; }
    }
}

static const char scrolltext[] =
    "        *** ARM-DOS 4.00 * THE DEMO ***     EUROPA MICRO SYSTEMS PROUDLY PRESENTS THE FIRST PC INTRO "
    "FOR THE ARM/AT, A 100 MHZ PERSONAL COMPUTER WITH AN ARM926EJ-S WHERE THE 8086 USED TO BE ...   "
    "NO 8086 WAS HARMED IN THE MAKING OF THIS INTRO: EVERY INSTRUCTION RUNNING RIGHT NOW IS ARM CODE.   "
    "THE INNER LOOPS ARE HAND-WRITTEN ARM ASSEMBLER ...   THE ROTOZOOMER STEPS U AND V WITH ONE ADD AND BUILDS "
    "EACH TEXEL ADDRESS WITH THE BARREL SHIFTER: FIVE INSTRUCTIONS A PIXEL.   THE TUNNEL LETS LDRB WRAP THE "
    "TEXTURE WITH A SHIFTED REGISTER OFFSET.   THIS SCROLLER USES CONDITIONAL EXECUTION: MOVS PUTS EACH FONT "
    "BIT IN THE CARRY FLAG AND STRCSB STORES ONLY WHERE IT WAS SET - NOT ONE BRANCH.   LDM AND STM MOVE THE "
    "64,000 BYTES OF EVERY FRAME TO THE VGA IN 2,000 INSTRUCTION PAIRS DURING THE VERTICAL RETRACE.   "
    "SMULBB, THE DSP MULTIPLY, BENDS THE SINES.   THE MUSIC IS PC SPEAKER, PLAYED FROM INT 08H WITH THE "
    "TIMER AT 1120 HZ - AS IT SHOULD BE.   GREETINGS TO EVERYONE WHO EVER TYPED  DEBUG  AND  G=100 ...   "
    "PRESS SPACE FOR THE NEXT PART, ESC TO GO BACK TO DOS.          ";

static void scroller(uint32_t t, int basey, int amp)
{
    static int len;
    if (!len) len = strlen(scrolltext);
    uint32_t pos = t / 8;                                  /* 140 pixels a second */
    for (int x = 0; x < W; x++) {
        uint32_t p = pos + x;
        uint32_t bits = colbits[(uint8_t)scrolltext[(p >> 4) % len]][(p >> 1) & 7];
        if (!bits) continue;
        int y = basey + (fx_smulbb(SIN(x * 3 + t / 3), amp) >> 14);
        fx_scroll_col(back + y * W + x, bits, 224);
    }
}

/* ================================================================ parts */

enum { P_INTRO, P_PLASMA, P_ROTO, P_TUNNEL, P_BARS, NPARTS };
static const uint16_t part_secs[NPARTS] = { 9, 18, 18, 18, 18 };
static uint32_t scroll_t0;

static void part_palette(int p)
{
    memset(pal, 0, sizeof pal);
    common_palette();
    cycling = 0;
    switch (p) {
    case P_PLASMA: plasma_palette(); cycling = 1; break;
    case P_ROTO: texture_palette(); break;
    case P_TUNNEL: tunnel_palette(); break;
    }
}

static int fade_level(uint32_t t, uint32_t len)
{
    const uint32_t f = TICK_HZ / 2;
    int b = 64;
    if (t < f) b = t * 64 / f;
    if (len - t < f) b = (len - t) * 64 / f;
    return b < 0 ? 0 : b > 64 ? 64 : b;
}

/* a line of text that fades in over 0.4 s and out over 0.4 s between from and to (seconds x 10) */
static int line_level(uint32_t t, int from, int to)
{
    uint32_t s = from * TICK_HZ / 10, e = to * TICK_HZ / 10, f = TICK_HZ * 4 / 10;
    if (t <= s || t >= e) return 0;
    if (t - s < f) return (t - s) * 63 / f;
    if (e - t < f) return (e - t) * 63 / f;
    return 63;
}

static void draw_intro(uint32_t t)
{
    fx_fill(back, 0, W * H);
    stars(2 + t / 250);
    int v;
    if ((v = line_level(t, 3, 33))) {                   /* the company */
        for (int k = 0; k < 8; k++) set_rgb(248 + k, v * (40 + k * 3) / 63, v * (40 + k * 3) / 63, v);
        textg("EUROPA", 160, 44, 4, 248, 1);
        text("MICRO SYSTEMS", 160, 116, 2, 255);
    } else if ((v = line_level(t, 34, 52))) {           /* presents */
        set_rgb(255, v * 3 / 4, v, v);
        text("presents", 160, 84, 2, 255);
    } else if ((v = line_level(t, 53, 95))) {           /* the title */
        for (int k = 0; k < 8; k++) set_rgb(248 + k, v, v * (63 - k * 5) / 63, v * k * 6 / 63);
        textg("ARM-DOS 4.00", 160, 50, 3, 248, 1);
        if (t > TICK_HZ * 68 / 10) {
            int w = t - TICK_HZ * 68 / 10;
            w = w > 400 ? 63 : w * 63 / 400;
            set_rgb(247, w * 3 / 4, w, w);
            text("the demo", 160, 120, 2, 247);
        }
    }
}

static void draw_plasma(uint32_t t)
{
    int t1 = t / 4, t2 = t / 7, t3 = t / 5;
    for (int y = 0; y < H; y++) {
        int o1 = (32 + (SIN(y * 5 + t1 * 3) * 32 >> 14) + t1) & 255;
        int o2 = (t2 * 2 - y) & 255;
        fx_plasma(back + y * W, s1 + o1, s2 + (o2 * 180 >> 8), rowtab[(y + t3) & 255]);
    }
    cycle = (t / 16) % 192;
}

static void draw_roto(uint32_t t)
{
    int ang = t / 4;
    int zoom = 320 + (SIN(t / 3) * 224 >> 14);                      /* 8.8 texels per pixel */
    int dudx = fx_smulbb(COS(ang), zoom) >> 14, dvdx = fx_smulbb(SIN(ang), zoom) >> 14;
    int dudy = -dvdx, dvdy = dudx;
    int cu = 128 * 256 + (SIN(t / 5) >> 3), cv = 128 * 256 + (COS(t / 7) >> 3);
    int u = cu - 160 * dudx - 100 * dudy, v = cv - 160 * dvdx - 100 * dvdy;
    uint32_t duv = (uint32_t)dudx * 65536u + (uint32_t)dvdx;
    for (int y = 0; y < H; y++) {
        fx_roto(back + y * W, tex, (uint32_t)u * 65536u + (uint32_t)v, duv);
        u += dudy; v += dvdy;
    }
}

static void draw_tunnel(uint32_t t)
{
    int ox = (TW - W) / 2 + (SIN(t / 3) * ((TW - W) / 2 - 1) >> 14);
    int oy = (TH - H) / 2 + (COS(t / 4) * ((TH - H) / 2 - 1) >> 14);
    uint32_t off = (((t / 3) & 255) << 8 | ((t / 9 + (SIN(t / 6) >> 8)) & 255)) << 16;
    for (int y = 0; y < H; y++) fx_tunnel(back + y * W, ttab + (y + oy) * TW + ox, tex, off, W);
}

static void draw_bars(uint32_t t)
{
    fx_fill(back, 0, W * H);
    stars(3);
    for (int b = 0; b < 4; b++) {
        int yb = 76 + (fx_smulbb(SIN(t / 2 + b * 110), 64) >> 14);
        for (int k = 0; k < 22; k++) {
            int row = yb + k - 11, d = k * 2 - 21;
            if (row < 0 || row >= H) continue;
            int lvl = 7 - (d < 0 ? -d : d) / 3;
            uint32_t c = 192 + b * 8 + (lvl < 0 ? 0 : lvl);
            fx_fill(back + row * W, c * 0x01010101u, W);
        }
    }
    static const char logo[] = "ARM/AT";
    for (int i = 0; i < 6; i++) {
        char s[2] = { logo[i], 0 };
        int y = 8 + (fx_smulbb(SIN(t / 2 + i * 90), 10) >> 14);
        textg(s, 160 - 3 * 32 + i * 32 + 16, y, 4, 248, 1);
    }
    for (int k = 0; k < 8; k++) set_rgb(248 + k, 63 - k * 2, 63 - k * 3, 63);
}

/* ================================================================= main */

static int key(void)
{
    struct armregs r = { 0 };
    r.r0 = 0x0100;
    _armdos_int16(&r);
    if (r.cpsr & ARM_CPSR_Z) return 0;
    r.r0 = 0;
    _armdos_int16(&r);
    return r.r0 & 0xFFFF;
}

static void set_mode(int m)
{
    struct armregs r = { 0 };
    r.r0 = m;
    _armdos_int10(&r);
}

int main(int argc, char **argv)
{
    int part = 0, quit_after = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] != '/' && a[0] != '-') continue;
        switch (a[1] | 0x20) {
        case 'q': music_enabled = 0; break;
        case 'p': part = atoi(a[2] ? a + 2 : i + 1 < argc ? argv[++i] : "0") % NPARTS; break;
        case 't': quit_after = atoi(a[2] ? a + 2 : i + 1 < argc ? argv[++i] : "0"); break;
        default:
            printf("DEMO [/Q] [/P n] [/T seconds]\n");
            return 1;
        }
    }
    struct armregs r = { 0 };
    r.r0 = 0x1A00;
    _armdos_int10(&r);
    if ((r.r0 & 0xFF) != 0x1A || (r.r1 & 0xFF) < 7) {
        printf("This demo requires a VGA.\n");
        return 1;
    }
    if (make_tables()) { printf("Not enough memory.\n"); return 8; }
    stars_init();

    set_mode(0x13);
    bright = 0;
    part_palette(part);
    upload_palette();
    timer_start();

    uint32_t start = pit_now(), busy = 0, frames = 0;
    uint32_t part_t0 = ticks;
    scroll_t0 = ticks;
    int quit = 0;
    while (!quit) {
        uint32_t f0 = pit_now();
        uint32_t now = ticks, t = now - part_t0, len = part_secs[part] * TICK_HZ;
        if (t >= len) {
            part = part + 1 >= NPARTS ? P_PLASMA : part + 1;
            part_t0 = now; t = 0; len = part_secs[part] * TICK_HZ;
            part_palette(part);
        }
        switch (part) {
        case P_INTRO: draw_intro(t); break;
        case P_PLASMA: draw_plasma(t); break;
        case P_ROTO: draw_roto(t); break;
        case P_TUNNEL: draw_tunnel(t); break;
        case P_BARS: draw_bars(t); break;
        }
        if (part != P_INTRO) scroller(now - scroll_t0, part == P_BARS ? 118 : 150, part == P_BARS ? 24 : 12);
        else scroll_t0 = now;
        bright = fade_level(t, len);
        busy += pit_now() - f0;

        wait_retrace();
        uint32_t f1 = pit_now();
        upload_palette();
        fx_copy(VGA, back, W * H);
        busy += pit_now() - f1;
        frames++;

        int k = key();
        if ((k & 0xFF) == 27) quit = 1;
        else if ((k & 0xFF) == ' ') part_t0 = ticks - len;
        if (quit_after && ticks > (uint32_t)quit_after * TICK_HZ) quit = 1;
    }
    uint32_t total = pit_now() - start;

    music_stop();
    timer_stop();
    set_mode(0x03);

    unsigned mhz = armdos_inb(0xF1);
    uint32_t ms = (uint32_t)((uint64_t)total * 1000 / 1193182);
    uint32_t pct = total ? (uint32_t)((uint64_t)busy * 100 / total) : 0;
    uint32_t fps10 = ms ? (uint32_t)((uint64_t)frames * 10000 / ms) : 0;
    printf("ARM-DOS 4.00: THE DEMO  (C) 1990 Europa Micro Systems\n\n");
    printf("%lu frames in %lu.%lu seconds: %lu.%lu frames per second.\n", (unsigned long)frames,
           (unsigned long)ms / 1000, (unsigned long)(ms % 1000) / 100, (unsigned long)fps10 / 10,
           (unsigned long)fps10 % 10);
    printf("CPU: %lu%% of an ARM926 at %u MHz.\n", (unsigned long)pct, mhz);
    return 0;
}
