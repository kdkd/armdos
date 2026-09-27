/* unaligned.h - little-endian loads from byte streams (VOC blocks, MIDI
 * chunks). DOS/4GW code read them with plain *(unsigned long *) casts: an
 * x86 does not care about alignment; an ARMv5 LDR from an address that is
 * not a multiple of 4 returns the rotated word (ARCH.md 2). GPL-2 or later. */
#ifndef ARMDOS_UNALIGNED_H
#define ARMDOS_UNALIGNED_H
#define RD16(p) ((unsigned)((const unsigned char *)(p))[0] | ((unsigned)((const unsigned char *)(p))[1] << 8))
#define RD32(p) (RD16(p) | ((unsigned long)RD16((const unsigned char *)(p) + 2) << 16))
#endif
