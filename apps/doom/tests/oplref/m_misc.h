#ifndef M_MISC_H
#define M_MISC_H
#include <stdio.h>
#include <stdarg.h>
#include "doomtype.h"
FILE *M_fopen(const char *f, const char *m);
int M_remove(const char *p);
boolean M_WriteFile(const char *name, const void *source, int length);
int M_ReadFile(const char *name, byte **buffer);
char *M_TempFile(const char *s);
int M_snprintf(char *buf, size_t buf_len, const char *s, ...);
boolean M_StringConcat(char *dest, const char *src, size_t dest_size);
#endif
