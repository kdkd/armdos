/* vt.h - ANSI-BBS / VT100 terminal emulator drawing into a text-mode cell
 * buffer (the screen at B800:0000, or an off-screen buffer). */
#ifndef VT_H
#define VT_H
#include <stdint.h>

struct vt {
    volatile uint16_t *cells;     /* w*h cells, char | attr<<8 */
    int w, h;
    int x, y, wrapnext;
    int fg, bg, bold, blink, rev, conceal, attr;
    int sx, sy, sfg, sbg, sbold, sblink, srev;
    int top, bot;                 /* scroll region, 0-based inclusive */
    int state, np, par[10], priv;
    int origin, autowrap;
    void *ctx;
    void (*scrolled)(void *ctx, const volatile uint16_t *line, int w);  /* line leaving the top */
    void (*reply)(void *ctx, const char *s);                             /* DSR/DA answers */
    void (*bell)(void *ctx);
    /* DCS strings (ESC P ... ESC \): passed whole to dcs(); ignored when it is NULL */
    void (*dcs)(void *ctx, const char *s);
    int dcsn;
    char dcsbuf[256];
};

void vt_init(struct vt *t, volatile uint16_t *cells, int w, int h);
void vt_reset(struct vt *t);
void vt_putc(struct vt *t, int c);
void vt_write(struct vt *t, const char *s, int n);
void vt_puts(struct vt *t, const char *s);
void vt_clear(struct vt *t);
int  vt_attr(struct vt *t);
#endif
