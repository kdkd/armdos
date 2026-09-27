/*
 * display_armdos.c - the Build engine's machine driver for ARM-DOS: video,
 * keyboard, mouse and timer, done the way DUKE3D.EXE did them on a 1996 PC
 * (Ken Silverman's A.ASM/ENGINE.C VGA code and the MACT library's keyboard
 * and mouse drivers), talking to the ARM-PC hardware (ARCH.md):
 *
 *  * video:    INT 10h AX=0013h, VGA mode 13h, 320x200x256 - the game's
 *              "VGA compatible" (chained, screen buffer) mode. The engine
 *              renders into a 64000-byte buffer and nextpage() copies it to
 *              0xA0000. The palette goes to the DAC (ports 3C8h/3C9h, 6 bits
 *              per gun). The ARM-PC's VGA has no VESA BIOS and no Mode X, so
 *              320x200 is the only screen mode (setup's other modes are gone).
 *  * keyboard: an INT 09h handler reads set-1 scan codes from port 60h and
 *              feeds the game's keyhandler() inside the interrupt, as the
 *              MACT keyboard ISR did; the BIOS never sees the keys; the old
 *              vector is restored and the shift flags cleared on exit.
 *  * mouse:    INT 33h (MOUSE.COM): AX=0 reset/detect, AX=0Bh motion
 *              counters (mickeys), AX=3 buttons.
 *  * timer:    the 120 Hz game clock is a task of the sound library's task
 *              manager (armdos/task_man_armdos.c: PIT channel 0 + INT 08h,
 *              BIOS tick chained), like GAME.C's TS_ScheduleTask(timerhandler).
 *  * idle:     WFI (CP15 wait-for-interrupt) whenever the game waits.
 *
 * "Build Engine & Tools" Copyright (c) 1993-1997 Ken Silverman
 * Ken Silverman's official web site: "http://www.advsys.net/ken"
 * See the included license file "BUILDLIC.TXT" for license info.
 * This file IS NOT A PART OF Ken Silverman's original release; it replaces
 * Ryan C. Gordon's SDL driver (display.c) of the Chocolate Duke3D port.
 * Written for ARM-DOS, 2026.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <armdos.h>

#include "platform.h"
#include "build.h"
#include "display.h"
#include "engine.h"
#include "draw.h"
#include "fixedPoint_math.h"
#include "audiolib/task_man.h"
#include "armdos_duke.h"

#define outb(p, v) armdos_outb((p), (uint8_t)(v))
#define inb(p)     armdos_inb(p)

/* ------------------------------------------------ engine driver globals -- */
int32_t xres, yres, bytesperline, imageSize, maxpages;
uint8_t *frameplace;
uint8_t *frameoffset;
uint8_t *screen, vesachecked;
int32_t buffermode, origbuffermode, linearmode;
uint8_t permanentupdate = 0, vgacompatible;
uint8_t lastPalette[768];

int32_t total_render_time = 1;
int32_t total_rendered_frames = 0;

static uint8_t *framebuf;              /* the screen buffer (64000 bytes) */
static int video_set;
int armdos_graphics;                    /* stdout -> port E9h while set */

/* ------------------------------------------------------------ keyboard -- */
static armdos_vect_t old_int09;
static int keyboard_installed;
static volatile unsigned int lastkey;
static int kb_skip;                    /* bytes of an E1 (Pause) sequence */

extern void keyhandler(void);

static void int09_handler(struct armregs *f)
{
    uint8_t sc = inb(0x60);

    if (kb_skip > 0)
        kb_skip--;
    else if (sc == 0xE1)
    {
        /* Pause: E1 1D 45 E1 9D C5 - one press of sc_Pause, never released
           (the game clears it), as the MACT keyboard handler reported it. */
        kb_skip = 5;
        lastkey = 0x59;
        keyhandler();
    }
    else if (sc != 0xFA && sc != 0xFE && sc != 0x00 && sc != 0xFF)
    {
        lastkey = sc;
        keyhandler();
    }
    outb(0x20, 0x20);
    (void)f;
}

uint8_t _readlastkeyhit(void)
{
    return (uint8_t)lastkey;
}

void initkeys(void) { }
void uninitkeys(void) { }

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
    /* we ate the break codes: no stuck Ctrl/Shift/Alt for DOS */
    ARMDOS_BDA[0x17] &= 0xF0;
    ARMDOS_BDA[0x18] &= 0xFC;
}

void _handle_events(void)
{
    /* keys arrive through INT 09h, the mouse is polled by the game */
}

void _idle(void)
{
    armdos_halt();                     /* until the next interrupt */
}

/* --------------------------------------------------------------- mouse -- */
static int mouse_present;

int setupmouse(void)
{
    struct armregs r;

    memset(&r, 0, sizeof(r));
    r.r0 = 0x0000;                     /* reset / detect */
    _armdos_int33(&r);
    mouse_present = (r.r0 & 0xFFFF) == 0xFFFF;
    moustat = mouse_present;
    return mouse_present;
}

void readmousexy(short *x, short *y)
{
    struct armregs r;
    short dx = 0, dy = 0;

    if (mouse_present)
    {
        memset(&r, 0, sizeof(r));
        r.r0 = 0x000B;                 /* motion counters since last call */
        _armdos_int33(&r);
        dx = (short)(r.r2 & 0xFFFF);
        dy = (short)(r.r3 & 0xFFFF);
    }
    if (x) *x = dx;
    if (y) *y = dy;
}

void readmousebstatus(short *bstatus)
{
    struct armregs r;
    short b = 0;

    if (mouse_present)
    {
        memset(&r, 0, sizeof(r));
        r.r0 = 0x0003;                 /* buttons and position */
        _armdos_int33(&r);
        b = (short)(r.r1 & 7);
    }
    if (bstatus) *bstatus = b;
}

/* ------------------------------------------------------------ joystick -- */
/* ARMDOS JOYSTICK: the game port at 201h (ARCH.md; emu/dev/gameport.mjs), read
   as the MACT library's JOYSTICK code did on a PC: write 201h to fire the four
   one-shots, then count polls until each axis bit drops (buttons are bits 4-7,
   0 = pressed). Axes 0-3 = A-X, A-Y, B-X, B-Y; buttons 0-3 = A1, A2, B1, B2.
   The counts are turned into the -32767..32767 the CONTROL code expects with
   the calibration CONTROL_CenterJoystick takes at start-up (centre, upper left,
   lower right, and the throttle/rudder centres for flight sticks). */
#define JOY_IO     ((volatile unsigned char *)(0x10000000 + 0x201))
#define JOY_LIMIT  4000                /* polls: ~4 ms, far beyond 100 kOhm */
static int joy_mask;                   /* axes that answer */
static int joy_raw[4], joy_btn;
static int joy_lo[4], joy_mid[4], joy_hi[4];

static void joy_poll(void)
{
    int n, v = 0xFF, left = joy_mask;
    uint32_t psr;
    /* a one-shot still timing ignores a fire (the 558): let them all end */
    for (n = 0; n < JOY_LIMIT && (*JOY_IO & joy_mask); n++) ;
    /* interrupts off while counting (CLI, as the MACT code did): an interrupt
       handler running in the middle would shorten the counts */
    __asm__ volatile("mrs %0, cpsr" : "=r"(psr));
    armdos_disable();
    *JOY_IO = 0;
    for (n = 1; n < JOY_LIMIT && left; n++)
    {
        int i;
        v = *JOY_IO;
        for (i = 0; i < 4; i++)
            if ((left >> i & 1) && !(v >> i & 1)) { joy_raw[i] = n; left &= ~(1 << i); }
    }
    if (!(psr & 0x80)) armdos_enable();
    for (n = 0; n < 4; n++) if (left >> n & 1) joy_raw[n] = JOY_LIMIT;
    joy_btn = (~*JOY_IO >> 4) & 15;
}

void _joystick_init(void)
{
    int i;
    joy_mask = 15;
    joy_poll();                        /* axes that time out: nothing plugged in */
    joy_mask = 0;
    for (i = 0; i < 4; i++)
        if (joy_raw[i] < JOY_LIMIT)
        {
            joy_mask |= 1 << i;
            joy_mid[i] = joy_raw[i];   /* until calibrated: a centred stick, */
            joy_lo[i] = joy_raw[i] / 10;           /* 0 .. 100 kOhm */
            joy_hi[i] = joy_raw[i] * 2 - joy_lo[i];
        }
}
int _joystick_present(void) { return (joy_mask & 3) == 3; }
void _joystick_deinit(void) { }
int _joystick_update(void) { if (!joy_mask) return 0; joy_poll(); return 1; }
int _joystick_axis(int axis)
{
    int v, d;
    if (axis < 0 || axis > 3 || !(joy_mask >> axis & 1)) return 0;
    v = joy_raw[axis] - joy_mid[axis];
    d = v < 0 ? joy_mid[axis] - joy_lo[axis] : joy_hi[axis] - joy_mid[axis];
    if (d < 1) d = 1;
    v = v * 32767 / d;
    return v < -32767 ? -32767 : v > 32767 ? 32767 : v;
}
int _joystick_hat(int hat) { return 0; }      /* no hats on a PC stick */
int _joystick_button(int button) { return button >= 0 && button < 4 ? (joy_btn >> button) & 1 : 0; }

/* CONTROL_CenterJoystick's "... and press a button": wait for a press and its
   release, then take the reading. point: 0 centre, 1 upper left, 2 lower
   right, 3 throttle centre (B-Y), 4 rudder centre (B-X). */
void _joystick_calibrate(int point)
{
    int i, r[4];
    if (!joy_mask) return;
    do joy_poll(); while (!joy_btn);
    for (i = 0; i < 4; i++) r[i] = joy_raw[i];
    do joy_poll(); while (joy_btn);
    switch (point)
    {
    case 0: for (i = 0; i < 2; i++) joy_mid[i] = r[i]; break;
    case 1: for (i = 0; i < 2; i++) joy_lo[i] = r[i]; break;
    case 2: for (i = 0; i < 2; i++) joy_hi[i] = r[i]; break;
    case 3: joy_mid[3] = r[3]; break;
    case 4: joy_mid[2] = r[2]; break;
    }
}

/* --------------------------------------------------------------- timer -- */
static task *timer_task;
static void (*usertimercallback)(void) = NULL;
static int timerticspersec;

static void timer_service(task *t)
{
    timerhandler();                    /* game.c: totalclock++ */
    if (usertimercallback)
        usertimercallback();
    (void)t;
}

void (*installusertimercallback(void (*callback)(void)))(void)
{
    void (*old)(void) = usertimercallback;
    usertimercallback = callback;
    return old;
}

int inittimer(int tickspersecond)
{
    if (timer_task)
        return 0;
    timerticspersec = tickspersecond;
    timer_task = TS_ScheduleTask(timer_service, tickspersecond, 1, NULL);
    TS_Dispatch();
    return timer_task ? 0 : -1;
}

void uninittimer(void)
{
    if (!timer_task)
        return;
    TS_Terminate(timer_task);
    timer_task = NULL;
    TS_Shutdown();
}

/* totalclock is advanced by the interrupt itself. */
void sampletimer(void) { }

/* milliseconds, from the game clock */
uint32_t getticks(void)
{
    uint32_t t = (uint32_t)totalclock;
    return (t / 3u) * 25u + ((t % 3u) * 25u) / 3u;
}

int gettimerfreq(void)
{
    return timerticspersec;
}

/* --------------------------------------------------------------- video -- */
static void bios_set_mode(unsigned mode)
{
    struct armregs r;

    memset(&r, 0, sizeof(r));
    r.r0 = mode & 0xFF;                /* AH=00h set video mode */
    _armdos_int10(&r);
}

void setvmode(int mode)
{
    bios_set_mode(mode);
    video_set = (mode == 0x13);
    armdos_graphics = video_set;
    if (mode == 0x03)
        qsetmode = 0;
}

void getvalidvesamodes(void)
{
    /* No VESA BIOS on the ARM-PC: only VGA mode 13h. */
    validmodecnt = 1;
    validmode[0] = 0x13;
    validmodexdim[0] = 320;
    validmodeydim[0] = 200;
}

int32_t _setgamemode(uint8_t davidoption, int32_t daxdim, int32_t daydim)
{
    int32_t i, j;

    getvalidvesamodes();
    if (!video_set)
    {
        bios_set_mode(0x13);
        video_set = 1;
        armdos_graphics = 1;
    }
    if (!framebuf)
    {
        framebuf = (uint8_t *)malloc(320 * 200);
        if (!framebuf)
            Error(EXIT_FAILURE, "Out of memory for the screen buffer\n");
        memset(framebuf, 0, 320 * 200);
    }

    setupmouse();

    vidoption = 2;                     /* "VGA compatible" 320x200 */
    xdim = xres = 320;
    ydim = yres = 200;
    bytesperline = 320;
    imageSize = 320 * 200;
    maxpages = 1;
    vesachecked = 1;
    vgacompatible = 1;
    linearmode = 1;
    buffermode = origbuffermode = 1;
    qsetmode = 200;
    activepage = visualpage = 0;
    frameoffset = frameplace = framebuf;

    j = ydim * 4 * sizeof(int32_t);   /* horizlookup & horizlookup2 */
    if (horizlookup)  free(horizlookup);
    if (horizlookup2) free(horizlookup2);
    horizlookup = (int32_t *)malloc(j);
    horizlookup2 = (int32_t *)malloc(j);

    j = 0;
    for (i = 0; i <= ydim; i++)
    {
        ylookup[i] = j;
        j += bytesperline;
    }
    horizycent = ((ydim * 4) >> 1);

    /* force drawrooms to call dosetaspect */
    oxyaspect = oxdimen = oviewingrange = -1;

    setBytesPerLine(bytesperline);
    setview(0L, 0L, xdim - 1, ydim - 1);
    setbrightness(curbrightness, palette);

    if (searchx < 0)
    {
        searchx = halfxdimen;
        searchy = (ydimen >> 1);
    }
    (void)davidoption; (void)daxdim; (void)daydim;
    return 0;
}

void _nextpage(void)
{
    if (video_set && framebuf)
        memcpy((void *)0xA0000, framebuf, 320 * 200);
    total_rendered_frames++;
}

void _updateScreenRect(int32_t x, int32_t y, int32_t w, int32_t h)
{
    _nextpage();
}

void *_getVideoBase(void)
{
    return framebuf;
}

void _uninitengine(void)
{
}

/* 256 entries of B, G, R, x (0-63) -> the DAC */
int VBE_setPalette(uint8_t *palettebuffer)
{
    uint8_t *p = palettebuffer;
    int i;

    for (i = 0; i < 256; i++)
    {
        lastPalette[i * 3 + 0] = p[i * 4 + 2];
        lastPalette[i * 3 + 1] = p[i * 4 + 1];
        lastPalette[i * 3 + 2] = p[i * 4 + 0];
    }
    outb(0x3C8, 0);
    for (i = 0; i < 256; i++, p += 4)
    {
        outb(0x3C9, p[2] & 63);
        outb(0x3C9, p[1] & 63);
        outb(0x3C9, p[0] & 63);
    }
    return 1;
}

int VBE_getPalette(int32_t start, int32_t num, uint8_t *palettebuffer)
{
    uint8_t *p = palettebuffer + start * 4;
    int i;

    outb(0x3C7, start);
    for (i = 0; i < num; i++)
    {
        p[2] = inb(0x3C9);
        p[1] = inb(0x3C9);
        p[0] = inb(0x3C9);
        p[3] = 0;
        p += 4;
    }
    return 1;
}

/* -------------------------------------------- 2D (map editor) helpers -- */
static uint8_t drawpixel_color;

uint8_t readpixel(uint8_t *location) { return *location; }
void drawpixel(uint8_t *location, uint8_t pixel) { *location = pixel; }
void setcolor16(uint8_t col) { drawpixel_color = col; }
void drawpixel16(int32_t offset) { drawpixel((uint8_t *)frameplace + offset, drawpixel_color); }

void fillscreen16(int32_t offset, int32_t color, int32_t blocksize)
{
    memset((uint8_t *)frameplace + offset, (int)color, blocksize);
}

void drawline16(int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint8_t col)
{
    int32_t dx = labs(x2 - x1), dy = -labs(y2 - y1);
    int32_t sx = x1 < x2 ? 1 : -1, sy = y1 < y2 ? 1 : -1, err = dx + dy;

    for (;;)
    {
        if (x1 >= 0 && x1 < xdim && y1 >= 0 && y1 < ydim)
            frameplace[ylookup[y1] + x1] = col;
        if (x1 == x2 && y1 == y2)
            break;
        int32_t e2 = 2 * err;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
}

void clear2dscreen(void)
{
    if (framebuf)
        memset(framebuf, 0, 320 * 200);
}

/* ------------------------------------------------------ screen capture -- */
/* Ken's screencapture(): a 320x200 PCX file (DUKE0000.PCX ...). */
int screencapture(char *filename, uint8_t inverseit)
{
    FILE *f;
    uint8_t hdr[128];
    int x, y;

    if (!framebuf)
        return -1;
    f = fopen(filename, "wb");
    if (!f)
        return -1;
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 10; hdr[1] = 5; hdr[2] = 1; hdr[3] = 8;
    hdr[8] = (xdim - 1) & 255; hdr[9] = (xdim - 1) >> 8;
    hdr[10] = (ydim - 1) & 255; hdr[11] = (ydim - 1) >> 8;
    hdr[12] = 320 & 255; hdr[13] = 320 >> 8;
    hdr[14] = 200; hdr[15] = 0;
    hdr[65] = 1;
    hdr[66] = xdim & 255; hdr[67] = xdim >> 8;
    hdr[68] = 1;
    fwrite(hdr, 1, 128, f);
    for (y = 0; y < ydim; y++)
    {
        uint8_t *row = framebuf + ylookup[y];
        for (x = 0; x < xdim; )
        {
            uint8_t c = row[x];
            int n = 1;
            if (inverseit) c ^= 255;
            while (x + n < xdim && n < 63 && (uint8_t)(row[x + n] ^ (inverseit ? 255 : 0)) == c)
                n++;
            if (n > 1 || c >= 0xC0)
                fputc(0xC0 | n, f);
            fputc(c, f);
            x += n;
        }
    }
    fputc(12, f);
    for (x = 0; x < 768; x++)
        fputc(lastPalette[x] << 2, f);
    fclose(f);
    return 0;
}

/* ------------------------------------------------------ start and exit -- */
void _platform_init(int argc, char **argv, const char *title, const char *iconName)
{
    _argc = argc;
    _argv = argv;
    keyboard_startup();
    (void)title; (void)iconName;
}

/* Called by Error() and at exit: give DOS its machine back. */
void _platform_shutdown(void)
{
    static int done;

    if (done)
        return;
    done = 1;
    armdos_sound_shutdown();
    keyboard_shutdown();
    uninittimer();
    TS_Shutdown();
    if (video_set)
    {
        bios_set_mode(0x03);
        video_set = 0;
        armdos_graphics = 0;
    }
}
