/*
 * SDL.h - NOT libSDL. The handful of SDL types and calls the Wolf4SDL
 * sources use, implemented directly on the ARM-DOS PC (see wolf_armdos.c).
 *
 * Copyright (C) 2026 the ARM-DOS project. GPL-2 or later, like Wolf4SDL.
 *
 * Wolf4SDL's platform files (id_vl.c, id_in.c, id_sd.c) were rewritten for
 * the bare hardware - VGA mode 13h, an INT 09h keyboard handler, the PIT and
 * INT 08h, the PC speaker and the AdLib ports - the way id's WOLF3D.EXE drove
 * a 1992 PC. What remains of "SDL" in the game code is:
 *
 *  - SDL_Color (palettes), SDL_Surface (an 8-bit pixel buffer: the off-screen
 *    buffer, and the VGA frame buffer at 0xA0000 itself), SDL_BlitSurface
 *    and SDL_FillRect on 8-bit surfaces;
 *  - SDL_GetTicks / SDL_Delay: milliseconds from the reprogrammed PIT, and a
 *    WFI (wait-for-interrupt) sleep;
 *  - SDL_SCANCODE_*: here these are the PC's own scan code set 1 values,
 *    because the keyboard handler reads port 0x60 like the original.
 *  - mouse state through the INT 33h mouse driver.
 */
#ifndef ARMDOS_SDL_SHIM_H
#define ARMDOS_SDL_SHIM_H

#include <stdint.h>
#include <stddef.h>

typedef uint8_t  Uint8;
typedef int8_t   Sint8;
typedef uint16_t Uint16;
typedef int16_t  Sint16;
typedef uint32_t Uint32;
typedef int32_t  Sint32;

typedef struct SDL_Color { Uint8 r, g, b, a; } SDL_Color;
#define SDL_ALPHA_OPAQUE 255

typedef struct SDL_Rect { int x, y, w, h; } SDL_Rect;

typedef struct SDL_Surface
{
    int   w, h;
    int   pitch;
    void *pixels;
    int   owned;            /* pixels were malloc'd by SDL_CreateRGBSurface */
} SDL_Surface;

#define SDL_MUSTLOCK(s)       0
#define SDL_LockSurface(s)    0
#define SDL_UnlockSurface(s)  ((void)0)

SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int w, int h, int depth,
                                  Uint32 r, Uint32 g, Uint32 b, Uint32 a);
SDL_Surface *ARMDOS_WrapSurface(void *pixels, int w, int h, int pitch);
void SDL_FreeSurface(SDL_Surface *s);
int  SDL_BlitSurface(SDL_Surface *src, const SDL_Rect *srect,
                     SDL_Surface *dst, SDL_Rect *drect);
int  SDL_FillRect(SDL_Surface *dst, const SDL_Rect *rect, Uint32 color);

int  ARMDOS_SaveBMP(SDL_Surface *s, const char *fname);   /* WSHOTnnn.BMP */

static inline const char *SDL_GetError(void) { return "?"; }

/* time: the PIT, reprogrammed by id_sd.c (140 or 700 Hz) */
Uint32 SDL_GetTicks(void);
void   SDL_Delay(Uint32 ms);

/* mouse: INT 33h */
#define SDL_BUTTON(x)        (1 << ((x) - 1))
#define SDL_BUTTON_LEFT      1
#define SDL_BUTTON_MIDDLE    2
#define SDL_BUTTON_RIGHT     3
Uint32 SDL_GetMouseState(int *x, int *y);
Uint32 SDL_GetRelativeMouseState(int *x, int *y);

/* keyboard: PC scan code set 1 (the id_in.h sc_ names map onto these) */
#define SDL_TEXTINPUTEVENT_TEXT_SIZE 32
#define SDL_NUM_SCANCODES   128
enum
{
    SDL_SCANCODE_UNKNOWN = 0x00,
    SDL_SCANCODE_ESCAPE = 0x01,
    SDL_SCANCODE_1 = 0x02, SDL_SCANCODE_2 = 0x03, SDL_SCANCODE_3 = 0x04,
    SDL_SCANCODE_4 = 0x05, SDL_SCANCODE_5 = 0x06, SDL_SCANCODE_6 = 0x07,
    SDL_SCANCODE_7 = 0x08, SDL_SCANCODE_8 = 0x09, SDL_SCANCODE_9 = 0x0a,
    SDL_SCANCODE_0 = 0x0b,
    SDL_SCANCODE_MINUS = 0x0c, SDL_SCANCODE_EQUALS = 0x0d,
    SDL_SCANCODE_BACKSPACE = 0x0e, SDL_SCANCODE_TAB = 0x0f,
    SDL_SCANCODE_Q = 0x10, SDL_SCANCODE_W = 0x11, SDL_SCANCODE_E = 0x12,
    SDL_SCANCODE_R = 0x13, SDL_SCANCODE_T = 0x14, SDL_SCANCODE_Y = 0x15,
    SDL_SCANCODE_U = 0x16, SDL_SCANCODE_I = 0x17, SDL_SCANCODE_O = 0x18,
    SDL_SCANCODE_P = 0x19,
    SDL_SCANCODE_RETURN = 0x1c, SDL_SCANCODE_LCTRL = 0x1d,
    SDL_SCANCODE_A = 0x1e, SDL_SCANCODE_S = 0x1f, SDL_SCANCODE_D = 0x20,
    SDL_SCANCODE_F = 0x21, SDL_SCANCODE_G = 0x22, SDL_SCANCODE_H = 0x23,
    SDL_SCANCODE_J = 0x24, SDL_SCANCODE_K = 0x25, SDL_SCANCODE_L = 0x26,
    SDL_SCANCODE_LSHIFT = 0x2a,
    SDL_SCANCODE_Z = 0x2c, SDL_SCANCODE_X = 0x2d, SDL_SCANCODE_C = 0x2e,
    SDL_SCANCODE_V = 0x2f, SDL_SCANCODE_B = 0x30, SDL_SCANCODE_N = 0x31,
    SDL_SCANCODE_M = 0x32,
    SDL_SCANCODE_RSHIFT = 0x36,
    SDL_SCANCODE_LALT = 0x38, SDL_SCANCODE_SPACE = 0x39,
    SDL_SCANCODE_CAPSLOCK = 0x3a,
    SDL_SCANCODE_F1 = 0x3b, SDL_SCANCODE_F2 = 0x3c, SDL_SCANCODE_F3 = 0x3d,
    SDL_SCANCODE_F4 = 0x3e, SDL_SCANCODE_F5 = 0x3f, SDL_SCANCODE_F6 = 0x40,
    SDL_SCANCODE_F7 = 0x41, SDL_SCANCODE_F8 = 0x42, SDL_SCANCODE_F9 = 0x43,
    SDL_SCANCODE_F10 = 0x44,
    SDL_SCANCODE_NUMLOCK = 0x45, SDL_SCANCODE_SCROLLLOCK = 0x46,
    SDL_SCANCODE_HOME = 0x47, SDL_SCANCODE_UP = 0x48, SDL_SCANCODE_PAGEUP = 0x49,
    SDL_SCANCODE_LEFT = 0x4b, SDL_SCANCODE_KP_5 = 0x4c, SDL_SCANCODE_RIGHT = 0x4d,
    SDL_SCANCODE_END = 0x4f, SDL_SCANCODE_DOWN = 0x50, SDL_SCANCODE_PAGEDOWN = 0x51,
    SDL_SCANCODE_INSERT = 0x52, SDL_SCANCODE_DELETE = 0x53,
    SDL_SCANCODE_F11 = 0x57, SDL_SCANCODE_F12 = 0x58,
    /* As on the PC the grey keys send the same codes (after an E0 prefix
     * the handler drops) as their keypad twins, and right Ctrl/Alt the
     * same as the left ones - exactly what WOLF3D.EXE saw. */
    SDL_SCANCODE_RCTRL = 0x1d, SDL_SCANCODE_RALT = 0x38,
    SDL_SCANCODE_KP_ENTER = 0x1c,
    SDL_SCANCODE_KP_2 = 0x50, SDL_SCANCODE_KP_4 = 0x4b,
    SDL_SCANCODE_KP_6 = 0x4d, SDL_SCANCODE_KP_8 = 0x48,
    /* Pause (E1 1D 45) sets Paused directly; this code only has a name. */
    SDL_SCANCODE_PAUSE = 0x7f,
};

#endif
