//
// doomgeneric_armdos.c - the ARM-DOS platform layer for doomgeneric.
//
// Copyright(C) 2026 the ARM-DOS project. GPL-2 or later, like the rest of
// DOOM (see LICENSE).
//
// DOOM as a DOS program on an ARM PC, done the way DOOM.EXE did it on a real
// PC in 1993 (ARCH.md describes the machine):
//
//  * video:    INT 10h AX=0013h sets VGA mode 13h; frames are copied as 8-bit
//              palette indices to 0xA0000; the PLAYPAL palette goes to the DAC
//              through ports 0x3C8/0x3C9 (6 bits per gun). Mode 3 on exit.
//  * keyboard: an INT 09h handler reads set-1 scan codes (with E0 prefixes)
//              from port 0x60 into a ring buffer and sends EOI; the old vector
//              is restored on exit. The BIOS never sees the keys (as in DOOM).
//  * timer:    PIT channel 0 reprogrammed to 140 Hz (the DMX sound library's
//              rate) with an INT 08h handler that counts ticks, plays the PC
//              speaker sound, advances the music and chains the BIOS tick at 18.2 Hz on average
//              (65536/8523 of its ticks), so the DOS clock keeps time.
//  * sound:    with BLASTER set, the Sound Blaster 16: DS* effects mixed into
//              an auto-init DMA stream from the SB's IRQ (i_sb_armdos.c) and
//              MUS music on the OPL3, one MUS tick per 140 Hz timer tick
//              (i_oplmus_armdos.c); else DP* PC speaker lumps: one tone per
//              tick through PIT channel 2 and port 0x61 (i_pcsound_armdos.c).
//  * idle:     WFI (CP15 wait-for-interrupt) while DOOM waits for its next tic.
//  * ENDOOM:   the text screen is copied to 0xB8000 after returning to mode 3.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <armdos.h>

#include "doomgeneric.h"
#include "doomkeys.h"
#include "i_video.h"
#include "i_armdos.h"
#include "d_event.h"
#include "m_argv.h"

// ---------------------------------------------------------------- ports ---

#define outb(p, v) armdos_outb((p), (uint8_t)(v))
#define inb(p)     armdos_inb(p)

#define PIT_CLOCK   1193182u
#define TIMER_HZ    140u
#define PIT_DIVISOR ((PIT_CLOCK + TIMER_HZ / 2) / TIMER_HZ)   // 8523

// ---------------------------------------------------------------- memory ---

// DOOM's zone (8 MB) and its other allocations come from the C runtime's
// heap, which continues in extended memory through XMS (HIMEM.SYS). Without
// an XMS driver, let it use raw extended memory (INT 15h AH=88h) instead.
unsigned _armdos_raw_extmem = 1;

// ---------------------------------------------------------------- state ---

static armdos_vect_t old_int08;
static armdos_vect_t old_int09;
static int timer_installed, keyboard_installed, video_set;

static volatile uint32_t timer_ticks;     // 140 Hz ticks since start
static uint32_t bios_acc;                 // PIT counts not yet given to BIOS

#define KBUF_SIZE 256
static volatile uint8_t kbuf[KBUF_SIZE];
static volatile uint32_t kbuf_head;       // written by the ISR
static uint32_t kbuf_tail;                // read by DG_GetKey

// ------------------------------------------------------ INT 08h (IRQ0) ---

static void int08_handler(struct armregs *f)
{
    timer_ticks++;

    I_ArmdosSpeakerTick();
    I_ArmdosMusicTick();
    I_ArmdosMpuMusicTick();

    // Chain the BIOS tick (0x46C, INT 1Ch, the DOS clock, floppy motor ...)
    // whenever a whole 65536-count period has elapsed, like DMX did. The
    // BIOS handler sends the EOI in that case.
    bios_acc += PIT_DIVISOR;
    if (bios_acc >= 65536u)
    {
        bios_acc -= 65536u;
        if (old_int08 != NULL)
        {
            old_int08(f);
            return;
        }
    }
    outb(0x20, 0x20);
}

static void pit_set_divisor(unsigned divisor)
{
    armdos_disable();
    outb(0x43, 0x36);                 // channel 0, lobyte/hibyte, mode 3
    outb(0x40, divisor & 0xFF);
    outb(0x40, (divisor >> 8) & 0xFF);
    armdos_enable();
}

static void timer_startup(void)
{
    if (timer_installed)
        return;
    old_int08 = armdos_getvect(0x08);
    armdos_disable();
    armdos_setvect(0x08, int08_handler);
    timer_installed = 1;
    armdos_enable();
    pit_set_divisor(PIT_DIVISOR);
}

static void timer_shutdown(void)
{
    if (!timer_installed)
        return;
    I_ArmdosSpeakerOff();
    pit_set_divisor(0);               // 65536: the BIOS's 18.2 Hz
    armdos_disable();
    armdos_setvect(0x08, old_int08);
    timer_installed = 0;
    armdos_enable();
}

// ------------------------------------------------------ INT 09h (IRQ1) ---

static void int09_handler(struct armregs *f)
{
    uint8_t sc = inb(0x60);
    uint32_t h = kbuf_head;

    if (h - kbuf_tail < KBUF_SIZE)
    {
        kbuf[h % KBUF_SIZE] = sc;
        kbuf_head = h + 1;
    }
    outb(0x20, 0x20);
}

static void keyboard_startup(void)
{
    if (keyboard_installed)
        return;
    old_int09 = armdos_getvect(0x09);
    armdos_disable();
    armdos_setvect(0x09, int09_handler);
    keyboard_installed = 1;
    armdos_enable();
}

static void keyboard_shutdown(void)
{
    if (!keyboard_installed)
        return;
    armdos_disable();
    armdos_setvect(0x09, old_int09);
    keyboard_installed = 0;
    armdos_enable();
    // The BIOS's shift state may be stale (we ate the break codes): clear it
    // so DOS does not come back with a stuck Ctrl or Shift.
    ARMDOS_BDA[0x17] &= 0xF0;
    ARMDOS_BDA[0x18] &= 0xFC;
}

// Scan code set 1 -> DOOM key. Vanilla bindings: Ctrl fires, Space opens
// doors, Alt strafes, Shift runs.
static const unsigned char scan_to_doom[128] =
{
    /* 00 */ 0, KEY_ESCAPE, '1', '2', '3', '4', '5', '6',
    /* 08 */ '7', '8', '9', '0', '-', '=', KEY_BACKSPACE, KEY_TAB,
    /* 10 */ 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',
    /* 18 */ 'o', 'p', '[', ']', KEY_ENTER, KEY_RCTRL, 'a', 's',
    /* 20 */ 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',
    /* 28 */ '\'', '`', KEY_RSHIFT, '\\', 'z', 'x', 'c', 'v',
    /* 30 */ 'b', 'n', 'm', ',', '.', '/', KEY_RSHIFT, KEYP_MULTIPLY,
    /* 38 */ KEY_RALT, ' ', KEY_CAPSLOCK, KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5,
    /* 40 */ KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_NUMLOCK, KEY_SCRLCK, KEYP_7,
    /* 48 */ KEYP_8, KEYP_9, KEYP_MINUS, KEYP_4, KEYP_5, KEYP_6, KEYP_PLUS, KEYP_1,
    /* 50 */ KEYP_2, KEYP_3, KEYP_0, KEYP_PERIOD, 0, 0, 0, KEY_F11,
    /* 58 */ KEY_F12, 0, 0, 0, 0, 0, 0, 0,
};

// E0-prefixed keys (the grey keys of the 101-key keyboard).
static unsigned char extended_to_doom(uint8_t code)
{
    switch (code)
    {
        case 0x1C: return KEY_ENTER;        // keypad Enter
        case 0x1D: return KEY_RCTRL;        // right Ctrl
        case 0x35: return '/';              // keypad /
        case 0x38: return KEY_RALT;         // right Alt
        case 0x47: return KEY_HOME;
        case 0x48: return KEY_UPARROW;
        case 0x49: return KEY_PGUP;
        case 0x4B: return KEY_LEFTARROW;
        case 0x4D: return KEY_RIGHTARROW;
        case 0x4F: return KEY_END;
        case 0x50: return KEY_DOWNARROW;
        case 0x51: return KEY_PGDN;
        case 0x52: return KEY_INS;
        case 0x53: return KEY_DEL;
        case 0x37: return KEY_PRTSCR;
        default:   return 0;                // incl. the fake shifts 2A/36
    }
}

int DG_GetKey(int *pressed, unsigned char *doomkey)
{
    static int prefix;          // 0, 0xE0, or a count of E1 bytes to skip

    while (kbuf_tail != kbuf_head)
    {
        uint8_t sc = kbuf[kbuf_tail % KBUF_SIZE];
        unsigned char key;

        kbuf_tail++;

        if (prefix > 0 && prefix < 0xE0)
        {
            // Pause: E1 1D 45 E1 9D C5; report it on the first make.
            if (--prefix == 0)
            {
                *pressed = 1;
                *doomkey = KEY_PAUSE;
                return 1;
            }
            continue;
        }
        if (sc == 0xE0)
        {
            prefix = 0xE0;
            continue;
        }
        if (sc == 0xE1)
        {
            prefix = 5;
            continue;
        }
        if (sc == 0xFA || sc == 0xFE || sc == 0x00 || sc == 0xFF)
        {
            prefix = 0;         // ACK / resend / overrun
            continue;
        }

        if (prefix == 0xE0)
            key = extended_to_doom(sc & 0x7F);
        else
            key = scan_to_doom[sc & 0x7F];
        prefix = 0;

        if (key == 0)
            continue;

        *pressed = (sc & 0x80) == 0;
        *doomkey = key;
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------- time ---

uint32_t DG_GetTicksMs(void)
{
    // 140 Hz ticks -> ms. Exact for DOOM's tic clock: 35 Hz = 4 ticks.
    // ms = t * 1000 / 140 = t * 50 / 7, without a 64-bit division.
    uint32_t t = timer_ticks;
    return (t / 7u) * 50u + ((t % 7u) * 50u) / 7u;
}

void DG_SleepMs(uint32_t ms)
{
    uint32_t start = timer_ticks;
    uint32_t need = (ms * TIMER_HZ + 999u) / 1000u;

    if (need == 0)
        need = 1;
    if (!timer_installed)
        return;
    // Sleep in WFI until enough timer ticks have passed: an idle browser
    // tab costs nothing while DOOM is ahead of its 35 Hz clock.
    while (timer_ticks - start < need)
        armdos_halt();
}

// ---------------------------------------------------------------- video ---

static void bios_set_mode(unsigned mode)
{
    struct armregs r;

    memset(&r, 0, sizeof(r));
    r.r0 = mode & 0xFF;             // AH=00h set video mode
    _armdos_int10(&r);
}

// ---------------------------------------------------------------- mouse ---
//
// DOOM.EXE's mouse (i_ibm.c I_StartupMouse / I_ReadMouse): through the INT 33h
// driver (MOUSE.COM), not the hardware. AX=0000h resets and detects it; every
// tic AX=0003h gives the buttons and AX=000Bh the mickeys moved since the last
// call, posted as one ev_mouse (x right = turn right, y up = walk forward).
// g_game.c scales them by mouse_sensitivity and maps buttons 1/2/3 to
// mouseb_fire/strafe/forward (0/1/2).

extern int usemouse;
static int mouse_present;

static int mouse_call(struct armregs *r, unsigned ax)
{
    memset(r, 0, sizeof(*r));
    r->r0 = ax;
    _armdos_int33(r);
    return r->r0 & 0xFFFF;
}

void I_ArmdosStartupMouse(void)
{
    struct armregs r;

    mouse_present = 0;
    if (!usemouse || M_CheckParm("-nomouse") > 0)
        return;
    printf("I_StartupMouse\n");
    // An empty IVT slot would return with AX unchanged (0).
    if (ARMDOS_IVT[0x33] == NULL || mouse_call(&r, 0x0000) != 0xFFFF)
    {
        printf("Mouse: not present\n");
        return;
    }
    printf("Mouse: detected\n");
    mouse_call(&r, 0x000B);             // clear the motion counters
    mouse_present = 1;
}

void I_ArmdosReadMouse(void)
{
    struct armregs r;
    event_t ev;

    if (!mouse_present)
        return;
    ev.type = ev_mouse;
    mouse_call(&r, 0x0003);
    ev.data1 = r.r1 & 7;                // BX: buttons (1 left, 2 right, 4 middle)
    mouse_call(&r, 0x000B);
    ev.data2 = (int16_t)r.r2;           // CX: horizontal mickeys
    ev.data3 = -(int16_t)r.r3;          // DX: vertical mickeys (down positive)
    ev.data4 = 0;
    D_PostEvent(&ev);
}

void I_ArmdosSetGraphicsMode(void)
{
    if (video_set)
        return;
    bios_set_mode(0x13);
    video_set = 1;
}

void I_ArmdosSetPalette(const struct color *colors)
{
    int i;

    outb(0x3C8, 0);
    for (i = 0; i < 256; i++)
    {
        outb(0x3C9, colors[i].r >> 2);
        outb(0x3C9, colors[i].g >> 2);
        outb(0x3C9, colors[i].b >> 2);
    }
}

void DG_DrawFrame(void)
{
    if (!video_set)
        return;
    if (palette_changed)
    {
        I_ArmdosSetPalette(colors);
        palette_changed = false;
    }
    // 8-bit indices straight into the linear mode 13h frame buffer.
    memcpy((void *)0xA0000, I_VideoBuffer, 320 * 200);
}

void DG_SetWindowTitle(const char *title)
{
    (void)title;
}

// ------------------------------------------------------ start and exit ---

static void armdos_atexit(void)
{
    I_ArmdosShutdown();
}

void I_ArmdosShutdown(void)
{
    keyboard_shutdown();
    timer_shutdown();
    if (video_set)
    {
        bios_set_mode(0x03);
        video_set = 0;
    }
}

void I_ArmdosEndoom(const unsigned char *endoom)
{
    struct armregs r;

    I_ArmdosShutdown();
    // The iconic exit screen: 80x25 characters + attributes, straight into
    // the colour text buffer, cursor on the second-to-last line.
    memcpy((void *)0xB8000, endoom, 80 * 25 * 2);
    memset(&r, 0, sizeof(r));
    r.r0 = 0x0200;                  // AH=02h set cursor position
    r.r1 = 0;                       // page 0
    r.r3 = (23 << 8) | 0;           // DH=row 23, DL=col 0
    _armdos_int10(&r);
    // ... and a newline, as DOOM.EXE's I_Quit did: the DOS prompt then
    // appears on the last line.
    printf("\n");
    fflush(stdout);
}

void DG_Init(void)
{
    atexit(armdos_atexit);
    printf("I_StartupTimer: PIT at %u Hz\n", TIMER_HZ);
    timer_startup();
    printf("I_StartupKeyboard\n");
    keyboard_startup();
}

int main(int argc, char **argv)
{
    if (!armdos_vga_present()) {        /* the Hercules card option */
        printf("DOOM requires a VGA.\n");
        return 1;
    }
    doomgeneric_Create(argc, argv);

    for (;;)
        doomgeneric_Tick();

    return 0;
}
