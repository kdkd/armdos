/* scr.h - direct text-mode video (B800:0000) and BIOS keyboard helpers. */
#ifndef SCR_H_INCLUDED
#define SCR_H_INCLUDED
#include <stdint.h>

#define SCR_W 80
#define SCR_H 25
#define VRAM ((volatile uint16_t *)(*(volatile uint8_t *)0x449 == 7 ? 0xB0000 : 0xB8000))   /* mode 7: Hercules/MDA */

/* keys: ASCII 1-255, or KEY_EXT | scan code for extended keys (ascii 0 / E0) */
#define KEY_EXT   0x100
#define K_UP      (KEY_EXT | 0x48)
#define K_DOWN    (KEY_EXT | 0x50)
#define K_LEFT    (KEY_EXT | 0x4B)
#define K_RIGHT   (KEY_EXT | 0x4D)
#define K_HOME    (KEY_EXT | 0x47)
#define K_END     (KEY_EXT | 0x4F)
#define K_PGUP    (KEY_EXT | 0x49)
#define K_PGDN    (KEY_EXT | 0x51)
#define K_INS     (KEY_EXT | 0x52)
#define K_DEL     (KEY_EXT | 0x53)
#define K_F(n)    (KEY_EXT | (0x3A + (n)))      /* F1..F10 */
#define K_ALT(sc) (KEY_EXT | (sc))
/* Alt-letter scan codes */
#define ALT_A 0x1E
#define ALT_B 0x30
#define ALT_C 0x2E
#define ALT_D 0x20
#define ALT_E 0x12
#define ALT_F 0x21
#define ALT_H 0x23
#define ALT_I 0x17
#define ALT_L 0x26
#define ALT_P 0x19
#define ALT_R 0x13
#define ALT_S 0x1F
#define ALT_T 0x14
#define ALT_V 0x2F
#define ALT_X 0x2D
#define ALT_Z 0x2C
#define K_ESC 27
#define K_ENTER 13
#define K_BS 8

void scr_init(void);
void scr_cursor(int x, int y);
void scr_cursor_on(int on);
void scr_putc(int x, int y, int ch, int attr);
void scr_puts(int x, int y, int attr, const char *s);
void scr_printf(int x, int y, int attr, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
void scr_fill(int x, int y, int w, int h, int ch, int attr);
void scr_box(int x, int y, int w, int h, int attr, int dbl, const char *title);
void scr_shadow(int x, int y, int w, int h);
void scr_save(uint16_t *buf);          /* 80*25 cells */
void scr_restore(const uint16_t *buf);
void scr_center(int y, int attr, const char *s);

int  key_ready(void);
int  key_get(void);                    /* waits (sleeping) */
int  key_shift(void);                  /* BIOS shift flags */
/* single-line editor in a field; returns 1 Enter, 0 Esc */
int  scr_input(int x, int y, int w, int attr, char *buf, int max);
void beep(int hz, int ms);
#endif
