/*
 * view.c - F3, the file viewer: text (tabs expanded, horizontal scrolling)
 * or hex, with search.
 */
#include <stdlib.h>
#include "acm.h"

#define VROWS 23

static uint8_t *data;
static uint32_t size;

static uint32_t next_line(uint32_t o)
{
    while (o < size && data[o] != '\n') o++;
    return o < size ? o + 1 : size;
}

static uint32_t prev_line(uint32_t o)
{
    if (o == 0) return 0;
    uint32_t p = o - 1;             /* the '\n' ending the previous line */
    while (p > 0 && data[p - 1] != '\n') p--;
    return p;
}

static uint32_t draw_text(uint32_t top, int col, int y0, int rows)
{
    uint32_t o = top;
    for (int r = 0; r < rows; r++) {
        int x = 0;
        fill(0, y0 + r, SCR_W, 1, ' ', A_PANEL);
        if (o >= size) continue;
        while (o < size && data[o] != '\n') {
            uint8_t c = data[o++];
            if (c == '\r') continue;
            if (c == '\t') { x = (x + 8) & ~7; continue; }
            if (x >= col && x - col < SCR_W) putc_(x - col, y0 + r, c, A_PANEL);
            x++;
        }
        if (o < size) o++;
    }
    return o;
}

static uint32_t draw_hex(uint32_t top, int y0, int rows)
{
    static const char hx[] = "0123456789ABCDEF";
    uint32_t o = top;
    for (int r = 0; r < rows; r++, o += 16) {
        fill(0, y0 + r, SCR_W, 1, ' ', A_PANEL);
        if (o >= size) continue;
        char t[12];
        for (int i = 0; i < 8; i++) t[i] = hx[(o >> (28 - 4 * i)) & 15];
        t[8] = ':'; t[9] = 0;
        put(0, y0 + r, t, A_PANEL);
        for (int i = 0; i < 16 && o + i < size; i++) {
            uint8_t b = data[o + i];
            putc_(10 + i * 3, y0 + r, hx[b >> 4], A_PANEL);
            putc_(11 + i * 3, y0 + r, hx[b & 15], A_PANEL);
            if (i == 7 && o + 8 < size) putc_(12 + i * 3, y0 + r, '-', A_PANEL);
            putc_(59 + i, y0 + r, b ? b : '.', A_PANEL);
        }
    }
    return o > size ? size : o;
}

static int lc(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static int search(const char *s, uint32_t from, uint32_t *at)
{
    int l = strlen(s);
    if (!l) return 0;
    for (uint32_t o = from; o + l <= size; o++) {
        int i = 0;
        while (i < l && lc(data[o + i]) == lc((uint8_t)s[i])) i++;
        if (i == l) { *at = o; return 1; }
    }
    return 0;
}

void view_file(const char *path)
{
    int h = d_open(path, 0);
    if (h < 0) { error_box("Cannot open the file", path); return; }
    size = d_filesize(h);
    uint32_t cap = size;
    data = 0;
    while (!(data = malloc(cap + 1)) && cap > 4096) cap /= 2;
    if (!data) { d_close(h); error_box("Not enough memory to view", path); return; }
    uint32_t got = 0;
    while (got < cap) {
        int want = cap - got > 32768 ? 32768 : cap - got;
        int n = d_read(h, data + got, want);
        if (n <= 0) break;
        got += n;
    }
    d_close(h);
    size = got;

    static char findstr[40];
    char title[90];
    strcpy(title, "View: ");
    strcat(title, path);
    str_lower(title + 6);
    int hex = 0, col = 0;
    uint32_t top = 0;
    uint16_t save[SCR_W * SCR_H];
    save_scr(save);
    void (*oldidle)(void) = idle_hook;
    idle_hook = 0;
    for (;;) {
        uint32_t bottom = hex ? draw_hex(top, 1, VROWS) : draw_text(top, col, 1, VROWS);
        fill(0, 0, SCR_W, 1, ' ', 0x30);
        putn(0, 0, title, 28, 0x30);
        char t[24], n[16];
        strcpy(t, "Col "); u2s(n, col); strcat(t, n);
        put(29, 0, t, 0x30);
        u2s(n, size); strcpy(t, n); strcat(t, " Bytes");
        put(66 - strlen(t), 0, t, 0x30);
        u2s_pad(t, size ? (uint32_t)((uint64_t)bottom * 100 / size) : 100, 3);
        strcat(t, "%");
        put(76, 0, t, 0x30);
        static const char *kb[10] = { "", "", "", "Hex", "", "", "Search", "", "", "Quit" };
        kb[3] = hex ? "Text" : "Hex";
        keybar_draw(kb);
        flush();
        cursor_at(0, -1);
        int k = getkey();
        if (k == K_MOUSE) {
            if (ms_y == KEYROW) k = K_F(ms_x / 8 + 1 > 10 ? 10 : ms_x / 8 + 1);
            else if (ms_y < 12) k = K_UP;
            else k = K_DOWN;
        }
        if (k == K_ESC || k == K_F(10) || k == K_F(3)) break;
        if (k == K_F(4)) {
            hex = !hex;
            if (hex) top &= ~15u;
            else top = prev_line(next_line(top));
            continue;
        }
        if (k == K_F(7)) {
            ditem it[] = {
                { DI_TEXT, 3, 2, 0, "Search for", 0, 0, 0 },
                { DI_INPUT, 3, 3, 34, 0, findstr, 40, 0 },
            };
            if (dialog("Search", 40, 6, &PAL_GREY, it, 2, 1) != 0 || !findstr[0]) continue;
            uint32_t at;
            if (search(findstr, hex ? top + 16 : next_line(top), &at) || search(findstr, 0, &at))
                top = hex ? at & ~15u : prev_line(next_line(at));
            else {
                static const char *const ok[] = { "OK" };
                message("Search", "String not found", findstr, &PAL_GREY, ok, 1);
            }
            continue;
        }
        int step = hex ? 16 : 0;
        switch (k) {
        case K_DOWN:
            if (bottom < size) top = hex ? top + step : next_line(top);
            break;
        case K_UP:
            top = hex ? (top >= 16 ? top - 16 : 0) : prev_line(top);
            break;
        case K_PGDN:
            for (int i = 0; i < VROWS - 1 && bottom < size; i++) {
                top = hex ? top + 16 : next_line(top);
                bottom = hex ? bottom + 16 : next_line(bottom);
            }
            break;
        case K_PGUP:
            for (int i = 0; i < VROWS - 1; i++) top = hex ? (top >= 16 ? top - 16 : 0) : prev_line(top);
            break;
        case K_HOME: case K_CHOME: case K_CPGUP: top = 0; col = 0; break;
        case K_END: case K_CEND: case K_CPGDN:
            if (hex) { top = size > VROWS * 16u ? ((size + 15) & ~15u) - VROWS * 16 : 0; }
            else {
                top = size;
                for (int i = 0; i < VROWS; i++) top = prev_line(top);
            }
            break;
        case K_LEFT: if (col >= 10) col -= 10; else col = 0; break;
        case K_RIGHT: if (!hex && col < 900) col += 10; break;
        }
    }
    idle_hook = oldidle;
    free(data);
    data = 0;
    restore_scr(save);
}
