/* dosvid.h - PC hardware services for BASIC.EXE (see dosvid.c). */
#ifndef DOSVID_H
#define DOSVID_H
#include <stdio.h>
void  dv_init(void);
void  dv_exit(void);
int   dv_is_tty(void);
FILE *dv_console(void);
void  dv_sync(void);
int   dv_break(void);
void  dv_clear_break(void);
int   dv_cols(void);
int   dv_mode(void);
void  dv_cls(void);
int   dv_locate(int row, int col, int cursor);   /* 1-based; <= 0 = keep */
int   dv_csrlin(void);
int   dv_pos(void);
int   dv_color(int a, int b, int c);            /* < 0 = omitted */
int   dv_palette(long attr, long color);
int   dv_screen(int mode);
int   dv_width(int cols);
void  dv_lastpoint(double *x, double *y);
int   dv_pset(double x, double y, int c, int preset);
int   dv_point(double x, double y);
int   dv_line(double x1, double y1, double x2, double y2, int c, int box);
int   dv_circle(double x, double y, double r, int c, double start, double end, double aspect);
int   dv_paint(double x, double y, int c, int border);
int   dv_draw(const char *s);
int   dv_inkey(char *out);
int   dv_getkey(void);
int   dv_sound(double hz, double ticks);
void  dv_sound_off(void);
int   dv_beep(void);
int   dv_play(const char *s);
void  dv_defseg(long seg);
int   dv_peek(long off, int *val);
int   dv_poke(long off, int val);
int   dv_inp(long port);
void  dv_out(long port, int val);
double dv_timer(void);
int   dv_files(FILE *out, const char *spec);
unsigned long dv_memfree(void);
int   dv_shell(void);
void  dv_reset(void);
int   dv_basic_mode(void);
#endif
