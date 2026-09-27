/*
 * scr.c - the screen: everything is drawn into a shadow copy of the 80x25
 * text page and copied to video memory (0xB8000) in one go, with the mouse
 * pointer hidden meanwhile. Box and line characters are CP437.
 */
#include "shell.h"

uint16_t sb[ROWS * COLS];

int s_mono(void) { return *(volatile uint8_t *)0x449 == 7; }

/* a colour cell as a monochrome display shows it best: the brighter of the two
   colours becomes the lit one (reverse video when it is the background) */
uint16_t s_cell(uint16_t v)
{
    static const uint8_t lum[16] = { 0, 1, 3, 4, 2, 3, 4, 7, 5, 6, 8, 9, 7, 8, 10, 11 };
    if (!s_mono()) return v;
    int a = v >> 8, fg = a & 15, bg = (a >> 4) & 7, na;
    if (bg == 7 || lum[bg] > lum[fg]) na = 0x70;
    else if (fg == bg) na = 0x00;
    else na = (fg >= 8 || (fg == 7 && bg)) ? 0x0F : 0x07;
    return (uint16_t)((na << 8) | (v & 0xFF));
}

void s_fill(int r, int c, int h, int w, int ch, int attr)
{
    for (int y = r; y < r + h; y++) {
        if (y < 0 || y >= ROWS) continue;
        for (int x = c; x < c + w; x++)
            if (x >= 0 && x < COLS) sb[y * COLS + x] = (uint16_t)((attr << 8) | (ch & 0xFF));
    }
}

void s_ch(int r, int c, int ch, int attr)
{
    if (r < 0 || r >= ROWS || c < 0 || c >= COLS) return;
    sb[r * COLS + c] = (uint16_t)((attr << 8) | (ch & 0xFF));
}

int s_getch(int r, int c) { return sb[r * COLS + c] & 0xFF; }

void s_put(int r, int c, const char *s, int attr)
{
    while (*s) s_ch(r, c++, (uint8_t)*s++, attr);
}

void s_putn(int r, int c, const char *s, int n, int attr)
{
    for (int i = 0; i < n; i++) {
        int ch = ' ';
        if (s && *s) ch = (uint8_t)*s++;
        s_ch(r, c + i, ch, attr);
    }
}

void s_attr(int r, int c, int w, int attr)
{
    for (int x = c; x < c + w; x++)
        if (r >= 0 && r < ROWS && x >= 0 && x < COLS)
            sb[r * COLS + x] = (uint16_t)((sb[r * COLS + x] & 0xFF) | (attr << 8));
}

void s_box(int r, int c, int h, int w, int attr)
{
    s_ch(r, c, B_TL, attr);
    s_fill(r, c + 1, 1, w - 2, B_H, attr);
    s_ch(r, c + w - 1, B_TR, attr);
    for (int y = r + 1; y < r + h - 1; y++) {
        s_ch(y, c, B_V, attr);
        s_ch(y, c + w - 1, B_V, attr);
    }
    s_ch(r + h - 1, c, B_BL, attr);
    s_fill(r + h - 1, c + 1, 1, w - 2, B_H, attr);
    s_ch(r + h - 1, c + w - 1, B_BR, attr);
}

void s_hdiv(int r, int c, int w, int attr)
{
    s_ch(r, c, B_LT, attr);
    s_fill(r, c + 1, 1, w - 2, B_H, attr);
    s_ch(r, c + w - 1, B_RT, attr);
}

void s_center(int r, int c, int w, const char *s, int attr)
{
    int n = strlen(s);
    s_put(r, c + (w - n) / 2, s, attr);
}

void s_flush(void)
{
    mouse_show(0);
    volatile uint16_t *vram = S_VRAM;
    for (int i = 0; i < ROWS * COLS; i++) vram[i] = s_cell(sb[i]);
    mouse_show(1);
}

void s_raw_clear(int attr)
{
    mouse_show(0);
    volatile uint16_t *vram = S_VRAM;
    for (int i = 0; i < ROWS * COLS; i++) vram[i] = s_cell((uint16_t)((attr << 8) | ' '));
    for (int i = 0; i < ROWS * COLS; i++) sb[i] = (uint16_t)((attr << 8) | ' ');
}

static int cur_r = -2, cur_c;

void s_cursor(int r, int c)
{
    struct armregs x = {0};
    if (r < 0) {
        if (cur_r == -1) return;
        x.r0 = 0x0100;
        x.r2 = 0x2000;
        _armdos_int10(&x);
        cur_r = -1;
        return;
    }
    if (cur_r < 0) {
        x.r0 = 0x0100;
        x.r2 = s_mono() ? 0x0B0C : 0x0607;
        _armdos_int10(&x);
    }
    if (r != cur_r || c != cur_c) {
        memset(&x, 0, sizeof x);
        x.r0 = 0x0200;
        x.r3 = (uint32_t)((r << 8) | c);
        _armdos_int10(&x);
    }
    cur_r = r;
    cur_c = c;
}

void s_init(void)
{
    struct armregs x = {0};
    x.r0 = 0x0F00;                      /* current mode */
    _armdos_int10(&x);
    if ((x.r0 & 0x7F) != 3 && (x.r0 & 0x7F) != 7) {
        memset(&x, 0, sizeof x);
        x.r0 = 0x0003;
        _armdos_int10(&x);
    }
    cur_r = -2;
    s_cursor(-1, 0);
}

/* leave the screen for DOS: the cursor shape back to normal */
void s_done(void)
{
    struct armregs x = {0};
    x.r0 = 0x0100;
    x.r2 = s_mono() ? 0x0B0C : 0x0607;
    _armdos_int10(&x);
    cur_r = -2;
}
