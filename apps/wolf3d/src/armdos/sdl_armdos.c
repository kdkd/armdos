/*
 * sdl_armdos.c - the 8-bit surfaces behind the SDL.h shim (see SDL.h).
 * Time (SDL_GetTicks/SDL_Delay) lives in id_sd.c with the timer interrupt,
 * the mouse in id_in.c with the keyboard.
 *
 * Copyright (C) 2026 the ARM-DOS project. GPL-2 or later, like Wolf4SDL.
 */
#include <stdlib.h>
#include <string.h>
#include "SDL.h"

SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int w, int h, int depth,
                                  Uint32 r, Uint32 g, Uint32 b, Uint32 a)
{
    SDL_Surface *s;

    (void)flags; (void)r; (void)g; (void)b; (void)a;
    if (depth != 8)
        return NULL;
    s = calloc(1, sizeof(*s));
    if (!s)
        return NULL;
    s->pixels = calloc(1, (size_t)w * h);
    if (!s->pixels)
    {
        free(s);
        return NULL;
    }
    s->w = w;
    s->h = h;
    s->pitch = w;
    s->owned = 1;
    return s;
}

SDL_Surface *ARMDOS_WrapSurface(void *pixels, int w, int h, int pitch)
{
    SDL_Surface *s = calloc(1, sizeof(*s));

    if (!s)
        return NULL;
    s->pixels = pixels;
    s->w = w;
    s->h = h;
    s->pitch = pitch;
    return s;
}

void SDL_FreeSurface(SDL_Surface *s)
{
    if (!s)
        return;
    if (s->owned)
        free(s->pixels);
    free(s);
}

static int clip(SDL_Rect *r, int w, int h)
{
    if (r->x < 0) { r->w += r->x; r->x = 0; }
    if (r->y < 0) { r->h += r->y; r->y = 0; }
    if (r->x + r->w > w) r->w = w - r->x;
    if (r->y + r->h > h) r->h = h - r->y;
    return r->w > 0 && r->h > 0;
}

int SDL_BlitSurface(SDL_Surface *src, const SDL_Rect *srect,
                    SDL_Surface *dst, SDL_Rect *drect)
{
    SDL_Rect s = srect ? *srect : (SDL_Rect){ 0, 0, src->w, src->h };
    int dx = drect ? drect->x : 0, dy = drect ? drect->y : 0;
    const uint8_t *sp;
    uint8_t *dp;
    int y;

    if (!clip(&s, src->w, src->h))
        return 0;
    if (dx < 0) { s.x -= dx; s.w += dx; dx = 0; }
    if (dy < 0) { s.y -= dy; s.h += dy; dy = 0; }
    if (dx + s.w > dst->w) s.w = dst->w - dx;
    if (dy + s.h > dst->h) s.h = dst->h - dy;
    if (s.w <= 0 || s.h <= 0)
        return 0;

    sp = (const uint8_t *)src->pixels + s.y * src->pitch + s.x;
    dp = (uint8_t *)dst->pixels + dy * dst->pitch + dx;
    if (s.w == src->pitch && s.w == dst->pitch)
        memcpy(dp, sp, (size_t)s.w * s.h);      /* the whole-frame copy */
    else
        for (y = 0; y < s.h; y++, sp += src->pitch, dp += dst->pitch)
            memcpy(dp, sp, s.w);
    return 0;
}

int SDL_FillRect(SDL_Surface *dst, const SDL_Rect *rect, Uint32 color)
{
    SDL_Rect r = rect ? *rect : (SDL_Rect){ 0, 0, dst->w, dst->h };
    uint8_t *dp;
    int y;

    if (!clip(&r, dst->w, dst->h))
        return 0;
    dp = (uint8_t *)dst->pixels + r.y * dst->pitch + r.x;
    for (y = 0; y < r.h; y++, dp += dst->pitch)
        memset(dp, (int)color, r.w);
    return 0;
}

/* An 8-bit BMP of a surface with the current palette (the debug-mode
 * screenshot key; Wolf4SDL used SDL_SaveBMP). */
extern SDL_Color curpal[256];

static void put16(uint8_t *p, unsigned v) { p[0] = v; p[1] = v >> 8; }
static void put32(uint8_t *p, uint32_t v) { put16(p, v & 0xffff); put16(p + 2, v >> 16); }

#include <stdio.h>
int ARMDOS_SaveBMP(SDL_Surface *s, const char *fname)
{
    uint8_t hdr[54 + 1024];
    uint32_t rowsize = (s->w + 3) & ~3u, size = rowsize * s->h;
    FILE *f;
    int i, y;

    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B'; hdr[1] = 'M';
    put32(hdr + 2, sizeof(hdr) + size);
    put32(hdr + 10, sizeof(hdr));
    put32(hdr + 14, 40);
    put32(hdr + 18, s->w);
    put32(hdr + 22, s->h);
    put16(hdr + 26, 1);
    put16(hdr + 28, 8);
    put32(hdr + 34, size);
    put32(hdr + 46, 256);
    for (i = 0; i < 256; i++)
    {
        hdr[54 + i * 4 + 0] = curpal[i].b;
        hdr[54 + i * 4 + 1] = curpal[i].g;
        hdr[54 + i * 4 + 2] = curpal[i].r;
    }
    f = fopen(fname, "wb");
    if (!f)
        return -1;
    fwrite(hdr, sizeof(hdr), 1, f);
    for (y = s->h - 1; y >= 0; y--)
    {
        static const uint8_t pad[4];
        fwrite((uint8_t *)s->pixels + y * s->pitch, s->w, 1, f);
        fwrite(pad, rowsize - s->w, 1, f);
    }
    fclose(f);
    return 0;
}
unsigned _armdos_raw_extmem = 1;   /* heap may use raw extended memory if no XMS (see sdk/README.md) */
