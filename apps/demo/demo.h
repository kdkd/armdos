/* demo.h - shared declarations for DEMO.EXE */
#ifndef DEMO_H
#define DEMO_H
#include <stdint.h>

/* fx.S */
void fx_copy(void *dst, const void *src, unsigned bytes);
void fx_fill(void *dst, uint32_t colour4, unsigned bytes);
void fx_plasma(uint8_t *dst, const uint8_t *s1, const uint8_t *s2, uint32_t row);
void fx_roto(uint8_t *dst, const uint8_t *tex, uint32_t uv, uint32_t duv);
void fx_tunnel(uint8_t *dst, const uint32_t *tab, const uint8_t *tex, uint32_t off, unsigned pixels);
void fx_scroll_col(uint8_t *dst, uint32_t bits, uint32_t colour);
int32_t fx_smulbb(int32_t a, int32_t b);
int32_t fx_smlabb(int32_t a, int32_t b, int32_t acc);

/* music.c */
void music_tick(void);
void music_stop(void);
extern volatile int music_enabled;

#endif
