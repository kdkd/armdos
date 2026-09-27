/* klib.h - freestanding helpers shared by IO.SYS, ARMDOS.SYS and HIMEM.SYS */
#ifndef KLIB_H
#define KLIB_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include "kabi.h"

void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strcpy(char *d, const char *s);
char *strcat(char *d, const char *s);
char *strchr(const char *s, int c);
size_t strlcpy(char *d, const char *s, size_t n);
int kvsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int ksnprintf(char *buf, size_t size, const char *fmt, ...);
int kint(int n, struct armregs *r);
void kdebug(const char *fmt, ...);

/* karm.S (ARM code, so that C may be built as Thumb) */
uint32_t irq_save(void);
void irq_restore(uint32_t cpsr);
void irq_on(void);
void irq_off(void);
void cpu_wfi(void);
uint32_t get_cpsr(void);
uint32_t read_far(void);
int svc21(struct armregs *r);

static inline int k_toupper(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }

#endif
