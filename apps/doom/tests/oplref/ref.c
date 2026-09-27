/* Reference OPL register stream for a MUS lump: Chocolate Doom's OPL music
 * player (i_oplmusic.c, midifile.c, mus2mid.c, memio.c - GPL-2, vendored
 * unchanged from chocolate-doom 895f581c) with a logging OPL "driver" and a
 * simulated callback timer. Used by apps/doom/tests/sound.mjs to check the
 * ARM-DOS DOOM's music (i_oplmus_armdos.c) note for note.
 *   ref DOOM1.WAD D_E1M1 volume seconds > out.txt   (lines: ms reg val, hex) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include "doomtype.h"
#include "i_sound.h"
#include "opl.h"
#include "m_misc.h"

static FILE *wad;
static uint32_t nlumps, diroff;
static void *lump(const char *name, int *len)
{
    for (uint32_t i = 0; i < nlumps; i++) {
        uint32_t e[4]; char n[9] = {0};
        fseek(wad, diroff + i * 16, SEEK_SET);
        fread(e, 4, 2, wad); fread(n, 1, 8, wad);
        if (!strncasecmp(n, name, 8)) {
            void *p = malloc(e[1]); fseek(wad, e[0], SEEK_SET); fread(p, 1, e[1], wad);
            if (len) *len = e[1];
            return p;
        }
    }
    fprintf(stderr, "no lump %s\n", name); exit(1);
}
void *W_CacheLumpName(const char *name, int tag) { (void)tag; return lump(name, NULL); }
void W_ReleaseLumpName(const char *name) { (void)name; }
void I_Error(const char *fmt, ...) { va_list a; va_start(a, fmt); vfprintf(stderr, fmt, a); va_end(a); exit(1); }

/* files: the player round-trips the converted MIDI through a temp file */
FILE *M_fopen(const char *f, const char *m) { return fopen(f, m); }
int M_remove(const char *p) { return remove(p); }
boolean M_WriteFile(const char *name, const void *src, int n) { FILE *f = fopen(name, "wb"); if (!f) return 0; fwrite(src, 1, n, f); fclose(f); return 1; }
int M_ReadFile(const char *name, byte **buf) { (void)name; (void)buf; return 0; }
char *M_TempFile(const char *s) { char *p = malloc(256); snprintf(p, 256, "oplref-%s", s); return p; }
int M_snprintf(char *b, size_t n, const char *s, ...) { va_list a; va_start(a, s); int r = vsnprintf(b, n, s, a); va_end(a); return r; }
boolean M_StringConcat(char *d, const char *s, size_t n) { strncat(d, s, n - strlen(d) - 1); return 1; }
int snd_samplerate = 44100;

/* the OPL "driver": log writes, run callbacks in time order */
static uint64_t now_us;
static struct cb { uint64_t t; opl_callback_t fn; void *data; } q[4096];
static int nq;
opl_init_result_t OPL_Init(unsigned int port) { (void)port; return OPL_INIT_OPL3; }
void OPL_Shutdown(void) {}
void OPL_SetSampleRate(unsigned int r) { (void)r; }
void OPL_WriteRegister(int reg, int value) { printf("%.3f %x %x\n", now_us / 1000.0, reg, value); }
void OPL_InitRegisters(int opl3)
{
    int r;
    for (r = 0x40; r <= 0x40 + 21; ++r) OPL_WriteRegister(r, 0x3f);
    for (r = 0x60; r <= 0xE0 + 21; ++r) OPL_WriteRegister(r, 0x00);
    for (r = 1; r < 0x40; ++r) OPL_WriteRegister(r, 0x00);
    OPL_WriteRegister(4, 0x60); OPL_WriteRegister(4, 0x80); OPL_WriteRegister(1, 0x20);
    (void)opl3;
}
void OPL_SetCallback(uint64_t us, opl_callback_t fn, void *data) { q[nq].t = now_us + us; q[nq].fn = fn; q[nq].data = data; nq++; }
void OPL_AdjustCallbacks(float f) { (void)f; }
void OPL_ClearCallbacks(void) { nq = 0; }
void OPL_Lock(void) {} void OPL_Unlock(void) {}
void OPL_SetPaused(int p) { (void)p; }

extern const music_module_t music_opl_module;
int main(int argc, char **argv)
{
    int len; uint32_t hdr[3];
    if (argc < 5) { fprintf(stderr, "usage: ref WAD LUMP volume seconds\n"); return 2; }
    wad = fopen(argv[1], "rb"); fread(hdr, 4, 3, wad); nlumps = hdr[1]; diroff = hdr[2];
    void *mus = lump(argv[2], &len);
    music_opl_module.Init();
    music_opl_module.SetMusicVolume(atoi(argv[3]));
    void *h = music_opl_module.RegisterSong(mus, len);
    printf("# play\n");
    music_opl_module.PlaySong(h, 1);
    uint64_t end = (uint64_t)(atof(argv[4]) * 1e6);
    while (nq) {
        int b = 0;
        for (int i = 1; i < nq; i++) if (q[i].t < q[b].t) b = i;
        struct cb c = q[b]; q[b] = q[--nq];
        if (c.t > end) break;
        now_us = c.t;
        c.fn(c.data);
    }
    return 0;
}
