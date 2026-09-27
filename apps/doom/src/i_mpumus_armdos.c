//
// i_mpumus_armdos.c - DOOM's music as General MIDI on the ARM-PC's MPU-401
// (the "General MIDI" / "Sound Canvas" music devices of DMX's SETUP).
//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard (chocolate-doom src/mus2mid.c: the MUS
// event and controller mapping this follows)
// Copyright(C) 2026 the ARM-DOS project
// GPL-2 or later (see LICENSE).
//
// Like the OPL module (i_oplmus_armdos.c), the MUS score is interpreted
// directly on DMX's clock - one MUS tick per 140 Hz timer interrupt
// (I_ArmdosMpuMusicTick from doomgeneric_armdos.c) - but each event becomes a
// MIDI message written to the MPU-401 in UART mode (sdk midi.h):
//   MUS channels 0-8 -> MIDI 1-9, 9-14 -> MIDI 11-16, 15 (percussion) -> MIDI 10;
//   release/play note -> note off/on (a play without velocity reuses the
//   channel's last one); pitch wheel byte b -> bend b * 64; system events
//   10-14 -> CC 120/123/126/127/121; controllers 0 program, 1 bank select,
//   2 modulation, 3 volume, 4 pan, 5 expression, 6 reverb, 7 chorus,
//   8 sustain, 9 soft pedal (mus2mid's table).
// The music volume slider scales each channel's volume (CC 7 = MUS volume x
// music volume / 127), as DMX's MIDI drivers did.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <armdos.h>
#include <midi.h>

#include "doomtype.h"
#include "i_sound.h"
#include "z_zone.h"
#include "i_armdos.h"

#define MUS_CHANNELS 16

typedef struct
{
    byte *data;
    int len;
    const byte *score, *end;
} mus_song_t;

static boolean music_initialized, playing, paused, looping;
static mus_song_t *song;
static const byte *score_pos;
static unsigned int delay;
static int music_volume = 127;
static byte chan_volume[MUS_CHANNELS];
static byte chan_velocity[MUS_CHANNELS];

static const byte ctl_map[10] = { 0, 0, 1, 7, 10, 11, 91, 93, 64, 67 };   // MUS controller -> MIDI CC (0 = program)
static const byte sys_map[15] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 120, 123, 126, 127, 121 };

static unsigned int midi_channel(int mus)
{
    if (mus == 15) return 9;
    return mus < 9 ? mus : mus + 1;
}

static void send_volume(int ch)
{
    mpu_msg(0xB0 | midi_channel(ch), 7, chan_volume[ch] * music_volume / 127);
}

static void all_notes_off(void)
{
    int ch;
    for (ch = 0; ch < MUS_CHANNELS; ch++)
    {
        mpu_msg(0xB0 | midi_channel(ch), 64, 0);
        mpu_msg(0xB0 | midi_channel(ch), 123, 0);
    }
}

static void RestartSong(void)
{
    int ch;
    for (ch = 0; ch < MUS_CHANNELS; ch++)
    {
        unsigned int m = midi_channel(ch);
        chan_volume[ch] = 100;
        chan_velocity[ch] = 127;
        mpu_msg(0xB0 | m, 121, 0);          // reset all controllers
        mpu_msg(0xB0 | m, 0, 0);            // bank 0
        if (ch != 15)
            mpu_msg(0xC0 | m, 0, 0);        // program 0
        mpu_msg(0xB0 | m, 10, 64);
        mpu_msg(0xE0 | m, 0, 64);
        send_volume(ch);
    }
    score_pos = song->score;
    delay = 0;
}

// One group of simultaneous events. Returns false when the score ended.
static boolean ProcessEvents(void)
{
    const byte *p = score_pos, *end = song->end;

    for (;;)
    {
        byte ev, ctl, val;
        int ch;
        unsigned int m;

        if (p >= end)
            return false;
        ev = *p++;
        ch = ev & 15;
        m = midi_channel(ch);
        switch ((ev >> 4) & 7)
        {
            case 0:                                     // release note
                if (p >= end) return false;
                mpu_msg(0x80 | m, *p++ & 0x7f, 64);
                break;
            case 1:                                     // play note
            {
                byte key;
                if (p >= end) return false;
                key = *p++;
                if (key & 0x80)
                {
                    if (p >= end) return false;
                    chan_velocity[ch] = *p++ & 0x7f;
                }
                mpu_msg(0x90 | m, key & 0x7f, chan_velocity[ch]);
                break;
            }
            case 2:                                     // pitch wheel
            {
                unsigned int bend;
                if (p >= end) return false;
                bend = (unsigned int)*p++ << 6;
                mpu_msg(0xE0 | m, bend & 0x7f, bend >> 7);
                break;
            }
            case 3:                                     // system event
                if (p >= end) return false;
                ctl = *p++;
                if (ctl < 15 && sys_map[ctl])
                    mpu_msg(0xB0 | m, sys_map[ctl], 0);
                break;
            case 4:                                     // controller
                if (p + 1 >= end) return false;
                ctl = *p++;
                val = *p++;
                if (val & 0x80)
                    val = 0x7f;
                if (ctl == 0)
                    mpu_msg(0xC0 | m, val, 0);
                else if (ctl == 3)
                {
                    chan_volume[ch] = val;
                    send_volume(ch);
                }
                else if (ctl < 10)
                    mpu_msg(0xB0 | m, ctl_map[ctl], val);
                break;
            case 5:                                     // end of measure
                break;
            case 6:                                     // score end
                score_pos = p;
                return false;
            default:
                break;
        }
        if (ev & 0x80)                                  // a delay follows
        {
            unsigned int d = 0;
            byte b;
            do
            {
                if (p >= end) return false;
                b = *p++;
                d = (d << 7) | (b & 0x7f);
            } while (b & 0x80);
            score_pos = p;
            delay = d;
            if (d)
                return true;
        }
    }
}

// From the 140 Hz timer interrupt: one MUS tick.
void I_ArmdosMpuMusicTick(void)
{
    int guard;

    if (!music_initialized || !playing || paused)
        return;
    if (delay > 0 && --delay > 0)
        return;
    for (guard = 0; guard < 64 && delay == 0; guard++)
    {
        if (!ProcessEvents())
        {
            if (looping)
            {
                score_pos = song->score;    // (DMX: the next tick starts from the top)
                delay = 1;
            }
            else
            {
                playing = false;
                all_notes_off();
            }
            break;
        }
    }
}

// ------------------------------------------------------------ the module

static boolean I_MPU_InitMusic(void)
{
    if (!mpu_info.present && !mpu_detect(NULL))
        return false;
    if (!mpu_uart())
        return false;
    {
        static const byte gm_on[] = { 0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7 };
        mpu_send(gm_on, sizeof gm_on);
    }
    music_initialized = true;
    return true;
}

static void I_MPU_ShutdownMusic(void)
{
    if (!music_initialized)
        return;
    armdos_disable();
    playing = false;
    music_initialized = false;
    armdos_enable();
    mpu_shutdown();
}

static void I_MPU_SetMusicVolume(int volume)
{
    int ch;

    if (volume < 0) volume = 0;
    if (volume > 127) volume = 127;
    if (!music_initialized || volume == music_volume)
        return;
    armdos_disable();
    music_volume = volume;
    for (ch = 0; ch < MUS_CHANNELS; ch++)
        send_volume(ch);
    armdos_enable();
}

static void I_MPU_PauseSong(void)
{
    if (!music_initialized)
        return;
    armdos_disable();
    paused = true;
    all_notes_off();
    armdos_enable();
}

static void I_MPU_ResumeSong(void)
{
    if (music_initialized)
        paused = false;
}

static void I_MPU_StopSong(void)
{
    if (!music_initialized)
        return;
    armdos_disable();
    playing = false;
    all_notes_off();
    song = NULL;
    armdos_enable();
}

static void I_MPU_PlaySong(void *handle, boolean loop)
{
    if (!music_initialized || handle == NULL)
        return;
    I_MPU_StopSong();
    armdos_disable();
    song = handle;
    looping = loop;
    RestartSong();
    paused = false;
    playing = true;
    armdos_enable();
}

static void *I_MPU_RegisterSong(void *data, int len)
{
    mus_song_t *s;
    const byte *d = data;
    int score_len, score_start;

    if (!music_initialized || len < 16 || memcmp(d, "MUS\x1a", 4) != 0)
        return NULL;
    score_len = d[4] | (d[5] << 8);
    score_start = d[6] | (d[7] << 8);
    if (score_start >= len)
        return NULL;
    s = Z_Malloc(sizeof(*s) + len, PU_STATIC, NULL);
    s->data = (byte *)(s + 1);
    memcpy(s->data, data, len);
    s->len = len;
    s->score = s->data + score_start;
    s->end = s->data + (score_start + score_len <= len ? score_start + score_len : len);
    return s;
}

static void I_MPU_UnRegisterSong(void *handle)
{
    if (!music_initialized || handle == NULL)
        return;
    if (handle == song)
        I_MPU_StopSong();
    Z_Free(handle);
}

static boolean I_MPU_MusicIsPlaying(void)
{
    return music_initialized && playing;
}

static snddevice_t mpu_music_devices[] = { SNDDEVICE_GENMIDI, SNDDEVICE_SOUNDCANVAS, SNDDEVICE_WAVEBLASTER };

music_module_t I_ArmdosMpuMusicModule =
{
    mpu_music_devices,
    arrlen(mpu_music_devices),
    I_MPU_InitMusic,
    I_MPU_ShutdownMusic,
    I_MPU_SetMusicVolume,
    I_MPU_PauseSong,
    I_MPU_ResumeSong,
    I_MPU_RegisterSong,
    I_MPU_UnRegisterSong,
    I_MPU_PlaySong,
    I_MPU_StopSong,
    I_MPU_MusicIsPlaying,
    NULL,
};
