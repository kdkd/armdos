/* ARM-DOS: a stand-in for libedit's <editline/readline.h>. Line editing is
   DOS's own (INT 21h buffered input: Backspace, F1/F3 recall the last line,
   Esc cancels); see dosline.c. */
#ifndef ARMDOS_READLINE_H
#define ARMDOS_READLINE_H
char *readline(const char *prompt);
static inline int add_history(const char *line) { (void) line; return 0; }
#endif
