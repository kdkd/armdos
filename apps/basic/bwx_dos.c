/*
 * bwx_dos.c - the ARM-DOS front end of Bywater BASIC (replaces bwx_tty.c):
 * CLS, LOCATE and COLOR on the real screen, and the GW-BASIC statements
 * bwBASIC does not have: SCREEN, PSET, PRESET, LINE, CIRCLE, PAINT, DRAW,
 * POINT, PALETTE, SOUND, BEEP, PLAY, CSRLIN, DEF SEG, KEY; plus Ctrl-Break.
 * The hardware work is in dosvid.c.
 *
 * Copyright (c) 2026 the ARM-DOS project. Part of the ARM-DOS port of
 * Bywater BASIC; distributed under the GNU GPL version 2 like bwBASIC.
 */
#include "bwbasic.h"
#include "dosvid.h"

/* ------------------------------------------------ the bwx_tty.c interface */
extern void
bwx_LOCATE (int Row, int Col)
{
  assert (My != NULL);
  if (Row < 1 || Col < 1 || dv_locate (Row, Col, -1) < 0)
  {
    WARN_ILLEGAL_FUNCTION_CALL;
    return;
  }
  My->SYSOUT->row = Row;
  My->SYSOUT->col = Col;
}

extern void
bwx_CLS (void)
{
  assert (My != NULL);
  fflush (My->SYSOUT->cfp);
  dv_cls ();
  My->SYSOUT->row = 1;
  My->SYSOUT->col = 1;
}

extern void
bwx_COLOR (int Fore, int Back)
{
  if (dv_color (Fore, Back, -1) < 0)
    WARN_ILLEGAL_FUNCTION_CALL;
}

/* ------------------------------------------------------------ start-up -- */
static void
bwx_dos_atexit (void)
{
  if (My && My->SYSOUT && My->SYSOUT->cfp)
    fflush (My->SYSOUT->cfp);
  fflush (stdout);
  dv_exit ();
}

extern void
bwx_dos_init (void)
{
  dv_init ();
  atexit (bwx_dos_atexit);
  if (dv_console ())
  {
    /* PRINT goes to the screen writer; errors too, in order with it */
    stdout = dv_console ();
    stderr = dv_console ();
    My->SYSOUT->cfp = stdout;
    My->SYSPRN->cfp = stderr;
    My->SYSOUT->width = dv_cols ();
  }
}

/* checked before every statement */
extern void
bwx_poll_break (void)
{
  if (dv_break ())
  {
    fflush (My->SYSOUT->cfp);
    bwx_STOP (TRUE);
  }
}

extern int
bwx_inkey (char *S)
{
  fflush (My->SYSOUT->cfp);
  return dv_inkey (S);
}

extern DoubleType
bwx_timer (void)
{
  return dv_timer ();
}

/* ------------------------------------------------------------ parsing -- */
/* [STEP] (x, y) */
static int
read_coord (LineType * l, DoubleType * x, DoubleType * y)
{
  int step = line_skip_word (l, "STEP");
  if (!line_skip_LparenChar (l)
      || !line_read_numeric_expression (l, x)
      || !line_skip_CommaChar (l)
      || !line_read_numeric_expression (l, y)
      || !line_skip_RparenChar (l))
  {
    return FALSE;
  }
  if (step)
  {
    double lx, ly;
    dv_lastpoint (&lx, &ly);
    *x += lx;
    *y += ly;
  }
  return TRUE;
}

/* an optional ", value" (empty allowed): 1 = got a value, 0 = empty or
   absent, -1 = error; *more says whether a comma was there at all */
static int
read_opt (LineType * l, DoubleType * v, int *more)
{
  *more = line_skip_CommaChar (l);
  if (!*more)
    return 0;
  if (line_is_eol (l) || buff_peek_char (l->buffer, &l->position, ','))
    return 0;
  return line_read_numeric_expression (l, v) ? 1 : -1;
}

static LineType *
gfx_result (LineType * l, int r)
{
  if (r == -1)
    WARN_ILLEGAL_FUNCTION_CALL;
  else if (r == -2)
    WARN_OUT_OF_MEMORY;
  return l;
}

static LineType *
cmd_pset (LineType * l, int preset)
{
  DoubleType x, y, c = -1;
  int more;
  if (!read_coord (l, &x, &y) || read_opt (l, &c, &more) < 0)
  {
    WARN_SYNTAX_ERROR;
    return l;
  }
  return gfx_result (l, dv_pset (x, y, (int) c, preset));
}

/* LINE [[STEP](x1,y1)]-[STEP](x2,y2)[,[color][,B[F]]] */
extern LineType *
bwx_LINE_graphics (LineType * l)
{
  DoubleType x1, y1, x2, y2, c = -1;
  int box = 0, more;
  double lx, ly;

  dv_lastpoint (&lx, &ly);
  x1 = lx;
  y1 = ly;
  if (!line_skip_MinusChar (l))
  {
    if (!read_coord (l, &x1, &y1) || !line_skip_MinusChar (l))
    {
      WARN_SYNTAX_ERROR;
      return l;
    }
  }
  if (!read_coord (l, &x2, &y2) || read_opt (l, &c, &more) < 0)
  {
    WARN_SYNTAX_ERROR;
    return l;
  }
  if (more && line_skip_CommaChar (l))
  {
    if (line_skip_word (l, "BF"))
      box = 2;
    else if (line_skip_word (l, "B"))
      box = 1;
    else if (!line_is_eol (l))
    {
      WARN_SYNTAX_ERROR;
      return l;
    }
  }
  return gfx_result (l, dv_line (x1, y1, x2, y2, (int) c, box));
}

/* CIRCLE [STEP](x,y),r[,color[,start[,end[,aspect]]]] */
static LineType *
cmd_circle (LineType * l)
{
  DoubleType x, y, r, c = -1, s = NAN, e = NAN, a = -1;
  int more;
  if (!read_coord (l, &x, &y) || !line_skip_CommaChar (l)
      || !line_read_numeric_expression (l, &r)
      || read_opt (l, &c, &more) < 0
      || (more && read_opt (l, &s, &more) < 0)
      || (more && read_opt (l, &e, &more) < 0)
      || (more && read_opt (l, &a, &more) < 0))
  {
    WARN_SYNTAX_ERROR;
    return l;
  }
  return gfx_result (l, dv_circle (x, y, r, (int) c, s, e, a));
}

/* PAINT [STEP](x,y)[,paint[,border]] */
static LineType *
cmd_paint (LineType * l)
{
  DoubleType x, y, c = -1, b = -1;
  int more;
  if (!read_coord (l, &x, &y) || read_opt (l, &c, &more) < 0
      || (more && read_opt (l, &b, &more) < 0))
  {
    WARN_SYNTAX_ERROR;
    return l;
  }
  return gfx_result (l, dv_paint (x, y, (int) c, (int) b));
}

/* DEF SEG [= segment] */
static LineType *
cmd_defseg (LineType * l)
{
  DoubleType v = 0;
  if (line_skip_EqualChar (l))
  {
    if (!line_read_numeric_expression (l, &v))
    {
      WARN_SYNTAX_ERROR;
      return l;
    }
    if (v < 0 || v > 65535)
    {
      WARN_ILLEGAL_FUNCTION_CALL;
      return l;
    }
  }
  dv_defseg ((long) v);
  return l;
}

/* KEY ON | OFF | LIST | n, a$ : there is no function-key line; accept */
static LineType *
cmd_key (LineType * l)
{
  if (line_skip_word (l, "ON") || line_skip_word (l, "OFF")
      || line_skip_word (l, "LIST"))
    return l;
  {
    DoubleType n;
    char *s = NULL;
    if (line_read_numeric_expression (l, &n) && line_skip_CommaChar (l)
        && line_read_string_expression (l, &s))
    {
      free (s);
      return l;
    }
  }
  WARN_SYNTAX_ERROR;
  return l;
}

extern LineType *
bwx_dos_command (int cmdnum, LineType * l)
{
  fflush (My->SYSOUT->cfp);
  switch (cmdnum)
  {
  case C_PSET:
    return cmd_pset (l, FALSE);
  case C_PRESET:
    return cmd_pset (l, TRUE);
  case C_CIRCLE:
    return cmd_circle (l);
  case C_PAINT:
    return cmd_paint (l);
  case C_DEF_SEG:
    return cmd_defseg (l);
  case C_KEY:
    return cmd_key (l);
  case C_BEEP:
    if (dv_beep ())
      bwx_poll_break ();
    return l;
  }
  WARN_INTERNAL_ERROR;
  return l;
}

/* ---------------------------------------------------------- functions -- */
static int
nth_number (VariableType * argv, int n, DoubleType * v)
{
  VariableType *a = argv;
  while (n-- > 0 && a)
    a = a->next;
  if (!a || VAR_IS_STRING (a))
    return FALSE;
  *v = *a->Value.Number;
  return TRUE;
}

static char *
nth_string (VariableType * argv, int n)
{
  VariableType *a = argv;
  while (n-- > 0 && a)
    a = a->next;
  if (!a || !VAR_IS_STRING (a))
    return NULL;
  a->Value.String->sbuffer[a->Value.String->length] = NulChar;
  return a->Value.String->sbuffer;
}

/* returns FALSE if 'id' is not ours; errors are raised here */
extern int
bwx_dos_function (int id, int argc, VariableType * argv, DoubleType * N)
{
  DoubleType X = 0, Y = 0, Z = 0;
  int r = 0;

  if (argc >= 1) nth_number (argv, 1, &X);
  if (argc >= 2) nth_number (argv, 2, &Y);
  if (argc >= 3) nth_number (argv, 3, &Z);
  *N = 0;
  fflush (My->SYSOUT->cfp);
  switch (id)
  {
  case F_SOUND_X_Y_N:
    r = dv_sound (X, Y);
    break;
  case F_SCREEN_X_N:
  case F_SCREEN_X_Y_N:
    r = dv_screen ((int) X);
    if (r == 0)
    {
      My->SYSOUT->width = dv_cols ();
      My->SYSOUT->col = 1;
    }
    break;
  case F_PLAY_A_N:
    r = dv_play (nth_string (argv, 1));
    break;
  case F_DRAW_A_N:
    r = dv_draw (nth_string (argv, 1));
    break;
  case F_POINT_X_Y_N:
    *N = dv_point (X, Y);
    break;
  case F_CSRLIN_N:
    *N = dv_csrlin ();
    break;
  case F_COLOR_X_N:
    r = dv_color ((int) X, -1, -1);
    break;
  case F_COLOR_X_Y_Z_N:
    r = dv_color ((int) X, (int) Y, (int) Z);
    break;
  case F_LOCATE_X_N:
    r = X < 1 ? -1 : dv_locate ((int) X, 0, -1);
    break;
  case F_LOCATE_X_Y_Z_N:
    r = X < 1 || Y < 1 ? -1 : dv_locate ((int) X, (int) Y, (int) Z);
    break;
  case F_PALETTE_X_Y_N:
    r = dv_palette ((long) X, (long) Y);
    break;
  case F_SHELL_N:
    *N = dv_shell ();
    break;
  default:
    return FALSE;
  }
  if (r == -1)
    WARN_ILLEGAL_FUNCTION_CALL;
  else if (r == 1)
    bwx_poll_break ();          /* Ctrl-Break during a sound */
  return TRUE;
}

/* EOF */

/* "NEXT I, J" -> "NEXT I: NEXT J" (bwBASIC's static scan pairs one NEXT
   with one FOR); called by bwb_ladd for every line entered or loaded.
   Returns 'buffer' itself when there is nothing to do. */
extern char *
bwx_expand_next (char *buffer)
{
  static char out[1024];
  char *p = buffer;
  size_t n = 0;
  int quote = 0, changed = 0, in_next = 0;

  while (*p && n < sizeof out - 8)
  {
    char c = *p;
    if (c == '"')
      quote = !quote;
    if (!quote)
    {
      if (c == '\'' || (bwb_strnicmp (p, "REM", 3) == 0
                        && (p == buffer || !bwb_isalnum ((unsigned char) p[-1]))))
      {
        break;                  /* the rest is a comment */
      }
      if (c == ':')
        in_next = 0;
      if (bwb_strnicmp (p, "NEXT", 4) == 0
          && (p == buffer || !bwb_isalnum ((unsigned char) p[-1]))
          && !bwb_isalnum ((unsigned char) p[4]))
      {
        in_next = 1;
      }
      else if (c == ',' && in_next)
      {
        memcpy (out + n, ": NEXT", 6);
        n += 6;
        p++;
        changed = 1;
        continue;
      }
    }
    out[n++] = c;
    p++;
  }
  if (!changed)
    return buffer;
  while (*p && n < sizeof out - 1)
    out[n++] = *p++;
  out[n] = NulChar;
  return out;
}

/* string comparison by length (embedded NULs count, as in GW-BASIC) */
extern int
bwx_bufcmp (const char *a, int alen, const char *b, int blen)
{
  int n = alen < blen ? alen : blen;
  int r = n > 0 ? memcmp (a, b, (size_t) n) : 0;
  if (r != 0)
    return r;
  return alen < blen ? -1 : alen > blen ? 1 : 0;
}
