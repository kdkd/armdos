/*
 * bwx_dos.h - ARM-DOS additions to Bywater BASIC (included at the end of
 * bwbasic.h when ARMDOS is defined). GPL-2, like bwBASIC.
 */
#ifndef BWX_DOS_H
#define BWX_DOS_H

/* new commands (statements with their own syntax) */
#define C_PSET      301     /* PSET [STEP](x,y)[,c]                         */
#define C_PRESET    302     /* PRESET [STEP](x,y)[,c]                       */
#define C_CIRCLE    303     /* CIRCLE [STEP](x,y),r[,c[,start[,end[,asp]]]] */
#define C_PAINT     304     /* PAINT [STEP](x,y)[,paint[,border]]           */
#define C_DEF_SEG   305     /* DEF SEG [= segment]                          */
#define C_KEY       306     /* KEY ON | OFF | LIST | n, a$  (accepted)      */
#define C_BEEP      307     /* BEEP                                         */

/* new intrinsic functions (also usable as statements: SOUND 440, 18) */
#define F_SOUND_X_Y_N      401
#define F_SCREEN_X_N       402
#define F_SCREEN_X_Y_N     403
#define F_PLAY_A_N         404
#define F_DRAW_A_N         405
#define F_POINT_X_Y_N      406
#define F_CSRLIN_N         407
#define F_COLOR_X_N        408
#define F_LOCATE_X_N       409
#define F_LOCATE_X_Y_Z_N   410
#define F_PALETTE_X_Y_N    411
#define F_COLOR_X_Y_Z_N    412
#define F_SHELL_N          413

/* the command table rows and the function table rows (bwx_dos.c) */
#define BWX_DOS_COMMANDS \
  { C_PSET, "PSET [STEP](x,y)[,color]", "Sets a pixel.", "PSET", B15 | G86 }, \
  { C_PRESET, "PRESET [STEP](x,y)[,color]", "Resets a pixel.", "PRESET", B15 | G86 }, \
  { C_CIRCLE, "CIRCLE [STEP](x,y),r[,color[,start[,end[,aspect]]]]", "Draws a circle.", "CIRCLE", B15 | G86 }, \
  { C_PAINT, "PAINT [STEP](x,y)[,paint[,border]]", "Fills an area.", "PAINT", B15 | G86 }, \
  { C_DEF_SEG, "DEF SEG [= segment]", "Sets the segment for PEEK and POKE.", "DEF SEG", B15 | G86 }, \
  { C_KEY, "KEY ON|OFF|LIST", "Function key display (accepted, no effect).", "KEY", G86 }, \
  { C_BEEP, "BEEP", "Sounds the speaker.", "BEEP", B15 | G86 },

#define BWX_DOS_FUNCTIONS \
  { F_SOUND_X_Y_N, "N  = SOUND( X, Y )", "Sounds X Hz for Y clock ticks (18.2 per second).", "SOUND", DoubleTypeCode, 2, P1NUM | P2NUM, P1ANY | P2ANY, B15 | G86 }, \
  { F_SCREEN_X_N, "N  = SCREEN( X )", "Sets the screen mode: 0 text, 1 320x200x4, 2 640x200x2, 13 320x200x256.", "SCREEN", DoubleTypeCode, 1, P1NUM, P1BYT, B15 | G86 }, \
  { F_SCREEN_X_Y_N, "N  = SCREEN( X, Y )", "Sets the screen mode (Y is ignored).", "SCREEN", DoubleTypeCode, 2, P1NUM | P2NUM, P1BYT | P2ANY, B15 | G86 }, \
  { F_PLAY_A_N, "N  = PLAY( A$ )", "Plays music (the music macro language).", "PLAY", DoubleTypeCode, 1, P1STR, P1ANY, B15 | G86 }, \
  { F_DRAW_A_N, "N  = DRAW( A$ )", "Draws lines (the graphics macro language).", "DRAW", DoubleTypeCode, 1, P1STR, P1ANY, B15 | G86 }, \
  { F_POINT_X_Y_N, "N  = POINT( X, Y )", "The colour of the pixel at X, Y (-1 if off screen).", "POINT", DoubleTypeCode, 2, P1NUM | P2NUM, P1ANY | P2ANY, B15 | G86 }, \
  { F_CSRLIN_N, "N  = CSRLIN", "The cursor row (1-based).", "CSRLIN", DoubleTypeCode, 0, PNONE, PNONE, B15 | G86 }, \
  { F_COLOR_X_N, "N  = COLOR( X )", "Sets the foreground colour.", "COLOR", DoubleTypeCode, 1, P1NUM, P1BYT, B15 | G86 }, \
  { F_COLOR_X_Y_Z_N, "N  = COLOR( X, Y, Z )", "Sets foreground, background and border colours.", "COLOR", DoubleTypeCode, 3, P1NUM | P2NUM | P3NUM, P1BYT | P2BYT | P3BYT, B15 | G86 }, \
  { F_LOCATE_X_N, "N  = LOCATE( X )", "Moves the cursor to row X.", "LOCATE", DoubleTypeCode, 1, P1NUM, P1BYT, B15 | G86 }, \
  { F_LOCATE_X_Y_Z_N, "N  = LOCATE( X, Y, Z )", "Moves the cursor to row X, column Y; Z = 0 hides it, 1 shows it.", "LOCATE", DoubleTypeCode, 3, P1NUM | P2NUM | P3NUM, P1BYT | P2BYT | P3BYT, B15 | G86 }, \
  { F_SHELL_N, "N  = SHELL", "Runs COMMAND.COM; EXIT returns to BASIC.", "SHELL", DoubleTypeCode, 0, PNONE, PNONE, B15 | G86 }, \
  { F_PALETTE_X_Y_N, "N  = PALETTE( X, Y )", "Sets palette entry X to colour Y (SCREEN 13: Y = &Hbbggrr, 0-63 each).", "PALETTE", DoubleTypeCode, 2, P1NUM | P2NUM, P1ANY | P2ANY, B15 | G86 },

extern void bwx_dos_init (void);
extern LineType *bwx_dos_command (int cmdnum, LineType * l);
extern LineType *bwx_LINE_graphics (LineType * l);
extern int bwx_dos_function (int id, int argc, VariableType * argv, DoubleType * N);
extern void bwx_poll_break (void);
extern int bwx_inkey (char *S);
extern DoubleType bwx_timer (void);
extern char *bwx_expand_next (char *buffer);
extern int bwx_bufcmp (const char *a, int alen, const char *b, int blen);

#endif
