/* scr.c - direct video + BIOS keyboard, see scr.h. */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <dos.h>
#include <bios.h>
#include <armdos.h>
#include "scr.h"
#include "comm.h"

void scr_init(void)
{
    union REGS r;
    r.x.ax = 0x0F00; int86(0x10, &r, &r);
    if ((r.h.al & 0x7F) != 3) { r.x.ax = 0x0003; int86(0x10, &r, &r); }
}

void scr_cursor(int x, int y)
{
    union REGS r;
    r.h.ah = 2; r.h.bh = 0; r.h.dh = (uint8_t)y; r.h.dl = (uint8_t)x;
    int86(0x10, &r, &r);
}

void scr_cursor_on(int on)
{
    union REGS r;
    r.h.ah = 1; r.x.cx = on ? 0x0D0E : 0x2000;
    int86(0x10, &r, &r);
}

void scr_putc(int x, int y, int ch, int attr)
{
    if (x < 0 || y < 0 || x >= SCR_W || y >= SCR_H) return;
    VRAM[y * SCR_W + x] = (uint16_t)((attr << 8) | (ch & 0xFF));
}

void scr_puts(int x, int y, int attr, const char *s)
{
    while (*s && x < SCR_W) scr_putc(x++, y, (uint8_t)*s++, attr);
}

void scr_printf(int x, int y, int attr, const char *fmt, ...)
{
    char b[160];
    va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    scr_puts(x, y, attr, b);
}

void scr_fill(int x, int y, int w, int h, int ch, int attr)
{
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) scr_putc(x + i, y + j, ch, attr);
}

void scr_box(int x, int y, int w, int h, int attr, int dbl, const char *title)
{
    static const uint8_t s1[] = { 0xDA, 0xBF, 0xC0, 0xD9, 0xC4, 0xB3 };
    static const uint8_t s2[] = { 0xC9, 0xBB, 0xC8, 0xBC, 0xCD, 0xBA };
    const uint8_t *s = dbl ? s2 : s1;
    scr_fill(x + 1, y + 1, w - 2, h - 2, ' ', attr);
    for (int i = 1; i < w - 1; i++) { scr_putc(x + i, y, s[4], attr); scr_putc(x + i, y + h - 1, s[4], attr); }
    for (int j = 1; j < h - 1; j++) { scr_putc(x, y + j, s[5], attr); scr_putc(x + w - 1, y + j, s[5], attr); }
    scr_putc(x, y, s[0], attr); scr_putc(x + w - 1, y, s[1], attr);
    scr_putc(x, y + h - 1, s[2], attr); scr_putc(x + w - 1, y + h - 1, s[3], attr);
    if (title && *title) {
        int l = (int)strlen(title);
        scr_printf(x + (w - l - 2) / 2, y, attr, " %s ", title);
    }
}

void scr_shadow(int x, int y, int w, int h)
{
    /* dim the cells right of and below a window, like every 1989 TUI */
    for (int j = y + 1; j <= y + h; j++)
        for (int i = x + w; i < x + w + 2; i++)
            if (i < SCR_W && j < SCR_H) VRAM[j * SCR_W + i] = (VRAM[j * SCR_W + i] & 0xFF) | 0x0800;
    for (int i = x + 2; i < x + w + 2 && i < SCR_W; i++)
        if (y + h < SCR_H) VRAM[(y + h) * SCR_W + i] = (VRAM[(y + h) * SCR_W + i] & 0xFF) | 0x0800;
}

void scr_save(uint16_t *buf) { for (int i = 0; i < SCR_W * SCR_H; i++) buf[i] = VRAM[i]; }
void scr_restore(const uint16_t *buf) { for (int i = 0; i < SCR_W * SCR_H; i++) VRAM[i] = buf[i]; }
void scr_center(int y, int attr, const char *s) { scr_puts((SCR_W - (int)strlen(s)) / 2, y, attr, s); }

int key_ready(void) { return _bios_keybrd(_KEYBRD_READY) != 0; }

int key_get(void)
{
    while (!key_ready()) { com_poll(); idle(); }
    unsigned k = _bios_keybrd(_KEYBRD_READ);
    unsigned a = k & 0xFF, sc = (k >> 8) & 0xFF;
    if (a == 0 || a == 0xE0) return KEY_EXT | sc;
    return (int)a;
}

int key_shift(void) { return _bios_keybrd(_KEYBRD_SHIFTSTATUS) & 0xFF; }

int scr_input(int x, int y, int w, int attr, char *buf, int max)
{
    int len = (int)strlen(buf), pos = len, first = 1;
    scr_cursor_on(1);
    for (;;) {
        int off = pos >= w ? pos - w + 1 : 0;
        for (int i = 0; i < w; i++) scr_putc(x + i, y, off + i < len ? (uint8_t)buf[off + i] : ' ', attr);
        scr_cursor(x + pos - off, y);
        int k = key_get();
        if (k == K_ENTER) { scr_cursor_on(0); return 1; }
        if (k == K_ESC) { scr_cursor_on(0); return 0; }
        if (k == K_BS) { if (pos > 0) { memmove(buf + pos - 1, buf + pos, len - pos + 1); pos--; len--; } }
        else if (k == K_DEL) { if (pos < len) { memmove(buf + pos, buf + pos + 1, len - pos); len--; } }
        else if (k == K_LEFT) { if (pos > 0) pos--; }
        else if (k == K_RIGHT) { if (pos < len) pos++; }
        else if (k == K_HOME) pos = 0;
        else if (k == K_END) pos = len;
        else if (k >= 32 && k < 256) {
            if (first) { len = pos = 0; buf[0] = 0; }   /* typing replaces the default */
            if (len < max) { memmove(buf + pos + 1, buf + pos, len - pos + 1); buf[pos++] = (char)k; len++; }
        }
        first = 0;
    }
}

void beep(int hz, int ms)
{
    unsigned div = 1193182u / (unsigned)hz;
    armdos_outb(0x43, 0xB6);
    armdos_outb(0x42, div & 0xFF);
    armdos_outb(0x42, div >> 8);
    armdos_outb(0x61, armdos_inb(0x61) | 3);
    delay_ms(ms);
    armdos_outb(0x61, armdos_inb(0x61) & ~3);
}
