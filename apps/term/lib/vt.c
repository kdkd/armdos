/* vt.c - ANSI-BBS / VT100 emulation (see vt.h).
 *
 * Supports what BBS software of the late 1980s sent: CSI cursor movement
 * (A B C D E F G H f d), erase (J K X), insert/delete (L M @ P), scroll (S T,
 * regions with r), SGR colours (0 1 2 4 5 7 8 22-27 30-37 39 40-47 49,
 * 90-97/100-107 as bright), save/restore (s u, ESC 7/8), DSR (5n, 6n),
 * DA (c), ESC D/M/E/c; controls BEL BS HT LF VT FF CR. Wrapping is
 * VT100-style deferred ("last column flag"), so an 80-column line followed by
 * CR LF does not produce an empty line. ANSI music (CSI M followed by a music
 * string) is off: CSI M is Delete Line, as on a VT102. */
#include <string.h>
#include <stdio.h>
#include "vt.h"

static const uint8_t ansi2pc[8] = { 0, 4, 2, 6, 1, 5, 3, 7 };

int vt_attr(struct vt *t)
{
    int f = ansi2pc[t->fg & 7] | (t->bold ? 8 : 0) | (t->fg & 8);
    int b = ansi2pc[t->bg & 7] | (t->blink ? 8 : 0);
    if (t->rev) { int x = f & 7; f = (f & 8) | (b & 7); b = (b & 8) | x; }
    if (t->conceal) f = b & 7;
    return (b << 4) | f;
}
static void setattr(struct vt *t) { t->attr = vt_attr(t); }

static void fillcells(struct vt *t, int from, int to)   /* [from, to) cell indices */
{
    uint16_t v = (uint16_t)((t->attr << 8) | ' ');
    for (int i = from; i < to; i++) t->cells[i] = v;
}

void vt_reset(struct vt *t)
{
    t->x = t->y = t->wrapnext = 0;
    t->fg = 7; t->bg = 0; t->bold = t->blink = t->rev = t->conceal = 0;
    t->sx = t->sy = 0; t->sfg = 7; t->sbg = 0; t->sbold = t->sblink = t->srev = 0;
    t->top = 0; t->bot = t->h - 1;
    t->state = 0; t->np = 0; t->priv = 0;
    t->origin = 0; t->autowrap = 1;
    setattr(t);
}

void vt_init(struct vt *t, volatile uint16_t *cells, int w, int h)
{
    memset(t, 0, sizeof *t);
    t->cells = cells; t->w = w; t->h = h;
    vt_reset(t);
}

void vt_clear(struct vt *t) { fillcells(t, 0, t->w * t->h); t->x = t->y = t->wrapnext = 0; }

static void scroll_up(struct vt *t, int top, int bot, int n)
{
    if (n <= 0) return;
    if (n > bot - top + 1) n = bot - top + 1;
    for (int k = 0; k < n; k++) {
        if (top == 0 && t->scrolled) t->scrolled(t->ctx, t->cells + top * t->w, t->w);
        for (int y = top; y < bot; y++)
            for (int x = 0; x < t->w; x++) t->cells[y * t->w + x] = t->cells[(y + 1) * t->w + x];
        fillcells(t, bot * t->w, (bot + 1) * t->w);
    }
}

static void scroll_down(struct vt *t, int top, int bot, int n)
{
    if (n <= 0) return;
    if (n > bot - top + 1) n = bot - top + 1;
    for (int k = 0; k < n; k++) {
        for (int y = bot; y > top; y--)
            for (int x = 0; x < t->w; x++) t->cells[y * t->w + x] = t->cells[(y - 1) * t->w + x];
        fillcells(t, top * t->w, (top + 1) * t->w);
    }
}

static void lf(struct vt *t)
{
    if (t->y == t->bot) scroll_up(t, t->top, t->bot, 1);
    else if (t->y < t->h - 1) t->y++;
}

static void ri(struct vt *t)
{
    if (t->y == t->top) scroll_down(t, t->top, t->bot, 1);
    else if (t->y > 0) t->y--;
}

static void clampxy(struct vt *t)
{
    if (t->x < 0) t->x = 0;
    if (t->x >= t->w) t->x = t->w - 1;
    if (t->y < 0) t->y = 0;
    if (t->y >= t->h) t->y = t->h - 1;
    t->wrapnext = 0;
}

static int P(struct vt *t, int i, int def) { return (i < t->np && t->par[i] > 0) ? t->par[i] : def; }

static void sgr(struct vt *t)
{
    if (t->np == 0) { t->np = 1; t->par[0] = 0; }
    for (int i = 0; i < t->np; i++) {
        int p = t->par[i];
        if (p < 0) p = 0;
        switch (p) {
        case 0: t->fg = 7; t->bg = 0; t->bold = t->blink = t->rev = t->conceal = 0; break;
        case 1: t->bold = 1; break;
        case 2: case 22: t->bold = 0; break;
        case 4: case 24: break;                 /* underline: not on a colour card */
        case 5: case 6: t->blink = 1; break;
        case 25: t->blink = 0; break;
        case 7: t->rev = 1; break;
        case 27: t->rev = 0; break;
        case 8: t->conceal = 1; break;
        case 28: t->conceal = 0; break;
        case 39: t->fg = 7; break;
        case 49: t->bg = 0; break;
        default:
            if (p >= 30 && p <= 37) t->fg = p - 30;
            else if (p >= 40 && p <= 47) t->bg = p - 40;
            else if (p >= 90 && p <= 97) t->fg = (p - 90) | 8;
            else if (p >= 100 && p <= 107) { t->bg = p - 100; t->blink = 1; }
        }
    }
    setattr(t);
}

static void erase_display(struct vt *t, int mode)
{
    int cur = t->y * t->w + t->x;
    if (mode == 0) fillcells(t, cur, t->w * t->h);
    else if (mode == 1) fillcells(t, 0, cur + 1);
    else { fillcells(t, 0, t->w * t->h); t->x = t->y = 0; }   /* ANSI.SYS/BBS: 2J homes */
}

static void erase_line(struct vt *t, int mode)
{
    int row = t->y * t->w;
    if (mode == 0) fillcells(t, row + t->x, row + t->w);
    else if (mode == 1) fillcells(t, row, row + t->x + 1);
    else fillcells(t, row, row + t->w);
}

static void reply(struct vt *t, const char *s) { if (t->reply) t->reply(t->ctx, s); }

static void csi(struct vt *t, int c)
{
    int n, row = t->y * t->w;
    char b[32];
    switch (c) {
    case 'A': t->y -= P(t, 0, 1); if (t->y < t->top && t->y + P(t, 0, 1) >= t->top) t->y = t->top; clampxy(t); break;
    case 'B': case 'e': t->y += P(t, 0, 1); if (t->y > t->bot && t->y - P(t, 0, 1) <= t->bot) t->y = t->bot; clampxy(t); break;
    case 'C': case 'a': t->x += P(t, 0, 1); clampxy(t); break;
    case 'D': t->x -= P(t, 0, 1); clampxy(t); break;
    case 'E': t->y += P(t, 0, 1); t->x = 0; clampxy(t); break;
    case 'F': t->y -= P(t, 0, 1); t->x = 0; clampxy(t); break;
    case 'G': case '`': t->x = P(t, 0, 1) - 1; clampxy(t); break;
    case 'd': t->y = P(t, 0, 1) - 1 + (t->origin ? t->top : 0); clampxy(t); break;
    case 'H': case 'f':
        t->y = P(t, 0, 1) - 1 + (t->origin ? t->top : 0);
        t->x = P(t, 1, 1) - 1;
        clampxy(t); break;
    case 'J': erase_display(t, t->np ? t->par[0] : 0); t->wrapnext = 0; break;
    case 'K': erase_line(t, t->np ? t->par[0] : 0); t->wrapnext = 0; break;
    case 'X': n = P(t, 0, 1); if (t->x + n > t->w) n = t->w - t->x; fillcells(t, row + t->x, row + t->x + n); break;
    case 'L': if (t->y >= t->top && t->y <= t->bot) scroll_down(t, t->y, t->bot, P(t, 0, 1)); break;
    case 'M': if (t->y >= t->top && t->y <= t->bot) {
                  /* delete lines without feeding the scrollback */
                  void (*s)(void *, const volatile uint16_t *, int) = t->scrolled;
                  t->scrolled = 0; scroll_up(t, t->y, t->bot, P(t, 0, 1)); t->scrolled = s;
              }
              break;
    case '@': n = P(t, 0, 1); if (n > t->w - t->x) n = t->w - t->x;
              for (int x = t->w - 1; x >= t->x + n; x--) t->cells[row + x] = t->cells[row + x - n];
              fillcells(t, row + t->x, row + t->x + n); break;
    case 'P': n = P(t, 0, 1); if (n > t->w - t->x) n = t->w - t->x;
              for (int x = t->x; x < t->w - n; x++) t->cells[row + x] = t->cells[row + x + n];
              fillcells(t, row + t->w - n, row + t->w); break;
    case 'S': scroll_up(t, t->top, t->bot, P(t, 0, 1)); break;
    case 'T': scroll_down(t, t->top, t->bot, P(t, 0, 1)); break;
    case 'm': sgr(t); break;
    case 's': t->sx = t->x; t->sy = t->y; break;
    case 'u': t->x = t->sx; t->y = t->sy; clampxy(t); break;
    case 'n':
        if (P(t, 0, 0) == 6) { snprintf(b, sizeof b, "\033[%d;%dR", t->y + 1, t->x + 1); reply(t, b); }
        else if (P(t, 0, 0) == 5) reply(t, "\033[0n");
        break;
    case 'c': if (!t->priv) reply(t, "\033[?1;0c"); break;
    case 'r':
        t->top = P(t, 0, 1) - 1; t->bot = P(t, 1, t->h) - 1;
        if (t->top < 0) t->top = 0;
        if (t->bot >= t->h) t->bot = t->h - 1;
        if (t->top >= t->bot) { t->top = 0; t->bot = t->h - 1; }
        t->x = 0; t->y = t->origin ? t->top : 0; t->wrapnext = 0;
        break;
    case 'h': case 'l':
        if (t->priv) for (int i = 0; i < t->np; i++) {
            if (t->par[i] == 7) t->autowrap = c == 'h';
            if (t->par[i] == 6) { t->origin = c == 'h'; t->x = 0; t->y = t->origin ? t->top : 0; }
        }
        break;
    default: break;
    }
}

void vt_putc(struct vt *t, int c)
{
    c &= 0xFF;
    switch (t->state) {
    case 1:                                  /* after ESC */
        t->state = 0;
        switch (c) {
        case '[': t->state = 2; t->np = 0; t->priv = 0; memset(t->par, 0, sizeof t->par); t->par[0] = -1; return;
        case '7': t->sx = t->x; t->sy = t->y; t->sfg = t->fg; t->sbg = t->bg; t->sbold = t->bold; t->sblink = t->blink; t->srev = t->rev; return;
        case '8': t->x = t->sx; t->y = t->sy; t->fg = t->sfg; t->bg = t->sbg; t->bold = t->sbold; t->blink = t->sblink; t->rev = t->srev; setattr(t); clampxy(t); return;
        case 'D': lf(t); return;
        case 'E': t->x = 0; lf(t); t->wrapnext = 0; return;
        case 'M': ri(t); return;
        case 'c': vt_reset(t); vt_clear(t); return;
        case '(': case ')': case '#': t->state = 3; return;
        case 'P': t->state = 4; t->dcsn = 0; return;          /* DCS ... ST */
        default: return;
        }
    case 4:                                  /* inside a DCS string: nothing is displayed */
        if (c == 27) { t->state = 5; return; }
        if (c == 7) goto dcs_end;            /* BEL ends it too (xterm style) */
        if (t->dcsn < (int)sizeof t->dcsbuf - 1) t->dcsbuf[t->dcsn++] = (char)c;
        return;
    case 5:                                  /* ESC inside DCS: ESC \ is the terminator */
        if (c != '\\') { t->state = 1; vt_putc(t, c); return; }
    dcs_end:
        t->state = 0;
        t->dcsbuf[t->dcsn] = 0;
        if (t->dcs) t->dcs(t->ctx, t->dcsbuf);
        return;
    case 3: t->state = 0; return;            /* ESC ( x etc: charset, ignored */
    case 2:                                  /* CSI */
        if (c >= '0' && c <= '9') {
            if (t->np == 0) t->np = 1;
            if (t->par[t->np - 1] < 0) t->par[t->np - 1] = 0;
            if (t->par[t->np - 1] < 10000) t->par[t->np - 1] = t->par[t->np - 1] * 10 + (c - '0');
            return;
        }
        if (c == ';') {
            if (t->np == 0) t->np = 1;
            if (t->np < 10) { t->par[t->np] = -1; t->np++; }
            return;
        }
        if (c == '?' || c == '=' || c == '>') { t->priv = 1; return; }
        if (c >= 0x20 && c < 0x40) return;   /* other intermediates */
        if (c == 27) { t->state = 1; return; }
        if (c < 0x20) break;                 /* controls inside a sequence execute */
        t->state = 0;
        csi(t, c);
        return;
    }
    switch (c) {
    case 27: t->state = 1; return;
    case 7: if (t->bell) t->bell(t->ctx); return;
    case 8: if (t->x > 0) t->x--; t->wrapnext = 0; return;
    case 9: t->x = (t->x + 8) & ~7; if (t->x >= t->w) t->x = t->w - 1; t->wrapnext = 0; return;
    case 10: case 11: lf(t); t->wrapnext = 0; return;
    case 12: vt_clear(t); return;            /* form feed clears the screen (ANSI-BBS) */
    case 13: t->x = 0; t->wrapnext = 0; return;
    case 0: case 14: case 15: case 127: return;
    default: break;
    }
    if (t->wrapnext) { t->x = 0; lf(t); t->wrapnext = 0; }
    t->cells[t->y * t->w + t->x] = (uint16_t)((t->attr << 8) | c);
    if (t->x == t->w - 1) { if (t->autowrap) t->wrapnext = 1; }
    else t->x++;
}

void vt_write(struct vt *t, const char *s, int n) { while (n-- > 0) vt_putc(t, (uint8_t)*s++); }
void vt_puts(struct vt *t, const char *s) { while (*s) vt_putc(t, (uint8_t)*s++); }
