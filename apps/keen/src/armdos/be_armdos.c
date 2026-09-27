/* be_armdos.c - the ReflectionHLE backend for ARM-DOS: KEEN.EXE's start-up
 * and everything the Keen Dreams code asks of "the machine" (BE_ST_* and the
 * file side of BE_Cross_*), done the way KDREAMS.EXE did it on a 1991 PC:
 *
 *   video     the EGA 320x200x16 mode (0Dh) is emulated in memory
 *             (be_egaemu.c: 64K addresses x 4 planes, with the CRTC start
 *             address, pel panning and line width) and shown in VGA mode 13h:
 *             DAC entries 0-15 hold the EGA palette, so each emulated EGA
 *             pixel is one mode 13h byte. Frames are copied to 0xA0000 when
 *             something changed and the game waits (VBL, a tic, a key).
 *   text      the text screens (the "One moment" loading screen, the exit
 *             screens, error messages) go straight to 0xB8000 in mode 03h.
 *   timer     INT 08h hooked, PIT channel 0 reprogrammed to the rate the
 *             sound manager asks for (140 Hz); the BIOS tick is chained every
 *             65,536 PIT counts so the DOS clock keeps 18.2 Hz.
 *   keyboard  INT 09h hooked; the set-1 scan code from port 60h goes to the
 *             game's INL_KeyService, as the original's ISR did.
 *   sound     PC speaker through PIT channel 2 and port 61h; AdLib sound
 *             effects through the OPL at 388h/389h.
 *   mouse     INT 33h, when a mouse driver is loaded.
 *   joystick  the game port at 201h, polled as ID_IN.C's IN_GetJoyAbs did (below).
 *   idle      WFI (CP15 wait-for-interrupt) whenever the game waits.
 *
 * The game data is NOT part of ARM-DOS: KEEN.EXE needs the files of the
 * unmodified Keen Dreams shareware v1.13 release (KEENDRMS.ZIP) in its
 * directory, including the original KDREAMS.EXE, from which (as
 * ReflectionHLE does) the tables that id's build linked into the program
 * are taken: the EXE is unpacked (LZEXE 0.91) and each table CRC-checked.
 *
 * Copyright (C) 2026 Europa Micro Systems (ARM-DOS). Written for ARM-DOS
 * against ReflectionHLE's backend interface (be_st.h, be_cross.h, (C) 2014-
 * 2026 NY00123, 3-clause BSD); this file is distributed under the GNU GPL
 * version 2 or later, like the Keen Dreams code it runs.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <direct.h>
#include <armdos.h>
#include <dos.h>
#include <sb.h>

#include "refkeen.h"
#include "be_cross_mem_internal.h"
#include "unlzexe/unlzexe.h"
#include "crc32/crc32.h"

/* ================================================================ globals */
const char **g_be_argv;
int g_be_argc;
void (*be_lastSetMainFuncPtr)(void);

uint64_t *g_armdosEgaGfx;                 /* 64K EGA addresses x 8 pixel bytes */
volatile int g_armdosGfxDirty;
extern unsigned char *g_be_current_exeImage;       /* be_cross_mem.c */

/* ================================================================ files */
/* v1.13 (KEENDRMS.ZIP, 1992-09-10): the file set ReflectionHLE checks
 * (be_gamedefs_kdreams.h), and the tables inside the unpacked KDREAMS.EXE */
static const struct { const char *name; long size; uint32_t crc; } g_reqFiles[] = {
    { "KDREAMS.AUD", 3498, 0x80ac85e5 },
    { "KDREAMS.CMP", 14189, 0x97628ca0 },
    { "KDREAMS.EGA", 213045, 0x2dc94687 },
    { "KDREAMS.EXE", 81619, 0x9dce0a39 },
    { "KDREAMS.MAP", 65673, 0x8dce09af },
    { "LAST.SHL", 1634, 0xc0a3560f },
};
static const struct { const char *name; uint32_t size, crc, off; } g_embedded[] = {
    { "AUDIODCT.KDR", 1024, 0x8b6116d7, 0x2a042 },
    { "AUDIOHHD.KDR", 340, 0x499e0cbf, 0x22880 },
    { "CONTEXT.KDR", 1283, 0x5a33439d, 0x229e0 },
    { "EGADICT.KDR", 1024, 0xa69af202, 0x29846 },
    { "EGAHEAD.KDR", 12068, 0xb9d789ee, 0x1cb20 },
    { "GAMETEXT.KDR", 413, 0xb0df2792, 0x22ef0 },
    { "MAPDICT.KDR", 1020, 0x9faa7213, 0x29c46 },
    { "MAPHEAD.KDR", 11824, 0xb2f36c60, 0x1fa50 },
    { "STORY.KDR", 2526, 0xcafc1d15, 0x23090 },
};
#define EXE_IMAGE_SIZE (213536 - 0x1c00)   /* the unpacked load image */

BE_FILE_T BE_Cross_open_readonly_for_reading(const char *filename) { return fopen(filename, "rb"); }
BE_FILE_T BE_Cross_open_rewritable_for_reading(const char *filename) { return fopen(filename, "rb"); }
BE_FILE_T BE_Cross_open_rewritable_for_overwriting(const char *filename) { return fopen(filename, "wb"); }

int BE_Cross_load_embedded_rsrc_to_mem(const char *filename, void **ptr)
{
    BE_FILE_T fp = BE_Cross_open_readonly_for_reading(filename);
    if (!fp) return -1;
    int size = BE_Cross_FileLengthFromHandle(fp);
    *ptr = malloc(size);
    int ok = *ptr && fread(*ptr, size, 1, fp) == 1;
    if (!ok) free(*ptr);
    fclose(fp);
    return ok ? size : -1;
}
void BE_Cross_free_mem_loaded_embedded_rsrc(void *ptr) { free(ptr); }

static void *BEL_Cross_GetEmbeddedData(const char *name, uint32_t *pSize)
{
    for (unsigned i = 0; i < sizeof g_embedded / sizeof g_embedded[0]; i++)
        if (!BE_Cross_strcasecmp(name, g_embedded[i].name)) {
            if (pSize) *pSize = g_embedded[i].size;
            return g_be_current_exeImage + g_embedded[i].off;
        }
    BE_ST_ExitWithErrorMsg("BE_Cross_GetEmbeddedData: Unrecognized embedded data name!");
    return NULL;
}
void *BE_Cross_GetNearEmbeddedData(const char *name, uint16_t *pSize)
{
    uint32_t size;
    void *p = BEL_Cross_GetEmbeddedData(name, &size);
    if (pSize) *pSize = (uint16_t)size;
    return p;
}
void *BE_Cross_GetFarEmbeddedData(const char *name, uint32_t *pSize) { return BEL_Cross_GetEmbeddedData(name, pSize); }

/* ================================================================ interrupts */
static armdos_vect_t g_oldInt8, g_oldInt9, g_oldInt23;
static void (*g_timerFunc)(void);
static void (*g_keyFunc)(uint8_t);
static volatile int g_timerIntCounter;        /* calls since the last "clear" */
static volatile uint32_t g_pitCountsLo, g_pitCountsHi;   /* PIT counts elapsed */
static uint32_t g_pitDivisor = 0x10000;
static uint32_t g_biosChain;                  /* PIT counts towards the next BIOS tick */
static int g_timerHooked, g_keyHooked;
static int g_lockDepth;

void BE_ST_LockAudioRecursively(void) { armdos_disable(); g_lockDepth++; }
void BE_ST_UnlockAudioRecursively(void) { if (g_lockDepth > 0 && --g_lockDepth == 0) armdos_enable(); }

static void BEL_ST_Int8(struct armregs *f)
{
    uint32_t lo = g_pitCountsLo + g_pitDivisor;
    if (lo < g_pitCountsLo) g_pitCountsHi++;
    g_pitCountsLo = lo;
    g_timerIntCounter++;
    if (g_timerFunc) g_timerFunc();
    g_biosChain += g_pitDivisor;
    if (g_biosChain >= 0x10000) {             /* the BIOS tick (18.2 Hz): it sends the EOI */
        g_biosChain -= 0x10000;
        armdos_callold(g_oldInt8, f);
        return;
    }
    armdos_outb(0x20, 0x20);
}

static void BEL_ST_HookTimer(void)
{
    if (g_timerHooked) return;
    g_oldInt8 = armdos_getvect(0x08);
    armdos_disable();
    armdos_setvect(0x08, BEL_ST_Int8);
    g_timerHooked = 1;
    armdos_enable();
}

static void BEL_ST_ProgramPIT(uint32_t divisor)
{
    armdos_disable();
    armdos_outb(0x43, 0x36);
    armdos_outb(0x40, divisor & 0xFF);
    armdos_outb(0x40, (divisor >> 8) & 0xFF);
    g_pitDivisor = divisor ? divisor : 0x10000;
    armdos_enable();
}

void BE_ST_StartAudioAndTimerInt(void (*funcPtr)(void))
{
    BEL_ST_HookTimer();
    armdos_disable();
    g_timerFunc = funcPtr;
    g_timerIntCounter = 0;
    armdos_enable();
}

void BE_ST_StopAudioAndTimerInt(void)
{
    armdos_disable();
    g_timerFunc = NULL;
    armdos_enable();
}

void BE_ST_SetTimer(uint16_t rateVal) { BEL_ST_ProgramPIT(rateVal ? rateVal : 0x10000); }

static void BEL_ST_UnhookTimer(void)
{
    if (!g_timerHooked) return;
    BEL_ST_ProgramPIT(0);                     /* 65536: 18.2 Hz */
    armdos_disable();
    armdos_setvect(0x08, g_oldInt8);
    g_timerHooked = 0;
    g_timerFunc = NULL;
    armdos_outb(0x61, armdos_inb(0x61) & 0xFC);
    armdos_enable();
}

/* PIT counts since start-up, as a 1.193182 MHz clock */
static uint64_t BEL_ST_PitNow(void)
{
    armdos_disable();
    uint64_t t = ((uint64_t)g_pitCountsHi << 32) | g_pitCountsLo;
    armdos_enable();
    return t;
}

static void BEL_ST_Int9(struct armregs *f)
{
    (void)f;
    uint8_t k = armdos_inb(0x60);
    if (g_keyFunc) g_keyFunc(k);
    armdos_outb(0x20, 0x20);
}

void BE_ST_StartKeyboardService(void (*funcPtr)(uint8_t))
{
    g_keyFunc = funcPtr;
    if (g_keyHooked) return;
    g_oldInt9 = armdos_getvect(0x09);
    armdos_disable();
    armdos_setvect(0x09, BEL_ST_Int9);
    g_keyHooked = 1;
    armdos_enable();
}

void BE_ST_StopKeyboardService(void)
{
    if (!g_keyHooked) return;
    armdos_disable();
    armdos_setvect(0x09, g_oldInt9);
    g_keyHooked = 0;
    *(volatile uint8_t *)0x417 &= 0xF0;       /* no shift/ctrl/alt left "held" for DOS */
    *(volatile uint8_t *)0x418 = 0;
    armdos_enable();
}

static void BEL_ST_Int23(struct armregs *f) { f->cpsr &= ~ARM_CPSR_C; }   /* Ctrl-C: carry on */

int16_t BE_ST_BiosScanCode(int16_t command)
{
    struct armregs r;
    memset(&r, 0, sizeof r);
    r.r0 = (uint32_t)(command & 0xFF) << 8;
    if (command == 1) {                       /* status: ZF set = nothing waiting */
        _armdos_int16(&r);
        return (r.cpsr & (1u << 30)) ? 0 : (int16_t)((r.r0 >> 8) & 0xFF);
    }
    _armdos_int16(&r);
    return (int16_t)((r.r0 >> 8) & 0xFF);
}

int16_t BE_ST_KbHit(void)
{
    struct armregs r;
    memset(&r, 0, sizeof r);
    r.r0 = 0x0100;
    _armdos_int16(&r);
    return (r.cpsr & (1u << 30)) ? 0 : 1;
}

/* ================================================================ video */
static int g_screenMode = 3;
static uint16_t g_startAddr;
static uint8_t g_pelPan;
static unsigned g_lineWidth = 40;             /* bytes (EGA addresses) per line */
static int g_txtX, g_txtY, g_txtColor = 7, g_txtBackground = 0, g_txtCursorOn = 1;
#define TXT ((volatile uint8_t *)(ARMDOS_BDA[0x49] == 7 ? 0xB0000 : 0xB8000))   /* mode 7: the Hercules card option */

static void BEL_ST_BiosSetMode(int mode)
{
    struct armregs r;
    memset(&r, 0, sizeof r);
    r.r0 = (uint32_t)mode & 0xFF;
    _armdos_int10(&r);
}

static void BEL_ST_BiosCursor(int x, int y)
{
    struct armregs r;
    memset(&r, 0, sizeof r);
    r.r0 = 0x0200;
    r.r3 = ((uint32_t)y << 8) | (uint32_t)x;
    _armdos_int10(&r);
}

/* EGA colours as the 200-line modes show them (RGBI; colour 6 is brown) */
static const uint8_t g_egaRGB[16][3] = {
    { 0, 0, 0 }, { 0, 0, 42 }, { 0, 42, 0 }, { 0, 42, 42 }, { 42, 0, 0 }, { 42, 0, 42 }, { 42, 21, 0 }, { 42, 42, 42 },
    { 21, 21, 21 }, { 21, 21, 63 }, { 21, 63, 21 }, { 21, 63, 63 }, { 63, 21, 21 }, { 63, 21, 63 }, { 63, 63, 21 }, { 63, 63, 63 },
};
static uint8_t g_dac[16];                     /* which EGA colour each attribute shows */

static void BEL_ST_SetDac(int index, int ega)
{
    armdos_outb(0x3C8, (uint8_t)index);
    armdos_outb(0x3C9, g_egaRGB[ega][0]);
    armdos_outb(0x3C9, g_egaRGB[ega][1]);
    armdos_outb(0x3C9, g_egaRGB[ega][2]);
}

void BE_ST_EGASetPaletteAndBorder(const uint8_t *palette)
{
    for (int i = 0; i < 16; i++) {
        int c = (palette[i] & 7) | ((palette[i] & 16) >> 1);   /* 6 EGA signals -> RGBI */
        if (g_dac[i] != c || g_screenMode != 0xD) { g_dac[i] = (uint8_t)c; if (g_screenMode == 0xD) BEL_ST_SetDac(i, c); }
    }
}

void BE_ST_SetBorderColor(uint8_t color) { (void)color; }   /* mode 13h here has no overscan colour */
void BE_ST_SetScreenStartAddress(uint16_t crtc) { if (crtc != g_startAddr) { g_startAddr = crtc; g_armdosGfxDirty = 1; } }
void BE_ST_SetScreenStartAddressHiPart(uint8_t hi) { BE_ST_SetScreenStartAddress((g_startAddr & 0xFF) | (hi << 8)); }
void BE_ST_EGASetPelPanning(uint8_t panning) { if ((panning & 7) != g_pelPan) { g_pelPan = panning & 7; g_armdosGfxDirty = 1; } }
void BE_ST_EGASetLineWidth(uint8_t widthInBytes) { unsigned w = widthInBytes & ~1u; if (w != g_lineWidth) { g_lineWidth = w; g_armdosGfxDirty = 1; } }
void BE_ST_EGASetSplitScreen(int16_t linenum) { (void)linenum; }
void BE_ST_MarkGfxForUpdate(void) { g_armdosGfxDirty = 1; }

uint8_t *BE_ST_GetTextModeMemoryPtr(void) { return (uint8_t *)TXT; }
uint8_t *BE_ST_GetCGAMemoryPtr(void) { static uint8_t cga[16384]; return cga; }
void BE_ST_CGAUpdateGFXBufferFromWrappedMem(const uint8_t *segPtr, const uint8_t *offInSegPtr, uint16_t byteLineWidth) { (void)segPtr; (void)offInSegPtr; (void)byteLineWidth; }

/* one 320-pixel line from the emulated EGA memory (pixel offset src, which
 * wraps at 512K = 64K addresses) to an aligned mode 13h line */
static inline void BEL_ST_CopyLine(uint32_t *dst, uint32_t src)
{
    const uint8_t *base = (const uint8_t *)g_armdosEgaGfx;
    if (src + 320 + 4 > 0x80000) {               /* wrapping: the slow way */
        uint8_t *d = (uint8_t *)dst;
        for (int i = 0; i < 320; i++) d[i] = base[(src + i) & 0x7FFFF];
        return;
    }
    unsigned sh = (src & 3) * 8;
    const uint32_t *s = (const uint32_t *)(base + (src & ~3u));
    if (!sh) { memcpy(dst, s, 320); return; }
    uint32_t a = *s++;
    for (int i = 0; i < 80; i++) {
        uint32_t b = *s++;
        dst[i] = (a >> sh) | (b << (32 - sh));
        a = b;
    }
}

static void BEL_ST_Present(void)
{
    if (!g_armdosGfxDirty || g_screenMode != 0xD) return;
    g_armdosGfxDirty = 0;
    uint32_t *dst = (uint32_t *)0xA0000;
    uint16_t addr = g_startAddr;
    for (int y = 0; y < 200; y++, dst += 80, addr = (uint16_t)(addr + g_lineWidth))
        BEL_ST_CopyLine(dst, (uint32_t)addr * 8 + g_pelPan);
}

void BE_ST_SetScreenMode(int mode)
{
    int clear = !(mode & 128);
    mode &= ~128;
    if (mode == 3) {
        BEL_ST_BiosSetMode(3);
        g_screenMode = 3;
        g_txtX = g_txtY = 0;
        g_txtCursorOn = 1;
        if (!clear) { /* nothing to keep: mode 03h cleared it */ }
        return;
    }
    if (mode == 0xD) {
        BEL_ST_BiosSetMode(0x13);
        g_screenMode = 0xD;
        g_pelPan = 0; g_lineWidth = 40; g_startAddr = 0;
        if (clear) memset(g_armdosEgaGfx, 0, 8 * 0x10000);
        for (int i = 0; i < 16; i++) { g_dac[i] = (uint8_t)i; BEL_ST_SetDac(i, i); }
        g_armdosGfxDirty = 1;
        BEL_ST_Present();
        return;
    }
    BE_ST_ExitWithErrorMsg("BE_ST_SetScreenMode: unsupported mode");
}

/* ---- text output (BE_ST_printf & co.) into the mode 03h screen */
static void BEL_ST_TextScroll(void)
{
    uint8_t attr = TXT[80 * 25 * 2 - 1];
    memmove((void *)TXT, (const void *)(TXT + 160), 160 * 24);
    for (int i = 0; i < 80; i++) { TXT[160 * 24 + 2 * i] = ' '; TXT[160 * 24 + 2 * i + 1] = attr; }
}

static void BEL_ST_TextPut(int ch, int colored, int requirecr)
{
    if (ch == '\t') {
        int next = (g_txtX & ~7) + 8;
        for (; g_txtX < next && g_txtX < 80; g_txtX++) {
            TXT[(g_txtY * 80 + g_txtX) * 2] = ' ';
            if (colored) TXT[(g_txtY * 80 + g_txtX) * 2 + 1] = (uint8_t)(g_txtColor | (g_txtBackground << 4));
        }
    } else if (ch == '\r') {
        if (requirecr) g_txtX = 0;
        return;
    } else if (ch == '\n') {
        if (!requirecr) g_txtX = 0;
        g_txtY++;
    } else {
        TXT[(g_txtY * 80 + g_txtX) * 2] = (uint8_t)ch;
        if (colored) TXT[(g_txtY * 80 + g_txtX) * 2 + 1] = (uint8_t)(g_txtColor | (g_txtBackground << 4));
        g_txtX++;
    }
    if (g_txtX >= 80) { g_txtX = 0; g_txtY++; }
    if (g_txtY >= 25) { g_txtY = 24; BEL_ST_TextScroll(); }
}

static void BEL_ST_TextOut(const char *s, int colored, int requirecr)
{
    if (g_screenMode != 3) BE_ST_SetScreenMode(3);
    while (*s) BEL_ST_TextPut((uint8_t)*s++, colored, requirecr);
    BEL_ST_BiosCursor(g_txtX, g_txtY);
}

void BE_ST_textcolor(int color) { g_txtColor = color; }
void BE_ST_textbackground(int color) { g_txtBackground = color; }
void BE_ST_clrscr(void)
{
    for (int i = 0; i < 80 * 25; i++) { TXT[2 * i] = ' '; TXT[2 * i + 1] = (uint8_t)(g_txtColor | (g_txtBackground << 4)); }
    g_txtX = g_txtY = 0;
    BEL_ST_BiosCursor(0, 0);
}
void BE_ST_MoveTextCursorTo(int x, int y) { g_txtX = x; g_txtY = y; BEL_ST_BiosCursor(x, y); }
void BE_ST_ToggleTextCursor(bool isEnabled)
{
    g_txtCursorOn = isEnabled;
    struct armregs r;
    memset(&r, 0, sizeof r);
    r.r0 = 0x0100;
    r.r2 = isEnabled ? 0x0E0F : 0x2000;       /* underline cursor / hidden */
    _armdos_int10(&r);
}
void BE_ST_RepeatCharWithColorAttributes(uint8_t ch, uint8_t attr, int count)
{
    while (count-- > 0) {
        TXT[(g_txtY * 80 + g_txtX) * 2] = ch;
        TXT[(g_txtY * 80 + g_txtX) * 2 + 1] = attr;
        if (++g_txtX >= 80) { g_txtX = 0; if (++g_txtY >= 25) { g_txtY = 24; BEL_ST_TextScroll(); } }
    }
}
void BE_ST_puts(const char *str) { BEL_ST_TextOut(str, 0, 0); BEL_ST_TextOut("\n", 0, 0); }
void BE_ST_cputs(const char *str) { BEL_ST_TextOut(str, 1, 1); }
void BE_ST_vprintf(const char *fmt, va_list args)
{
    char b[512];
    vsnprintf(b, sizeof b, fmt, args);
    BEL_ST_TextOut(b, 0, 0);
}
void BE_ST_printf(const char *fmt, ...) { va_list ap; va_start(ap, fmt); BE_ST_vprintf(fmt, ap); va_end(ap); }
void BE_ST_cprintf(const char *fmt, ...)
{
    char b[512];
    va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    BEL_ST_TextOut(b, 1, 1);
}

/* host display toggles: nothing to toggle on a real screen */
bool BE_ST_HostGfx_CanToggleAspectRatio(void) { return false; }
bool BE_ST_HostGfx_GetAspectRatioToggle(void) { return false; }
void BE_ST_HostGfx_SetAspectRatioToggle(bool t) { (void)t; }
bool BE_ST_HostGfx_CanToggleFullScreen(void) { return false; }
bool BE_ST_HostGfx_GetFullScreenToggle(void) { return false; }
void BE_ST_HostGfx_SetFullScreenToggle(bool t) { (void)t; }
void BE_ST_HostGfx_ToggleFullScreen(void) {}
void BE_ST_HostGfx_SetAbsMouseCursorToggle(bool t) { (void)t; }

/* ================================================================ waiting */
static void BEL_ST_Idle(void)
{
    BEL_ST_Present();
    armdos_halt();                            /* WFI until the next interrupt */
}

void BE_ST_PollEvents(void) { BEL_ST_Present(); }
void BE_ST_ShortSleep(void) { BEL_ST_Idle(); }

/* wait for `pit` PIT counts (1.193182 MHz) of time */
static void BEL_ST_WaitPit(uint64_t pit)
{
    if (!g_timerHooked) {                     /* only the BIOS tick: 0x46C */
        uint32_t ticks = (uint32_t)((pit + 65535) / 65536);
        uint32_t t0 = *(volatile uint32_t *)0x46C;
        while (*(volatile uint32_t *)0x46C - t0 < ticks) BEL_ST_Idle();
        return;
    }
    uint64_t end = BEL_ST_PitNow() + pit;
    while (BEL_ST_PitNow() < end) BEL_ST_Idle();
}

void BE_ST_WaitForNewVerticalRetraces(int16_t number)
{
    BEL_ST_Present();
    if (number <= 0) return;
    if (!g_timerHooked) {
        /* nothing wakes us often enough: poll the retrace bit, as the original did */
        for (int i = 0; i < number; i++) {
            while (armdos_inb(0x3DA) & 8) ;
            while (!(armdos_inb(0x3DA) & 8)) ;
        }
        return;
    }
    BEL_ST_WaitPit((uint64_t)number * 1193182 / 70);
}

void BE_ST_Delay(uint16_t msec) { BEL_ST_Present(); BEL_ST_WaitPit((uint64_t)msec * 1193182 / 1000); }
void BE_ST_DelayPrecise(uint64_t nsec) { BEL_ST_WaitPit(nsec * 1193182 / 1000000000u); }

int BE_ST_TimerIntClearLastCalls(void)
{
    armdos_disable();
    int n = g_timerIntCounter;
    g_timerIntCounter = 0;
    armdos_enable();
    return n;
}

/* ReflectionHLE's semantics (be_timing.c): wait for nCalls timer interrupts
 * since the last clear; overshoot is remembered and taken off the next wait */
static int g_timerIntCounterOffset;
void BE_ST_TimerIntCallsDelayWithOffset(int nCalls)
{
    if (nCalls <= g_timerIntCounterOffset) {
        if (nCalls > 0) { g_timerIntCounterOffset -= nCalls; BE_ST_TimerIntClearLastCalls(); }
        BE_ST_PollEvents();
        return;
    }
    armdos_disable();
    int oldCount = g_timerIntCounter;
    g_timerIntCounter += g_timerIntCounterOffset;
    armdos_enable();
    BEL_ST_Present();
    while (g_timerIntCounter - oldCount < nCalls) BEL_ST_Idle();
    g_timerIntCounterOffset = (BE_ST_TimerIntClearLastCalls() - oldCount) - nCalls;
}

/* ================================================================ sound */
void BE_ST_PCSpeakerSetInvFreq(uint16_t spkInvFreq)
{
    armdos_outb(0x43, 0xB6);
    armdos_outb(0x42, spkInvFreq & 0xFF);
    armdos_outb(0x42, spkInvFreq >> 8);
    armdos_outb(0x61, armdos_inb(0x61) | 3);
}
void BE_ST_PCSpeakerSetConstVal(bool isUp)
{
    /* the original only ever turns the speaker off this way (port 61h bits 0-1) */
    armdos_outb(0x61, (armdos_inb(0x61) & 0xFC) | (isUp ? 2 : 0));
}
void BE_ST_BSound(uint16_t frequency) { if (frequency) BE_ST_PCSpeakerSetInvFreq((uint16_t)(1193182 / frequency)); }
void BE_ST_BNoSound(void) { BE_ST_PCSpeakerSetConstVal(0); }

static int g_oplPresent = -1;
bool BE_ST_IsEmulatedOPLChipReady(void)
{
    if (g_oplPresent < 0) g_oplPresent = opl_detect() ? 1 : 0;
    return g_oplPresent;
}
void BE_ST_OPL2Write(uint8_t reg, uint8_t val)
{
    /* as the original alOut: index, 6 status reads, data, 35 status reads */
    armdos_outb(0x388, reg);
    for (int i = 0; i < 6; i++) (void)armdos_inb(0x388);
    armdos_outb(0x389, val);
    for (int i = 0; i < 35; i++) (void)armdos_inb(0x388);
}

/* digitized sound: only the 2015 port had it */
void BE_ST_StartDigiAudioInt(void (*funcPtr)(void)) { (void)funcPtr; }
void BE_ST_StopDigiAudioInt(void) {}
void BE_ST_SetDigiSoundFreq(int freq) { (void)freq; }
void BE_ST_PlaySoundEffect(void *data, int numOfSamples, int bits) { (void)data; (void)numOfSamples; (void)bits; }
void BE_ST_StopSoundEffect(void) {}

/* ================================================================ mouse, joystick */
static int g_mousePresent;
static int BEL_ST_Mouse(int ax, int *bx, int *cx, int *dx)
{
    struct armregs r;
    memset(&r, 0, sizeof r);
    r.r0 = (uint32_t)ax;
    _armdos_int33(&r);
    if (bx) *bx = (int16_t)r.r1;
    if (cx) *cx = (int16_t)r.r2;
    if (dx) *dx = (int16_t)r.r3;
    return (int)(r.r0 & 0xFFFF);
}
bool BE_ST_ArmDos_ResetMouse(void)
{
    g_mousePresent = armdos_getvect(0x33) != NULL && BEL_ST_Mouse(0, NULL, NULL, NULL) == 0xFFFF;
    return g_mousePresent;
}
void BE_ST_ResetEmuMouse(void) { BE_ST_ArmDos_ResetMouse(); }
void BE_ST_GetEmuAccuMouseMotion(int16_t *optX, int16_t *optY)
{
    int cx = 0, dx = 0;
    if (g_mousePresent) BEL_ST_Mouse(0x0B, NULL, &cx, &dx);
    if (optX) *optX = (int16_t)cx;
    if (optY) *optY = (int16_t)dx;
}
uint16_t BE_ST_GetEmuMouseButtons(void)
{
    int bx = 0;
    if (g_mousePresent) BEL_ST_Mouse(3, &bx, NULL, NULL);
    return (uint16_t)bx;
}
/* ---- ARM-DOS joystick (the game port at 201h, ARCH.md 4): KDREAMS.EXE's ID_IN.C
 * IN_GetJoyAbs and INL_GetJoyButtons, done the way its assembly did them. With
 * interrupts off (pushf/cli): read 201h and write it back (fires the four one-shots),
 * then poll it, adding the stick's X bit and Y bit to two counters, until both bits
 * are 0 or MaxJoyValue (5000) polls have passed ("a silly value - abort"); the counts
 * are shifted down by the bit numbers (xs, ys) as the original's SHR did. A 201h read
 * is a 1 us ISA cycle, so a centred stick counts about 550, as on a 386. */
#define ARMDOS_JOY_MAXVALUE 5000        /* ID_IN.C MaxJoyValue */
void BE_ST_GetEmuJoyAxes(uint16_t joy, uint16_t *optX, uint16_t *optY)
{
    volatile uint8_t *port = (volatile uint8_t *)(0x10000000 + 0x201);
    unsigned xs = joy ? 2 : 0, ys = joy ? 3 : 1;
    unsigned xb = 1u << xs, yb = 1u << ys, x = 0, y = 0, b, n = ARMDOS_JOY_MAXVALUE;
    uint32_t psr;
    __asm__ volatile("mrs %0, cpsr" : "=r"(psr));
    armdos_disable();
    *port = *port;                      /* in al,dx / out dx,al: clear the resistors */
    do {
        b = *port;
        if (--n == 0) break;            /* dec bp / jz done */
        x += b & xb;
        y += b & yb;
    } while (b & (xb | yb));
    __asm__ volatile("msr cpsr_c, %0" : : "r"(psr & 0xFF) : "memory");
    if (optX) *optX = (uint16_t)(x >> xs);
    if (optY) *optY = (uint16_t)(y >> ys);
}
uint16_t BE_ST_GetEmuJoyButtons(uint16_t joy)
{
    return (uint16_t)(((armdos_inb(0x201) >> (joy ? 6 : 4)) & 3) ^ 3);   /* 0 = pressed on the port */
}

/* ReflectionHLE's alternative controller schemes (game pads, touch screens) */
bool BE_ST_IsValidPadButton(int padAction) { (void)padAction; return false; }
bool BE_ST_IsValidPadAxis(int padAction) { (void)padAction; return false; }
void BE_ST_AltControlScheme_Push(void) {}
void BE_ST_AltControlScheme_Pop(void) {}
void BE_ST_AltControlScheme_Reset(void) {}
void BE_ST_AltControlScheme_PrepareControllerMapping(const BE_ST_ControllerMapping *mapping) { (void)mapping; }
void BE_ST_AltControlScheme_UpdateVirtualMouseCursor(int x, int y) { (void)x; (void)y; }
void BE_ST_AltControlScheme_InitTouchControlsUI(BE_ST_OnscreenTouchControl *c) { (void)c; }
void BE_ST_SetAppQuitCallback(void (*funcPtr)(void)) { (void)funcPtr; }

/* ================================================================ exit */
static void BEL_ST_RestoreMachine(void)
{
    BE_ST_StopKeyboardService();
    BEL_ST_UnhookTimer();
    armdos_outb(0x61, armdos_inb(0x61) & 0xFC);
    if (g_oldInt23) { armdos_setvect(0x23, g_oldInt23); g_oldInt23 = NULL; }
}

void BE_ST_HandleExit(int status)
{
    BEL_ST_RestoreMachine();
    if (g_screenMode != 3) BE_ST_SetScreenMode(3);
    if (!g_txtCursorOn) BE_ST_ToggleTextCursor(true);
    BEL_ST_BiosCursor(g_txtX, g_txtY < 24 ? g_txtY : 24);
    exit(status);
}

void BE_ST_QuickExit(void) { BE_ST_HandleExit(0); }

void BE_ST_ExitWithErrorMsg(const char *msg)
{
    BEL_ST_RestoreMachine();
    if (g_screenMode != 3) BE_ST_SetScreenMode(3);
    BE_ST_puts(msg);
    armdos_debug(msg);
    armdos_debug("\n");
    BE_ST_HandleExit(1);
}

/* "execv": the shareware v1.13 runs LOADSCN.EXE LAST.SHL ENDSCN.SCN on quit
 * (the ordering-information text screen); ReflectionHLE has it as a function */
void BE_Cross_Bexecv(void (*mainFunc)(void), const char **argv, void (*finalizer)(void), bool passArgsToMainFunc)
{
    (void)finalizer;
    int argc = 0;
    while (argv[argc]) argc++;
    g_be_argv = argv;
    g_be_argc = argc;
    if (passArgsToMainFunc) ((int (*)(int, const char **))mainFunc)(argc, argv);
    else mainFunc();
    BE_ST_HandleExit(0);
}

/* ================================================================ start-up */
void kdreams_exe_main(void);
void RefKeen_Patch_id_ca(void);
void RefKeen_Patch_id_mm(void);
void RefKeen_Patch_id_us(void);
void RefKeen_Patch_id_rf(void);
void RefKeen_Patch_id_rf_a(void);
void RefKeen_Patch_id_vw(void);
void RefKeen_Patch_id_vw_ac(void);
void RefKeen_Patch_id_vw_ae(void);
void RefKeen_Patch_kd_demo(void);
void RefKeen_Patch_kd_keen(void);
void RefKeen_Patch_kd_play(void);
void RefKeen_FillObjStatesWithDOSPointers(void);
void RefKeen_Load_Embedded_Resources_From_kdreams_exe(void);

static void BEL_Fail(const char *msg)
{
    fputs(msg, stderr);
    exit(1);
}

static const char g_noDataMsg[] =
    "Commander Keen in Keen Dreams - the engine is here, but not the game data.\n"
    "\n"
    "Keen Dreams is shareware (Softdisk Publishing, 1991-92). Its files are not\n"
    "part of ARM-DOS: run GETKEEN to call The ARM Pit BBS and download the\n"
    "unmodified shareware release KEENDRMS.ZIP (v1.13), or unzip it into this\n"
    "directory yourself. Needed: KDREAMS.EXE KDREAMS.EGA KDREAMS.MAP KDREAMS.AUD\n"
    "KDREAMS.CMP LAST.SHL\n";

/* the data may be in the current directory or next to KEEN.EXE */
static void BEL_FindData(void)
{
    if (access("KDREAMS.EXE", 0) == 0) return;
    char dir[128];
    snprintf(dir, sizeof dir, "%s", _armdos_progpath ? _armdos_progpath : "");
    char *bs = strrchr(dir, '\\');
    if (!bs) return;
    *bs = 0;
    if (dir[1] == ':') _chdrive(toupper((unsigned char)dir[0]) - 'A' + 1);
    chdir(dir[2] ? dir : "\\");
}

static void BEL_CheckFiles(void)
{
    for (unsigned i = 0; i < sizeof g_reqFiles / sizeof g_reqFiles[0]; i++) {
        FILE *f = fopen(g_reqFiles[i].name, "rb");
        if (!f) { BEL_Fail(g_noDataMsg); }
        long len = BE_Cross_FileLengthFromHandle(f);
        uint32_t crc = 0;
        if (len == g_reqFiles[i].size) {
            static uint8_t buf[4096];
            size_t n;
            while ((n = fread(buf, 1, sizeof buf, f)) > 0) crc = Crc32_ComputeBuf(crc, buf, n);
        }
        fclose(f);
        if (len != g_reqFiles[i].size || crc != g_reqFiles[i].crc) {
            fprintf(stderr, "%s is not the one from the Keen Dreams shareware v1.13 (KEENDRMS.ZIP).\n"
                    "KEEN runs only with that release's unmodified files.\n", g_reqFiles[i].name);
            exit(1);
        }
    }
}

static void BEL_LoadExeImage(void)
{
    FILE *f = fopen("KDREAMS.EXE", "rb");
    uint16_t extraPara = 0;
    if (!f || !Unlzexe_unpack(f, g_be_current_exeImage, EXE_IMAGE_SIZE, &extraPara))
        BEL_Fail("KDREAMS.EXE could not be unpacked.\n");
    fclose(f);
    for (unsigned i = 0; i < sizeof g_embedded / sizeof g_embedded[0]; i++)
        if (Crc32_ComputeBuf(0, g_be_current_exeImage + g_embedded[i].off, g_embedded[i].size) != g_embedded[i].crc) {
            fprintf(stderr, "KDREAMS.EXE: unexpected contents (%s).\n", g_embedded[i].name);
            exit(1);
        }
}

/* the C runtime may continue the heap in raw extended memory without HIMEM.SYS */
unsigned _armdos_raw_extmem = 1;

/* for VW_VideoID (id_vw_a.c): no VGA with the Hercules card option */
int be_armdos_vga_present(void) { return armdos_vga_present(); }

int main(int argc, char **argv)
{
    /* ReflectionHLE's argv workaround: v1.13 writes argv[1..3] on quit */
    static const char *args[12] = { "KDREAMS.EXE", "", "", "", "", "", "", "", "", "", "", NULL };
    for (int i = 1; i < argc && i < 10; i++) args[i] = argv[i];
    g_be_argv = args;
    g_be_argc = argc < 10 ? argc : 10;

    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "/?") || !strcmp(argv[i], "-?")) {
            printf("Commander Keen in Keen Dreams (shareware v1.13 data) for ARM-DOS\n"
                   "Keen Dreams (C) 1991-92 Softdisk Publishing; source released under\n"
                   "the GNU GPL by Javier M. Chavez (2014); port: ReflectionHLE + ARM-DOS.\n\n"
                   "KEEN [/NOAL] [/NOJOYS] [/NOMOUSE]\n\n"
                   "Needs the unmodified KEENDRMS.ZIP files in its directory (GETKEEN.BAT).\n");
            return 0;
        }

    BEL_FindData();
    BEL_CheckFiles();

    g_be_emulatedMemSpace = malloc(EMULATED_CONVENTIONAL_SIZE + EMULATED_EMS_SIZE);
    g_armdosEgaGfx = malloc(8 * 0x10000);
    if (!g_be_emulatedMemSpace || !g_armdosEgaGfx)
        BEL_Fail("Not enough memory to run Keen Dreams (1.2 MB of extended memory needed).\n");
    memset(g_be_emulatedMemSpace, 0, EMULATED_CONVENTIONAL_SIZE + EMULATED_EMS_SIZE);
    memset(g_armdosEgaGfx, 0, 8 * 0x10000);
    g_be_current_exeImage = g_be_emulatedMemSpace + EMULATED_PSP_SIZE;

    BEL_LoadExeImage();

    /* what BE_Cross_InitGame and BEL_Cross_DoCallMainFunc do */
    RefKeen_Patch_id_ca();
    RefKeen_Patch_id_mm();
    RefKeen_Patch_id_us();
    RefKeen_Patch_id_rf();
    RefKeen_Patch_id_rf_a();
    RefKeen_Patch_id_vw();
    RefKeen_Patch_id_vw_ac();
    RefKeen_Patch_id_vw_ae();
    RefKeen_Patch_kd_demo();
    RefKeen_Patch_kd_keen();
    RefKeen_Patch_kd_play();
    RefKeen_FillObjStatesWithDOSPointers();
    RefKeen_Load_Embedded_Resources_From_kdreams_exe();
    g_be_current_exeTotalMem = EMULATED_PSP_SIZE + EXE_IMAGE_SIZE;
    BEL_Cross_ClearMemory();

    g_oldInt23 = armdos_getvect(0x23);
    armdos_setvect(0x23, BEL_ST_Int23);

    be_lastSetMainFuncPtr = kdreams_exe_main;
    kdreams_exe_main();
    BE_ST_HandleExit(0);
    return 0;
}
