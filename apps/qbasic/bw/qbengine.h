/*
 * qbengine.h - Bywater BASIC as the engine of QB.EXE (ARM QuickBASIC):
 * load the program from the editor, run / continue / step it, execute
 * Immediate-window lines, report errors by line. Implemented in
 * qbengine.inc (included at the end of bwbasic.c, for its static helpers).
 *
 * Part of the ARM-DOS port of Bywater BASIC; GNU GPL version 2.
 */
#ifndef QBENGINE_H
#define QBENGINE_H

#ifdef __cplusplus
extern "C" {
#endif

/* how a run ended */
enum
{
  QBE_END = 0,          /* the program ended (END, or its last line) */
  QBE_ERROR,            /* a run-time or structure error: qbe_error() */
  QBE_SYSTEM,           /* SYSTEM */
  QBE_ABORT             /* the IDE stopped it (qbe_abort) */
};

int qbe_init (void);                  /* once; 0 = out of memory */
/* a new program: SUB names first (for "Name args" calls), then the lines
   in order; qbe_load_line returns the line number it got (0 = none) */
void qbe_load_begin (void);
void qbe_declare_sub (const char *name);
int qbe_load_line (const char *text);
/* prepare a run from the beginning (RUN: variables cleared, structure
   checked): QBE_END if ready, QBE_ERROR (a structure error) */
int qbe_start (void);
/* run the prepared program until it ends */
int qbe_run (void);
/* Before every statement of the program: hook(line, calls, why), calls =
   the SUB/FUNCTION/GOSUB depth, why = 1 after Ctrl+Break or STOP, else 0.
   The IDE may stop there (it runs its own event loop) and then return to
   go on, or call qbe_abort() (which does not return). */
extern void (*qbe_hook) (int line, int calls, int why);
void qbe_abort (void);
int qbe_running (void);               /* inside qbe_run */
/* Debug / Set Next Statement: the statement after the current one is the
   first one of the line with this number (TRUE if there is one) */
int qbe_set_next (int number);
/* execute an Immediate-window line (also while the program is stopped) */
int qbe_immediate (const char *line);
/* the last error: message; *line = its line (0 = immediate / none) */
const char *qbe_error (int *line);

/* the output screen around a run (newrun: text mode, default colours),
   the "Press any key to continue" line, the SCREEN mode left (0 = text) */
void qbe_output_begin (int newrun);
void qbe_output_end (void);
void qbe_output_message (const char *msg);
void qbe_output_unmessage (void);
int qbe_basic_mode (void);

/* for the interpreter */
extern int qbe_break_pending;
void qbe_stop (int how);
void qbe_set_error (int line, const char *msg);
void qbe_statement (void *line);      /* (LineType *) */
char *qbe_translate (char *buffer);   /* QBasic -> bwBASIC spelling */

#ifdef __cplusplus
}
#endif

#endif
