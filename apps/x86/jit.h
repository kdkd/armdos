/* jit.h - the x86 -> ARM translator (jit.c) */
#ifndef JIT_H
#define JIT_H
#include <stdint.h>
int  jit_init(void);
int  jit_try(uint32_t ip);
void jit_invalidate(uint32_t lin);
void jit_invalidate_range(uint32_t lin, uint32_t len);
void jit_stats(char *buf, int n);
void elbow_desc_announce(void);       /* ports FCh-FFh: the page's ELBOW view (ARCH.md 4.7) */
void elbow_desc_clear(void);
#endif
