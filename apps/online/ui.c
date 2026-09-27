/* ui.c - the screen furniture of ARM-DOS Online: title bar, status bar, message boxes,
 * prompts, the big logo, the mouse (INT 33h). */
#include <stdio.h>
#include <string.h>
#include <dos.h>
#include "online.h"

/* ------------------------------------------------------------ mouse */
int mouse_present;
static int mouse_shown, mouse_was_down;

static void mcall(struct armregs *r) { _armdos_int33(r); }

void mouse_init(void)
{
    struct armregs r;
    memset(&r, 0, sizeof r);
    /* no driver: the vector is empty and the call comes back unchanged */
    armdos_vect_t v = armdos_getvect(0x33);
    if (!v) { mouse_present = 0; return; }
    r.r0 = 0;
    mcall(&r);
    mouse_present = (r.r0 & 0xFFFF) == 0xFFFF;
    mouse_shown = 0;
    if (mouse_present) { memset(&r, 0, sizeof r); r.r0 = 4; r.r2 = 632; r.r3 = 192; mcall(&r); }   /* out of the way */
}

void mouse_show(int on)
{
    if (!mouse_present || on == mouse_shown) return;
    struct armregs r;
    memset(&r, 0, sizeof r);
    r.r0 = on ? 1 : 2;
    mcall(&r);
    mouse_shown = on;
}

int mouse_click(int *x, int *y)
{
    if (!mouse_present) return 0;
    struct armregs r;
    memset(&r, 0, sizeof r);
    r.r0 = 3;
    mcall(&r);
    int down = r.r1 & 1;
    int click = down && !mouse_was_down;
    mouse_was_down = down;
    if (click) { *x = (int)(r.r2 & 0xFFFF) / 8; *y = (int)(r.r3 & 0xFFFF) / 8; }
    return click;
}

/* ------------------------------------------------------------ bars */
static char cur_channel[32];

void ui_titlebar(const char *channel)
{
    struct dostime_t t;
    char clock[12];
    if (channel) snprintf(cur_channel, sizeof cur_channel, "%s", channel);
    scr_fill(0, 0, 80, 1, ' ', ATTR_BAR);
    scr_puts(0, 0, ATTR_LOGO, " \xFE ARM-DOS Online ");
    if (cur_channel[0]) scr_printf(18, 0, ATTR_BAR, "\x10 %s", cur_channel);
    _dos_gettime(&t);
    snprintf(clock, sizeof clock, "%2d:%02d %s", t.hour % 12 ? t.hour % 12 : 12, t.minute, t.hour < 12 ? "AM" : "PM");
    scr_puts(79 - (int)strlen(clock), 0, ATTR_BAR, clock);
}

void ui_statusbar(void)
{
    char b[81];
    uint32_t s = online_seconds();
    scr_fill(0, 24, 80, 1, ' ', ATTR_BAR);
    if (net.carrier)
        snprintf(b, sizeof b, " %ld bps \xB3 Online %02lu:%02lu:%02lu \xB3 %s", net.rate,
                 (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60), (unsigned long)(s % 60),
                 net.waiting ? (net.status[0] ? net.status : "Please wait...") :
                 net.incoming ? "Receiving..." : "F1=Help  Alt-X=Sign Off");
    else
        snprintf(b, sizeof b, " Offline \xB3 %s", net.result[0] ? net.result : "Not connected");
    scr_puts(0, 24, ATTR_BAR, b);
    if (net.carrier) {
        snprintf(b, sizeof b, "%lu bytes in ", (unsigned long)net.rxbytes);
        scr_puts(79 - (int)strlen(b), 24, 0x78, b);
    }
}

void ui_help(const char *text)
{
    scr_fill(0, 23, 80, 1, ' ', ATTR_HELP);
    scr_puts(1, 23, ATTR_HELP, text);
}

static uint32_t last_tick;
void ui_tick(void)
{
    if (TICKS() - last_tick < 9) return;
    last_tick = TICKS();
    int m = mouse_shown; mouse_show(0);
    ui_titlebar(NULL);
    ui_statusbar();
    mouse_show(m);
}

int ui_idle_key(void)
{
    net_poll();
    ui_tick();
    if (key_ready()) return key_get();
    return 0;
}

int ui_wait_key(void)
{
    for (;;) {
        int k = ui_idle_key();
        if (k) return k;
        if (net.lost) return KEY_LOST;
        idle();
    }
}

/* ------------------------------------------------------------ boxes */
static int wrap(const char *text, int w, char lines[][72], int maxl)
{
    int n = 0;
    const char *p = text;
    while (*p && n < maxl) {
        int len = 0, brk = -1;
        while (p[len] && p[len] != '\n' && len < w) { if (p[len] == ' ') brk = len; len++; }
        if (p[len] && p[len] != '\n' && brk > 0) len = brk;
        memcpy(lines[n], p, len); lines[n][len] = 0; n++;
        p += len;
        if (*p == '\n' || *p == ' ') p++;
    }
    return n;
}

int ui_message(const char *title, const char *text, int attr)
{
    static uint16_t save[80 * 25];
    char lines[12][72];
    int n = wrap(text, 56, lines, 12);
    int w = 62, h = n + 5, x = (80 - w) / 2, y = (25 - h) / 2;
    mouse_show(0);
    scr_save(save);
    scr_box(x, y, w, h, attr, 1, title);
    for (int i = 0; i < n; i++) scr_puts(x + 3, y + 2 + i, attr, lines[i]);
    scr_puts(x + (w - 22) / 2, y + h - 2, (attr & 0xF0) | 0x0F, "Press any key . . .");
    scr_shadow(x, y, w, h);
    mouse_show(1);
    int k;
    int mx, my;
    for (;;) {
        k = ui_idle_key();
        if (k) break;
        if (mouse_click(&mx, &my)) { k = K_ENTER; break; }
        if (net.lost) { k = KEY_LOST; break; }
        idle();
    }
    mouse_show(0);
    scr_restore(save);
    mouse_show(1);
    return k;
}

int ui_prompt(const char *title, const char *label, char *buf, int max)
{
    static uint16_t save[80 * 25];
    int w = 62, h = 7, x = 9, y = 8;
    mouse_show(0);
    scr_save(save);
    scr_box(x, y, w, h, ATTR_BOX, 1, title);
    scr_puts(x + 3, y + 2, ATTR_BOX, label);
    scr_puts(x + 3, y + 5, ATTR_BOX, "Enter=OK  Esc=Cancel");
    scr_shadow(x, y, w, h);
    int ok = scr_input(x + 3, y + 3, w - 6, ATTR_FIELD, buf, max);
    scr_restore(save);
    mouse_show(1);
    return ok && buf[0];
}

/* ------------------------------------------------------------ the logo: 5x5 letters in half blocks */
static const char *const glyphs[] = {
    "A.###.#...#######...##...#",
    "R####.#...#####.#..#.#...#",
    "M#...###.###.#.##...##...#",
    "-.........##.........",
    "D####.#...##...##...#####.",
    "O.###.#...##...##...#.###.",
    "S.#####.....###.....#####.",
    "N#...###..##.#.##..###...#",
    "L#....#....#....#....#####",
    "I###.#..#..#.###",
    "E######....####.#....#####",
    " ..........",
    0 };

void ui_logo(int y, int bg)
{
    static const char text[] = "ARM-DOS ONLINE";
    static const uint8_t col[3] = { 0x0E, 0x0C, 0x04 };
    char px[5][80];
    int w = 0;
    memset(px, 0, sizeof px);
    for (const char *t = text; *t; t++) {
        const char *g = NULL;
        for (int i = 0; glyphs[i]; i++) if (glyphs[i][0] == *t) g = glyphs[i] + 1;
        if (!g) continue;
        int gw = (int)strlen(g) / 5;
        for (int r = 0; r < 5; r++) for (int c = 0; c < gw && w + c < 80; c++) px[r][w + c] = g[r * gw + c] == '#';
        w += gw + 1;
    }
    w--;
    int x0 = (80 - w) / 2;
    for (int row = 0; row < 3; row++) {
        for (int c = 0; c < w; c++) {
            int top = px[row * 2][c], bot = row * 2 + 1 < 5 ? px[row * 2 + 1][c] : 0;
            int ch = top && bot ? 0xDB : top ? 0xDF : bot ? 0xDC : ' ';
            uint8_t a = (uint8_t)((bg & 0xF0) | col[row]);
            scr_putc(x0 + c, y + row, ch, a);
        }
    }
}
