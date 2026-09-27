/*
 * support_armdos.c - small pieces the DOS build of Duke Nukem 3D got from
 * Watcom's C library, DOS/4GW and the sound library's assembly: interrupt
 * flag save/restore (INTERRUP.H), the DPMI calls (DPMI.H), Z_AvailHeap,
 * and the game's music loader (SOUNDS.C playmusic()).
 *
 * Copyright (C) 1994-1996 James R. Dose / 3D Realms (the interfaces);
 * this implementation (C) 2026 the ARM-DOS project. GPL-2 or later.
 */
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <malloc.h>
#include <armdos.h>

#include "audiolib/dpmi.h"
#include "audiolib/music.h"
#include "filesystem.h"
#include "armdos_duke.h"

/* ---- INTERRUP.H: pushfd / pop eax / cli  and  push eax / popfd --------- */
unsigned long DisableInterrupts(void)
{
    uint32_t cpsr;
    __asm__ volatile("mrs %0, cpsr" : "=r"(cpsr));
    armdos_disable();
    return cpsr;
}

void RestoreInterrupts(unsigned long flags)
{
    if (!(flags & ARM_CPSR_I))
        armdos_enable();
}

/* ---- DPMI.H: no paging on the ARM-PC, nothing to lock; "DOS memory" is
 * any memory (the 8237 reaches all ARM-PC RAM, ARCH.md 4.2). ------------- */
int DPMI_LockMemory(void *address, unsigned length) { return DPMI_Ok; }
int DPMI_LockMemoryRegion(void *start, void *end) { return DPMI_Ok; }
int DPMI_UnlockMemory(void *address, unsigned length) { return DPMI_Ok; }
int DPMI_UnlockMemoryRegion(void *start, void *end) { return DPMI_Ok; }

int DPMI_GetDOSMemory(void **ptr, int *descriptor, unsigned length)
{
    void *p = malloc(length);
    if (!p)
        return DPMI_Error;
    *ptr = p;
    *descriptor = (int)p;
    return DPMI_Ok;
}

int DPMI_FreeDOSMemory(int descriptor)
{
    free((void *)descriptor);
    return DPMI_Ok;
}

/* ---- odds and ends of the Watcom library -------------------------------- */
#undef min
#undef max
int min(int a, int b) { return a < b ? a : b; }
int max(int a, int b) { return a > b ? a : b; }
#undef stricmp
int stricmp(const char *a, const char *b) { return strcasecmp(a, b); }

/* DOS/4GW's free memory, as Z_AvailHeap reported it to GAME.C's
 * "You don't have enough free memory" check: the largest block the C
 * runtime's heap (XMS) can still give. */
int32_t Z_AvailHeap(void)
{
    size_t lo = 0, hi = 32u << 20;

    while (hi - lo > 65536)
    {
        size_t mid = lo + (hi - lo) / 2;
        void *p = malloc(mid);
        if (p) { free(p); lo = mid; }
        else hi = mid;
    }
    return (int32_t)lo;
}

/* ---- SOUNDS.C playmusic(): the MIDI file from the group file into
 * MusicPtr (72000 bytes), looped on the OPL (MUSIC.C / MIDI.C / AL_MIDI.C) */
extern uint8_t MusicPtr[72000];

void PlayMusic(char *fn)
{
    int32_t fp, l;

    fp = kopen4load(fn, 0);
    if (fp == -1)
        return;
    l = kfilelength(fp);
    if (l >= 72000)
    {
        kclose(fp);
        return;
    }
    kread(fp, MusicPtr, l);
    kclose(fp);
    MUSIC_PlaySong((char *)MusicPtr, MUSIC_LoopSong);
}

/* ---- on exit and on Error(): the sound card and the OPL must be quiet and
 * the SB's interrupt vector restored before DOS gets the machine back
 * (FX_Shutdown / MUSIC_Shutdown are no-ops when not installed). ---------- */
#include "audiolib/fx_man.h"

void armdos_sound_shutdown(void)
{
    static int done;

    if (done)
        return;
    done = 1;
    FX_Shutdown();
    MUSIC_Shutdown();
}

/* ---- read() that reads everything asked for (see doscmpat.h) ---------- */
#undef read
int armdos_fullread(int fd, void *buf, unsigned n)
{
    unsigned char *p = buf;
    int got = 0;

    while (n)
    {
        int k = read(fd, p, n);
        if (k < 0)
            return got ? got : k;
        if (k == 0)
            break;
        p += k; got += k; n -= k;
    }
    return got;
}

/* ---- stdout while the game owns the screen (see doscmpat.h) ----------- */
#include <stdarg.h>
#include <stdio.h>
#undef printf
#undef puts
extern int armdos_graphics;             /* display_armdos.c: mode 13h is set */

int armdos_printf(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    int n;

    va_start(ap, fmt);
    if (!armdos_graphics)
    {
        n = vprintf(fmt, ap);
        va_end(ap);
        return n;
    }
    n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    armdos_debug(buf);
    return n;
}

int armdos_puts(const char *s)
{
    if (!armdos_graphics)
        return puts(s);
    armdos_debug(s);
    armdos_debug("\n");
    return 1;
}
