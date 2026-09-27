/* speak.c - plays the speech engine's 8-bit 11025 Hz samples on ARM-DOS.
 *
 * Sound Blaster: one continuous 8-bit auto-init DMA stream (sb_start), fed
 * from a ring buffer the main program fills; the SB's IRQ drains it and plays
 * silence when it is empty. The number of samples played so far lets the
 * caller show each word as it is spoken.
 *
 * No Sound Blaster: the PC speaker, "RealSound" style - PIT channel 0 is
 * speeded up to the sample rate and its IRQ writes each sample as a one-shot
 * (mode 0) pulse width on channel 2, whose output drives the speaker. The
 * BIOS's 18.2 Hz tick is still delivered (every 607th interrupt is chained).
 */
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include <sb.h>
#include <armdos.h>
#include "tts.h"
#include "speak.h"

#define RING 8192u                 /* power of two */
static volatile unsigned char ring[RING];
static volatile unsigned rhead, rtail;          /* rhead: written by us, rtail: by the IRQ */
static volatile unsigned long played;           /* samples taken from the ring */
static unsigned long queued;                    /* samples put into the ring for this utterance */
static int mode;                                /* SPK_NONE / SPK_SB / SPK_PCSPK */
static volatile int active, generating;
static volatile unsigned long underruns;   /* samples of silence inserted mid-utterance */

/* ---- Sound Blaster ---- */
/* The card runs at twice the synthesis rate (22050 Hz) and each sample is
 * preceded by the mean of it and the one before (linear interpolation): the
 * voice is band-limited to ~4 kHz anyway, and the finer DAC steps halve the
 * size of any glitch a late or early DMA fetch can cause. */
static unsigned char last_out = 0x80;

static void sb_fill(void *buf, unsigned bytes, void *user)
{
    unsigned char *b = buf;
    unsigned t = rtail, h = rhead, i;
    unsigned char prev = last_out;
    (void)user;
    for (i = 0; i + 1 < bytes; i += 2) {
        unsigned char v;
        if (t != h) { v = ring[t]; t = (t + 1) & (RING - 1); played++; }
        else { v = 0x80; if (generating) underruns++; }
        b[i] = (unsigned char)((prev + v + 1) >> 1);
        b[i + 1] = v;
        prev = v;
    }
    last_out = prev;
    rtail = t;
}

/* ---- PC speaker ---- */
#define PIT_HZ 1193182u
#define DIV (PIT_HZ / TTS_RATE)    /* 108 */
static armdos_vect_t old08;
static unsigned bios_acc;
static int pc_on;

static void pc_irq(struct armregs *f)
{
    unsigned t = rtail;
    unsigned char s = 0x80;
    if (t != rhead) { s = ring[t]; rtail = (t + 1) & (RING - 1); played++; }
    /* pulse width 1..DIV-1 counts */
    armdos_outb(0x42, (unsigned char)(1 + (s * (DIV - 3)) / 255));
    bios_acc += DIV;
    if (bios_acc >= 65536u) { bios_acc -= 65536u; armdos_callold(old08, f); }
    else armdos_outb(0x20, 0x20);
}

static void pc_start(void)
{
    if (pc_on) return;
    bios_acc = 0;
    old08 = armdos_getvect(0x08);
    armdos_disable();
    armdos_outb(0x43, 0x90);                            /* ch2: lobyte, mode 0 */
    armdos_outb(0x42, DIV / 2);
    armdos_outb(0x61, armdos_inb(0x61) | 3);            /* gate + speaker data */
    armdos_setvect(0x08, pc_irq);
    armdos_outb(0x43, 0x34);                            /* ch0: lo/hi, mode 2 */
    armdos_outb(0x40, DIV & 0xFF); armdos_outb(0x40, DIV >> 8);
    pc_on = 1;
    armdos_enable();
}

static void pc_stop(void)
{
    if (!pc_on) return;
    armdos_disable();
    armdos_outb(0x43, 0x36); armdos_outb(0x40, 0); armdos_outb(0x40, 0);   /* 18.2 Hz again */
    armdos_setvect(0x08, old08);
    armdos_outb(0x61, armdos_inb(0x61) & ~3);
    armdos_outb(0x43, 0xB6);                            /* ch2 back to a square wave */
    pc_on = 0;
    armdos_enable();
}

static void at_exit(void) { pc_stop(); sb_stop(); }

int speak_init(int want)
{
    static int reg;
    if (!reg) { atexit(at_exit); reg = 1; }
    mode = SPK_NONE;
    if (want == SPK_NONE) return mode;
    if ((want == SPK_AUTO || want == SPK_SB) && sb_detect(0)) {
        rhead = rtail = 0;
        sb_speaker(1);
        if (sb_start(TTS_RATE * 2, SB_8BIT, 1024, sb_fill, 0)) mode = SPK_SB;
    }
    if (mode == SPK_NONE && (want == SPK_AUTO || want == SPK_PCSPK)) mode = SPK_PCSPK;
    return mode;
}

int speak_mode(void) { return mode; }

void speak_shutdown(void) { pc_stop(); sb_stop(); mode = SPK_NONE; }

static unsigned ring_free(void) { return (rtail - rhead - 1) & (RING - 1); }

/* generate as much as fits */
static void fill_ring(void)
{
    static unsigned char tmp[256];
    while (generating) {
        unsigned fr = ring_free(), n, i, h;
        int k;
        if (fr < sizeof tmp) break;
        k = tts_generate(tmp, sizeof tmp);
        if (k <= 0) { generating = 0; break; }
        h = rhead;
        for (i = 0, n = (unsigned)k; i < n; i++) { ring[h] = tmp[i]; h = (h + 1) & (RING - 1); }
        rhead = h;
        queued += (unsigned)k;
    }
}

static unsigned long start_played;

void speak_start(const char *text)
{
    speak_stop();
    tts_begin(text);
    if (mode == SPK_NONE) { active = 0; generating = 0; return; }
    armdos_disable();
    start_played = played;
    armdos_enable();
    queued = 0;
    generating = 1; active = 1;
    fill_ring();
    if (mode == SPK_PCSPK) pc_start();
}

int speak_pump(void)
{
    if (!active) return 0;
    fill_ring();
    if (!generating && rtail == rhead) {
        active = 0;
        if (mode == SPK_PCSPK) pc_stop();
        return 0;
    }
    return 1;
}

long speak_position(void)
{
    unsigned long p;
    if (!active) return tts_total_samples();
    armdos_disable(); p = played; armdos_enable();
    return (long)(p - start_played);
}

void speak_stop(void)
{
    generating = 0;
    armdos_disable();
    rhead = rtail;         /* drop what is queued */
    armdos_enable();
    if (active && mode == SPK_PCSPK) pc_stop();
    active = 0;
}

int speak_busy(void) { return active; }
unsigned long speak_underruns(void) { return underruns; }

/* ---- Ctrl-C / Ctrl-Break ---------------------------------------------------
 * INT 23h (DOS's Ctrl-C) and INT 1Bh (the BIOS's Ctrl-Break) only raise a
 * flag; the program polls speak_break_pending() in its loops, which also
 * catches a Ctrl-C (03h) waiting in the BIOS keyboard buffer, and then stops
 * the sound and exits cleanly. */
#include <bios.h>
volatile int speak_break;
static armdos_vect_t old23, old1b;
static int brk_on;

static void int23(struct armregs *f) { (void)f; speak_break = 1; }
static void int1b(struct armregs *f) { speak_break = 1; armdos_callold(old1b, f); }
static void brk_restore(void)
{
    if (!brk_on) return;
    armdos_setvect(0x23, old23);
    armdos_setvect(0x1B, old1b);
    brk_on = 0;
}

void speak_break_install(void)
{
    if (brk_on) return;
    old23 = armdos_getvect(0x23);
    old1b = armdos_getvect(0x1B);
    armdos_setvect(0x23, int23);
    armdos_setvect(0x1B, int1b);
    brk_on = 1;
    atexit(brk_restore);
}

int speak_break_pending(void)
{
    unsigned k;
    if (speak_break) return 1;
    k = _bios_keybrd(_KEYBRD_READY);
    if (k && (k & 0xFF) == 3) { _bios_keybrd(_KEYBRD_READ); speak_break = 1; }
    return speak_break;
}
