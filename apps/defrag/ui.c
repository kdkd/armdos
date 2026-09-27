/*
 * ui.c - the ARM Disk Optimizer's screen: the cluster map, the Status and
 * Legend boxes, the Optimize menu, dialogs, keyboard and INT 33h mouse, and
 * the pacing that makes a run take about as long as it would have on a 1990
 * disk (a gentle delay per operation, from a simple seek + transfer model).
 *
 * Everything is composed in sb[] and copied to the text screen by ui_flush(),
 * which writes only the cells that changed (with the mouse cursor hidden).
 */
#include "defrag.h"
#include <stdlib.h>

#define W 80
#define H 25
#define MAP_X 1
#define MAP_Y 1
#define MAP_W 78
#define MAP_H 16

/* colours */
#define A_BAR     0x70
#define A_BARHOT  0x74
#define A_BLUE    0x17
#define A_FRAME   0x1F
#define A_TITLE   0x1E
#define A_TEXT    0x1F
#define A_DIM     0x17
#define A_FREE    0x19
#define A_USED    0x1F
#define A_FIXED   0x1E
#define A_BAD     0x1C
#define A_READ    0x3F
#define A_WRITE   0x4F
#define A_DLG     0x70
#define A_DLGHI   0x7F
#define A_DLGTTL  0x71
#define A_BTNFOC  0x1F
#define A_SHADOW  0x08
#define A_PROG    0x1E
#define A_PROG0   0x13

int fast, autorun, bw;

static uint16_t sb[W * H], shown[W * H];
static int shown_valid, mouse_ok, mouse_vis, mono;
static uint8_t *tr;                     /* transient cell states */
static unsigned cpb, ncell, map_y = MAP_Y;
static uint32_t t0, debt_us, last_sec = 0xFFFFFFFF;
static unsigned st_cluster, st_pct;
static const char *st_method;
static int esc_pending;

#define TICKS (*(volatile uint32_t *)(0x46C))

/* ------------------------------------------------------------- text */

char *commas(char *p, uint32_t v)
{
    char t[16];
    int n = 0, g = 0;
    do {
        if (g == 3) { t[n++] = ','; g = 0; }
        t[n++] = '0' + v % 10; v /= 10; g++;
    } while (v);
    while (n) *p++ = t[--n];
    *p = 0;
    return p;
}

static void pc(int x, int y, int ch, int a)
{
    if (x >= 0 && x < W && y >= 0 && y < H) sb[y * W + x] = (uint8_t)ch | (a << 8);
}
static void ps(int x, int y, const char *s, int a) { while (*s) pc(x++, y, (uint8_t)*s++, a); }
static void fill(int x, int y, int w, int h, int ch, int a)
{
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) pc(x + i, y + j, ch, a);
}
static void setattr(int x, int y, int w, int a)
{
    for (int i = x; i < x + w; i++)
        if (i >= 0 && i < W && y >= 0 && y < H) sb[y * W + i] = (sb[y * W + i] & 0xFF) | (a << 8);
}
static void box(int x, int y, int w, int h, int a, int dbl)
{
    int tl = dbl ? 0xC9 : 0xDA, tr_ = dbl ? 0xBB : 0xBF, bl = dbl ? 0xC8 : 0xC0, br = dbl ? 0xBC : 0xD9;
    int hz = dbl ? 0xCD : 0xC4, vt = dbl ? 0xBA : 0xB3;
    pc(x, y, tl, a); pc(x + w - 1, y, tr_, a); pc(x, y + h - 1, bl, a); pc(x + w - 1, y + h - 1, br, a);
    for (int i = 1; i < w - 1; i++) { pc(x + i, y, hz, a); pc(x + i, y + h - 1, hz, a); }
    for (int j = 1; j < h - 1; j++) { pc(x, y + j, vt, a); pc(x + w - 1, y + j, vt, a); }
}
static void shadow(int x, int y, int w, int h)
{
    for (int j = y + 1; j <= y + h; j++) setattr(x + w, j, 2, A_SHADOW);
    setattr(x + 2, y + h, w, A_SHADOW);
}
static void center(int x, int w, int y, const char *s, int a) { ps(x + (w - (int)strlen(s)) / 2, y, s, a); }
/* "&X" marks the hot letter */
static int hotlen(const char *s) { int n = 0; for (; *s; s++) if (*s != '&') n++; return n; }
static void phot(int x, int y, const char *s, int a, int hot)
{
    for (; *s; s++) {
        if (*s == '&') { s++; if (!*s) break; pc(x++, y, (uint8_t)*s, hot); continue; }
        pc(x++, y, (uint8_t)*s, a);
    }
}

/* ------------------------------------------------------------ screen */

static void int10(unsigned ax, unsigned bx, unsigned cx, unsigned dx)
{
    R r = { 0 };
    r.r0 = ax; r.r1 = bx; r.r2 = cx; r.r3 = dx;
    _armdos_int10(&r);
}

static int int33(int ax, int bx, int *cx, int *dx)
{
    R r = { 0 };
    r.r0 = ax; r.r1 = bx;
    if (cx) r.r2 = *cx;
    if (dx) r.r3 = *dx;
    _armdos_int33(&r);
    if (cx) *cx = r.r2 & 0xFFFF;
    if (dx) *dx = r.r3 & 0xFFFF;
    return ax == 5 || ax == 6 ? (int)(r.r1 & 0xFFFF) : (int)(r.r0 & 0xFFFF);
}

static void mouse_show(int on)
{
    if (!mouse_ok || on == mouse_vis) return;
    int33(on ? 1 : 2, 0, 0, 0);
    mouse_vis = on;
}

static uint16_t mono_cell(uint16_t x)
{
    static const uint8_t lum[16] = { 0, 1, 3, 4, 2, 3, 4, 7, 5, 6, 8, 9, 7, 8, 10, 11 };
    int a = x >> 8, fg = a & 15, bg = (a >> 4) & 7, na;
    if (bg == 7 || lum[bg] > lum[fg]) na = 0x70;
    else if (fg == bg) na = 0x00;
    else na = (fg >= 8 || (fg == 7 && bg)) ? 0x0F : 0x07;
    return (uint16_t)((na << 8) | (x & 0xFF));
}

void ui_flush(void)
{
    volatile uint16_t *v = ARMDOS_TEXT_VRAM;
    int from = -1, to = -1;
    if (!shown_valid) { from = 0; to = W * H - 1; }
    else for (int i = 0; i < W * H; i++) if (shown[i] != sb[i]) { if (from < 0) from = i; to = i; }
    if (from < 0) return;
    int vis = mouse_vis;
    if (vis) mouse_show(0);
    for (int i = from; i <= to; i++) { shown[i] = sb[i]; v[i] = mono ? mono_cell(sb[i]) : sb[i]; }
    shown_valid = 1;
    if (vis) mouse_show(1);
}

void ui_init(void)
{
    R r = { 0 };
    r.r0 = 0x0F00;
    _armdos_int10(&r);
    int mode = r.r0 & 0x7F;
    mono = mode == 7 || bw;
    if (mode != 3 && mode != 7) int10(0x0003, 0, 0, 0);
    int10(0x0100, 0, 0x2000, 0);            /* no cursor */
    int10(0x1003, 0, 0, 0);                 /* bright backgrounds, no blinking */
    if (ARMDOS_IVT[0x33]) {
        r.r0 = 0;
        _armdos_int33(&r);
        mouse_ok = (r.r0 & 0xFFFF) == 0xFFFF;
    }
    shown_valid = 0;
    mouse_show(1);
}

void ui_exit(void)
{
    mouse_show(0);
    if (mouse_ok) { R r = { 0 }; _armdos_int33(&r); }
    int10(0x1003, 1, 0, 0);
    int10(0x0600, 0x0700, 0, (24 << 8) | 79);   /* clear */
    int10(0x0200, 0, 0, 0);
    int10(0x0100, 0, 0x0607, 0);
    ARMDOS_BDA[0x60] = 7; ARMDOS_BDA[0x61] = 6;
}

void ui_bar(const char *text)
{
    fill(0, 24, W, 1, ' ', A_BAR);
    ps(1, 24, text, A_BAR);
    pc(58, 24, 0xB3, A_BAR);
    ps(60, 24, "ARM Disk Optimizer", A_BAR);
    ui_flush();
}

static void menubar(int open)
{
    fill(0, 0, W, 1, ' ', A_BAR);
    int a = open ? 0x07 : A_BAR;
    fill(1, 0, 10, 1, ' ', a);
    phot(2, 0, "&Optimize", a, open ? 0x07 : A_BARHOT);
    ps(59, 0, "F1=Help  Alt=Menu", A_BAR);
}

static void legend_box(void)
{
    const int x = 46, y = 18, w = 33;
    fill(x + 1, y + 1, w - 2, 4, ' ', A_TEXT);
    box(x, y, w, 6, A_FRAME, 0);
    center(x, w, y, " Legend ", A_TITLE);
    pc(x + 2, y + 1, 0xFE, A_USED);  ps(x + 3, y + 1, " - Used", A_DIM);
    pc(x + 2, y + 2, 0xB0, A_FREE);  ps(x + 3, y + 2, " - Unused", A_DIM);
    pc(x + 2, y + 3, 'B', A_BAD);    ps(x + 3, y + 3, " - Bad", A_DIM);
    pc(x + 17, y + 1, 'r', A_READ);  ps(x + 18, y + 1, " - Reading", A_DIM);
    pc(x + 17, y + 2, 'W', A_WRITE); ps(x + 18, y + 2, " - Writing", A_DIM);
    pc(x + 17, y + 3, 'X', A_FIXED); ps(x + 18, y + 3, " - Unmovable", A_DIM);
    char t[40], *p = t;
    if (cpb) {
        memcpy(p, "Drive ", 6); p += 6;
        *p++ = 'A' + dr; *p++ = ':'; *p++ = ' ';
        memcpy(p, "1 block = ", 10); p += 10;
        p = commas(p, cpb);
        strcpy(p, cpb == 1 ? " cluster" : " clusters");
        ps(x + 2, y + 4, t, A_TEXT);
    }
}

static void status_box(void)
{
    const int x = 1, y = 18, w = 44;
    fill(x + 1, y + 1, w - 2, 4, ' ', A_TEXT);
    box(x, y, w, 6, A_FRAME, 0);
    center(x, w, y, " Status ", A_TITLE);
    if (!st_method) return;
    char t[40], *p;
    strcpy(t, "Cluster ");
    commas(t + 8, st_cluster);
    ps(x + 3, y + 1, t, A_TEXT);
    p = t;
    p = commas(p, st_pct); *p++ = '%'; *p = 0;
    ps(x + w - 4 - (int)strlen(t), y + 1, t, A_TEXT);
    int bw_ = w - 6, fillw = bw_ * st_pct / 100;
    for (int i = 0; i < bw_; i++) pc(x + 3 + i, y + 2, i < fillw ? 0xDB : 0xB0, i < fillw ? A_PROG : A_PROG0);
    uint32_t e = ui_elapsed();
    p = t;
    strcpy(p, "Elapsed Time: "); p += 14;
    *p++ = '0' + e / 36000 % 10; *p++ = '0' + e / 3600 % 10; *p++ = ':';
    *p++ = '0' + e / 600 % 6; *p++ = '0' + e / 60 % 10; *p++ = ':';
    *p++ = '0' + e / 10 % 6; *p++ = '0' + e % 10; *p = 0;
    center(x, w, y + 3, t, A_TEXT);
    center(x, w, y + 4, st_method, A_TITLE);
}

void ui_frame(void)
{
    fill(0, 0, W, H, ' ', A_BLUE);
    menubar(0);
    status_box();
    legend_box();
    ui_bar("");
}

/* -------------------------------------------------------------- map */

void ui_map_setup(void)
{
    unsigned cells = MAP_W * MAP_H;
    cpb = (nclus + cells - 1) / cells;
    if (!cpb) cpb = 1;
    ncell = (nclus + cpb - 1) / cpb;
    map_y = MAP_Y + (MAP_H - (ncell + MAP_W - 1) / MAP_W) / 2;
    free(tr);
    tr = calloc(ncell + 1, 1);
    legend_box();
}

static void draw_cell(unsigned i)
{
    int x = MAP_X + i % MAP_W, y = map_y + i / MAP_W;
    if (tr && tr[i]) { pc(x, y, tr[i] == CS_READ ? 'r' : 'W', tr[i] == CS_READ ? A_READ : A_WRITE); return; }
    unsigned c = 2 + i * cpb, e = c + cpb, used = 0, fixed = 0, bad = 0;
    if (e > maxc + 1) e = maxc + 1;
    for (; c < e; c++) {
        unsigned o = owner[c];
        if (o == OW_FREE) continue;
        if (o == OW_BAD) bad = 1;
        else if (fixed_cluster(c)) fixed = 1;
        else used = 1;
    }
    if (fixed) pc(x, y, 'X', A_FIXED);
    else if (bad) pc(x, y, 'B', A_BAD);
    else if (used) pc(x, y, 0xFE, A_USED);
    else pc(x, y, 0xB0, A_FREE);
}

static void wait_ticks(unsigned n);

void ui_map_all(int reveal)
{
    fill(MAP_X - 1, MAP_Y, MAP_W + 2, MAP_H, ' ', A_BLUE);
    if (!owner) { ui_flush(); return; }
    for (unsigned i = 0; i < ncell; i++) {
        if (tr) tr[i] = 0;
        draw_cell(i);
        if (reveal && !fast && (i + 1) % MAP_W == 0) { ui_flush(); wait_ticks(1); }
    }
    ui_flush();
}

void ui_cells(unsigned c, unsigned k, int state)
{
    unsigned a = (c - 2) / cpb, b = (c + k - 1 - 2) / cpb;
    for (unsigned i = a; i <= b && i < ncell; i++) { tr[i] = state; draw_cell(i); }
}

void ui_refresh(unsigned c, unsigned k)
{
    unsigned a = (c - 2) / cpb, b = (c + k - 1 - 2) / cpb;
    for (unsigned i = a; i <= b && i < ncell; i++) { tr[i] = 0; draw_cell(i); }
}

/* ------------------------------------------------------------ timing */

void ui_timer_start(void) { t0 = TICKS; debt_us = 0; last_sec = 0xFFFFFFFF; }
uint32_t ui_elapsed(void) { return (uint32_t)((uint64_t)(TICKS - t0) * 10 / 182); }

void ui_status(unsigned cluster, unsigned pct, const char *method)
{
    st_cluster = cluster; st_pct = pct; st_method = method;
    status_box();
    last_sec = ui_elapsed();
}

void ui_tick(void)
{
    if (!st_method) return;
    uint32_t e = ui_elapsed();
    if (e != last_sec) { last_sec = e; status_box(); ui_flush(); }
}

static int key_ready(void)
{
    R r = { 0 };
    r.r0 = 0x1100;
    _armdos_int16(&r);
    return !(r.cpsr & ARM_CPSR_Z);
}

static int read_key(void)
{
    R r = { 0 };
    r.r0 = 0x1000;
    _armdos_int16(&r);
    int k = r.r0 & 0xFFFF;
    if ((k & 0xFF) == 0xE0 && (k >> 8)) k &= 0xFF00;
    return k;
}

static void wait_ticks(unsigned n)
{
    uint32_t start = TICKS;
    ui_flush();
    while (TICKS - start < n) {
        if (key_ready()) {
            int k = read_key();
            if ((k & 0xFF) == 27) esc_pending = 1;
        }
        ui_tick();
        armdos_halt();
    }
}

/* an old disk: floppy ~40 KB/s with slow seeks, the hard disk ~900 KB/s */
void ui_pace(uint32_t sectors, unsigned ops)
{
    if (fast) return;
    debt_us += floppy ? ops * 10000u + sectors * 5000u : ops * 4000u + sectors * 350u;
    if (debt_us >= 54925) {
        unsigned n = debt_us / 54925;
        debt_us -= n * 54925;
        wait_ticks(n);
    }
}

int ui_poll_stop(void)
{
    ui_tick();
    while (key_ready()) {
        int k = read_key();
        if ((k & 0xFF) == 27) esc_pending = 1;
    }
    if (!esc_pending) return 0;
    esc_pending = 0;
    if (autorun) return 1;
    static const char *const l[] = { "You pressed ESC.", "", "Do you want to stop DEFRAG?" };
    static const char *const b[] = { "&Resume", "&Stop" };
    ui_bar("Optimization paused");
    int r = ui_dialog("Stop Defrag", l, 3, 0, 0, 0, b, 2, 0);
    ui_bar(r == 1 ? "Stopping..." : "Optimizing...   ESC=Stop Defrag");
    return r == 1;
}

/* ------------------------------------------------------------- input */

static int ms_x, ms_y;

static int mouse_click(void)
{
    if (!mouse_ok) return 0;
    int cx = 0, dx = 0;
    if (int33(5, 0, &cx, &dx)) { ms_x = cx / 8; ms_y = dx / 8; return 1; }
    return 0;
}

/* 1 = key in *key, 2 = mouse click at ms_x/ms_y, 3 = Alt pressed and released alone */
static int wait_event(int *key)
{
    int alt = 0;
    for (;;) {
        ui_flush();
        if (key_ready()) { *key = read_key(); return 1; }
        if (mouse_click()) return 2;
        int a = (ARMDOS_BDA[0x17] & 0x08) != 0;
        if (a) alt = 1;
        else if (alt) return 3;
        armdos_halt();
    }
}

int ui_wait_event(int *key)
{
    int e = wait_event(key);
    if (e == 2) return (ms_y == 0 && ms_x >= 1 && ms_x <= 10) ? 2 : 0;
    if (e == 3) return 2;
    return 1;
}

/* ------------------------------------------------------------ dialogs */

int ui_dialog(const char *title, const char *const *lines, int nlines,
              const char *const *radios, int nradios, int *radsel,
              const char *const *buttons, int nbuttons, int defbtn)
{
    if (autorun) return defbtn;
    static uint16_t save[W * H];
    memcpy(save, sb, sizeof sb);
    int w = (int)strlen(title) + 8, i;
    for (i = 0; i < nlines; i++) if ((int)strlen(lines[i]) + 8 > w) w = strlen(lines[i]) + 8;
    for (i = 0; i < nradios; i++) if (hotlen(radios[i]) + 14 > w) w = hotlen(radios[i]) + 14;
    int bwid = 0;
    for (i = 0; i < nbuttons; i++) bwid += hotlen(buttons[i]) + 4 + (i ? 3 : 0);
    if (bwid + 8 > w) w = bwid + 8;
    int h = 4 + nlines + (nradios ? nradios + (nlines ? 1 : 0) : 0) + 2;
    int x = (W - w) / 2, y = (H - 1 - h) / 2;
    if (y < 2) y = 2;
    int focus = nradios ? -1 : defbtn;      /* -1 = the radio group */
    int sel = radsel ? *radsel : 0;
    int bx[8];
    for (;;) {
        fill(x, y, w, h, ' ', A_DLG);
        box(x, y, w, h, A_DLG, 1);
        char t[64];
        t[0] = ' '; strcpy(t + 1, title); strcat(t, " ");
        center(x, w, y, t, A_DLGTTL);
        int yy = y + 2;
        for (i = 0; i < nlines; i++) center(x, w, yy++, lines[i], A_DLG);
        if (nradios) {
            if (nlines) yy++;
            int rx = x + (w - hotlen(radios[0]) - 6) / 2;
            for (i = 0; i < nradios; i++)
                if (hotlen(radios[i]) + 6 > x + w - rx - 2) rx = x + 3;
            for (i = 0; i < nradios; i++, yy++) {
                int a = (focus == -1 && i == sel) ? A_BTNFOC : A_DLG;
                ps(rx, yy, i == sel ? "(\x07) " : "( ) ", A_DLG);
                phot(rx + 4, yy, radios[i], a, a == A_BTNFOC ? 0x1E : 0x7F);
            }
        }
        yy++;
        int bxx = x + (w - bwid) / 2;
        for (i = 0; i < nbuttons; i++) {
            int a = focus == i ? A_BTNFOC : A_DLG;
            bx[i] = bxx;
            pc(bxx, yy, '<', a); pc(bxx + 1, yy, ' ', a);
            phot(bxx + 2, yy, buttons[i], a, focus == i ? 0x1E : 0x7F);
            pc(bxx + 2 + hotlen(buttons[i]), yy, ' ', a);
            pc(bxx + 3 + hotlen(buttons[i]), yy, '>', a);
            bxx += hotlen(buttons[i]) + 7;
        }
        shadow(x, y, w, h);
        int key, e = wait_event(&key);
        int result = -2;
        if (e == 2) {
            if (ms_y == yy) {
                for (i = 0; i < nbuttons; i++)
                    if (ms_x >= bx[i] && ms_x < bx[i] + hotlen(buttons[i]) + 4) result = i;
            } else if (nradios && ms_y >= yy - 1 - nradios && ms_y < yy - 1 && ms_x > x && ms_x < x + w - 1) {
                sel = ms_y - (yy - 1 - nradios);
                focus = -1;
            }
        } else if (e == 1) {
            int c = key & 0xFF, s = key >> 8;
            if (c == 27) result = -1;
            else if (c == 13) result = focus >= 0 ? focus : defbtn;
            else if (c == 9 || s == 0x0F) {
                int n = nbuttons + (nradios ? 1 : 0), cur = focus + (nradios ? 1 : 0);
                cur = c == 9 ? (cur + 1) % n : (cur + n - 1) % n;
                focus = cur - (nradios ? 1 : 0);
            } else if (s == 0x48 && nradios) { sel = (sel + nradios - 1) % nradios; focus = -1; }
            else if (s == 0x50 && nradios) { sel = (sel + 1) % nradios; focus = -1; }
            else if ((s == 0x4B || s == 0x4D) && focus >= 0)
                focus = (focus + (s == 0x4D ? 1 : nbuttons - 1)) % nbuttons;
            else if (c > ' ') {
                int u = c & 0xDF;
                for (i = 0; i < nbuttons; i++) {
                    const char *p = strchr(buttons[i], '&');
                    if (p && (p[1] & 0xDF) == u) result = i;
                }
                for (i = 0; i < nradios && result == -2; i++) {
                    const char *p = strchr(radios[i], '&');
                    if (p && (p[1] & 0xDF) == u) { sel = i; focus = -1; }
                }
            }
        }
        if (result != -2) {
            if (radsel && result >= 0) *radsel = sel;
            memcpy(sb, save, sizeof sb);
            ui_flush();
            return result;
        }
    }
}

void ui_message(const char *title, const char *const *lines, int n)
{
    static const char *const ok[] = { "OK" };
    ui_dialog(title, lines, n, 0, 0, 0, ok, 1, 0);
}

/* --------------------------------------------------------------- menu */

static const char *const items[] = {
    "&Begin Optimization...  Alt+B",
    "&Drive...",
    "Optimization &Method...",
    "&File Sort...",
    "Map &Legend...",
    "Fast &Speed",
    "&About Defrag...",
    0,
    "E&xit                   Alt+X",
};
static const int item_id[] = { MI_BEGIN, MI_DRIVE, MI_METHOD, MI_SORT, MI_LEGEND, MI_FAST, MI_ABOUT, -1, MI_EXIT };
#define NITEMS 9

int ui_menu(void)
{
    static uint16_t save[W * H];
    memcpy(save, sb, sizeof sb);
    const int x = 1, y = 1, w = 34, h = NITEMS + 2;
    int sel = 0, result = -2;
    ui_bar("Use the arrow keys and Enter to choose, Esc to close");
    while (result == -2) {
        menubar(1);
        fill(x, y, w, h, ' ', A_DLG);
        box(x, y, w, h, A_DLG, 0);
        for (int i = 0; i < NITEMS; i++) {
            int yy = y + 1 + i;
            if (!items[i]) {
                pc(x, yy, 0xC3, A_DLG); pc(x + w - 1, yy, 0xB4, A_DLG);
                for (int j = 1; j < w - 1; j++) pc(x + j, yy, 0xC4, A_DLG);
                continue;
            }
            int a = i == sel ? 0x07 : A_DLG;
            fill(x + 1, yy, w - 2, 1, ' ', a);
            if (item_id[i] == MI_FAST && fast) pc(x + 1, yy, 0xFB, a);
            phot(x + 2, yy, items[i], a, i == sel ? 0x0F : A_BARHOT);
        }
        shadow(x, y, w, h);
        int key, e = wait_event(&key);
        if (e == 3) result = -1;
        else if (e == 2) {
            if (ms_x > x && ms_x < x + w - 1 && ms_y > y && ms_y < y + h - 1 && items[ms_y - y - 1])
                result = item_id[ms_y - y - 1];
            else if (!(ms_y == 0 && ms_x >= 1 && ms_x <= 10)) result = -1;
        } else {
            int c = key & 0xFF, s = key >> 8;
            if (c == 27) result = -1;
            else if (c == 13) result = item_id[sel];
            else if (s == 0x48) { do sel = (sel + NITEMS - 1) % NITEMS; while (!items[sel]); }
            else if (s == 0x50) { do sel = (sel + 1) % NITEMS; while (!items[sel]); }
            else if (s == 0x30 && !c) result = MI_BEGIN;
            else if (s == 0x2D && !c) result = MI_EXIT;
            else if (c > ' ') {
                for (int i = 0; i < NITEMS; i++) {
                    const char *p = items[i] ? strchr(items[i], '&') : 0;
                    if (p && (p[1] & 0xDF) == (c & 0xDF)) result = item_id[i];
                }
            }
        }
    }
    memcpy(sb, save, sizeof sb);
    menubar(0);
    ui_flush();
    return result;
}
