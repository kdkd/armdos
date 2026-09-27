/* apps/paint/paint.h - ARM Paint: shared declarations. */
#ifndef PAINT_H
#define PAINT_H
#include <stdint.h>

#define W 320
#define H 200

extern uint8_t img[W * H];          /* the picture */
extern uint8_t scr[W * H];          /* the screen being composed */
extern uint8_t pal[768];            /* the picture's palette (6-bit DAC values) */
extern uint8_t defpal[768];         /* the VGA's default palette, read at start-up */
extern const uint8_t *font8;        /* the ROM 8x8 font (INT 10h AX=1130h BH=03h) */
extern int ui_blk, ui_drk, ui_gry, ui_wht, ui_hil, ui_lit;   /* chrome colours (nearest in pal) */

/* gfx.c: drawing into a 320x200 buffer, clipped */
void pset(uint8_t *b, int x, int y, int c);
void hline(uint8_t *b, int x0, int x1, int y, int c);
void vline(uint8_t *b, int x, int y0, int y1, int c);
void fillrect(uint8_t *b, int x0, int y0, int x1, int y1, int c);
void frame(uint8_t *b, int x0, int y0, int x1, int y1, int c);
void stamp(uint8_t *b, int x, int y, int size, int c);          /* round brush of diameter size */
void line(uint8_t *b, int x0, int y0, int x1, int y1, int c, int size);
void rectangle(uint8_t *b, int x0, int y0, int x1, int y1, int c, int filled, int size);
void ellipse(uint8_t *b, int x0, int y0, int x1, int y1, int c, int filled, int size);
void flood(uint8_t *b, int x, int y, int c);
void spray(uint8_t *b, int x, int y, int radius, int n, int c);
void glyph8(uint8_t *b, int x, int y, int ch, int c);             /* transparent */
int  text8(uint8_t *b, int x, int y, const char *s, int c);       /* returns the end x */
void text8bg(uint8_t *b, int x, int y, const char *s, int c, int bg);
unsigned rnd(void);

/* file.c */
int  save_picture(const char *path);         /* .BMP -> BMP, else PCX; 0 = ok */
int  load_picture(const char *path, char *msg);   /* 0 = ok; msg gets a note (cropped ...) */
int  pcx_encode_line(const uint8_t *src, int n, uint8_t *out);

/* print.c */
enum { PQ_DRAFT = 0, PQ_LETTER = 1 };
enum { DITHER_FS = 0, DITHER_ORDERED = 1 };
int  print_picture(int quality, int dither, const char *title, int (*progress)(int band, int bands));
int  printer_status(void);                   /* INT 17h AH=02h: AH */

/* paint.c helpers used by the others */
void set_dac(const uint8_t *p);
void pick_ui_colours(void);
int  nearest(const uint8_t *p, int r, int g, int b);

#endif
