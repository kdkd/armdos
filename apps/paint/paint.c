/* apps/paint/paint.c - ARM Paint, a 256-colour paint program for ARM-DOS.
 *
 * VGA mode 13h, the mouse through INT 33h (the arrow keys move the cursor when
 * there is none), a tool box, the 256-colour palette strip, menus, PCX / BMP
 * files, printing to LPT1 (print.c). Written for ARM-DOS; see README.md.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dos.h>
#include <direct.h>
#include <io.h>
#include <armdos.h>
#include "paint.h"

uint8_t img[W * H], scr[W * H], pal[768], defpal[768];
const uint8_t *font8;
int ui_blk, ui_drk, ui_gry, ui_wht, ui_hil, ui_lit;
static int ui_q[4], ui_red, ui_grn, ui_blu;

/* ------------------------------------------------------------------ layout */
#define MENU_H  10
#define TB_W    32
#define PAL_Y   176
#define CV_X    TB_W
#define CV_Y    MENU_H
#define CV_W    (W - TB_W)          /* 288 */
#define CV_H    (PAL_Y - MENU_H)    /* 166 */
#define ZOOM    4
#define ZV_W    (CV_W / ZOOM)       /* 72 image pixels across in FatBits */
#define ZV_H    (CV_H / ZOOM)       /* 41 */

enum { T_PENCIL, T_BRUSH, T_SPRAY, T_LINE, T_RECT, T_FRECT, T_ELLIPSE, T_FELLIPSE,
       T_FILL, T_TEXT, T_ERASER, T_PICKER, T_ZOOM, T_HAND, T_UNDO, T_PALETTE, NTOOLS };
static const char *const tool_name[NTOOLS] = {
    "Pencil", "Brush", "Airbrush", "Line", "Box", "Filled box", "Ellipse", "Filled ellipse",
    "Fill", "Text", "Eraser", "Pick colour", "Zoom", "Scroll", "Undo", "Palette" };
static const int sizes[4] = { 1, 3, 5, 8 };

static int tool = T_PENCIL, prev_tool = T_PENCIL, fg = 15, bg = 0, bsize = 1;
static int ox, oy;                  /* the view's origin in the picture */
static int zoom, zx, zy, grid = 1, fullscreen;
static int mx = 160, my = 100, have_mouse;
static int kb_btn, kb_hold, kb_click;   /* the keyboard's mouse button */
static char filename[80];           /* full path, "" = untitled */
static char dialog_dir[80];
static int modified;
static char status[48];
void command(int id);
static unsigned status_until;

/* ---------------------------------------------------------------- undo */
#define UNDO_MAX 8
#define SNAP (W * H + 768)
static uint8_t *undo_buf[UNDO_MAX];
static int undo_n, undo_top, undo_levels;

static void undo_init(void)
{
    for (undo_levels = 0; undo_levels < UNDO_MAX; undo_levels++)
        if (!(undo_buf[undo_levels] = malloc(SNAP))) break;
}
static void push_undo(void)
{
    if (!undo_levels) return;
    memcpy(undo_buf[undo_top], img, W * H);
    memcpy(undo_buf[undo_top] + W * H, pal, 768);
    undo_top = (undo_top + 1) % undo_levels;
    if (undo_n < undo_levels) undo_n++;
}
/* the picture as it was when the current operation started */
static void restore_top(void)
{
    if (!undo_n) return;
    memcpy(img, undo_buf[(undo_top + undo_levels - 1) % undo_levels], W * H);
}
static int pop_undo(void)
{
    if (!undo_n) return 0;
    undo_top = (undo_top + undo_levels - 1) % undo_levels;
    undo_n--;
    memcpy(img, undo_buf[undo_top], W * H);
    memcpy(pal, undo_buf[undo_top] + W * H, 768);
    return 1;
}

/* ----------------------------------------------------------- BIOS, mouse */
static void int10(struct armregs *r) { _armdos_int10(r); }

void set_dac(const uint8_t *p)
{
    armdos_outb(0x3C8, 0);
    for (int i = 0; i < 768; i++) armdos_outb(0x3C9, p[i]);
}
static void read_dac(uint8_t *p)
{
    armdos_outb(0x3C7, 0);
    for (int i = 0; i < 768; i++) p[i] = armdos_inb(0x3C9) & 63;
}
int nearest(const uint8_t *p, int r, int g, int b)
{
    int best = 0, bd = 1 << 30;
    for (int i = 0; i < 256; i++) {
        int dr = p[i * 3] - r, dg = p[i * 3 + 1] - g, db = p[i * 3 + 2] - b;
        int d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}
/* the tool box and menus are drawn in the picture's own palette */
void pick_ui_colours(void)
{
    ui_blk = nearest(pal, 0, 0, 0);
    ui_drk = nearest(pal, 21, 21, 21);
    ui_gry = nearest(pal, 42, 42, 42);
    ui_wht = nearest(pal, 63, 63, 63);
    ui_hil = nearest(pal, 0, 0, 42);
    ui_lit = nearest(pal, 52, 52, 52);
    ui_red = nearest(pal, 63, 0, 0); ui_grn = nearest(pal, 0, 63, 0); ui_blu = nearest(pal, 0, 0, 63);
    ui_q[0] = ui_red; ui_q[1] = ui_grn; ui_q[2] = ui_blu; ui_q[3] = nearest(pal, 63, 63, 0);
}
static void apply_palette(void) { set_dac(pal); pick_ui_colours(); }

static int mouse(int ax, int *bx, int *cx, int *dx)
{
    struct armregs r;
    memset(&r, 0, sizeof r);
    r.r0 = (unsigned)ax; r.r1 = bx ? (unsigned)*bx : 0; r.r2 = cx ? (unsigned)*cx : 0; r.r3 = dx ? (unsigned)*dx : 0;
    _armdos_int33(&r);
    if (bx) *bx = (int)(r.r1 & 0xFFFF);
    if (cx) *cx = (int)(r.r2 & 0xFFFF);
    if (dx) *dx = (int)(r.r3 & 0xFFFF);
    return (int)(r.r0 & 0xFFFF);
}
static void mouse_to(int x, int y)
{
    if (!have_mouse) return;
    int cx = x * 2, dx = y;
    mouse(4, 0, &cx, &dx);
}
static int key_ready(void)
{
    struct armregs r;
    memset(&r, 0, sizeof r); r.r0 = 0x0100;
    _armdos_int16(&r);
    return !(r.cpsr & ARM_CPSR_Z);
}
static int key_read(void)
{
    struct armregs r;
    memset(&r, 0, sizeof r);
    _armdos_int16(&r);
    int k = (int)(r.r0 & 0xFFFF);
    if ((k & 0xFF) == 0xE0 && (k >> 8)) k &= 0xFF00;
    return k;
}
static int shifted(void)
{
    struct armregs r;
    memset(&r, 0, sizeof r); r.r0 = 0x0200;
    _armdos_int16(&r);
    return (r.r0 & 3) != 0;
}
static unsigned ticks(void) { return ARMDOS_BIOS_TICKS; }

/* ------------------------------------------------------------- events */
enum { K_UP = 0x4800, K_DOWN = 0x5000, K_LEFT = 0x4B00, K_RIGHT = 0x4D00, K_PGUP = 0x4900,
       K_PGDN = 0x5100, K_HOME = 0x4700, K_END = 0x4F00, K_INS = 0x5200, K_DEL = 0x5300,
       K_F1 = 0x3B00, K_F10 = 0x4400, K_ALTF = 0x2100, K_ALTE = 0x1200, K_ALTO = 0x1800, K_ALTX = 0x2D00 };
#define KEYCODE(k) (((k) & 0xFF) ? ((k) & 0xFF) : ((k) & 0xFF00))

typedef struct { int key, x, y, b, press, release, moved; } ev_t;
static int last_b, synth_up;
static int in_dialog;               /* menus and dialogs: arrows are keys, the cursor an arrow */

/* The next thing that happens: a key, the mouse moving, a button. Halts the
 * CPU (WFI) while nothing does; with tick != 0 it also returns on every timer tick. */
static void next_event(ev_t *e, int tick)
{
    unsigned t0 = ticks();
    for (;;) {
        memset(e, 0, sizeof *e);
        if (kb_click) { kb_click--; if (!kb_click) kb_btn = kb_hold; }
        if (key_ready()) {
            int k = key_read();
            k = KEYCODE(k);
            int step = shifted() ? 8 : 1, nx = mx, ny = my;
            switch (in_dialog ? 0 : k) {
            case K_UP: ny -= step; break;
            case K_DOWN: ny += step; break;
            case K_LEFT: nx -= step; break;
            case K_RIGHT: nx += step; break;
            case ' ': kb_btn = 1; kb_click = 2; break;
            case K_INS: kb_hold ^= 1; kb_btn = kb_hold; break;
            default: e->key = k;
            }
            if (nx != mx || ny != my) {
                mx = nx < 0 ? 0 : nx > W - 1 ? W - 1 : nx;
                my = ny < 0 ? 0 : ny > H - 1 ? H - 1 : ny;
                mouse_to(mx, my);
                e->moved = 1;
            }
        }
        int b = 0;
        if (have_mouse) {
            int bx = 0, cx = 0, dx = 0;
            mouse(3, &bx, &cx, &dx);
            int nx = cx / 2, ny = dx;
            if (nx != mx || ny != my) { mx = nx; my = ny; e->moved = 1; }
            b = bx & 3;
            /* a click that came and went between two looks (AX=0005h counts presses) */
            for (int k = 0; k < 2; k++) {
                int bb = k, cnt = 0, px = 0, py = 0;
                mouse(5, &bb, &px, &py);
                cnt = bb;
                if (cnt && !(b & (1 << k)) && !(last_b & (1 << k))) { b |= 1 << k; synth_up |= 1 << k; }
            }
        }
        if (synth_up & last_b) { b &= ~(synth_up & last_b); synth_up &= ~last_b; }
        b |= kb_btn;
        e->x = mx; e->y = my; e->b = b;
        e->press = b & ~last_b; e->release = last_b & ~b;
        last_b = b;
        if (e->key || e->moved || e->press || e->release) return;
        if (kb_click || synth_up) continue;
        if (tick && ticks() != t0) return;
        armdos_halt();
    }
}

/* ------------------------------------------------------------ drawing UI */
static void set_status(const char *s)
{
    strncpy(status, s, sizeof status - 1);
    status[sizeof status - 1] = 0;
    status_until = ticks() + 55;               /* 3 seconds */
}

static void bevel(int x0, int y0, int x1, int y1, int in)
{
    fillrect(scr, x0, y0, x1, y1, ui_gry);
    hline(scr, x0, x1, y0, in ? ui_drk : ui_wht); vline(scr, x0, y0, y1, in ? ui_drk : ui_wht);
    hline(scr, x0, x1, y1, in ? ui_wht : ui_drk); vline(scr, x1, y0, y1, in ? ui_wht : ui_drk);
}

/* 12x12 icons: # = ink, . = paper, anything else = the button */
static const char *const icons[NTOOLS][12] = {
    { "         ## ", "        #..#", "       #..# ", "      #..#  ", "     #..#   ", "    #..#    ",
      "   #..#     ", "  #..#      ", " ##.#       ", " ###        ", " #          ", "            " },
    { "         ###", "        ####", "       #### ", "      ####  ", "     ####   ", "    #..#    ",
      "   #..#     ", "  ###       ", " ####       ", " ####       ", "####        ", "##          " },
    { "   # #  #   ", "  #  # #  # ", "    # ##    ", "  ### #  #  ", "  #.#       ", " ######     ",
      " #....#     ", " #....#     ", " #....#     ", " #....#     ", " #....#     ", " ######     " },
    { "#           ", " #          ", "  #         ", "   #        ", "    #       ", "     #      ",
      "      #     ", "       #    ", "        #   ", "         #  ", "          # ", "           #" },
    { "            ", "############", "#..........#", "#..........#", "#..........#", "#..........#",
      "#..........#", "#..........#", "#..........#", "############", "            ", "            " },
    { "            ", "############", "############", "############", "############", "############",
      "############", "############", "############", "############", "            ", "            " },
    { "    ####    ", "  ##....##  ", " #........# ", "#..........#", "#..........#", "#..........#",
      "#..........#", " #........# ", "  ##....##  ", "    ####    ", "            ", "            " },
    { "    ####    ", "  ########  ", " ########## ", "############", "############", "############",
      "############", " ########## ", "  ########  ", "    ####    ", "            ", "            " },
    { "    ##      ", "   #..#     ", "  #....#    ", " #......#   ", "#......###  ", " #....#.### ",
      "  #..#..### ", "   ##...### ", "    #..# ## ", "     ##  ## ", "          # ", "            " },
    { "    ####    ", "   ##  ##   ", "  ##    ##  ", "  ##    ##  ", "  ########  ", "  ##    ##  ",
      "  ##    ##  ", "  ##    ##  ", " ####  #### ", "            ", "            ", "            " },
    { "      ######", "     #....##", "    #....#.#", "   #....#..#", "  #....#..# ", " #....#..#  ",
      "######..#   ", "#....#.#    ", "#....##     ", "######      ", "            ", "            " },
    { "         ## ", "        ####", "       #####", "      ####  ", "     #.##   ", "    #..#    ",
      "   #..#     ", "  #..#      ", " #..#       ", " ##.#       ", "#  #        ", "            " },
    { "   ####     ", "  #....#    ", " #......#   ", " #......#   ", " #......#   ", " #......#   ",
      "  #....#    ", "   #####    ", "       ###  ", "        ### ", "         ###", "          ##" },
    { "     ##     ", "    ####    ", "   ######   ", "     ##     ", "  #  ##  #  ", " ########## ",
      " ########## ", "  #  ##  #  ", "     ##     ", "   ######   ", "    ####    ", "     ##     " },
    { "            ", "   #        ", "  ##        ", " #########  ", "  ##      # ", "   #       #",
      "           #", "           #", "          # ", "   #######  ", "            ", "            " },
    { "            ", " ####  #### ", " #  #  #  # ", " #  #  #  # ", " ####  #### ", "            ",
      " ####  #### ", " #  #  #  # ", " #  #  #  # ", " ####  #### ", "            ", "            " },
};

static void draw_icon(int t, int x, int y, int sel)
{
    bevel(x, y, x + 15, y + 15, sel);
    if (sel) fillrect(scr, x + 1, y + 1, x + 14, y + 14, ui_blk);
    for (int r = 0; r < 12; r++)
        for (int c = 0; c < 12; c++) {
            char ch = icons[t][r][c];
            if (ch == '#') pset(scr, x + 2 + c, y + 2 + r, sel ? ui_wht : ui_blk);
            else if (ch == '.') pset(scr, x + 2 + c, y + 2 + r, sel ? ui_drk : ui_wht);
        }
    if (t == T_PALETTE) {                        /* four colours in the squares */
        for (int i = 0; i < 4; i++) {
            int qx = x + 4 + (i & 1) * 6, qy = y + 4 + (i >> 1) * 5;
            fillrect(scr, qx, qy, qx + 1, qy + 1, ui_q[i]);
        }
    }
}

static void draw_toolbox(void)
{
    for (int t = 0; t < NTOOLS; t++) draw_icon(t, (t & 1) * 16, MENU_H + (t >> 1) * 16, t == tool);
    /* brush sizes */
    int y0 = MENU_H + 8 * 16;
    bevel(0, y0, TB_W - 1, PAL_Y - 1, 0);
    for (int i = 0; i < 4; i++) {
        int yy = y0 + 2 + i * 9;
        if (sizes[i] == bsize) fillrect(scr, 2, yy, TB_W - 3, yy + 8, ui_blk);
        stamp(scr, 16, yy + 4, sizes[i], sizes[i] == bsize ? ui_wht : ui_blk);
    }
}

static void draw_palette_strip(void)
{
    bevel(0, PAL_Y, TB_W - 1, H - 1, 1);
    fillrect(scr, 10, PAL_Y + 8, 28, H - 3, ui_blk);
    fillrect(scr, 11, PAL_Y + 9, 27, H - 4, bg);
    fillrect(scr, 3, PAL_Y + 3, 20, PAL_Y + 15, ui_blk);
    fillrect(scr, 4, PAL_Y + 4, 19, PAL_Y + 14, fg);
    for (int i = 0; i < 256; i++) {
        int x = TB_W + (i & 31) * 9, y = PAL_Y + (i >> 5) * 3;
        fillrect(scr, x, y, x + 8, y + 2, i);
    }
    /* the foreground colour's cell gets a notch above and below */
    int fx = TB_W + (fg & 31) * 9, fy = PAL_Y + (fg >> 5) * 3;
    hline(scr, fx + 3, fx + 5, fy, ui_wht); hline(scr, fx + 3, fx + 5, fy + 2, ui_blk);
}

static const char *const menu_title[3] = { "File", "Edit", "Options" };
static const int menu_x[3] = { 8, 56, 104 };

static void draw_menubar(int open)
{
    fillrect(scr, 0, 0, W - 1, MENU_H - 1, ui_gry);
    hline(scr, 0, W - 1, MENU_H - 1, ui_blk);
    for (int i = 0; i < 3; i++) {
        int w = (int)strlen(menu_title[i]) * 8;
        if (open == i) fillrect(scr, menu_x[i] - 4, 0, menu_x[i] + w + 3, MENU_H - 2, ui_blk);
        text8(scr, menu_x[i], 1, menu_title[i], open == i ? ui_wht : ui_blk);
    }
    char s[48];
    if (status[0] && (int)(status_until - ticks()) > 0) strcpy(s, status);
    else {
        status[0] = 0;
        int ix = mx, iy = my, on = 0;
        if (fullscreen) on = 1;
        else if (mx >= CV_X && my >= CV_Y && my < PAL_Y) {
            on = 1;
            if (zoom) { ix = zx + (mx - CV_X) / ZOOM; iy = zy + (my - CV_Y) / ZOOM; }
            else { ix = ox + mx - CV_X; iy = oy + my - CV_Y; }
        }
        if (on && ix < W && iy < H) sprintf(s, "%s %3d,%3d", zoom ? "x4" : "  ", ix, iy);
        else strcpy(s, tool_name[tool]);
    }
    int n = (int)strlen(s);
    if (n > 18) n = 18, s[18] = 0;
    text8(scr, W - 4 - n * 8, 1, s, ui_blk);
}

static void draw_canvas(void)
{
    if (!zoom) {
        for (int y = 0; y < CV_H; y++) memcpy(scr + (CV_Y + y) * W + CV_X, img + (oy + y) * W + ox, CV_W);
        return;
    }
    for (int y = 0; y < CV_H; y++) {
        uint8_t *d = scr + (CV_Y + y) * W + CV_X;
        int iy = zy + y / ZOOM;
        if (y >= ZV_H * ZOOM || iy >= H) { memset(d, ui_drk, CV_W); continue; }
        const uint8_t *s = img + iy * W + zx;
        int gl = grid && (y % ZOOM) == ZOOM - 1;
        for (int x = 0; x < CV_W; x++)
            d[x] = gl || (grid && (x % ZOOM) == ZOOM - 1) ? (uint8_t)ui_drk : s[x / ZOOM];
    }
    /* the actual-size view in the corner */
    int px = W - ZV_W - 4, py = CV_Y + 3;
    frame(scr, px - 1, py - 1, px + ZV_W, py + ZV_H, ui_blk);
    for (int y = 0; y < ZV_H; y++) memcpy(scr + (py + y) * W + px, img + (zy + y) * W + zx, ZV_W);
}

/* the text being typed with the text tool */
static int text_on, text_x, text_y, text_c, text_n;
static char text_buf[256];

static void cursor_sprite(void)
{
    static const char *const arrow[14] = {
        "X          ", "XX         ", "X.X        ", "X..X       ", "X...X      ", "X....X     ",
        "X.....X    ", "X......X   ", "X.......X  ", "X....XXXXX ", "X..X..X    ", "X.X X..X   ",
        "XX   X..X  ", "      XX   " };
    int canvas = !in_dialog && (fullscreen || (mx >= CV_X && my >= CV_Y && my < PAL_Y));
    if (!canvas || tool == T_HAND) {
        for (int r = 0; r < 14; r++)
            for (int c = 0; c < 11; c++) {
                char ch = arrow[r][c];
                if (ch == 'X') pset(scr, mx + c, my + r, ui_blk);
                else if (ch == '.') pset(scr, mx + c, my + r, ui_wht);
            }
        return;
    }
    /* a crosshair that shows up on light and dark */
    for (int d = -6; d <= 6; d++) {
        if (d > -2 && d < 2) continue;
        int ps[4][2] = { { mx + d, my }, { mx, my + d } };
        for (int k = 0; k < 2; k++) {
            int x = ps[k][0], y = ps[k][1];
            if ((unsigned)x >= W || (unsigned)y >= H) continue;
            const uint8_t *c = pal + scr[y * W + x] * 3;
            scr[y * W + x] = (uint8_t)((c[0] * 3 + c[1] * 6 + c[2]) > 320 ? ui_blk : ui_wht);
        }
    }
}

static void caret(void)
{
    if (!text_on || !((ticks() >> 3) & 1)) return;
    int lines = 0, col = 0;
    for (int i = 0; i < text_n; i++) { if (text_buf[i] == '\n') { lines++; col = 0; } else col++; }
    int ix = text_x + col * 8, iy = text_y + lines * 8;
    int sx, sy, sw = 8, sh = 1;
    if (fullscreen) { sx = ix; sy = iy + 8; }
    else if (zoom) { sx = CV_X + (ix - zx) * ZOOM; sy = CV_Y + (iy + 8 - zy) * ZOOM; sw = 8 * ZOOM; sh = 2; }
    else { sx = CV_X + ix - ox; sy = CV_Y + iy + 8 - oy; }
    if (!fullscreen && (sx < CV_X || sy < CV_Y || sy >= PAL_Y)) return;
    fillrect(scr, sx, sy, sx + sw - 1, sy + sh - 1, fg);
}

static void compose(void)
{
    if (fullscreen) memcpy(scr, img, W * H);
    else { draw_canvas(); draw_menubar(-1); draw_toolbox(); draw_palette_strip(); }
    caret();
}
static void blit(void)
{
    cursor_sprite();
    memcpy((void *)ARMDOS_VGA_VRAM, scr, W * H);
}

/* --------------------------------------------------------------- dialogs */
typedef struct { int x, y, w; const char *label; } button_t;

static void dlg_box(int x0, int y0, int x1, int y1, const char *title)
{
    fillrect(scr, x0 + 3, y0 + 3, x1 + 3, y1 + 3, ui_blk);        /* shadow */
    bevel(x0, y0, x1, y1, 0);
    frame(scr, x0 + 2, y0 + 2, x1 - 2, y1 - 2, ui_drk);
    if (title) {
        fillrect(scr, x0 + 3, y0 + 3, x1 - 3, y0 + 12, ui_hil);
        int n = (int)strlen(title);
        text8(scr, (x0 + x1) / 2 - n * 4, y0 + 4, title, ui_wht);
    }
}
static void draw_button(const button_t *b, int def)
{
    int w = b->w ? b->w : (int)strlen(b->label) * 8 + 12;
    bevel(b->x, b->y, b->x + w - 1, b->y + 13, 0);
    if (def) frame(scr, b->x - 1, b->y - 1, b->x + w, b->y + 14, ui_blk);
    text8(scr, b->x + (w - (int)strlen(b->label) * 8) / 2, b->y + 3, b->label, ui_blk);
}
static int in_button(const button_t *b, int x, int y)
{
    int w = b->w ? b->w : (int)strlen(b->label) * 8 + 12;
    return x >= b->x && x < b->x + w && y >= b->y && y < b->y + 14;
}

/* A message box. lines separated by \n; buttons "OK" or "Yes|No|Cancel".
 * Returns the button's index; Esc = the last. */
static int msgbox_(const char *title, const char *text, const char *buttons)
{
    char lines[14][37];
    int nl = 0, wmax = (int)strlen(title);
    for (const char *p = text; *p && nl < 14;) {
        int n = 0;
        while (p[n] && p[n] != '\n' && n < 36) n++;
        memcpy(lines[nl], p, n); lines[nl][n] = 0;
        if (n > wmax) wmax = n;
        nl++;
        p += n;
        if (*p == '\n') p++;
    }
    button_t bt[3];
    char blabel[3][12];
    int nb = 0;
    for (const char *p = buttons; *p && nb < 3;) {
        int n = 0;
        while (p[n] && p[n] != '|' && n < 11) n++;
        memcpy(blabel[nb], p, n); blabel[nb][n] = 0;
        bt[nb].label = blabel[nb]; bt[nb].w = 64;
        nb++; p += n;
        if (*p == '|') p++;
    }
    int bw = nb * 64 + (nb - 1) * 8;
    int w = wmax * 8 + 24;
    if (w < bw + 24) w = bw + 24;
    int h = nl * 9 + 44;
    int x0 = (W - w) / 2, y0 = (H - h) / 2;
    for (int i = 0; i < nb; i++) { bt[i].x = (W - bw) / 2 + i * 72; bt[i].y = y0 + h - 22; }
    int focus = 0;
    for (;;) {
        compose();
        dlg_box(x0, y0, x0 + w - 1, y0 + h - 1, title);
        for (int i = 0; i < nl; i++) text8(scr, x0 + 12, y0 + 18 + i * 9, lines[i], ui_blk);
        for (int i = 0; i < nb; i++) draw_button(&bt[i], i == focus);
        blit();
        ev_t e;
        next_event(&e, 0);
        if (e.key == 27) return nb - 1;
        if (e.key == 13) return focus;
        if (e.key == 9 || e.key == K_RIGHT) focus = (focus + 1) % nb;
        if (e.key == 0x0F00 || e.key == K_LEFT) focus = (focus + nb - 1) % nb;
        if (e.key > 0 && e.key < 256)
            for (int i = 0; i < nb; i++) if (toupper(e.key) == blabel[i][0]) return i;
        if (e.release) for (int i = 0; i < nb; i++) if (in_button(&bt[i], e.x, e.y)) return i;
    }
}
static int msgbox(const char *title, const char *text, const char *buttons) { in_dialog++; int r = msgbox_(title, text, buttons); in_dialog--; return r; }

static const char *base_name(const char *p)
{
    const char *s = strrchr(p, '\\');
    if (!s) s = strchr(p, ':');
    return s ? s + 1 : p;
}

/* a line editor inside a dialog: returns 1 when the key was taken */
static int edit_key(char *s, int max, int k)
{
    int n = (int)strlen(s);
    if (k == 8) { if (n) s[n - 1] = 0; return 1; }
    if (k > 32 && k < 127 && n < max) { s[n] = (char)toupper(k); s[n + 1] = 0; return 1; }
    return 0;
}

/* --------------------------------------------------------- file dialog */
#define MAXENT 160
static char ents[MAXENT][14];
static char ent_kind[MAXENT];       /* 'd' directory, 'v' drive, 'f' file */
static int nents;

static int ent_cmp(int a, int b)
{
    if (ent_kind[a] != ent_kind[b]) {
        static const char order[] = "dfv";
        return (int)(strchr(order, ent_kind[a]) - order) - (int)(strchr(order, ent_kind[b]) - order);
    }
    return strcmp(ents[a], ents[b]);
}
static void read_dir(const char *dir)
{
    char pat[96];
    struct find_t f;
    nents = 0;
    snprintf(pat, sizeof pat, "%s*.*", dir);
    int r = _dos_findfirst(pat, _A_SUBDIR, &f);
    while (!r && nents < MAXENT - 4) {
        if (f.attrib & _A_SUBDIR) {
            if (strcmp(f.name, ".")) { strcpy(ents[nents], f.name); ent_kind[nents++] = 'd'; }
        } else {
            const char *d = strrchr(f.name, '.');
            if (d && (!strcmp(d, ".PCX") || !strcmp(d, ".BMP"))) { strcpy(ents[nents], f.name); ent_kind[nents++] = 'f'; }
        }
        r = _dos_findnext(&f);
    }
    strcpy(ents[nents], "A:"); ent_kind[nents++] = 'v';
    strcpy(ents[nents], "C:"); ent_kind[nents++] = 'v';
    /* insertion sort: directories, files, drives */
    for (int i = 1; i < nents; i++) {
        char t[14], k = ent_kind[i];
        memcpy(t, ents[i], 14);
        int j = i - 1;
        ents[MAXENT - 1][0] = 0;
        while (j >= 0) {
            memcpy(ents[MAXENT - 1], t, 14); ent_kind[MAXENT - 1] = k;
            if (ent_cmp(j, MAXENT - 1) <= 0) break;
            memcpy(ents[j + 1], ents[j], 14); ent_kind[j + 1] = ent_kind[j];
            j--;
        }
        memcpy(ents[j + 1], t, 14); ent_kind[j + 1] = k;
    }
}

static int first_file(void)
{
    for (int i = 0; i < nents; i++) if (ent_kind[i] == 'f') return i;
    return -1;
}

static void dir_up(char *dir)
{
    int n = (int)strlen(dir);
    if (n <= 3) return;
    dir[n - 1] = 0;
    char *s = strrchr(dir, '\\');
    if (s) s[1] = 0;
}

static void make_path(char *out, const char *dir, const char *name)
{
    if (name[0] && (name[1] == ':' || name[0] == '\\')) {
        if (name[0] == '\\') snprintf(out, 80, "%c:%s", dir[0], name);
        else strncpy(out, name, 79), out[79] = 0;
    } else snprintf(out, 80, "%s%s", dir, name);
    const char *b = base_name(out);
    if (!strchr(b, '.')) strncat(out, ".PCX", 79 - strlen(out));
}

/* "Open" / "Save As": returns 1 with the chosen full path */
static int file_dialog_(const char *title, char *path, int saving)
{
    char dir[80], name[64];
    strcpy(dir, dialog_dir);
    strcpy(name, saving && filename[0] ? base_name(filename) : "");
    read_dir(dir);
    int sel = saving ? -1 : first_file(), top = 0;
    unsigned last_click = 0; int last_sel = -2;
    const int x0 = 40, y0 = 24, x1 = 279, y1 = 179, lx = 48, ly = 72, rows = 10;
    button_t ok = { 216, 74, 56, "OK" }, cancel = { 216, 94, 56, "Cancel" };
    for (;;) {
        compose();
        dlg_box(x0, y0, x1, y1, title);
        text8(scr, 48, 40, "File:", ui_blk);
        fillrect(scr, 92, 38, 271, 48, ui_wht); frame(scr, 92, 38, 271, 48, ui_blk);
        text8(scr, 95, 40, name, ui_blk);
        if ((ticks() >> 3) & 1) vline(scr, 95 + (int)strlen(name) * 8, 39, 47, ui_blk);
        char d[24];
        int dl = (int)strlen(dir);
        snprintf(d, sizeof d, dl > 22 ? "..%s" : "%s", dl > 22 ? dir + dl - 20 : dir);
        text8(scr, 48, 56, d, ui_drk);
        fillrect(scr, lx - 2, ly - 2, lx + 150, ly + rows * 9 + 1, ui_wht);
        frame(scr, lx - 2, ly - 2, lx + 150, ly + rows * 9 + 1, ui_blk);
        for (int i = 0; i < rows && top + i < nents; i++) {
            int k = top + i;
            char s[20];
            if (ent_kind[k] == 'd') snprintf(s, sizeof s, "[%s]", ents[k]);
            else if (ent_kind[k] == 'v') snprintf(s, sizeof s, "[-%c-]", ents[k][0]);
            else snprintf(s, sizeof s, "%s", ents[k]);
            int yy = ly + i * 9;
            if (k == sel) fillrect(scr, lx - 1, yy - 1, lx + 149, yy + 7, ui_hil);
            text8(scr, lx + 2, yy, s, k == sel ? ui_wht : ui_blk);
        }
        /* scroll arrows */
        bevel(lx + 152, ly - 2, lx + 162, ly + 8, 0); glyph8(scr, lx + 154, ly - 1, 0x18, ui_blk);
        bevel(lx + 152, ly + rows * 9 - 9, lx + 162, ly + rows * 9 + 1, 0); glyph8(scr, lx + 154, ly + rows * 9 - 8, 0x19, ui_blk);
        draw_button(&ok, 1); draw_button(&cancel, 0);
        blit();
        ev_t e;
        next_event(&e, 1);
        int accept = 0;
        if (e.key == 27) return 0;
        if (e.key == 13) accept = 1;
        else if (e.key == K_DOWN || e.key == K_UP || e.key == K_PGDN || e.key == K_PGUP) {
            int dlt = e.key == K_DOWN ? 1 : e.key == K_UP ? -1 : e.key == K_PGDN ? rows : -rows;
            sel += dlt;
            if (sel < 0) sel = 0;
            if (sel >= nents) sel = nents - 1;
            if (ent_kind[sel] == 'f') strcpy(name, ents[sel]);
        } else if (e.key) edit_key(name, 60, e.key);
        if (e.press & 1) {
            if (in_button(&ok, e.x, e.y)) accept = 1;
            else if (in_button(&cancel, e.x, e.y)) return 0;
            else if (e.x >= lx + 152 && e.x <= lx + 162 && e.y >= ly - 2 && e.y <= ly + 8) top = top > 0 ? top - 1 : 0;
            else if (e.x >= lx + 152 && e.x <= lx + 162 && e.y >= ly + rows * 9 - 9 && e.y <= ly + rows * 9 + 1) { if (top + rows < nents) top++; }
            else if (e.x >= lx - 2 && e.x <= lx + 150 && e.y >= ly && e.y < ly + rows * 9) {
                int k = top + (e.y - ly) / 9;
                if (k < nents) {
                    if (k == last_sel && ticks() - last_click < 10) { sel = k; strcpy(name, ent_kind[k] == 'f' ? ents[k] : ""); accept = 2; }
                    else { sel = k; if (ent_kind[k] == 'f') strcpy(name, ents[k]); }
                    last_sel = k; last_click = ticks();
                }
            }
        }
        if (sel >= 0) { if (sel < top) top = sel; if (sel >= top + rows) top = sel - rows + 1; }
        if (!accept) continue;
        /* Enter on a directory or drive (with no name typed) goes there */
        if (sel >= 0 && ent_kind[sel] == 'f' && !name[0]) strcpy(name, ents[sel]);
        if (sel >= 0 && ent_kind[sel] != 'f' && (!name[0] || accept == 2)) {
            if (ent_kind[sel] == 'v') {
                char t[80], pat[8];
                snprintf(t, sizeof t, "%c:\\", ents[sel][0]);
                snprintf(pat, sizeof pat, "%s*.*", t);
                struct find_t f;
                int fr = _dos_findfirst(pat, _A_SUBDIR, &f);
                if (fr && fr != 2 && fr != 18) {
                    char q[40];
                    snprintf(q, sizeof q, "Drive %c: is not ready.", t[0]);
                    msgbox(title, q, "OK");
                    continue;
                }
                strcpy(dir, t);
            } else if (!strcmp(ents[sel], "..")) dir_up(dir);
            else if (strlen(dir) + strlen(ents[sel]) < 76) { strcat(dir, ents[sel]); strcat(dir, "\\"); }
            read_dir(dir); sel = first_file(); top = 0; name[0] = 0;
            continue;
        }
        if (!name[0]) continue;
        make_path(path, dir, name);
        if (saving && !access(path, 0)) {
            char q[64];
            snprintf(q, sizeof q, "%s already exists.\nReplace it?", base_name(path));
            if (msgbox(title, q, "Yes|No") != 0) continue;
        }
        strcpy(dialog_dir, dir);
        return 1;
    }
}
static int file_dialog(const char *title, char *path, int saving) { in_dialog++; int r = file_dialog_(title, path, saving); in_dialog--; return r; }

/* -------------------------------------------------------------- the menus */
enum { M_NEW = 1, M_OPEN, M_SAVE, M_SAVEAS, M_PRINT, M_ABOUT, M_QUIT,
       M_UNDO, M_CLEAR, M_FLIPH, M_FLIPV, M_SWAP,
       M_PALETTE, M_DEFPAL, M_GRID, M_FULL, M_HELP };
typedef struct { const char *label, *keys; int id; char hot; } item_t;
static const item_t menu_file[] = {
    { "New", "^N", M_NEW, 'N' }, { "Open...", "^O", M_OPEN, 'O' }, { "Save", "^S", M_SAVE, 'S' }, { "Save As...", "", M_SAVEAS, 'A' },
    { "Print...", "^P", M_PRINT, 'P' }, { "-", "", 0, 0 }, { "About...", "", M_ABOUT, 'B' }, { "Quit", "Alt+X", M_QUIT, 'Q' }, { 0, 0, 0, 0 } };
static const item_t menu_edit[] = {
    { "Undo", "^Z", M_UNDO, 'U' }, { "Clear", "", M_CLEAR, 'C' }, { "-", "", 0, 0 }, { "Flip Horizontal", "", M_FLIPH, 'H' },
    { "Flip Vertical", "", M_FLIPV, 'V' }, { "Swap Colours", "X", M_SWAP, 'W' }, { 0, 0, 0, 0 } };
static const item_t menu_opts[] = {
    { "Palette...", "", M_PALETTE, 'P' }, { "Default Palette", "", M_DEFPAL, 'D' }, { "-", "", 0, 0 },
    { "Zoom Grid", "", M_GRID, 'G' }, { "Full Screen", "Tab", M_FULL, 'F' }, { "-", "", 0, 0 }, { "Keys...", "F1", M_HELP, 'K' }, { 0, 0, 0, 0 } };
static const item_t *const menus[3] = { menu_file, menu_edit, menu_opts };

static int menu_at(int x)
{
    for (int i = 0; i < 3; i++) {
        int w = (int)strlen(menu_title[i]) * 8;
        if (x >= menu_x[i] - 4 && x <= menu_x[i] + w + 3) return i;
    }
    return -1;
}

/* Runs a menu: the mouse (press-drag-release or click-click) or the keys.
 * held = opened by a mouse press. Returns the chosen id or 0. */
static int run_menu_(int m, int held)
{
    int sel = held ? -1 : 0, moved_in = 0;
    for (;;) {
        const item_t *it = menus[m];
        int n = 0, wmax = 0;
        for (; it[n].label; n++) {
            int w = (int)(strlen(it[n].label) + strlen(it[n].keys)) + 2;
            if (w > wmax) wmax = w;
        }
        int x0 = menu_x[m] - 4, y0 = MENU_H - 1, x1 = x0 + wmax * 8 + 12, y1 = y0 + n * 10 + 4;
        if (sel >= 0 && it[sel].id == 0) sel++;
        compose();
        draw_menubar(m);
        fillrect(scr, x0 + 3, y0 + 3, x1 + 3, y1 + 3, ui_blk);
        bevel(x0, y0, x1, y1, 0);
        for (int i = 0; i < n; i++) {
            int yy = y0 + 3 + i * 10;
            if (it[i].id == 0) { hline(scr, x0 + 3, x1 - 3, yy + 4, ui_drk); continue; }
            int on = i == sel;
            if (on) fillrect(scr, x0 + 2, yy - 1, x1 - 2, yy + 8, ui_hil);
            int c = on ? ui_wht : ui_blk;
            if ((it[i].id == M_GRID && grid) || (it[i].id == M_FULL && fullscreen)) glyph8(scr, x0 + 1, yy, 0xFB, c);
            if (it[i].id == M_UNDO && !undo_n) c = on ? ui_lit : ui_drk;
            text8(scr, x0 + 8, yy, it[i].label, c);
            const char *h = strchr(it[i].label, it[i].hot);
            if (!h) h = strchr(it[i].label, tolower(it[i].hot));
            if (h) hline(scr, x0 + 8 + (int)(h - it[i].label) * 8, x0 + 14 + (int)(h - it[i].label) * 8, yy + 8, c);
            text8(scr, x1 - 4 - (int)strlen(it[i].keys) * 8, yy, it[i].keys, c);
        }
        blit();
        ev_t e;
        next_event(&e, 0);
        int over = -1;
        if (e.x > x0 && e.x < x1 && e.y > y0 && e.y < y1 - 2) {
            over = (e.y - y0 - 2) / 10;
            if (over >= n || it[over].id == 0) over = -1;
        }
        if (e.moved) {
            if (over >= 0) { sel = over; moved_in = 1; }
            else if (e.y < MENU_H && e.b) { int k = menu_at(e.x); if (k >= 0 && k != m) { m = k; sel = -1; continue; } }
        }
        if (e.release) {
            if (over >= 0 && (moved_in || !held)) return it[over].id;
            if (e.y < MENU_H && menu_at(e.x) == m) { held = 0; continue; }
            if (moved_in || !held) return 0;
            held = 0;
        }
        if (e.press) {
            if (over >= 0) { sel = over; continue; }
            if (e.y < MENU_H) { int k = menu_at(e.x); if (k >= 0) { if (k == m) return 0; m = k; sel = -1; held = 1; moved_in = 0; continue; } }
            return 0;
        }
        if (e.key == 27) return 0;
        if (e.key == 13 && sel >= 0) return it[sel].id;
        if (e.key == K_DOWN) { do sel = (sel + 1) % n; while (it[sel].id == 0); }
        if (e.key == K_UP) { if (sel < 0) sel = 0; do sel = (sel + n - 1) % n; while (it[sel].id == 0); }
        if (e.key == K_RIGHT) { m = (m + 1) % 3; sel = 0; }
        if (e.key == K_LEFT) { m = (m + 2) % 3; sel = 0; }
        if (e.key > 32 && e.key < 127)
            for (int i = 0; i < n; i++) if (it[i].id && it[i].hot == toupper(e.key)) return it[i].id;
    }
}
static int run_menu(int m, int held) { in_dialog++; int r = run_menu_(m, held); in_dialog--; return r; }

/* ------------------------------------------------------------ palette editor */
static void spread(int a, int b)
{
    if (a > b) { int t = a; a = b; b = t; }
    if (b - a < 2) return;
    for (int i = a + 1; i < b; i++)
        for (int k = 0; k < 3; k++)
            pal[i * 3 + k] = (uint8_t)(pal[a * 3 + k] + (pal[b * 3 + k] - pal[a * 3 + k]) * (i - a) / (b - a));
}

static void palette_dialog_(void)
{
    uint8_t saved[768];
    memcpy(saved, pal, 768);
    int sel = fg, spreading = 0, drag = -1;
    const int x0 = 20, y0 = 16, x1 = 299, y1 = 183, gx = 30, gy = 34;
    button_t bt[4] = { { 30, 160, 56, "OK" }, { 92, 160, 56, "Cancel" }, { 154, 160, 64, "Default" }, { 224, 160, 64, "Spread" } };
    for (;;) {
        compose();
        dlg_box(x0, y0, x1, y1, "Palette");
        frame(scr, gx - 1, gy - 1, gx + 128, gy + 96, ui_blk);
        for (int i = 0; i < 256; i++) fillrect(scr, gx + (i & 15) * 8, gy + (i >> 4) * 6, gx + (i & 15) * 8 + 7, gy + (i >> 4) * 6 + 5, i);
        int sx = gx + (sel & 15) * 8, sy = gy + (sel >> 4) * 6;
        frame(scr, sx - 1, sy - 1, sx + 8, sy + 6, ui_wht); frame(scr, sx - 2, sy - 2, sx + 9, sy + 7, ui_blk);
        char s[40];
        sprintf(s, "Colour %3d", sel);
        text8(scr, 170, 34, s, ui_blk);
        fillrect(scr, 170, 46, 289, 69, ui_blk); fillrect(scr, 171, 47, 288, 68, sel);
        static const char *const chn = "RGB";
        for (int k = 0; k < 3; k++) {
            int yy = 78 + k * 14;
            glyph8(scr, 170, yy, chn[k], ui_blk);
            frame(scr, 181, yy - 1, 246, yy + 8, ui_blk);
            fillrect(scr, 182, yy, 245, yy + 7, ui_wht);
            int v = pal[sel * 3 + k];
            fillrect(scr, 182, yy, 182 + v, yy + 7, k == 0 ? ui_red : k == 1 ? ui_grn : ui_blu);
            sprintf(s, "%2d", v);
            text8(scr, 252, yy, s, ui_blk);
        }
        text8(scr, 170, 124, spreading ? "Click the other" : "Keys: r/R g/G", ui_drk);
        text8(scr, 170, 134, spreading ? "end colour." : "b/B, arrows", ui_drk);
        for (int i = 0; i < 4; i++) draw_button(&bt[i], i == 0);
        blit();
        ev_t e;
        next_event(&e, 0);
        int changed = 0;
        if (e.key == 27) { memcpy(pal, saved, 768); apply_palette(); return; }
        if (e.key == 13) break;
        switch (e.key) {
        case K_LEFT: sel = (sel + 255) & 255; break;
        case K_RIGHT: sel = (sel + 1) & 255; break;
        case K_UP: sel = (sel + 240) & 255; break;
        case K_DOWN: sel = (sel + 16) & 255; break;
        case 'r': case 'g': case 'b': case 'R': case 'G': case 'B': {
            int k = (int)(strchr("rgb", tolower(e.key)) - "rgb");
            int v = pal[sel * 3 + k] + (isupper(e.key) ? 1 : -1);
            if (v >= 0 && v <= 63) { pal[sel * 3 + k] = (uint8_t)v; changed = 1; }
            break; }
        }
        if (e.press & 1) {
            if (e.x >= gx && e.x < gx + 128 && e.y >= gy && e.y < gy + 96) {
                int k = ((e.y - gy) / 6) * 16 + (e.x - gx) / 8;
                if (spreading) { spread(sel, k); spreading = 0; changed = 1; }
                sel = k;
            } else if (in_button(&bt[0], e.x, e.y)) break;
            else if (in_button(&bt[1], e.x, e.y)) { memcpy(pal, saved, 768); apply_palette(); return; }
            else if (in_button(&bt[2], e.x, e.y)) { memcpy(pal, defpal, 768); changed = 1; }
            else if (in_button(&bt[3], e.x, e.y)) spreading = 1;
            else if (e.x >= 181 && e.x <= 246 && e.y >= 77 && e.y < 77 + 3 * 14) drag = (e.y - 77) / 14;
        }
        if (!(e.b & 1)) drag = -1;
        if (drag >= 0 && (e.b & 1)) {
            int v = e.x - 182;
            v = v < 0 ? 0 : v > 63 ? 63 : v;
            if (pal[sel * 3 + drag] != v) { pal[sel * 3 + drag] = (uint8_t)v; changed = 1; }
        }
        if (changed) apply_palette();
    }
    fg = sel;
    if (memcmp(saved, pal, 768)) {
        /* undo gets the palette as it was */
        uint8_t now[768];
        memcpy(now, pal, 768); memcpy(pal, saved, 768);
        push_undo();
        memcpy(pal, now, 768);
        modified = 1;
    }
    apply_palette();
}
static void palette_dialog(void) { in_dialog++; palette_dialog_(); in_dialog--; }

/* ------------------------------------------------------------------ print */
static int print_quality = PQ_DRAFT, print_dither = DITHER_FS;

static int print_progress(int band, int bands)
{
    compose();
    dlg_box(70, 76, 249, 123, "Printing");
    char s[40];
    sprintf(s, "Line %d of %d", band + 1, bands);
    text8(scr, 90, 94, s, ui_blk);
    frame(scr, 89, 105, 230, 112, ui_blk);
    fillrect(scr, 90, 106, 90 + 140 * band / bands, 111, ui_hil);
    blit();
    while (key_ready()) if ((key_read() & 0xFF) == 27) return 1;
    return 0;
}

static void print_dialog_(void)
{
    const int x0 = 44, y0 = 40, x1 = 275, y1 = 159;
    button_t pr = { 74, 136, 72, "Print" }, cn = { 170, 136, 72, "Cancel" };
    static const char *const q[2] = { "Draft (60 dpi)", "Letter quality (120)" };
    static const char *const d[2] = { "Floyd-Steinberg", "Ordered (Bayer)" };
    for (;;) {
        compose();
        dlg_box(x0, y0, x1, y1, "Print");
        text8(scr, 54, 58, "Quality:", ui_blk);
        for (int i = 0; i < 2; i++) {
            glyph8(scr, 62, 70 + i * 10, i == print_quality ? 0x07 : 0x09, ui_blk);
            text8(scr, 74, 70 + i * 10, q[i], ui_blk);
            if (i == 0) text8(scr, 74 + 15 * 8, 70, "", ui_blk);
        }
        text8(scr, 54, 94, "Dithering:", ui_blk);
        for (int i = 0; i < 2; i++) {
            glyph8(scr, 62, 106 + i * 10, i == print_dither ? 0x07 : 0x09, ui_blk);
            text8(scr, 74, 106 + i * 10, d[i], ui_blk);
        }
        draw_button(&pr, 1); draw_button(&cn, 0);
        blit();
        ev_t e;
        next_event(&e, 0);
        int go = 0;
        if (e.key == 27) return;
        if (e.key == 13) go = 1;
        if (e.key == 'd' || e.key == 'D') print_quality = PQ_DRAFT;
        if (e.key == 'l' || e.key == 'L') print_quality = PQ_LETTER;
        if (e.key == 'f' || e.key == 'F') print_dither = DITHER_FS;
        if (e.key == 'o' || e.key == 'O') print_dither = DITHER_ORDERED;
        if (e.key == K_UP || e.key == K_DOWN) print_quality ^= 1;
        if (e.press & 1) {
            if (in_button(&pr, e.x, e.y)) go = 1;
            else if (in_button(&cn, e.x, e.y)) return;
            for (int i = 0; i < 2; i++) {
                if (e.y >= 69 + i * 10 && e.y < 79 + i * 10 && e.x >= 60 && e.x < 240) print_quality = i;
                if (e.y >= 105 + i * 10 && e.y < 115 + i * 10 && e.x >= 60 && e.x < 240) print_dither = i;
            }
        }
        if (!go) continue;
        int st = printer_status();
        if ((st & 0x29) || !(st & 0x10)) { msgbox("Print", "The printer is not ready.", "OK"); return; }
        char title[100];
        snprintf(title, sizeof title, "ARM Paint - %s", filename[0] ? filename : "UNTITLED.PCX");
        int r = print_picture(print_quality, print_dither, title, print_progress);
        if (r < 0) msgbox("Print", r == -2 ? "Not enough memory to print." : "Printer error.", "OK");
        else set_status(r ? "Printing cancelled" : "Printed");
        return;
    }
}
static void print_dialog(void) { in_dialog++; print_dialog_(); in_dialog--; }

/* ------------------------------------------------------------ file commands */
static int save_as(void);
static int save(void)
{
    if (!filename[0]) return save_as();
    if (save_picture(filename)) {
        char s[100];
        snprintf(s, sizeof s, "Cannot save %s.", base_name(filename));
        msgbox("Save", s, "OK");
        return 0;
    }
    modified = 0;
    char s[48];
    snprintf(s, sizeof s, "Saved %s", base_name(filename));
    set_status(s);
    return 1;
}
static int save_as(void)
{
    char p[80];
    if (!file_dialog("Save As", p, 1)) return 0;
    strcpy(filename, p);
    return save();
}
/* 1 = go ahead (saved or discarded), 0 = cancelled */
static int ask_save(void)
{
    if (!modified) return 1;
    char q[80];
    snprintf(q, sizeof q, "Save the changes to\n%s?", filename[0] ? base_name(filename) : "UNTITLED.PCX");
    int r = msgbox("ARM Paint", q, "Yes|No|Cancel");
    if (r == 0) return save();
    return r == 1;
}
static void open_file(const char *p)
{
    char msg[64];
    push_undo();
    int r = load_picture(p, msg);
    if (r) {
        pop_undo();
        char s[120];
        snprintf(s, sizeof s, "%s:\n%s", base_name(p),
                 r == -1 ? "File not found." : r == -3 ? "Not a 256-colour PCX or BMP\nfile ARM Paint can read." :
                 r == -4 ? "Not enough memory." : "Not a PCX or BMP picture.");
        msgbox("Open", s, "OK");
        return;
    }
    undo_n = 0;
    strncpy(filename, p, 79);
    for (char *c = filename; *c; c++) *c = (char)toupper(*c);
    apply_palette();
    modified = 0;
    zoom = 0; ox = oy = 0;
    if (msg[0]) set_status(msg);
}

static void about(void)
{
    msgbox("About ARM Paint",
           "ARM Paint  Version 1.00\n"
           "256 colours, PCX and BMP pictures\n\n"
           "Copyright (C) Europa Micro Systems\n1988.  All rights reserved.", "OK");
}
static void help(void)
{
    msgbox("Keys",
           "Left button: foreground colour\n"
           "Right button: background colour\n"
           "Arrows: cursor (Shift: faster)\n"
           "Space clicks, Ins holds button\n"
           "P pencil  B brush  A airbrush\n"
           "L line  R box  E ellipse\n"
           "Shift+R/E filled  F fill\n"
           "T text  Z zoom  K pick colour\n"
           "1-4 brush  [ ] colour  X swap\n"
           "PgUp PgDn Home End: scroll\n"
           "Tab full screen  ^Z undo", "OK");
}

/* ----------------------------------------------------------------- tools */
static int op = -1, ob, oc, sx0, sy0, lx, ly, hand_ox, hand_oy, hand_x, hand_y;

static int canvas_pos(int x, int y, int *ix, int *iy)
{
    if (fullscreen) { *ix = x; *iy = y; return 1; }
    if (x < CV_X || y < CV_Y || y >= PAL_Y) return 0;
    if (zoom) { *ix = zx + (x - CV_X) / ZOOM; *iy = zy + (y - CV_Y) / ZOOM; }
    else { *ix = ox + x - CV_X; *iy = oy + y - CV_Y; }
    return 1;
}
static void clamp_view(void)
{
    if (ox < 0) ox = 0;
    if (ox > W - CV_W) ox = W - CV_W;
    if (oy < 0) oy = 0;
    if (oy > H - CV_H) oy = H - CV_H;
    if (zx < 0) zx = 0;
    if (zx > W - ZV_W) zx = W - ZV_W;
    if (zy < 0) zy = 0;
    if (zy > H - ZV_H) zy = H - ZV_H;
}

static void text_render(void)
{
    restore_top();
    int x = text_x, y = text_y;
    for (int i = 0; i < text_n; i++) {
        if (text_buf[i] == '\n') { x = text_x; y += 8; continue; }
        glyph8(img, x, y, (uint8_t)text_buf[i], text_c);
        x += 8;
    }
}
static void text_end(void) { text_on = 0; }

static void set_tool(int t)
{
    if (t == T_UNDO) { text_end(); if (pop_undo()) { apply_palette(); modified = 1; } else set_status("Nothing to undo"); return; }
    if (t == T_PALETTE) { text_end(); palette_dialog(); return; }
    if (t != T_TEXT) text_end();
    if (t != tool && tool != T_PICKER && tool != T_ZOOM) prev_tool = tool;
    tool = t;
}

static void shape(int x, int y)
{
    restore_top();
    int x0 = sx0, y0 = sy0;
    if (shifted()) {                         /* squares, circles, lines at 45 degrees */
        int dx = x - x0, dy = y - y0, ax = abs(dx), ay = abs(dy);
        if (op == T_LINE) {
            if (ax > 2 * ay) y = y0; else if (ay > 2 * ax) x = x0;
            else { int m = ax > ay ? ax : ay; x = x0 + (dx < 0 ? -m : m); y = y0 + (dy < 0 ? -m : m); }
        } else {
            int m = ax > ay ? ax : ay;
            x = x0 + (dx < 0 ? -m : m); y = y0 + (dy < 0 ? -m : m);
        }
    }
    switch (op) {
    case T_LINE: line(img, x0, y0, x, y, oc, bsize); break;
    case T_RECT: case T_FRECT: rectangle(img, x0, y0, x, y, oc, op == T_FRECT, bsize); break;
    case T_ELLIPSE: case T_FELLIPSE: ellipse(img, x0, y0, x, y, oc, op == T_FELLIPSE, bsize); break;
    }
}

static void pointer(const ev_t *e)
{
    int ix, iy;
    int on = canvas_pos(e->x, e->y, &ix, &iy);
    if (op >= 0) {                                   /* an operation in progress */
        if (op == T_HAND) {
            int k = zoom ? ZOOM : 1;
            if (zoom) { zx = hand_ox - (e->x - hand_x) / k; zy = hand_oy - (e->y - hand_y) / k; }
            else { ox = hand_ox - (e->x - hand_x); oy = hand_oy - (e->y - hand_y); }
            clamp_view();
        } else {
            if (!on) {
                /* outside the canvas: follow along its edge */
                int cx = e->x < CV_X ? CV_X : e->x, cy = e->y < CV_Y ? CV_Y : e->y >= PAL_Y ? PAL_Y - 1 : e->y;
                if (fullscreen) cx = e->x, cy = e->y;
                canvas_pos(cx, cy, &ix, &iy);
            }
            switch (op) {
            case T_PENCIL: line(img, lx, ly, ix, iy, oc, 1); break;
            case T_BRUSH: line(img, lx, ly, ix, iy, oc, bsize); break;
            case T_ERASER: {
                int s = 4 + bsize * 2;
                int dx = abs(ix - lx), dy = abs(iy - ly), n = dx > dy ? dx : dy;
                for (int i = 0; i <= n; i++) {
                    int px = n ? lx + (ix - lx) * i / n : ix, py = n ? ly + (iy - ly) * i / n : iy;
                    fillrect(img, px - s / 2, py - s / 2, px + s / 2 - 1, py + s / 2 - 1, oc);
                }
                break; }
            case T_SPRAY: spray(img, ix, iy, 4 + bsize * 2, 6 + bsize * 2, oc); break;
            case T_PICKER: if (ob == 1) fg = img[iy * W + ix]; else bg = img[iy * W + ix]; break;
            case T_LINE: case T_RECT: case T_FRECT: case T_ELLIPSE: case T_FELLIPSE: shape(ix, iy); break;
            }
            lx = ix; ly = iy;
        }
        if (e->release & ob) {
            if (op == T_PICKER) tool = prev_tool;
            op = -1;
        }
        return;
    }
    if (!e->press) return;
    int b = (e->press & 1) ? 1 : 2;
    if (!fullscreen) {
        if (e->y < MENU_H) {
            int m = menu_at(e->x);
            if (m >= 0) {
                command(run_menu(m, 1));
            }
            return;
        }
        if (e->x < TB_W && e->y < PAL_Y) {
            int t = ((e->y - MENU_H) / 16) * 2 + e->x / 16;
            if (e->y < MENU_H + 8 * 16) { if (t >= 0 && t < NTOOLS) set_tool(t); }
            else { int k = (e->y - MENU_H - 8 * 16 - 2) / 9; if (k >= 0 && k < 4) bsize = sizes[k]; }
            return;
        }
        if (e->y >= PAL_Y) {
            if (e->x >= TB_W) {
                int i = ((e->y - PAL_Y) / 3) * 32 + (e->x - TB_W) / 9;
                if (i >= 0 && i < 256) { if (b == 1) fg = i; else bg = i; }
            } else palette_dialog();
            return;
        }
    }
    if (!on) return;
    oc = b == 1 ? fg : bg;
    ob = b;
    switch (tool) {
    case T_ZOOM:
        if (zoom) zoom = 0;
        else { zoom = 1; zx = ix - ZV_W / 2; zy = iy - ZV_H / 2; clamp_view(); }
        tool = prev_tool;
        return;
    case T_FILL: push_undo(); flood(img, ix, iy, oc); modified = 1; return;
    case T_TEXT:
        text_end(); push_undo();
        text_on = 1; text_x = ix; text_y = iy; text_c = oc; text_n = 0; modified = 1;
        return;
    case T_HAND: op = T_HAND; hand_x = e->x; hand_y = e->y; hand_ox = zoom ? zx : ox; hand_oy = zoom ? zy : oy; return;
    case T_PICKER: op = T_PICKER; break;
    default: push_undo(); modified = 1; op = tool; break;
    }
    if (tool == T_ERASER) oc = bg;
    sx0 = lx = ix; sy0 = ly = iy;
    ev_t e2 = *e;
    e2.release = 0;
    pointer(&e2);                                 /* the first dot */
}

static void flip(int horiz)
{
    push_undo(); modified = 1;
    if (horiz) {
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W / 2; x++) { uint8_t t = img[y * W + x]; img[y * W + x] = img[y * W + W - 1 - x]; img[y * W + W - 1 - x] = t; }
    } else {
        uint8_t row[W];
        for (int y = 0; y < H / 2; y++) {
            memcpy(row, img + y * W, W); memcpy(img + y * W, img + (H - 1 - y) * W, W); memcpy(img + (H - 1 - y) * W, row, W);
        }
    }
}

static int quitting;
void command(int id)
{
    char p[80];
    switch (id) {
    case M_NEW:
        if (!ask_save()) return;
        memset(img, bg, W * H); filename[0] = 0; modified = 0; undo_n = 0; zoom = 0; ox = oy = 0; text_end();
        break;
    case M_OPEN:
        if (!ask_save()) return;
        text_end();
        if (file_dialog("Open", p, 0)) open_file(p);
        break;
    case M_SAVE: text_end(); save(); break;
    case M_SAVEAS: text_end(); save_as(); break;
    case M_PRINT: text_end(); print_dialog(); break;
    case M_ABOUT: about(); break;
    case M_QUIT: if (ask_save()) quitting = 1; break;
    case M_UNDO: set_tool(T_UNDO); break;
    case M_CLEAR: text_end(); push_undo(); memset(img, bg, W * H); modified = 1; break;
    case M_FLIPH: text_end(); flip(1); break;
    case M_FLIPV: text_end(); flip(0); break;
    case M_SWAP: { int t = fg; fg = bg; bg = t; break; }
    case M_PALETTE: set_tool(T_PALETTE); break;
    case M_DEFPAL: push_undo(); memcpy(pal, defpal, 768); apply_palette(); modified = 1; break;
    case M_GRID: grid ^= 1; break;
    case M_FULL: fullscreen ^= 1; break;
    case M_HELP: help(); break;
    }
}

static void key(int k)
{
    if (text_on) {
        if (k == 27) { text_end(); return; }
        if (k == 8) { if (text_n) text_n--; text_render(); return; }
        if (k == 13) { if (text_n < 255) text_buf[text_n++] = '\n'; return; }
        if (k >= 32 && k < 256) { if (text_n < 255) text_buf[text_n++] = (char)k; text_render(); return; }
    }
    switch (k) {
    case 14: command(M_NEW); return;
    case 15: command(M_OPEN); return;
    case 19: command(M_SAVE); return;
    case 16: command(M_PRINT); return;
    case 26: command(M_UNDO); return;
    case 17: case K_ALTX: command(M_QUIT); return;
    case K_F10: case K_ALTF: command(run_menu(0, 0)); return;
    case K_ALTE: command(run_menu(1, 0)); return;
    case K_ALTO: command(run_menu(2, 0)); return;
    case K_F1: help(); return;
    case 9: fullscreen ^= 1; return;
    case K_PGUP: if (zoom) zy -= ZV_H / 2; else oy -= 16; clamp_view(); return;
    case K_PGDN: if (zoom) zy += ZV_H / 2; else oy += 16; clamp_view(); return;
    case K_HOME: if (zoom) zx -= ZV_W / 2; else ox -= 16; clamp_view(); return;
    case K_END: if (zoom) zx += ZV_W / 2; else ox += 16; clamp_view(); return;
    case 27: if (zoom) zoom = 0; else if (fullscreen) fullscreen = 0; return;
    }
    if (k > 255) return;
    switch (k) {
    case 'p': case 'P': set_tool(T_PENCIL); break;
    case 'b': case 'B': set_tool(T_BRUSH); break;
    case 'a': case 'A': set_tool(T_SPRAY); break;
    case 'l': case 'L': set_tool(T_LINE); break;
    case 'r': set_tool(T_RECT); break;
    case 'R': set_tool(T_FRECT); break;
    case 'e': set_tool(T_ELLIPSE); break;
    case 'E': set_tool(T_FELLIPSE); break;
    case 'f': case 'F': set_tool(T_FILL); break;
    case 't': case 'T': set_tool(T_TEXT); break;
    case 'x': case 'X': command(M_SWAP); break;
    case 'k': case 'K': set_tool(T_PICKER); break;
    case 'u': case 'U': command(M_UNDO); break;
    case 'z': case 'Z':
        if (zoom) zoom = 0;
        else { int ix, iy; if (!canvas_pos(mx, my, &ix, &iy)) { ix = ox + CV_W / 2; iy = oy + CV_H / 2; }
               zoom = 1; zx = ix - ZV_W / 2; zy = iy - ZV_H / 2; clamp_view(); }
        break;
    case 'h': case 'H': set_tool(T_HAND); break;
    case 'c': case 'C': set_tool(T_PALETTE); break;
    case '1': case '2': case '3': case '4': bsize = sizes[k - '1']; break;
    case '[': fg = (fg + 255) & 255; break;
    case ']': fg = (fg + 1) & 255; break;
    case '{': bg = (bg + 255) & 255; break;
    case '}': bg = (bg + 1) & 255; break;
    }
}

/* ------------------------------------------------------------------ main */
static void crit_fail(struct armregs *f) { f->r0 = (f->r0 & ~0xFFu) | 3; }   /* INT 24h: Fail */

static void splash(void)
{
    unsigned t0 = ticks();
    for (;;) {
        compose();
        dlg_box(60, 60, 259, 139, 0);
        const char *s1 = "ARM Paint";
        for (int dx = 0; dx < 2; dx++) text8(scr, 160 - 36 + dx, 72, s1, ui_hil);
        /* rainbow bar */
        for (int x = 0; x < 160; x++) vline(scr, 80 + x, 86, 89, 32 + x * 24 / 160);
        text8(scr, 160 - 13 * 4, 96, "Version 1.00", ui_blk);
        text8(scr, 72, 110, "(C) Europa Micro Systems", ui_blk);
        text8(scr, 160 - 11 * 4, 122, "1988", ui_drk);
        blit();
        ev_t e;
        next_event(&e, 1);
        if (e.key || e.press || ticks() - t0 > 36) return;
    }
}

int main(int argc, char **argv)
{
    struct armregs r;
    memset(&r, 0, sizeof r); r.r0 = 0x0F00; int10(&r);
    int old_mode = (int)(r.r0 & 0x7F);
    memset(&r, 0, sizeof r); r.r0 = 0x0013; int10(&r);
    memset(&r, 0, sizeof r); r.r0 = 0x1130; r.r1 = 0x0300; int10(&r);
    font8 = (const uint8_t *)r.r6;
    read_dac(defpal);
    memcpy(pal, defpal, 768);
    pick_ui_colours();
    undo_init();
    _dos_setvect(0x24, crit_fail);

    int bx = 0;
    have_mouse = mouse(0, &bx, 0, 0) == 0xFFFF;
    if (have_mouse) {
        int cx = 0, dx = 639; mouse(7, 0, &cx, &dx);
        cx = 0; dx = 199; mouse(8, 0, &cx, &dx);
        mouse_to(mx, my);
    }
    /* the dialogs start in PAINT.EXE's own directory */
    strncpy(dialog_dir, _armdos_progpath ? _armdos_progpath : "", 79);
    char *s = strrchr(dialog_dir, '\\');
    if (s) s[1] = 0;
    else { if (!getcwd(dialog_dir, 76)) strcpy(dialog_dir, "C:\\"); if (dialog_dir[strlen(dialog_dir) - 1] != '\\') strcat(dialog_dir, "\\"); }

    memset(img, 15, W * H);
    fg = 0; bg = 15;
    int want_splash = 1;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '/' && (argv[i][1] | 0x20) == 'n') { want_splash = 0; continue; }
        char p[80];
        if (!strchr(argv[i], '\\') && !strchr(argv[i], ':')) {
            if (!getcwd(p, 70)) p[0] = 0;
            if (p[0] && p[strlen(p) - 1] != '\\') strcat(p, "\\");
            strncat(p, argv[i], 79 - strlen(p));
        } else strncpy(p, argv[i], 79), p[79] = 0;
        for (char *c = p; *c; c++) *c = (char)toupper(*c);
        if (!strchr(base_name(p), '.')) strncat(p, ".PCX", 79 - strlen(p));
        if (!access(p, 0)) { open_file(p); want_splash = 0; }
        else strcpy(filename, p);                /* a new picture of that name */
    }
    if (want_splash) splash();
    if (!have_mouse) set_status("No mouse: see F1");

    while (!quitting) {
        compose();
        blit();
        ev_t e;
        next_event(&e, op == T_SPRAY || text_on);
        if (e.key) key(e.key);
        pointer(&e);
        if (op == T_SPRAY && !e.moved) spray(img, lx, ly, 4 + bsize * 2, 4 + bsize, oc);
    }
    memset(&r, 0, sizeof r); r.r0 = (unsigned)(old_mode == 0x13 ? 3 : old_mode); int10(&r);
    if (have_mouse) mouse(0, &bx, 0, 0);
    return 0;
}
