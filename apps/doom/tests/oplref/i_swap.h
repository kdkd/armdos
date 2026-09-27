#ifndef I_SWAP_H
#define I_SWAP_H
#define SHORT(x) ((signed short)(x))
#define LONG(x)  ((signed int)(x))
#define SYS_LITTLE_ENDIAN
#define SDL_SwapBE32(x) __builtin_bswap32(x)
#define SDL_SwapBE16(x) __builtin_bswap16(x)
#define SDL_SwapLE16(x) (x)
#define SDL_SwapLE32(x) (x)
#endif
