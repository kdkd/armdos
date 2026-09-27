/*
 * dosline.c - readline() for Open Adventure on ARM-DOS.
 *
 * Prints the prompt and reads one line from standard input through DOS
 * (so input redirection works, and on the console DOS's buffered input
 * gives Backspace, Esc and the F1/F3 template keys). Returns a malloc'd
 * line without the newline, or NULL at end of input, as readline() does.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "editline/readline.h"

char *readline(const char *prompt)
{
    char buf[1024];
    size_t n;

    if (prompt)
        fputs(prompt, stdout);
    fflush(stdout);
    if (!fgets(buf, sizeof buf, stdin))
        return NULL;
    n = strcspn(buf, "\r\n");
    if (buf[n] == '\0' && n == sizeof buf - 1) {    /* overlong: drop the rest */
        int c;
        while ((c = getchar()) != EOF && c != '\n')
            ;
    }
    buf[n] = '\0';
    return strdup(buf);
}
