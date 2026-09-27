#ifndef LIB_H
#define LIB_H
#include <stdint.h>
typedef unsigned int u32;
typedef int s32;
extern u32 board_id;
void putch(int c);
void flush(void);
void puts_(const char *s);
void printf_(const char *fmt, ...);
void exit(int code) __attribute__((noreturn));
int semihost(int op, void *arg);
void *memcpy(void *d, const void *s, unsigned n);
void *memset(void *d, int c, unsigned n);
int memcmp(const void *x, const void *y, unsigned n);
unsigned strlen(const char *s);
static inline int on_qemu(void) { return board_id == 0x183; }
#endif
