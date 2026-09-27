/*
 * music.c - a PC speaker chiptune, played from the timer interrupt like the
 * intros of 1990: one square-wave voice, chords as 70 Hz arpeggios, kick and
 * snare as quick pitch sweeps and noise, all squeezed through PIT channel 2.
 *
 * music_tick() is called 70 times a second from the demo's INT 08h handler.
 *
 * Copyright (C) 1990 Europa Micro Systems (ARM-DOS project).
 */
#include <stdint.h>
#include "armdos.h"
#include "demo.h"

/* PIT divisors for MIDI notes 36 (C2) .. 96 (C7): 1193182 / f */
static const uint16_t divisor[61] = {
    18243,17219,16252,15340,14479,13667,12899,12175,11492,10847,10238,9664,
    9121,8609,8126,7670,7240,6833,6450,6088,5746,5424,5119,4832,
    4561,4305,4063,3835,3620,3417,3225,3044,2873,2712,2560,2416,
    2280,2152,2032,1918,1810,1708,1612,1522,1437,1356,1280,1208,
    1140,1076,1016,959,905,854,806,761,718,678,640,604,570,
};

/* Melody: one character pair per row (a 16th note, 8 ticks): note letter
 * + octave digit ("A4", "c5" = C#5), ". " holds, "- " stops. 16 rows a bar. */
static const char *const melody[8] = {
    "A4. . . E5. . . D5. C5. B4. C5. ",   /* Am */
    "A4. . . . . - . F4. A4. C5. A4. ",   /* F  */
    "G4. . . C5. . . E5. D5. C5. D5. ",   /* C  */
    "B4. . . . . - . G4. A4. B4. D5. ",   /* G  */
    "- . . . . . . . E5. . . A5. . . ",   /* Am */
    "F5. . . E5. C5. A4. . . - . . . ",   /* F  */
    "- . . . . . . . G5. . . E5. . . ",   /* C  */
    "D5. . . B4. G4. D5. . . B4. . . ",   /* G  */
};
/* chords per bar (MIDI notes, arpeggiated an octave up) */
static const uint8_t chord[4][3] = { { 69, 72, 76 }, { 65, 69, 72 }, { 72, 76, 79 }, { 67, 71, 74 } };
/* drums per row: K kick, S snare, . none */
static const char drums[16] = { 'K','.','.','.','S','.','.','.','K','.','K','.','S','.','.','.' };
/* song: pattern A (bars 0-3) then B (4-7), forever */

static int note_of(const char *p)
{
    static const int8_t base[7] = { 9, 11, 0, 2, 4, 5, 7 };    /* A B C D E F G */
    char c = p[0];
    int sharp = c >= 'a';
    if (sharp) c -= 32;
    return 12 * (p[1] - '0' + 1) + base[c - 'A'] + sharp;
}

static uint32_t tick;           /* 70 Hz ticks since the start */
static int cur_note;            /* melody note sounding, 0 = none */
static int note_age;
static uint32_t rnd = 0x1990;
static int speaker_on;
volatile int music_enabled = 1;

static void sound(unsigned div)
{
    if (!div) {
        if (speaker_on) { armdos_outb(0x61, armdos_inb(0x61) & ~3); speaker_on = 0; }
        return;
    }
    armdos_outb(0x43, 0xB6);
    armdos_outb(0x42, div & 0xFF);
    armdos_outb(0x42, div >> 8);
    if (!speaker_on) { armdos_outb(0x61, armdos_inb(0x61) | 3); speaker_on = 1; }
}

static unsigned note_div(int n)
{
    while (n < 36) n += 12;
    while (n > 96) n -= 12;
    return divisor[n - 36];
}

void music_tick(void)
{
    if (!music_enabled) { sound(0); return; }
    uint32_t row = tick >> 3, sub = tick & 7;
    unsigned bar = (row >> 4) & 7, r = row & 15;
    const char *m = melody[bar] + r * 2;
    if (sub == 0) {
        if (m[0] == '-') cur_note = 0;
        else if (m[0] != '.') { cur_note = note_of(m); note_age = 0; }
    }
    unsigned div = 0;
    char d = drums[r];
    if (d == 'K' && sub < 3) {
        div = 11000 + sub * 9000;                      /* 108 Hz sweeping down */
    } else if (d == 'S' && sub < 2) {
        rnd = rnd * 1103515245u + 12345u;
        div = 250 + ((rnd >> 16) & 1023);             /* noise burst */
    } else if (cur_note) {
        div = note_div(cur_note);
        if (note_age > 12) {                           /* delayed vibrato */
            int v = (note_age & 7) < 4 ? (note_age & 3) : 4 - (note_age & 3);
            div += (div >> 7) * (v - 2) / 2;
        }
        note_age++;
    } else {
        const uint8_t *c = chord[bar & 3];
        div = note_div(c[tick % 3]);
    }
    sound(div);
    tick++;
}

void music_stop(void)
{
    music_enabled = 0;
    sound(0);
    armdos_outb(0x61, armdos_inb(0x61) & ~3);
}
