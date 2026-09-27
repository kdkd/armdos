/*
 * kbd86.c - the "BIOS INT 09h" for bytes the x86 program's own keyboard
 * handler has already taken from the controller: scan code set 1 -> the BIOS
 * keyboard buffer and shift flags in the (shared, real) BIOS data area.
 * A port of the ARM-PC BIOS's int09_handler/translate (bios/kbd.c), so keys
 * come out exactly as they do without an x86 program in between.
 *
 * Copyright (c) 2026 Europa Micro Systems. Part of ARM-DOS (apps/x86).
 */
#include <armdos.h>
#include "x86.h"

#define B8(o)  (*(volatile uint8_t *)(0x400 + (o)))
#define B16(o) (*(volatile uint16_t *)(0x400 + (o)))
#define KBFLAGS 0x17
#define KBFLAGS2 0x18
#define KBHEAD 0x1A
#define KBTAIL 0x1C
#define KBSTART 0x80
#define KBEND 0x82
#define KBFLAGS3 0x96
#define BREAKF 0x71

#define RSHIFT  0x01
#define LSHIFT  0x02
#define CTRL    0x04
#define ALT     0x08
#define SCROLL  0x10
#define NUM     0x20
#define CAPS    0x40
#define INSERT  0x80

static const char k_normal[0x3A] =
    "\0\x1b" "1234567890-=\b\t" "qwertyuiop[]\r\0" "asdfghjkl;'`\0\\" "zxcvbnm,./\0*\0 ";
static const char k_shift[0x3A] =
    "\0\x1b" "!@#$%^&*()_+\b\0" "QWERTYUIOP{}\r\0" "ASDFGHJKL:\"~\0|" "ZXCVBNM<>?\0*\0 ";
static const char kp_digits[13] = "789-456+1230.";

static uint8_t e0_pending, e1_pending, alt_num, alt_num_used;

static void stuff(uint16_t key)
{
    uint16_t tail = B16(KBTAIL), next = tail + 2;
    if (next >= B16(KBEND)) next = B16(KBSTART);
    if (next == B16(KBHEAD)) return;
    *(volatile uint16_t *)(0x400 + tail) = key;
    B16(KBTAIL) = next;
}

static uint16_t translate(uint8_t sc, uint8_t flags, int e0)
{
    int shift = flags & (LSHIFT | RSHIFT), ctrl = flags & CTRL, alt = flags & ALT;
    if (sc >= 0x3B && sc <= 0x44) {
        int n = sc - 0x3B;
        if (alt) return (0x68 + n) << 8;
        if (ctrl) return (0x5E + n) << 8;
        if (shift) return (0x54 + n) << 8;
        return sc << 8;
    }
    if (sc == 0x57 || sc == 0x58) {
        int n = sc - 0x57;
        if (alt) return (0x8B + n) << 8;
        if (ctrl) return (0x89 + n) << 8;
        if (shift) return (0x87 + n) << 8;
        return (0x85 + n) << 8;
    }
    if (sc >= 0x47 && sc <= 0x53 && sc != 0x4A && sc != 0x4E) {
        if (!e0 && !alt && (((flags & NUM) != 0) ^ (shift != 0)))
            return (sc << 8) | (uint8_t)kp_digits[sc - 0x47];
        if (ctrl) {
            static const uint8_t ctl[13] = { 0x77, 0x8D, 0x84, 0, 0x73, 0x8F, 0x74, 0, 0x75, 0x91, 0x76, 0x92, 0x93 };
            return (ctl[sc - 0x47] << 8) | (e0 ? 0xE0 : 0);
        }
        if (alt && e0) return (sc + 0x50) << 8;
        return (sc << 8) | (e0 ? 0xE0 : 0);
    }
    if (sc == 0x4A) return alt ? 0x4A00 : ctrl ? 0x8E00 : 0x4A2D;
    if (sc == 0x4E) return alt ? 0x4E00 : ctrl ? 0x9000 : 0x4E2B;
    if (e0 && sc == 0x1C) return ctrl ? 0xE00A : alt ? 0xA600 : 0xE00D;
    if (e0 && sc == 0x35) return alt ? 0xA400 : ctrl ? 0x9500 : 0xE02F;
    if (sc == 0x37 && !e0) return alt ? 0x3700 : ctrl ? 0x9600 : 0x372A;
    if (sc >= 0x3A) return 0;
    if (alt) {
        if (sc >= 0x02 && sc <= 0x0D) return (sc + 0x76) << 8;
        if (sc == 0x39) return 0x3920;
        if (sc == 0x0E) return 0x0E00;
        if (sc == 0x1C) return 0x1C00;
        if (sc == 0x0F) return 0xA500;
        if (sc == 0x01) return 0x0100;
        return sc << 8;
    }
    if (ctrl) {
        char c = k_normal[sc];
        if (c >= 'a' && c <= 'z') return (sc << 8) | (c - 'a' + 1);
        switch (sc) {
        case 0x01: return 0x011B;
        case 0x03: return 0x0300;
        case 0x07: return 0x071E;
        case 0x0C: return 0x0C1F;
        case 0x0E: return 0x0E7F;
        case 0x0F: return 0x9400;
        case 0x1A: return 0x1A1B;
        case 0x1B: return 0x1B1D;
        case 0x1C: return 0x1C0A;
        case 0x2B: return 0x2B1C;
        case 0x39: return 0x3920;
        }
        return 0;
    }
    char c = shift ? k_shift[sc] : k_normal[sc];
    if (flags & CAPS) {
        if (c >= 'a' && c <= 'z') c -= 32;
        else if (c >= 'A' && c <= 'Z') c += 32;
    }
    if (sc == 0x0F && shift) return 0x0F00;
    if (!c) return 0;
    return (sc << 8) | (uint8_t)c;
}

/* one scan code byte, as the BIOS's INT 09h would process it */
void kbd86_process(uint8_t code)
{
    if (code == 0xE0) { e0_pending = 1; B8(KBFLAGS3) |= 0x02; return; }
    if (code == 0xE1) { e1_pending = 2; return; }
    if (e1_pending) {
        /* Pause (E1 1D 45 E1 9D C5): no pause loop here - the program runs on */
        --e1_pending;
        return;
    }
    int e0 = e0_pending;
    e0_pending = 0;
    B8(KBFLAGS3) &= ~0x02;
    int brk = code & 0x80;
    uint8_t sc = code & 0x7F;
    volatile uint8_t *fl = &B8(KBFLAGS), *fl2 = &B8(KBFLAGS2);
    if (e0 && (sc == 0x2A || sc == 0x36)) return;
    switch (sc) {
    case 0x2A: if (brk) *fl &= ~LSHIFT; else *fl |= LSHIFT; return;
    case 0x36: if (brk) *fl &= ~RSHIFT; else *fl |= RSHIFT; return;
    case 0x1D:
        if (brk) { *fl &= ~CTRL; if (e0) B8(KBFLAGS3) &= ~0x04; else *fl2 &= ~0x01; }
        else { *fl |= CTRL; if (e0) B8(KBFLAGS3) |= 0x04; else *fl2 |= 0x01; }
        return;
    case 0x38:
        if (brk) {
            *fl &= ~ALT;
            if (e0) B8(KBFLAGS3) &= ~0x08; else *fl2 &= ~0x02;
            if (alt_num_used) { stuff(alt_num); alt_num = 0; alt_num_used = 0; }
        } else {
            if (!(*fl & ALT)) { alt_num = 0; alt_num_used = 0; }
            *fl |= ALT;
            if (e0) B8(KBFLAGS3) |= 0x08; else *fl2 |= 0x02;
        }
        return;
    case 0x3A: if (brk) *fl2 &= ~0x40; else if (!(*fl2 & 0x40)) { *fl ^= CAPS; *fl2 |= 0x40; } return;
    case 0x45: if (brk) *fl2 &= ~0x20; else if (!(*fl2 & 0x20)) { *fl ^= NUM; *fl2 |= 0x20; } return;
    case 0x46:
        if (!brk && (*fl & CTRL)) {             /* Ctrl-Break */
            B16(KBHEAD) = B16(KBTAIL);
            B8(BREAKF) |= 0x80;
            stuff(0x0000);
            pend_set(PEND_1B);
            return;
        }
        if (brk) *fl2 &= ~0x10; else if (!(*fl2 & 0x10)) { *fl ^= SCROLL; *fl2 |= 0x10; }
        return;
    }
    if (brk) return;
    if (sc == 0x53 && (*fl & CTRL) && (*fl & ALT)) {           /* Ctrl-Alt-Del */
        B16(0x72) = 0x1234;
        armdos_outb(0x64, 0xFE);
        return;
    }
    if ((*fl & ALT) && !e0 && sc >= 0x47 && sc <= 0x52) {
        char d = kp_digits[sc - 0x47];
        if (d >= '0' && d <= '9') { alt_num = alt_num * 10 + (d - '0'); alt_num_used = 1; return; }
    }
    uint16_t key = translate(sc, *fl, e0);
    if ((key >> 8) == 0x52 && (key & 0xFF) != '0') *fl ^= INSERT;
    if (key) stuff(key);
}
