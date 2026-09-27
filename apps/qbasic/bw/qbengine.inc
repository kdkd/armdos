/*
 * qbengine.inc - the QB.EXE engine interface (qbengine.h), included at the
 * end of bwbasic.c under QBIDE so it can use bwbasic.c's static functions
 * (bwb_init, bwb_ladd, bwb_xtxtline).
 *
 * The program lives in the IDE's editor; for every run it is loaded into
 * bwBASIC line by line (the number bwBASIC gives each line is mapped back
 * to the editor line by the IDE). Errors, END, STOP, SYSTEM and Ctrl-Break
 * come back here (bwBASIC's longjmp to My->mark) instead of printing and
 * returning to a BASIC prompt. A break keeps bwBASIC's execution stack, so
 * the program can continue (F5) or be stepped (F8).
 *
 * Copyright (c) 2026 the ARM-DOS project. Part of the ARM-DOS port of
 * Bywater BASIC; distributed under the GNU GPL version 2 like bwBASIC.
 */
#include "dosvid.h"

int qbe_break_pending;                  /* Ctrl+Break or STOP seen */
void (*qbe_hook) (int line, int calls, int why);
static int qbe_status;
static int qbe_err_line;
static char qbe_err_msg[128];
static int qbe_in_run;                  /* inside qbe_run */
static int qbe_in_immediate;
static int qbe_jmp_ok;                  /* My->mark is set */

#define QBE_MAX_SUBS 128
static char qbe_subs[QBE_MAX_SUBS][NameLengthMax + 1];
static int qbe_nsubs;

void
qbe_set_error (int line, const char *msg)
{
  if (qbe_status == QBE_ERROR)
    return;                     /* the first one counts */
  qbe_status = QBE_ERROR;
  qbe_err_line = line;
  snprintf (qbe_err_msg, sizeof qbe_err_msg, "%s", msg ? msg : "");
}

const char *
qbe_error (int *line)
{
  if (line)
    *line = qbe_err_line;
  return qbe_err_msg;
}

void
qbe_stop (int how)
{
  qbe_status = how;
  bwb_clrexec ();
  SetOnError (0);
  if (qbe_jmp_ok)
    longjmp (My->mark, 1);
  exit (0);
}

void
qbe_abort (void)
{
  qbe_stop (QBE_ABORT);
}

int
qbe_running (void)
{
  return qbe_in_run;
}

/* the SUB / FUNCTION / GOSUB depth */
static int
qbe_calls (void)
{
  StackType *s;
  int n = 0;
  for (s = My->StackHead; s != NULL; s = s->next)
    if (s->ExecCode == EXEC_FUNCTION || s->ExecCode == EXEC_GOSUB)
      n++;
  return n;
}

void
qbe_statement (void *line)
{
  LineType *l = (LineType *) line;
  int why = qbe_break_pending;
  if (!qbe_in_run || qbe_in_immediate)
    return;
  if (l->cmdnum == C_REM || l->cmdnum == C_DATA)
    return;                     /* not statements one stops at */
  qbe_break_pending = 0;
  qbe_hook (l->number, qbe_calls (), why);
}

/* The QuickBASIC dialect: GW-BASIC's commands and functions plus the
   structured ones of Bywater BASIC 3 (whose dialect has many more: COUNT,
   PI, MAX, STR ... which would take away variable names QuickBASIC
   programs use). */
static const char *const qbe_b15_keep[] = {
  "CALL", "CASE", "CASE ELSE", "CONST", "DEFLNG", "DO", "END FUNCTION",
  "END SELECT", "END SUB", "EXIT", "EXIT DO", "EXIT FOR", "EXIT FUNCTION",
  "EXIT SUB", "FUNCTION", "LOOP", "SELECT", "SELECT CASE", "SUB", "UNTIL",
  "COMMAND$", "FREEFILE", "LBOUND", "UBOUND", "SLEEP", NULL
};

static int
qbe_keep (const char *name)
{
  int i;
  for (i = 0; qbe_b15_keep[i]; i++)
    if (bwb_stricmp (name, qbe_b15_keep[i]) == 0)
      return TRUE;
  return FALSE;
}

static void
qbe_dialect (void)
{
  size_t i;
  for (i = 0; i < NUM_COMMANDS; i++)
  {
    CommandType *c = &IntrinsicCommandTable[i];
    if ((c->OptionVersionBitmask & B15) && !(c->OptionVersionBitmask & G86)
        && !qbe_keep (c->name))
      c->OptionVersionBitmask &= ~B15;
  }
  for (i = 0; i < NUM_FUNCTIONS; i++)
  {
    IntrinsicFunctionType *f = &IntrinsicFunctionTable[i];
    if ((f->OptionVersionBitmask & B15) && !(f->OptionVersionBitmask & G86)
        && !qbe_keep (f->Name))
      f->OptionVersionBitmask &= ~B15;
  }
}

int
qbe_init (void)
{
  qbe_dialect ();
  if (bwb_init () == FALSE)
    return 0;
  bwx_dos_init ();
  dv_exit ();                   /* dv_init again for every run */
  My->CurrentVersion->OptionDateFormat = "%m-%d-%Y";   /* as QuickBASIC's */
  My->OptionDigitsInteger = 7;  /* single precision: PRINT 1/3 = .3333333 */
  return 1;
}

void
qbe_load_begin (void)
{
  LineType *l, *n;
  bwb_clrexec ();
  for (l = My->StartMarker->next; l != My->EndMarker; l = n)
  {
    n = l->next;
    bwb_freeline (l);
  }
  My->StartMarker->next = My->EndMarker;
  My->DataLine = My->EndMarker;
  My->DataPosition = 0;
  My->ContinueLine = NULL;
  My->NextValidLineNumber = MINLIN;
  /* forget the old program's SUBs, FUNCTIONs and labels: bwb_ladd looks
     names up in that table, and a stale "Title" label made the new
     "Title:" line a call of it ("Syntax error") */
  My->IsScanRequired = TRUE;
  bwb_scan ();
  My->IsScanRequired = TRUE;
  qbe_nsubs = 0;
  qbe_status = QBE_END;
  qbe_err_line = 0;
  qbe_err_msg[0] = NulChar;
}

void
qbe_declare_sub (const char *name)
{
  if (qbe_nsubs < QBE_MAX_SUBS && name && *name)
  {
    snprintf (qbe_subs[qbe_nsubs], sizeof qbe_subs[0], "%s", name);
    qbe_nsubs++;
  }
}

int
qbe_bufcmp (const char *a, int alen, const char *b, int blen)
{
  int n = alen < blen ? alen : blen;
  int r = n > 0 ? memcmp (a, b, (size_t) n) : 0;
  if (r != 0)
    return r;
  return alen < blen ? -1 : alen > blen ? 1 : 0;
}

/* ---------------------------------------------- QBasic -> bwBASIC ---- */

static int
qbe_idchar (int c)
{
  return bwb_isalnum (c) || c == '.' || c == '_';
}

/* is s (length n) the keyword kw (case-insensitive, whole word)? */
static int
qbe_word (const char *s, int n, const char *kw)
{
  return (int) bwb_strlen (kw) == n && bwb_strnicmp (s, kw, n) == 0;
}

static int
qbe_is_sub (const char *s, int n)
{
  int i;
  for (i = 0; i < qbe_nsubs; i++)
    if (qbe_word (s, n, qbe_subs[i]))
      return TRUE;
  return FALSE;
}

/* Translate one line: DECLARE, SHARED and COMMON become remarks, DIM
   SHARED is DIM, "SUB x STATIC" loses STATIC, "Name args" (a SUB called
   without CALL) becomes "CALL Name(args)". */
char *
qbe_translate (char *in)
{
  static char out[MAX_LINE_LENGTH + 64];
  size_t o = 0;
  const char *p = in;
  int stmt = TRUE;              /* at the start of a statement */
  int changed = FALSE;

  while (*p && o < sizeof out - 16)
  {
    if (*p == '"')
    {
      /* a string: copy it */
      out[o++] = *p++;
      while (*p && *p != '"' && o < sizeof out - 16)
        out[o++] = *p++;
      if (*p == '"')
        out[o++] = *p++;
      stmt = FALSE;
      continue;
    }
    if (*p == '\'')
      break;                    /* a comment: the rest as it is */
    if (*p == ':')
    {
      out[o++] = *p++;
      stmt = TRUE;
      continue;
    }
    if (*p == ' ' || *p == '\t')
    {
      out[o++] = *p++;
      continue;
    }
    if (bwb_isalpha ((unsigned char) *p))
    {
      const char *w = p;
      int n;
      while (qbe_idchar ((unsigned char) *p) || (p > w && (*p == '$' || *p == '%' || *p == '!' || *p == '#' || *p == '&')))
      {
        if (*p == '$' || *p == '%' || *p == '!' || *p == '#' || *p == '&')
        {
          p++;
          break;
        }
        p++;
      }
      n = (int) (p - w);
      if (qbe_word (w, n, "REM"))
      {
        p = w;
        break;
      }
      if (stmt)
      {
        if (qbe_word (w, n, "DECLARE") || qbe_word (w, n, "SHARED") || qbe_word (w, n, "COMMON")
            || qbe_word (w, n, "STATIC") || qbe_word (w, n, "DEFSNG"))
        {
          /* the rest of the statement becomes a remark */
          const char *e = p;
          memcpy (out + o, "REM ", 4);
          o += 4;
          memcpy (out + o, w, n);
          o += n;
          while (*e && *e != ':' && o < sizeof out - 16)
            out[o++] = *e++;
          p = e;
          changed = TRUE;
          stmt = FALSE;
          continue;
        }
        if (qbe_word (w, n, "SLEEP"))
        {
          /* SLEEP without a number: until a key is pressed */
          const char *q = p;
          while (*q == ' ')
            q++;
          if (*q == NulChar || *q == ':' || *q == '\'')
          {
            const char *k = "DO: LOOP WHILE INKEY$ = \"\"";
            memcpy (out + o, k, strlen (k));
            o += strlen (k);
            changed = TRUE;
            stmt = FALSE;
            continue;
          }
        }
        if (qbe_word (w, n, "CONST") || qbe_word (w, n, "WIDTH") || qbe_word (w, n, "LOCATE"))
        {
          /* CONST A = 1, B = 2 -> CONST A = 1: CONST B = 2;
             WIDTH 80, 25 -> WIDTH 80;
             LOCATE , , 0 -> LOCATE CSRLIN, POS(0), 0 */
          const char *e = p;
          int depth = 0, quote = FALSE, arg = 0;
          int isConst = qbe_word (w, n, "CONST"), isWidth = qbe_word (w, n, "WIDTH");
          memcpy (out + o, w, n);
          o += n;
          if (!isConst && !isWidth)
          {
            /* LOCATE: empty row / column */
            const char *q = e;
            while (*q == ' ')
              q++;
            if (*q == ',')
            {
              memcpy (out + o, " CSRLIN", 7);
              o += 7;
              changed = TRUE;
              e = q;
            }
          }
          while (*e && o < sizeof out - 24 && (quote || (*e != ':' && *e != '\'')))
          {
            char c = *e;
            if (c == '"')
              quote = !quote;
            else if (!quote && c == '(')
              depth++;
            else if (!quote && c == ')')
              depth--;
            else if (!quote && depth == 0 && c == ',')
            {
              arg++;
              if (isConst)
              {
                memcpy (out + o, ": CONST", 7);
                o += 7;
                e++;
                changed = TRUE;
                continue;
              }
              if (isWidth)
              {
                /* the rest (the number of lines) is dropped */
                while (*e && *e != ':' && *e != '\'')
                  e++;
                changed = TRUE;
                break;
              }
              /* LOCATE r, , c: the column */
              {
                const char *q = e + 1;
                while (*q == ' ')
                  q++;
                if (arg == 1 && *q == ',')
                {
                  memcpy (out + o, ", POS(0)", 8);
                  o += 8;
                  e = q;
                  changed = TRUE;
                  continue;
                }
              }
            }
            out[o++] = c;
            e++;
          }
          p = e;
          stmt = FALSE;
          continue;
        }
        if ((qbe_word (w, n, "DIM") || qbe_word (w, n, "REDIM")))
        {
          const char *q = p;
          while (*q == ' ')
            q++;
          if (bwb_strnicmp (q, "SHARED", 6) == 0 && !qbe_idchar ((unsigned char) q[6]))
          {
            memcpy (out + o, w, n);
            o += n;
            out[o++] = ' ';
            p = q + 6;
            while (*p == ' ')
              p++;
            changed = TRUE;
            stmt = FALSE;
            continue;
          }
        }
        if (qbe_word (w, n, "SUB") || qbe_word (w, n, "FUNCTION"))
        {
          /* SUB name (args) STATIC */
          const char *e = p + bwb_strlen (p);
          const char *t = e;
          while (t > p && t[-1] == ' ')
            t--;
          if (t - p >= 7 && bwb_strnicmp (t - 6, "STATIC", 6) == 0 && t[-7] == ' ')
          {
            memcpy (out + o, w, (size_t) (t - 7 - w));
            o += (size_t) (t - 7 - w);
            p = e;
            changed = TRUE;
            stmt = FALSE;
            continue;
          }
        }
        if (qbe_is_sub (w, n))
        {
          /* Name [args] -> CALL Name[(args)] */
          const char *q = p;
          while (*q == ' ')
            q++;
          if (*q != '=' && *q != '(')
          {
            const char *e = q;
            int quote = FALSE;
            memcpy (out + o, "CALL ", 5);
            o += 5;
            memcpy (out + o, w, n);
            o += n;
            while (*e && (quote || (*e != ':' && *e != '\'')))
            {
              if (*e == '"')
                quote = !quote;
              if (!quote && (e[-1] == ' ') && bwb_strnicmp (e, "ELSE", 4) == 0
                  && !qbe_idchar ((unsigned char) e[4]))
                break;          /* IF x THEN Name 1 ELSE ... */
              e++;
            }
            while (e > q && e[-1] == ' ')
              e--;
            if (e > q)
            {
              out[o++] = '(';
              while (q < e && o < sizeof out - 16)
                out[o++] = *q++;
              out[o++] = ')';
            }
            p = e;
            changed = TRUE;
            stmt = FALSE;
            continue;
          }
        }
      }
      memcpy (out + o, w, n);
      o += n;
      /* THEN / ELSE start a statement (IF x THEN Name 1 ELSE Name 2) */
      stmt = qbe_word (w, n, "THEN") || qbe_word (w, n, "ELSE");
      continue;
    }
    out[o++] = *p++;
    stmt = FALSE;
  }
  if (!changed)
    return in;
  while (*p && o < sizeof out - 1)
    out[o++] = *p++;
  out[o] = NulChar;
  return out;
}

int
qbe_load_line (const char *text)
{
  char *buf = My->ConsoleInput;
  int before = My->NextValidLineNumber;
  snprintf (buf, MAX_LINE_LENGTH, "%s", text);
  CleanLine (buf);
  if (is_empty_string (buf))
    return 0;
  buf = qbe_translate (buf);
  if (buf != My->ConsoleInput)
  {
    bwb_strcpy (My->ConsoleInput, buf);
    buf = My->ConsoleInput;
  }
  if (!bwb_ladd (buf, My->StartMarker, FALSE))
    return 0;
  /* the number it got: the last one before NextValidLineNumber */
  if (My->NextValidLineNumber > before)
    return My->NextValidLineNumber - 1;
  return before;
}

/* --------------------------------------------------------------- run -- */

int
qbe_start (void)
{
  static char empty[] = "";
  static LineType runline;
  LineType *r;

  qbe_status = QBE_END;
  qbe_err_line = 0;
  qbe_err_msg[0] = NulChar;
  qbe_break_pending = 0;
  WARN_CLEAR;
  My->ERR = 0;
  runline.buffer = empty;
  runline.position = 0;
  /* the structure first (FOR without NEXT ...) */
  My->IsScanRequired = TRUE;
  if (bwb_scan () == FALSE)
  {
    qbe_set_error (qbe_err_line, qbe_err_msg[0] ? qbe_err_msg : "Syntax error");
    return QBE_ERROR;
  }
  r = bwb_RUN (&runline);
  if (r == My->EndMarker || My->StackHead == NULL)
  {
    if (qbe_status != QBE_ERROR)
      qbe_set_error (0, My->ERROR4[0] ? My->ERROR4 : "Cannot run the program");
    return QBE_ERROR;
  }
  return QBE_END;
}

int
qbe_run (void)
{
  qbe_status = QBE_END;
  WARN_CLEAR;
  My->ERR = 0;
  qbe_jmp_ok = TRUE;
  qbe_in_run = TRUE;
  if (setjmp (My->mark) == 0)
  {
    while (My->StackHead != NULL)
    {
      LineType *l = My->StackHead->line;
      if (l == NULL || l == My->EndMarker)
      {
        bwb_clrexec ();
        break;
      }
      bwb_execline ();
    }
  }
  qbe_jmp_ok = FALSE;
  qbe_in_run = FALSE;
  bwb_clrexec ();
  return qbe_status;
}

int
qbe_set_next (int number)
{
  LineType *l;
  if (!qbe_in_run || My->StackHead == NULL)
    return FALSE;
  for (l = My->StartMarker->next; l != My->EndMarker; l = l->next)
    if (l->number == number)
    {
      l->position = 0;
      My->StackHead->line = l;
      My->ThisLine = l;
      return TRUE;
    }
  return FALSE;
}

int
qbe_immediate (const char *line)
{
  StackType *volatile savedHead;
  volatile int savedDepth;
  volatile int savedJmp = qbe_jmp_ok;
  volatile int savedStatus = qbe_status;
  LineType *volatile savedThis = My->ThisLine;
  jmp_buf savedMark;
  char *buf = My->ConsoleInput;
  int result;

  snprintf (buf, MAX_LINE_LENGTH, "%s", line);
  CleanLine (buf);
  if (is_empty_string (buf))
    return QBE_END;
  buf = qbe_translate (buf);
  if (buf != My->ConsoleInput)
  {
    bwb_strcpy (My->ConsoleInput, buf);
    buf = My->ConsoleInput;
  }
  /* a stopped program's stack and return point stay out of the way */
  memcpy (savedMark, My->mark, sizeof (jmp_buf));
  savedHead = My->StackHead;
  savedDepth = My->StackDepthInteger;
  My->StackHead = NULL;
  My->StackDepthInteger = 0;
  qbe_status = QBE_END;
  qbe_err_line = 0;
  qbe_err_msg[0] = NulChar;
  WARN_CLEAR;
  My->ERR = 0;
  if (!qbe_in_run && My->IsScanRequired && My->StartMarker->next != My->EndMarker)
    bwb_scan ();                /* SUBs and labels of the program */
  qbe_in_immediate = TRUE;
  qbe_jmp_ok = TRUE;
  if (setjmp (My->mark) == 0)
  {
    bwb_xtxtline (buf);
    while (My->StackHead != NULL)
      bwb_execline ();
  }
  qbe_in_immediate = FALSE;
  bwb_clrexec ();
  result = qbe_status;
  My->StackHead = savedHead;
  My->StackDepthInteger = savedDepth;
  My->ThisLine = savedThis;
  memcpy (My->mark, savedMark, sizeof (jmp_buf));
  qbe_jmp_ok = savedJmp;
  qbe_status = savedStatus;
  WARN_CLEAR;
  My->ERR = 0;
  if (result == QBE_ERROR)
    qbe_err_line = 0;
  return result;
}

/* the output screen around a run: our Ctrl-Break handlers, the console */
void
qbe_output_begin (int newrun)
{
  dv_init ();
  if (newrun)
    dv_reset ();
  My->SYSOUT->width = dv_cols ();
  My->SYSOUT->col = dv_pos ();
}

void
qbe_output_end (void)
{
  fflush (My->SYSOUT->cfp);
  dv_exit ();
}

/* "Press any key to continue" on the bottom line; qbe_output_unmessage
   puts the line (and the cursor) back as the program left them */
static unsigned short qbe_saved_row[132];
static int qbe_saved_cols, qbe_saved_cursor, qbe_saved_valid;

void
qbe_output_message (const char *msg)
{
  fflush (My->SYSOUT->cfp);
  qbe_saved_valid = FALSE;
  if (dv_basic_mode () == 0)
  {
    volatile unsigned short *v = (volatile unsigned short *) 0xB8000;
    int cols = *(volatile unsigned short *) 0x44A;
    int rows = *(volatile unsigned char *) 0x484 + 1;
    int i;
    if (cols > 132)
      cols = 132;
    qbe_saved_cols = cols;
    qbe_saved_cursor = *(volatile unsigned short *) 0x450;
    for (i = 0; i < cols; i++)
      qbe_saved_row[i] = v[(rows - 1) * cols + i];
    qbe_saved_valid = TRUE;
    for (i = 0; i < cols; i++)
      v[(rows - 1) * cols + i] = (unsigned short) (0x0700 | (msg[i] && i < (int) strlen (msg) ? (unsigned char) msg[i] : ' '));
    dv_locate (rows, (int) strlen (msg) + 1, -1);
    return;
  }
  /* graphics: through the BIOS */
  dv_locate (25, 1, -1);
  fputs (msg, My->SYSOUT->cfp);
  fflush (My->SYSOUT->cfp);
}

void
qbe_output_unmessage (void)
{
  if (qbe_saved_valid && dv_basic_mode () == 0)
  {
    volatile unsigned short *v = (volatile unsigned short *) 0xB8000;
    int rows = *(volatile unsigned char *) 0x484 + 1;
    int i;
    for (i = 0; i < qbe_saved_cols; i++)
      v[(rows - 1) * qbe_saved_cols + i] = qbe_saved_row[i];
    dv_locate ((qbe_saved_cursor >> 8) + 1, (qbe_saved_cursor & 0xFF) + 1, -1);
  }
  qbe_saved_valid = FALSE;
}

int
qbe_basic_mode (void)
{
  return dv_basic_mode ();
}

